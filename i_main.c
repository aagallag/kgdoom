#ifndef LINUX
#include <libtransistor/err.h>
#include <libtransistor/ipc/bsd.h>
#include "wad.h"
#include <unistd.h>
#endif

#include "config.h"
#include "doomdef.h"
#include "doomstat.h"

#include "m_argv.h"
#include "d_main.h"
#include "d_net.h"
#include "i_video.h"
#include "z_zone.h"
#include "g_game.h"
#include "i_system.h"
#include "i_sound.h"

#include <setjmp.h>

#ifdef LINUX
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#else
#include <sys/fcntl.h>
#endif

#include "network.h"
#include "cl_cmds.h"

// for exit
jmp_buf exitenv;

extern int	key_use;

static struct sockaddr_in client_addr =
{
	.sin_family = AF_INET,
};

int client_socket;

int keep_alive;
int last_packet_tic;

#ifndef LINUX

#ifdef SWITCH_SERVER
static FILE http_stdout;
static const char http_get_template[] = "GET /files/%s HTTP/1.1\r\nHost: pegaswitch.local\r\nUser-Agent: kgDOOM\r\nAccept-Encoding: none\r\nConnection: close\r\n\r\n";

static struct sockaddr_in server_addr =
{
	.sin_family = AF_INET,
	.sin_port = htons(80)
};
static int http_socket;
#endif

// +temporary
struct bsd_pollfd
{
	int	fd;
	short	events;
	short	revents;
};
#define	BSD_POLLIN	0x0001
// -temporary

#ifdef DBG_SERVER
int client_fd;
#endif

int printf_net(const char *str)
{
#ifdef DBG_SERVER
	return bsd_send(client_fd, str, strlen(str), 0);
#else
	return 0;
#endif
}

static void connect_net()
{
#ifdef DBG_SERVER
	char server_ip_addr[4] = {192, 168, 2, 42};
	struct sockaddr_in log_server_addr = {
		.sin_family = AF_INET,
		.sin_port = htons(5555),
		.sin_addr = {
			.s_addr = *(uint32_t*) server_ip_addr
		}
	};

	client_fd = bsd_socket(2, 1, 6); // AF_INET, SOCK_STREAM, PROTO_TCP
	if(client_fd < 0) {
		printf("failed to create socket: 0x%x, %d\n", bsd_result, bsd_errno);
		return;
	}
	printf("made client socket %d\n", client_fd);
	svcSleepThread(100000000*10);

	if(bsd_connect(client_fd, (struct sockaddr*) &log_server_addr, sizeof(log_server_addr)) < 0) {
		printf("failed to connect socket: 0x%x, %d\n", bsd_result, bsd_errno);
		return;
	}

	printf_net("testing printf_net func...\n");
#endif
}

#ifdef SWITCH_SERVER
static int stdout_http(struct _reent *reent, void *v, const char *ptr, int len)
{
	bsd_send(http_socket, ptr, len, 0);
	return len;
}

static int parse_header(char *buf, int len, int *offset, int *got)
{
	char *ptr = buf;
	char *pptr = buf;
	int ret;
	int state = 0;
	int content_length = 0;

	while(len)
	{
		// get some bytes
		ret = bsd_recv(http_socket, ptr, len, 0);
		if(ret <= 0)
			return -1;
		ptr += ret;
		// parse line(s)
		while(1)
		{
			char *tptr = pptr;
			char *eptr = pptr + ret;
			// get line end
			while(tptr < eptr)
			{
				if(*tptr == '\n')
				{
					if(tptr - pptr <= 1)
					{
						// empty line, header ending
						if(state)
						{
							*offset = (int)((tptr + 1) - buf);
							*got = (int)((ptr - buf) - *offset);
							return content_length;
						} else
							return -2;
					}
					// got one line, parse it
					if(state)
					{
						if(!strncmp(pptr, "Content-Length:", 15))
							sscanf(pptr + 15, "%d", &content_length);
					} else
					{
						int v1, v2, res;
						// HTTP response
						state = 1;
						if(sscanf(pptr, "HTTP/%d.%d %d", &v1, &v2, &res) != 3 || !res)
							return -1;
						if(res != 200)
							return -res;
					}
					// go next
					pptr = tptr + 1;
					break;
				}
				tptr++;
			}
			if(tptr == pptr)
				// no more lines left
				break;
		}
		// go next
		len -= ret;
	}
	
	return -1;
}

