#define	BGCOLOR		7
#define	FGCOLOR		8


#ifdef NORMALUNIX
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif


#include "doomdef.h"
#include "doomstat.h"

#include "dstrings.h"
#include "sounds.h"


#include "z_zone.h"
#include "w_wad.h"
#include "s_sound.h"
#include "v_video.h"

#include "f_finale.h"

#include "m_argv.h"
#include "m_misc.h"
#include "m_menu.h"

#include "i_system.h"
#include "i_sound.h"
#include "i_video.h"

#include "g_game.h"

#include "hu_stuff.h"
#include "wi_stuff.h"
#include "st_stuff.h"
#include "am_map.h"

#include "p_setup.h"
#include "r_local.h"


#include "d_main.h"

//
// D-DoomLoop()
// Not a globally visible function,
//  just included for source reference,
//  called by D_DoomMain, never exits.
// Manages timing and IO,
//  calls all ?_Responder, ?_Ticker, and ?_Drawer,
//  calls I_GetTime, I_StartFrame, and I_StartTic
//
void D_DoomLoop (void);


boolean		devparm;	// started game with -devparm
boolean         nomonsters;	// checkparm of -nomonsters
boolean         respawnparm;	// checkparm of -respawn
boolean         fastparm;	// checkparm of -fast

boolean         drone;

//extern int soundVolume;
//extern  int	sfxVolume;
//extern  int	musicVolume;

extern  boolean	inhelpscreens;

skill_t		startskill;
int             startepisode;
int		startmap;
boolean		autostart;

FILE*		debugfile;

boolean		advancedemo;
void D_AdvanceDemo (void);



char		wadfile[1024];		// primary wad file
char		mapdir[1024];           // directory of development maps
char		basedefault[1024];      // default file


void D_ProcessEvents (void);
void G_BuildTiccmd (ticcmd_t* cmd);
void D_DoAdvanceDemo (void);

#ifndef SERVER
//
// EVENT HANDLING
//
// Events are asynchronous inputs generally generated by the game user.
// Events can be discarded if no responder claims them
//
event_t         events[MAXEVENTS];
int             eventhead;
int 		eventtail;


//
// D_PostEvent
// Called by the I/O functions when input is detected
//
void D_PostEvent (event_t* ev)
{
    events[eventhead] = *ev;
    eventhead++;
    eventhead &= (MAXEVENTS-1);
}


//
// D_ProcessEvents
// Send all the events of the given timestamp down the responder chain
//
void D_ProcessEvents (void)
{
    event_t*	ev;

    for ( ; eventtail != eventhead ; eventtail++, eventtail &= (MAXEVENTS-1) )
    {
	ev = &events[eventtail];
	if (M_Responder (ev))
	    continue;               // menu ate the event
	G_Responder (ev);
    }
}

//
// D_Display
//  draw current display, possibly wiping it from the previous
//

extern  boolean setsizeneeded;
extern  int             showMessages;
void R_ExecuteSetViewSize (void);

void D_Display (void)
{
    static  boolean		viewactivestate = false;
    static  boolean		menuactivestate = false;
    static  boolean		inhelpscreensstate = false;
    static  gamestate_t		oldgamestate = -1;
    int				nowtime;
    int				tics;
    int				y;
    boolean			done;
    boolean			redrawsbar;

    if (nodrawers)
	return;                    // for comparative timing / profiling
		
    redrawsbar = false;
    
    // change the view size if needed
    if (setsizeneeded)
    {
	R_ExecuteSetViewSize ();
	oldgamestate = -1;                      // force background redraw
    }

    if (gamestate == GS_LEVEL && gametic)
	HU_Erase();
    
    // do buffered drawing
    switch (gamestate)
    {
      case GS_LEVEL:
	if (!gametic)
	    break;
	if (automapactive)
	    AM_Drawer ();
	break;

      case GS_INTERMISSION:
	WI_Drawer ();
	break;

      case GS_FINALE:
	F_Drawer ();
	break;

      case GS_DEMOSCREEN:
	D_PageDrawer ();
	break;
    }
    
    // draw buffered stuff to screen
    I_UpdateNoBlit ();
    
    // draw the view directly
    if (gamestate == GS_LEVEL && gametic && (!netgame || netgame > 2))
    {
	if(!automapactive)
	    R_RenderPlayerView (&players[displayplayer]);
	ST_Drawer(0, 0);
	HU_Drawer ();
    }

    // clean up border stuff
    if (gamestate != oldgamestate && gamestate != GS_LEVEL)
	I_SetPalette (W_CacheLumpName ("PLAYPAL"));

    // see if the border needs to be initially drawn
    if (gamestate == GS_LEVEL && oldgamestate != GS_LEVEL)
    {
	viewactivestate = false;        // view was not active
//	R_FillBackScreen ();    // draw the pattern into the back screen
    }

    menuactivestate = menuactive;
    viewactivestate = viewactive;
    inhelpscreensstate = inhelpscreens;
    oldgamestate = gamestate;
    
    // draw pause pic
    if (paused)
    {
	if (automapactive)
	    y = 4;
	else
	    y = viewwindowy+4;
	V_DrawPatch(viewwindowx+(scaledviewwidth-68)/2,
			  y,0,W_CacheLumpName ("M_PAUSE"));
    }

    // [kg] network screen
    D_NetDrawer();

    // menus go directly to the screen
    M_Drawer ();          // menu is drawn even on top of everything
    NetUpdate ();         // send out any new accumulation


    // normal update
	I_FinishUpdate ();              // page flip or blit buffer
	return;
}