int http_get_file(const char *path, void **buff)
{
	char temp[1024];
	int ret, offs, got;
	void *ptr;
	int size;

	http_socket = bsd_socket(2, 1, 6); // AF_INET, SOCK_STREAM, PROTO_TCP
	if(http_socket < 0)
		return -1;

	if(bsd_connect(http_socket, (struct sockaddr*) &server_addr, sizeof(server_addr)) < 0)
	{
		bsd_close(http_socket);
		return -2;
	}

	// make a request
	fprintf(&http_stdout, http_get_template, path);

	// get an answer
	ret = parse_header(temp, sizeof(temp), &offs, &got);
	// load it now
	if(ret > 0)
	{
		printf("- HTTP file size: %iB\n", ret);
		*buff = Z_Malloc(ret, PU_STATIC, NULL);
		ptr = *buff;
		size = ret;
		if(got)
		{
			memcpy(ptr, temp + offs, got);
			ptr += got;
			size -= got;
		}
		while(size)
		{
			got = bsd_recv(http_socket, ptr, size, 0);
			if(got <= 0)
			{
				bsd_close(http_socket);
				printf("- read error\n");
				return -4;
			}
			size -= got;
			ptr += got;
		}
		printf("- file loaded\n");
	}

	bsd_close(http_socket);
	return ret;
}

#else

bool file_exists(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		return false;
	}
	return true;
}

int select_wad()
{
	if (file_exists("/sd/switch/kgdoom/doom.wad")) {
		return DOOM1;
	} else if (file_exists("/sd/switch/kgdoom/doom2.wad")) {
		return DOOM2;
	} else if (file_exists("/sd/switch/kgdoom/doomu.wad")) {
		return ULTIMATE_DOOM;
	} else if (file_exists("/sd/switch/kgdoom/tnt.wad")) {
		return TNT;
	} else if (file_exists("/sd/switch/kgdoom/plutonia.wad")) {
		return PLUTONIA;
	} else if (file_exists("/sd/switch/kgdoom/freedoom1.wad")) {
		return FREEDOOM1;
	} else if (file_exists("/sd/switch/kgdoom/freedoom2.wad")) {
		return FREEDOOM2;
	} else {
		return SHAREWARE;
	}
}

int sdcard_get_file(const char *path, void **buff)
{
	printf_net("http_get_file was called...\n");

	// Open file
	FILE *f;

	if (strcmp(path, "doom.wad") == 0)
	{
		f = fopen("/sd/switch/kgdoom/doom.wad", "rb");
	} else if (strcmp(path, "doomu.wad") == 0)
	{
		f = fopen("/sd/switch/kgdoom/doomu.wad", "rb");
	} else if (strcmp(path, "tnt.wad") == 0)
	{
		f = fopen("/sd/switch/kgdoom/tnt.wad", "rb");
	} else if (strcmp(path, "plutonia.wad") == 0)
	{
		f = fopen("/sd/switch/kgdoom/plutonia.wad", "rb");
	} else if (strcmp(path, "doom2.wad") == 0)
	{
		f = fopen("/sd/switch/kgdoom/doom2.wad", "rb");
	} else if (strcmp(path, "feedoom1.wad") == 0)
	{
		f = fopen("/sd/switch/kgdoom/feedoom1.wad", "rb");
	} else if (strcmp(path, "feedoom2.wad") == 0)
	{
		f = fopen("/sd/switch/kgdoom/feedoom2.wad", "rb");
	} else
	{
		f = fopen("/sd/switch/kgdoom/doom1.wad", "rb");
	}


	if (f == NULL) {
		printf_net("Failed to open /sd/switch/kgdoom/doom.wad\n");
		return 1;
	}
	printf_net("Open worked\n");

	// Get file length
	fseek(f, 0L, SEEK_END);
	int len = ftell(f);
	rewind(f);
	printf_net("Got filesize of wad...\n");

	// Malloc memory
	void *ptr;
	*buff = Z_Malloc(len, PU_STATIC, NULL);
	ptr = *buff;
	printf("Malloc done...\n");

	// Read file contents
	int rd = fread(ptr, 1, len, f);
	if (rd <= 0) {
		printf_net("fread return zero or less\n");
		return 1;
	}

	// Close
	fclose(f);
	printf_net("just called fclose...\n");

	return rd;
}
#endif

char *test_argv[] =
{
	"doom",
	"-warp",
	"1",
	"2",
	NULL
};

static int bsd_ipc_initializations = 0;
static ipc_object_t bsd_object;

result_t bsd_ipc_init() {
	result_t r = RESULT_OK;

	if(bsd_ipc_initializations++ > 0) {
		return r;
	}

	r = sm_get_service(&bsd_object, bsd_get_socket_service_name());
	if(r) {
		goto fail_sm;
	}
	return 0;

fail_sm:
	sm_finalize();
fail:
	bsd_ipc_initializations--;
	return r;
}

// +temporary
int bsd_poll(struct bsd_pollfd *fds, int nfds, int timeout)
{
	result_t r;

	int32_t raw[] = {nfds, timeout};

	ipc_buffer_t fds_in = {
		.addr = fds,
		.size = sizeof(struct bsd_pollfd) * nfds,
		.type = 0x21
	};

	ipc_buffer_t fds_out = {
		.addr = fds,
		.size = sizeof(struct bsd_pollfd) * nfds,
		.type = 0x22
	};

	ipc_buffer_t *buffers[] = {
		&fds_in,
		&fds_out,
	};

	ipc_request_t rq = ipc_default_request;
	rq.request_id = 6;
	rq.raw_data = (uint32_t*)raw;
	rq.raw_data_size = sizeof(raw);
	rq.num_buffers = 2;
	rq.buffers = buffers;

	int32_t response[2]; // ret, errno
  
	ipc_response_fmt_t rs = ipc_default_response_fmt;
	rs.raw_data_size = sizeof(response);
	rs.raw_data = (uint32_t*) response;
  
	r = ipc_send(bsd_object, &rq, &rs);
	if(r) {
		bsd_result = r;
		return -1;
	}

	if(response[0] < 0) {
		bsd_result = LIBTRANSISTOR_ERR_BSD_ERRNO_SET;
		bsd_errno = response[1];
		return -1;
	}
  
	return response[0];
}
// -temporary

#endif

void main_finish()
{
	if(netgame)
		CL_Disconnect();
	I_ShutdownSound();
	I_ShutdownGraphics();
#ifndef LINUX
	display_finalize();
	vi_finalize();
	gpu_finalize();
	hid_finalize();
	bsd_finalize();
	sm_finalize();
#endif
}

void I_Error (char *error, ...)
{
	va_list vl;
	int ret;
	va_start(vl, error);
	ret = vprintf(error, vl);
	va_end(vl);

	printf("\n");

	longjmp(exitenv, 2);
}

//
// I_Quit
//
void I_Quit (void)
{
	longjmp(exitenv, 1);
}

int main(int argc, char **argv)
{
	int ret;

	myargc = argc;
	myargv = argv;

#ifndef LINUX
	result_t r;

	if((r = sm_init()) != RESULT_OK)
	{
		printf("- failed to init sm: 0x%x\n", r);
		return 1;
	}

	if((r = bsd_init()) != RESULT_OK)
	{
		printf("- failed to init bsd: 0x%x, %d\n", r, bsd_errno);
		sm_finalize();
		return 2;
	}

	if((r = hid_init()) != RESULT_OK)
	{
		printf("- failed to init hid: 0x%x\n", r);
		bsd_finalize();
		sm_finalize();
		return 3;
	}
	bsd_ipc_init();

	connect_net();
#endif

	srand(time(NULL));

	ret = setjmp(exitenv);
	if(ret)
	{
		main_finish();
		return ret - 1;
	}

	D_DoomMain();
	return 0;
}

//
// client functions

void I_InitNetwork()
{
	int ip[4];
	int port;
	int p = M_CheckParm ("-connect");

	if(p && p < myargc-1 && sscanf(myargv[p+1], "%u.%u.%u.%u:%u", &ip[0], &ip[1], &ip[2], &ip[3], &port) == 5)
	{
		netgame = 1;
		strcpy(network_message, "connecting to server\n....");
		client_addr.sin_addr.s_addr = make_ip(ip[0],ip[1],ip[2],ip[3]);
		client_addr.sin_port = htons(port);
		client_socket = bsd_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if(client_socket < 0)
			I_Error("I_InitNetwork: failed to create socket");
		if(bsd_connect(client_socket, (struct sockaddr*)&client_addr, sizeof(client_addr)))
			I_Error("I_InitNetwork: failed to connect socket");
		// local spectator player
		consoleplayer = MAXPLAYERS;
	} else
	{
		// singleplayer game
		consoleplayer = 0;
		playercount = 1;
	}
	displayplayer = consoleplayer;
	playeringame[consoleplayer] = true;
}