#endif

#ifndef LINUX
void I_UpdateSound();
#endif

void I_NetUpdate();

//
//  D_DoomLoop
//
void D_DoomLoop (void)
{
/*		
    if (M_CheckParm ("-debugfile"))
    {
	char    filename[20];
	sprintf (filename,"debug%i.txt",consoleplayer);
	printf ("debug output to: %s\n",filename);
	debugfile = fopen (filename,"w");
    }
*/	

    printf("D_DoomLoop: it begins ...\n");
#ifdef SERVER
    while (1)
    {
	extern void I_NetworkPoll();
	I_NetworkPoll();
	// process one or more tics
	TryRunTics();
	// update network
	I_NetUpdate();
    }
#else
    while (1)
    {
	// update network
	I_NetUpdate();
	// frame syncronous IO operations
	I_StartFrame ();
	// process one or more tics
	TryRunTics();
	// move positional sounds
	S_UpdateSounds (players[displayplayer].mo);
#ifndef LINUX
	// update sound for switch
	I_UpdateSound();
#endif
	// Update display, next frame, with current state.
	D_Display ();
    }
#endif
}


#ifndef SERVER

//
//  DEMO LOOP
//
int             demosequence;
int             pagetic;
char                    *pagename;


//
// D_PageTicker
// Handles timing for warped projection
//
void D_PageTicker (void)
{
    if (--pagetic < 0)
	D_AdvanceDemo ();
}

//
// D_PageDrawer
//
void D_PageDrawer (void)
{
    V_DrawPatch (0,0, 0, W_CacheLumpName(pagename));
}


//
// D_AdvanceDemo
// Called after each demo or intro demosequence finishes
//
void D_AdvanceDemo (void)
{
    advancedemo = true;
}


//
// This cycles through the demo sequences.
// FIXME - version dependend demo numbers?
//
 void D_DoAdvanceDemo (void)
{
    players[consoleplayer].playerstate = PST_LIVE;  // not reborn
    advancedemo = false;
    usergame = false;               // no save / end game here
    paused = false;
    gameaction = ga_nothing;

    if ( gamemode == retail )
      demosequence = (demosequence+1)%7;
    else
      demosequence = (demosequence+1)%6;
    
    switch (demosequence)
    {
      case 0:
	if ( gamemode == commercial )
	    pagetic = 35 * 11;
	else
	    pagetic = 170;
	gamestate = GS_DEMOSCREEN;
	pagename = "TITLEPIC";
	if ( gamemode == commercial )
	  S_StartMusic(mus_dm2ttl);
	else
	  S_StartMusic (mus_intro);
	break;
      case 1:
//	G_DeferedPlayDemo ("demo1");
	break;
      case 2:
	pagetic = 200;
	gamestate = GS_DEMOSCREEN;
	pagename = "CREDIT";
	break;
      case 3:
//	G_DeferedPlayDemo ("demo2");
	break;
      case 4:
	gamestate = GS_DEMOSCREEN;
	if ( gamemode == commercial)
	{
	    pagetic = 35 * 11;
	    pagename = "TITLEPIC";
	    S_StartMusic(mus_dm2ttl);
	}
	else
	{
	    pagetic = 200;

	    if ( gamemode == retail )
	      pagename = "CREDIT";
	    else
	      pagename = "HELP2";
	}
	break;
      case 5:
//	G_DeferedPlayDemo ("demo3");
	break;
        // THE DEFINITIVE DOOM Special Edition demo
      case 6:
//	G_DeferedPlayDemo ("demo4");
	break;
    }
}

//
// D_StartTitle
//
void D_StartTitle (void)
{
    gameaction = ga_nothing;
    demosequence = -1;
    D_AdvanceDemo ();
}

#endif

//      print title for every printed line
char            title[128];

void I_InitNetwork();

//
// D_DoomMain
//
void D_DoomMain (void)
{
    int             p;
    char *iwad;

    printf ("Z_Init: Init zone memory allocation daemon. \n");
    Z_Init ();

    printf ("I_InitNetwork: init kgsws' multiplayer protocol\n");
    I_InitNetwork();

    printf ("W_Init: Init WADfiles.\n");

	// pick an IWAD
	gamemode = shareware;
	iwad = "doom1.wad";

	if(M_CheckParm ("-doom"))
	{
		gamemode = registered;
		iwad = "doom.wad";
	} else
	if(M_CheckParm ("-doomu"))
	{
		gamemode = retail;
		iwad = "doomu.wad";
	} else
	if(M_CheckParm ("-tnt"))
	{
		gamemode = commercial;
		iwad = "tnt.wad";
	} else
	if(M_CheckParm ("-plutonia"))
	{
		gamemode = commercial;
		iwad = "plutonia.wad";
	} else
	if(M_CheckParm ("-doom2"))
	{
		gamemode = commercial;
		iwad = "doom2.wad";
	} else
	if(M_CheckParm ("-freedoom"))
	{
		gamemode = retail;
		iwad = "freedoom1.wad";
	} else
	if(M_CheckParm ("-freedoom2"))
	{
		gamemode = commercial;
		iwad = "freedoom2.wad";
	}

	W_LoadWad(iwad);

	// add PWADs
	p = M_CheckParm ("-file");
	if(p)
	{
		// the parms after p are wadfile/lump names,
		// until end of parms or another - preceded parm
		while(++p != myargc && myargv[p][0] != '-')
			W_LoadWad(myargv[p]);
	}

#ifndef SERVER
    printf ("V_Init: Init 2D lib.\n");
    V_Init();
#endif

    setbuf (stdout, NULL);

    nomonsters = M_CheckParm ("-nomonsters");
    respawnparm = M_CheckParm ("-respawn");
//    fastparm = M_CheckParm ("-fast");

    switch ( gamemode )
    {
      case retail:
	sprintf (title,
		 "                         "
		 "The Ultimate DOOM Startup v%i.%i"
		 "                           ",
		 VERSION/100,VERSION%100);
	break;
      case shareware:
	sprintf (title,
		 "                            "
		 "DOOM Shareware Startup v%i.%i"
		 "                           ",
		 VERSION/100,VERSION%100);
	break;
      case registered:
	sprintf (title,
		 "                            "
		 "DOOM Registered Startup v%i.%i"
		 "                           ",
		 VERSION/100,VERSION%100);
	break;
      case commercial:
	sprintf (title,
		 "                         "
		 "DOOM 2: Hell on Earth v%i.%i"
		 "                           ",
		 VERSION/100,VERSION%100);
	break;
/*FIXME
       case pack_plut:
	sprintf (title,
		 "                   "
		 "DOOM 2: Plutonia Experiment v%i.%i"
		 "                           ",
		 VERSION/100,VERSION%100);
	break;
      case pack_tnt:
	sprintf (title,
		 "                     "
		 "DOOM 2: TNT - Evilution v%i.%i"
		 "                           ",
		 VERSION/100,VERSION%100);
	break;
*/
      default:
	sprintf (title,
		 "                     "
		 "Public DOOM - v%i.%i"
		 "                           ",
		 VERSION/100,VERSION%100);
	break;
    }
    
    printf ("%s\n",title);

//    if (devparm)
//	printf(D_DEVSTR);

    // turbo option
    if ( (p=M_CheckParm ("-turbo")) )
    {
	int     scale = 200;
	extern int forwardmove[2];
	extern int sidemove[2];
	
	if (p<myargc-1)
	    scale = atoi (myargv[p+1]);
	if (scale < 10)
	    scale = 10;
	if (scale > 400)
	    scale = 400;
	printf ("turbo scale: %i%%\n",scale);
	forwardmove[0] = forwardmove[0]*scale/100;
	forwardmove[1] = forwardmove[1]*scale/100;
	sidemove[0] = sidemove[0]*scale/100;
	sidemove[1] = sidemove[1]*scale/100;
    }
    
    // add any files specified on the command line with -file wadfile
    // to the wad list
    //
    // convenience hack to allow -wart e m to add a wad file
    // prepend a tilde to the filename so wadfile will be reloadable
/*    p = M_CheckParm ("-wart");
    if (p)
    {
	myargv[p][4] = 'p';     // big hack, change to -warp

	// Map name handling.
	switch (gamemode )
	{
	  case shareware:
	  case retail:
	  case registered:
	    sprintf (file,"~"DEVMAPS"E%cM%c.wad",
		     myargv[p+1][0], myargv[p+2][0]);
	    printf("Warping to Episode %s, Map %s.\n",
		   myargv[p+1],myargv[p+2]);
	    break;
	    
	  case commercial:
	  default:
	    p = atoi (myargv[p+1]);
	    if (p<10)
	      sprintf (file,"~"DEVMAPS"cdata/map0%i.wad", p);
	    else
	      sprintf (file,"~"DEVMAPS"cdata/map%i.wad", p);
	    break;
	}
	D_AddFile (file);
    }

    p = M_CheckParm ("-file");
    if (p)
    {
	// the parms after p are wadfile/lump names,
	// until end of parms or another - preceded parm
	modifiedgame = true;            // homebrew levels
	while (++p != myargc && myargv[p][0] != '-')
	    D_AddFile (myargv[p]);
    }

    p = M_CheckParm ("-playdemo");

    if (!p)
	p = M_CheckParm ("-timedemo");

    if (p && p < myargc-1)
    {
	sprintf (file,"%s.lmp", myargv[p+1]);
	D_AddFile (file);
	printf("Playing demo %s.lmp.\n",myargv[p+1]);
    }
*/
    // get skill / episode / map from parms
    startskill = sk_medium;
    startepisode = 1;
    startmap = 1;
    autostart = false;

    if(M_CheckParm("-deathmatch"))
	deathmatch = true;
		
    p = M_CheckParm ("-skill");
    if (p && p < myargc-1)
    {
	startskill = myargv[p+1][0]-'1';
	autostart = true;
    }

    p = M_CheckParm ("-episode");
    if (p && p < myargc-1)
    {
	startepisode = myargv[p+1][0]-'0';
	startmap = 1;
	autostart = true;
    }
	
    p = M_CheckParm ("-timer");
    if (p && p < myargc-1 && deathmatch)
    {
	int     time;
	time = atoi(myargv[p+1]);
	printf("Levels will end after %d minute",time);
	if (time>1)
	    printf("s");
	printf(".\n");
    }

    p = M_CheckParm ("-avg");
    if (p && p < myargc-1 && deathmatch)
	printf("Austin Virtual Gaming: Levels will end after 20 minutes\n");

    p = M_CheckParm ("-warp");
    if (p && p < myargc-1)
    {
	if (gamemode == commercial)
	    startmap = atoi (myargv[p+1]);
	else
	{
	    startepisode = myargv[p+1][0]-'0';
	    startmap = myargv[p+2][0]-'0';
	}
	autostart = true;
    }

#ifndef SERVER    
    // init subsystems
    printf ("M_LoadDefaults: Load system defaults.\n");
    M_LoadDefaults ();              // load before initing other systems
#endif

    // Check and print which version is executed.
    switch ( gamemode )
    {
      case shareware:
      case indetermined:
	printf (
	    "===========================================================================\n"
	    "                                Shareware!\n"
	    "===========================================================================\n"
	);
	break;
      case registered:
      case retail:
      case commercial:
	printf (
	    "===========================================================================\n"
	    "                 Commercial product - do not distribute!\n"
	    "         Please report software piracy to the SPA: 1-800-388-PIR8\n"
	    "===========================================================================\n"
	);
	break;
	
      default:
	// Ouch.
	break;
    }

#ifndef SERVER
    printf ("M_Init: Init miscellaneous info.\n");
    M_Init ();
#endif

    printf ("R_Init: Init DOOM refresh daemon - ");
    R_Init ();

    printf ("\nP_Init: Init Playloop state.\n");
    P_Init ();

    printf ("I_Init: Setting up machine state.\n");
    I_Init ();

    printf ("S_Init: Setting up sound.\n");
    S_Init (snd_SfxVolume /* *8 */, snd_MusicVolume /* *8*/ );

#ifdef SERVER
    G_InitNew (startskill, startepisode, startmap);
#else
    printf ("HU_Init: Setting up heads up display.\n");
    HU_Init ();

    printf ("ST_Init: Init status bar.\n");
    ST_Init ();

    if (netgame)
	D_StartNet();
    else
	if(autostart)
	    G_DeferedInitNew (startskill, startepisode, startmap);
	else
	    D_StartTitle();                // start up intro loop

#endif

    D_DoomLoop ();  // never returns
}