void I_NetUpdate()
{
	static int animate;
	int length;
	uint8_t cmd;
	uint32_t packet;
	static uint32_t packetnum;
#ifndef LINUX
	static struct bsd_pollfd pfd;
#endif

	if(!netgame)
		return;

	// read and parse all packets
	while(1)
	{
#ifdef LINUX
		length = 0;
		ioctl(client_socket, FIONREAD, &length);
		if(length <= 0)
			break;
		if(length > MAX_PACKET_LEN) // should never happen with normal clients
			length = MAX_PACKET_LEN;
		recvlen = recv(client_socket, recvbuf, length, 0);
#else
		pfd.fd = client_socket;
		pfd.events = BSD_POLLIN;
		pfd.revents = 0;

		length = bsd_poll(&pfd, 1, 0);
		if(length != 1 || !(pfd.revents & BSD_POLLIN))
			break;

		length = MAX_PACKET_LEN;
		recvlen = bsd_recv(client_socket, recvbuf, length, 0);
		if(recvlen <= 0)
			break;
#endif
		recvpos = 0;
		if(!NET_GetUint32(&packet))
		{
			if(packet == 0xFFFFFFFF)
			{
				// unreliable; get current reliable packet number, just to check for any losses
				NET_GetUint32(&packet);
				// check for eventual packet loss, do not advance anything
				if(packet >= packetnum)
				{
					// missed at least one packet, request
					CL_RequestResend(packetnum);
					// ignore rest of this packet
					continue;
				}
			} else
			{
				if(packet != packetnum)
				{
					if(packet > packetnum)
					{
						// request resend
						CL_RequestResend(packetnum);
					}
					// ignore rest of this packet
					continue;
				} else
					// advance counter
					packetnum = packet + 1;
			}
			while(!NET_GetByte(&cmd))
			{
				last_packet_tic = gametic;
				switch(cmd)
				{
					case SVM_CONNECT:
						CL_CmdConnected();
					break;
					case SVM_DISCONNECT:
						CL_CmdDisconnected();
					break;
					case SVM_KEEP_ALIVE:
						// do nothing
					break;
					case SVM_CHANGE_SECTOR:
						CL_CmdChangeSector();
					break;
					case SVM_CHANGE_LSIDE:
						CL_CmdChangeSidedef();
					break;
					case SVM_SPAWN_MOBJ:
						CL_CmdSpawnMobj();
					break;
					case SVM_SPAWN_PLAYER:
						CL_CmdSpawnPlayer();
					break;
					case SVM_CEILING:
						CL_CmdCeiling();
					break;
					case SVM_FLOOR:
						CL_CmdFloor();
					break;
					case SVM_PLAYER_INFO:
						CL_CmdPlayerInfo();
					break;
					case SVM_UPDATE_MOBJ:
						CL_CmdUpdateMobj();
					break;
					case SVM_REMOVE_MOBJ:
						CL_CmdRemoveMobj();
					break;
					case SVM_PLAYER_PICKUP:
						CL_CmdPlayerPickup();
					break;
					case SVM_PLAYER_INVENTORY:
						CL_CmdPlayerInventory();
					break;
					case SVM_PLAYER_MESSAGE:
						CL_CmdPlayerMessage();
					break;
					case SVM_SOUND:
						CL_CmdSound();
					break;
					default:
						// something is wrong, ignore packet
						recvpos = recvlen;
					break;
				}
			}
		}
	}

	if(netgame == 1)
	{
		// connecting
		if(net_timeout < gametic)
		{
			// send request
			CL_Connect();
			// reset timeout
			net_timeout = gametic + TICRATE;
			network_message[22+animate] = '.';
			animate = (animate + 1) & 3;
			network_message[22+animate] = 0;
		}
	} else
	if(netgame == 2)
	{
		if(gamestate == GS_LEVEL)
		{
			if(net_timeout < gametic)
			{
				net_timeout = gametic + CLIENT_MOTDTIME;
				netgame = 3;
				CL_Loaded();
				printf("- level loaded\n");
			}
		}
	} else
	{
		if(net_timeout && net_timeout < gametic)
		{
			// remove MOTD
			net_timeout = 0;
			network_message[0] = 0;
		}
		if(netgame == 3)
		{
			if(last_packet_tic + 2 * SERVER_CLIENT_KEEPALIVE < gametic)
			{
				netgame = 4;
				strcpy(network_message, "Connection lost ...");
			} else
			if(keep_alive < gametic)
				CL_KeepAlive();
		} else
		{
			if(last_packet_tic + 2 * SERVER_CLIENT_KEEPALIVE > gametic)
			{
				netgame = 3;
				network_message[0] = 0;
			}
		}
	}

	// spectator join
	if(players[consoleplayer].cheats & CF_SPECTATOR && (gamekeydown[key_use] || joybuttons[joybuse]))
	{
		gamekeydown[key_use] = 0;
		joybuttons[joybuse] = 0;
		CL_Join(0);
	}

	// send out all buffered commands
	NET_SendCommand();
}

