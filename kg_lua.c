// LUA game scripting
// by kgsws
// TODO: better string search algorithm for Lua indexing
#include "doomdef.h"
#include "doomstat.h"
#include "i_system.h"
#include "p_local.h"
#include "p_pickup.h"
#include "p_generic.h"

#include "w_wad.h"
#include "z_zone.h"

#include "s_sound.h"

#include "m_random.h"

#include "p_inventory.h"

#include "g_game.h"

#include "r_sky.h"

#include "st_stuff.h"
#include "hu_stuff.h"

#ifdef LINUX
#include <lua5.3/lua.h>
#include <lua5.3/lauxlib.h>
#include <lua5.3/lualib.h>
#else
#include "lua5.3.4/lua.h"
#include "lua5.3.4/lauxlib.h"
#include "lua5.3.4/lualib.h"
#endif

#include "kg_3dfloor.h"
#include "kg_lua.h"

#ifdef SERVER
#include <netinet/in.h>
#include "network.h"
#include "sv_cmds.h"
#endif

// function export masks
#define LUA_EXPORT_SETUP	1
#define LUA_EXPORT_LEVEL	2

// used to export C functions
typedef struct
{
	char *name;
	int (*func)(lua_State *L);
	int mask;
} luafunc_t;

// used to make 'model' of a C structure
typedef struct lua_table_model_s
{
	const char *name;
	int offset;
	int ltype;
	int (*set)(lua_State*, void*, void*);
	int (*get)(lua_State*, void*, void*);
} lua_table_model_t;

// model for mobj flags
typedef struct
{
	const char *name;
	uint64_t value; // flag combinations are used
} lua_mobjflag_t;

// model for exported state actions
typedef struct
{
	const char *name;
	void (*func)(mobj_t*);
} lua_mobjaction_t;

// model for exported integers
typedef struct
{
	const char *name;
	int value;
} lua_intvalue_t;

const char *const thinker_names[] =
{
	"invalid",
	"mobj",
	"mobjinfo",
	"player",
	"sector",
	"line",
	"genericPlane",
	"genericCaller",
	"textureScroll",
};

static lua_State *luaS_game;
static boolean lua_setup;

static int linespec_table[256];
int linedef_side;

void *game_backpatch;

static int generic_sector_removed;

static lua_State *luaS_PIT;
static int block_lua_func;
static int block_lua_arg;
static int block_lua_x0;
static int block_lua_x1;
static int block_lua_y0;
static int block_lua_y1;
boolean PIT_LuaCheckThing(mobj_t* thing);

//
// tables

int LUA_GetMobjTypeParam(lua_State *L, int idx);

static int LUA_ThinkerIndex(lua_State *L);

static int LUA_print(lua_State *L);
static int LUA_createMobjType(lua_State *L);
static int LUA_setPlayerType(lua_State *L);
static int LUA_addWeaponType(lua_State *L);
static int LUA_addKeyType(lua_State *L);
static int LUA_addAnimation(lua_State *L);
static int LUA_doomRandom(lua_State *L);
static int LUA_spawnMobj(lua_State *L);
static int LUA_blockThingsIterator(lua_State *L);
static int LUA_globalPlayersIterator(lua_State *L);
static int LUA_globalThingsIterator(lua_State *L);
static int LUA_globalSectorsIterator(lua_State *L);
static int LUA_globalLinesIterator(lua_State *L);
static int LUA_sectorTagIterator(lua_State *L);
static int LUA_thingTagIterator(lua_State *L);
static int LUA_setFakeContrast(lua_State *L);
static int LUA_setSky(lua_State *L);
static int LUA_setBackground(lua_State *L);

static int LUA_removeMobjFromMobj(lua_State *L);
static int LUA_faceFromMobj(lua_State *L);
static int LUA_teleportMobj(lua_State *L);
static int LUA_checkMobjPos(lua_State *L);
static int LUA_spawnMissile(lua_State *L);
static int LUA_attackAim(lua_State *L);
static int LUA_lineTarget(lua_State *L);
static int LUA_meleeRange(lua_State *L);
static int LUA_mobjDistance(lua_State *L);
static int LUA_damageFromMobj(lua_State *L);
static int LUA_lineAttack(lua_State *L);
static int LUA_thrustFromMobj(lua_State *L);
static int LUA_sightCheckFromMobj(lua_State *L);
static int LUA_radiusDamageFromMobj(lua_State *L);
static int LUA_damageScaleFromMobj(lua_State *L);
static int LUA_resetFlagsFromMobj(lua_State *L);

static int LUA_soundFromMobj(lua_State *L);

static int LUA_mobjInventoryGive(lua_State *L);
static int LUA_mobjInventoryTake(lua_State *L);
static int LUA_mobjInventoryCheck(lua_State *L);
static int LUA_mobjInventorySetMax(lua_State *L);
static int LUA_mobjTickerSet(lua_State *L);
static int LUA_mobjTickerDel(lua_State *L);
static int LUA_mobjTickerCheck(lua_State *L);

static int LUA_setPlayerMessage(lua_State *L);
static int LUA_playerHudMessage(lua_State *L);
static int LUA_playerHudAlign(lua_State *L);
static int LUA_playerHudScale(lua_State *L);
static int LUA_setPlayerWeapon(lua_State *L);
static int LUA_playerRefireWeapon(lua_State *L);
static int LUA_playerFlashWeapon(lua_State *L);

static int LUA_heightFromSector(lua_State *L);
static int LUA_lightFromSector(lua_State *L);
static int LUA_shortTexFromSector(lua_State *L);
static int LUA_genericPlaneFromSector(lua_State *L);
static int LUA_genericCallFromSector(lua_State *L);
static int LUA_sectorThingIterator(lua_State *L);
static int LUA_sectorLineIterator(lua_State *L);
static int LUA_sectorSetDamage(lua_State *L);
static int LUA_sectorAdd3DFloor(lua_State *L);

static int LUA_stopFromGeneric(lua_State *L);
static int LUA_sleepFromGeneric(lua_State *L);
static int LUA_reverseFromGeneric(lua_State *L);

static int LUA_stopFromLine(lua_State *L);

static int LUA_doSwitchTextureLine(lua_State *L);
static int LUA_togSwitchTextureLine(lua_State *L);
static int LUA_scrollFromLine(lua_State *L);

static int LUA_animationFromMobj(lua_State *L);

static int func_set_states(lua_State *L, void *dst, void *o);
static int func_set_damagescale(lua_State *L, void *dst, void *o);
static int func_set_fixedt(lua_State *L, void *dst, void *o);
static int func_get_fixedt(lua_State *L, void *dst, void *o);
static int func_set_short(lua_State *L, void *dst, void *o);
static int func_get_short(lua_State *L, void *dst, void *o);
static int func_set_byte(lua_State *L, void *dst, void *o);
static int func_get_byte(lua_State *L, void *dst, void *o);

static int func_set_lua_regfunc(lua_State *L, void *dst, void *o);
static int func_set_lua_registry(lua_State *L, void *dst, void *o);
static int func_get_lua_registry(lua_State *L, void *dst, void *o);

static int func_set_readonly(lua_State *L, void *dst, void *o);
static int func_get_writeonly(lua_State *L, void *dst, void *o);
static int func_set_mobjangle(lua_State *L, void *dst, void *o);
static int func_get_mobjangle(lua_State *L, void *dst, void *o);
static int func_set_mobjtype(lua_State *L, void *dst, void *o);
static int func_set_mobjpitch(lua_State *L, void *dst, void *o);
static int func_set_radius(lua_State *L, void *dst, void *o);
static int func_set_mobj(lua_State *L, void *dst, void *o);
static int func_get_ptr(lua_State *L, void *dst, void *o);
static int func_get_sector(lua_State *L, void *dst, void *o);
static int func_set_playermobj(lua_State *L, void *dst, void *o);
static int func_get_playermoving(lua_State *L, void *dst, void *o);

static int func_get_removemobj(lua_State *L, void *dst, void *o);
static int func_get_facemobj(lua_State *L, void *dst, void *o);
static int func_get_teleportmobj(lua_State *L, void *dst, void *o);
static int func_get_checkmobjpos(lua_State *L, void *dst, void *o);
static int func_get_spawnmissile(lua_State *L, void *dst, void *o);
static int func_get_attackaim(lua_State *L, void *dst, void *o);
static int func_get_linetarget(lua_State *L, void *dst, void *o);
static int func_get_meleerange(lua_State *L, void *dst, void *o);
static int func_get_distance(lua_State *L, void *dst, void *o);
static int func_get_damage(lua_State *L, void *dst, void *o);
static int func_get_lineattack(lua_State *L, void *dst, void *o);
static int func_get_thrustmobj(lua_State *L, void *dst, void *o);
static int func_get_checkmobjsight(lua_State *L, void *dst, void *o);
static int func_get_mobjradiusdmg(lua_State *L, void *dst, void *o);
static int func_get_mobjdmgscale(lua_State *L, void *dst, void *o);
static int func_get_mobjresetflags(lua_State *L, void *dst, void *o);

static int func_get_mobjsound_body(lua_State *L, void *dst, void *o);
static int func_get_mobjsound_weapon(lua_State *L, void *dst, void *o);
static int func_get_mobjsound_pickup(lua_State *L, void *dst, void *o);

static int func_get_mobj_invgive(lua_State *L, void *dst, void *o);
static int func_get_mobj_invtake(lua_State *L, void *dst, void *o);
static int func_get_mobj_invcheck(lua_State *L, void *dst, void *o);
static int func_get_mobj_invsetmax(lua_State *L, void *dst, void *o);
static int func_get_mobj_tickerset(lua_State *L, void *dst, void *o);
static int func_get_mobj_tickerdel(lua_State *L, void *dst, void *o);
static int func_get_mobj_tickerchk(lua_State *L, void *dst, void *o);

static int func_set_colormap(lua_State *L, void *dst, void *o);
static int func_get_colormap(lua_State *L, void *dst, void *o);
static int func_set_colorfog(lua_State *L, void *dst, void *o);
static int func_get_colorfog(lua_State *L, void *dst, void *o);

static int func_setplayermessage(lua_State *L, void *dst, void *o);
static int func_playerhudmessage(lua_State *L, void *dst, void *o);
static int func_playerhudalign(lua_State *L, void *dst, void *o);
static int func_playerhudscale(lua_State *L, void *dst, void *o);
static int func_setplayerweapon(lua_State *L, void *dst, void *o);
static int func_playerrefire(lua_State *L, void *dst, void *o);
static int func_playerwflash(lua_State *L, void *dst, void *o);

static int func_set_shade(lua_State *L, void *dst, void *o);
static int func_get_shade(lua_State *L, void *dst, void *o);
static int func_set_deepfog(lua_State *L, void *dst, void *o);
static int func_get_deepfog(lua_State *L, void *dst, void *o);
static int func_set_secretsector(lua_State *L, void *dst, void *o);
static int func_get_secretsector(lua_State *L, void *dst, void *o);
static int func_get_lowefloorsector(lua_State *L, void *dst, void *o);
static int func_get_highfloorsector(lua_State *L, void *dst, void *o);
static int func_get_nextfloorsector(lua_State *L, void *dst, void *o);
static int func_get_loweceilingsector(lua_State *L, void *dst, void *o);
static int func_get_highceilingsector(lua_State *L, void *dst, void *o);
static int func_get_highlightsector(lua_State *L, void *dst, void *o);
static int func_get_lowlightsector(lua_State *L, void *dst, void *o);
static int func_get_shortexfloorsector(lua_State *L, void *dst, void *o);
static int func_get_genericfloorsector(lua_State *L, void *dst, void *o);
static int func_get_genericceilingsector(lua_State *L, void *dst, void *o);
static int func_get_genericcallsectorfloor(lua_State *L, void *dst, void *o);
static int func_get_genericcallsectorceiling(lua_State *L, void *dst, void *o);
static int func_get_genericcallsector(lua_State *L, void *dst, void *o);
static int func_get_thingitersector(lua_State *L, void *dst, void *o);
static int func_get_lineitersector(lua_State *L, void *dst, void *o);
static int func_get_setdamagesector(lua_State *L, void *dst, void *o);
static int func_get_add3dfloorsector(lua_State *L, void *dst, void *o);

static int func_set_sectorheight(lua_State *L, void *dst, void *o);
static int func_set_flattexture(lua_State *L, void *dst, void *o);
static int func_get_flattexture(lua_State *L, void *dst, void *o);

static int func_get_lumpname(lua_State *L, void *dst, void *o);
static int func_set_lumpname_optional(lua_State *L, void *dst, void *o);

static int func_set_renderstyle(lua_State *L, void *dst, void *o);
//static int func_get_renderstyle(lua_State *L, void *dst, void *o);

static int func_set_sideset(lua_State *L, void *dst, void *o);
static int func_get_sideset(lua_State *L, void *dst, void *o);

static int func_set_walltexture(lua_State *L, void *dst, void *o);
static int func_get_walltexture(lua_State *L, void *dst, void *o);
static int func_set_line_horizon(lua_State *L, void *dst, void *o);
static int func_get_line_horizon(lua_State *L, void *dst, void *o);
static int func_get_linedefsectorF(lua_State *L, void *dst, void *o);
static int func_get_linedefsectorB(lua_State *L, void *dst, void *o);
static int func_getlinefunc_ptr(lua_State *L, void *dst, void *o);
static int func_get_doswitchline(lua_State *L, void *dst, void *o);
static int func_get_togswitchline(lua_State *L, void *dst, void *o);
static int func_get_scrollerline(lua_State *L, void *dst, void *o);

static int func_get_genericpltypef(lua_State *L, void *dst, void *o);
static int func_get_genericpltypec(lua_State *L, void *dst, void *o);
static int func_get_genericpltypel(lua_State *L, void *dst, void *o);
static int func_get_genericplsuspended(lua_State *L, void *dst, void *o);
static int func_get_genericplraising(lua_State *L, void *dst, void *o);
static int func_get_genericpllowering(lua_State *L, void *dst, void *o);
static int func_get_genericstop(lua_State *L, void *dst, void *o);
static int func_get_genericsleep(lua_State *L, void *dst, void *o);
static int func_get_genericreverse(lua_State *L, void *dst, void *o);

static int func_get_texfuncstop(lua_State *L, void *dst, void *o);

// all exported LUA functions
static const luafunc_t lua_functions[] =
{
	{"print", LUA_print, LUA_EXPORT_SETUP | LUA_EXPORT_LEVEL},
	// loading stage
	{"createMobjType", LUA_createMobjType, LUA_EXPORT_SETUP},
	{"setPlayerType", LUA_setPlayerType, LUA_EXPORT_SETUP},
	{"addWeaponType", LUA_addWeaponType, LUA_EXPORT_SETUP},
	{"addKeyType", LUA_addKeyType, LUA_EXPORT_SETUP},
	{"addAnimation", LUA_addAnimation, LUA_EXPORT_SETUP},
	// map stage
	{"doomRandom", LUA_doomRandom, LUA_EXPORT_LEVEL},
	{"spawnMobj", LUA_spawnMobj, LUA_EXPORT_LEVEL},
	{"blockThingsIterator", LUA_blockThingsIterator, LUA_EXPORT_LEVEL},
	{"globalPlayersIterator", LUA_globalPlayersIterator, LUA_EXPORT_LEVEL},
	{"globalThingsIterator", LUA_globalThingsIterator, LUA_EXPORT_LEVEL},
	{"globalSectorsIterator", LUA_globalSectorsIterator, LUA_EXPORT_LEVEL},
	{"globalLinesIterator", LUA_globalLinesIterator, LUA_EXPORT_LEVEL},
	{"sectorTagIterator", LUA_sectorTagIterator, LUA_EXPORT_LEVEL},
	{"thingTagIterator", LUA_thingTagIterator, LUA_EXPORT_LEVEL},
	{"fakeContrast", LUA_setFakeContrast, LUA_EXPORT_LEVEL},
	{"setSky", LUA_setSky, LUA_EXPORT_LEVEL},
	{"setBackground", LUA_setBackground, LUA_EXPORT_LEVEL},
};

// all mobj flags
static const lua_mobjflag_t lua_mobjflags[] =
{
	{"special", MF_SPECIAL},
	{"solid", MF_SOLID},
	{"shootable", MF_SHOOTABLE},
	{"noSector", MF_NOSECTOR},
	{"noBlockmap", MF_NOBLOCKMAP},
	{"ambush", MF_AMBUSH},
	{"spawnCeiling", MF_SPAWNCEILING},
	{"dropOff", MF_DROPOFF},
	{"pickup", MF_PICKUP},
	{"noClip", MF_NOCLIP},
	{"slide", MF_SLIDE},
	{"float", MF_FLOAT},
	{"missile", MF_MISSILE},
	{"dropped", MF_DROPPED},
	{"noBlood", MF_NOBLOOD},
	{"corpse", MF_CORPSE},
	{"countKill", MF_COUNTKILL},
	{"countItem", MF_COUNTITEM},
	{"notInDeathmatch", MF_NOTDMATCH},
	{"isMonster", MF_ISMONSTER},
	{"noRadiusDamage", MF_NORADIUSDMG},
	{"noRadiusDmgZ", MF_NORADIUSZ},
	{"skullFly", MF_SKULLFLY},
	{"noZChange", MF_NOZCHANGE},
	{"noZSpawnCheck", MF_NOZSPAWNCHECK},
	{"troughMobj", MF_TROUGHMOBJ},
	{"fullVolume", MF_FULLVOLUME},
	{"noDeathPull", MF_NODEATHPULL},
	{"invulnerable", MF_INVULNERABLE},
	{"wallBounce", MF_WALLBOUNCE},
	{"mobjBounce", MF_MOBJBOUNCE},
	{"noTarget", MF_NOTARGET},
	{"pushable", MF_PUSHABLE},
	{"skyExplode", MF_SKYEXPLODE},
	{"cantUseLines", MF_CANTUSE},
	{"cantBumpLines", MF_CANTBUMP},
	// custom Lua flags
	{"custom0", MF_CUSTOM0},
	{"custom1", MF_CUSTOM1},
	{"custom2", MF_CUSTOM2},
	{"custom3", MF_CUSTOM3},
	// flag combinations
	{"Monster", MF_ISMONSTER | MF_COUNTKILL | MF_SOLID | MF_SHOOTABLE},
	{"Projectile", MF_MISSILE | MF_NOBLOCKMAP | MF_DROPOFF | MF_NOZCHANGE},
};

// all exported state actions
static const lua_mobjaction_t lua_mobjactions[] =
{
	{"Look", A_Look},
	{"Chase", A_Chase},
	{"Fall", A_Fall},
	{"FaceTarget", A_FaceTarget},

	{"SoundSee", A_SoundSee},
	{"SoundAttack", A_SoundAttack},
	{"SoundPain", A_SoundPain},
	{"SoundActive", A_SoundActive},
	{"SoundDeath", A_SoundDeath},
	{"SoundXDeath", A_SoundXDeath},

	{"WeaponRaise", A_WeaponRaise},
	{"WeaponReady", A_WeaponReady},
	{"WeaponReadyForced", A_WeaponReadyForced},
	{"WeaponLower", A_WeaponLower},
	{"WeaponFlash", A_WeaponFlash},
	{"WeaponRefire", A_WeaponRefire},

	{"NoiseAlert", A_NoiseAlert},
};

// all mobj type values
static const lua_table_model_t lua_mobjtype[] =
{
	// numbers
	{"ednum", offsetof(mobjinfo_t, doomednum), LUA_TNUMBER},
	{"health", offsetof(mobjinfo_t, spawnhealth), LUA_TNUMBER},
	{"reactionTime", offsetof(mobjinfo_t, reactiontime), LUA_TNUMBER},
	{"painChance", offsetof(mobjinfo_t, painchance), LUA_TNUMBER},
	{"speed", offsetof(mobjinfo_t, speed), LUA_TNUMBER, func_set_fixedt, func_get_fixedt},
	{"radius", offsetof(mobjinfo_t, radius), LUA_TNUMBER, func_set_fixedt, func_get_fixedt},
	{"height", offsetof(mobjinfo_t, height), LUA_TNUMBER, func_set_fixedt, func_get_fixedt},
	{"mass", offsetof(mobjinfo_t, mass), LUA_TNUMBER},
	{"damage", offsetof(mobjinfo_t, damage), LUA_TNUMBER},
	{"gravity", offsetof(mobjinfo_t, gravity), LUA_TNUMBER, func_set_fixedt, func_get_fixedt},
	{"bounce", offsetof(mobjinfo_t, bounce), LUA_TNUMBER, func_set_fixedt, func_get_fixedt},
	{"viewz", offsetof(mobjinfo_t, viewz), LUA_TNUMBER, func_set_fixedt, func_get_fixedt},
	{"shootz", offsetof(mobjinfo_t, shootz), LUA_TNUMBER, func_set_fixedt, func_get_fixedt},
	{"bobz", offsetof(mobjinfo_t, bobz), LUA_TNUMBER, func_set_fixedt, func_get_fixedt},
	{"stepheight", offsetof(mobjinfo_t, stepheight), LUA_TNUMBER, func_set_fixedt, func_get_fixedt},
	{"species", offsetof(mobjinfo_t, species), LUA_TNUMBER},
	{"block", offsetof(mobjinfo_t, blocking), LUA_TNUMBER},
	{"pass", offsetof(mobjinfo_t, canpass), LUA_TNUMBER},
	{"icon", offsetof(mobjinfo_t, icon), LUA_TSTRING, func_set_lumpname_optional, func_get_lumpname},
	{"maxcount", offsetof(mobjinfo_t, maxcount), LUA_TNUMBER},
	{"material", offsetof(mobjinfo_t, material), LUA_TNUMBER},
	{"translation", offsetof(mobjinfo_t, translation), LUA_TSTRING, func_set_colormap, func_get_colormap},
	{"render", offsetof(mobjinfo_t, render), LUA_TSTRING, func_set_renderstyle, func_get_writeonly},
	{"action", offsetof(mobjinfo_t, lua_action), LUA_TFUNCTION, func_set_lua_regfunc, func_get_lua_registry},
	{"arg", offsetof(mobjinfo_t, lua_arg), LUA_TNIL, func_set_lua_registry, func_get_lua_registry},
	{"action_crash", offsetof(mobjinfo_t, crash_action), LUA_TFUNCTION, func_set_lua_regfunc, func_get_lua_registry},
	// states; keep same order as in 'mobjinfo_t'
	{"_spawn", offsetof(mobjinfo_t, spawnstate), LUA_TTABLE, func_set_states},
	{"_see", offsetof(mobjinfo_t, seestate), LUA_TTABLE, func_set_states},
	{"_pain", offsetof(mobjinfo_t, painstate), LUA_TTABLE, func_set_states},
	{"_melee", offsetof(mobjinfo_t, meleestate), LUA_TTABLE, func_set_states},
	{"_missile", offsetof(mobjinfo_t, missilestate), LUA_TTABLE, func_set_states},
	{"_death", offsetof(mobjinfo_t, deathstate), LUA_TTABLE, func_set_states},
	{"_xdeath", offsetof(mobjinfo_t, xdeathstate), LUA_TTABLE, func_set_states},
	{"_raise", offsetof(mobjinfo_t, raisestate), LUA_TTABLE, func_set_states},
	{"_crush", offsetof(mobjinfo_t, crushstate), LUA_TTABLE, func_set_states},
	{"_heal", offsetof(mobjinfo_t, healstate), LUA_TTABLE, func_set_states},
	{"_wRaise", offsetof(mobjinfo_t, weaponraise), LUA_TTABLE, func_set_states},
	{"_wReady", offsetof(mobjinfo_t, weaponready), LUA_TTABLE, func_set_states},
	{"_wLower", offsetof(mobjinfo_t, weaponlower), LUA_TTABLE, func_set_states},
	{"_wFireMain", offsetof(mobjinfo_t, weaponfiremain), LUA_TTABLE, func_set_states},
	{"_wFireAlt", offsetof(mobjinfo_t, weaponfirealt), LUA_TTABLE, func_set_states},
	{"_wFlashMain", offsetof(mobjinfo_t, weaponflashmain), LUA_TTABLE, func_set_states},
	{"_wFlashAlt", offsetof(mobjinfo_t, weaponflashalt), LUA_TTABLE, func_set_states},
	// sounds
	{"seeSound", offsetof(mobjinfo_t, seesound), LUA_TSTRING, func_set_lumpname_optional, func_get_lumpname},
	{"attackSound", offsetof(mobjinfo_t, attacksound), LUA_TSTRING, func_set_lumpname_optional, func_get_lumpname},
	{"painSound", offsetof(mobjinfo_t, painsound), LUA_TSTRING, func_set_lumpname_optional, func_get_lumpname},
	{"activeSound", offsetof(mobjinfo_t, activesound), LUA_TSTRING, func_set_lumpname_optional, func_get_lumpname},
	{"deathSound", offsetof(mobjinfo_t, deathsound), LUA_TSTRING, func_set_lumpname_optional, func_get_lumpname},
	{"xdeathSound", offsetof(mobjinfo_t, xdeathsound), LUA_TSTRING, func_set_lumpname_optional, func_get_lumpname},
	{"bounceSound", offsetof(mobjinfo_t, bouncesound), LUA_TSTRING, func_set_lumpname_optional, func_get_lumpname},
	// damage scales
	{"damageScale", 0, LUA_TTABLE, func_set_damagescale},
	{"damagetype", offsetof(mobjinfo_t, damagetype), LUA_TNUMBER},
};

// all mobj values
static const lua_table_model_t lua_mobj[] =
{
	{"angle", offsetof(mobj_t, angle), LUA_TNUMBER, func_set_mobjangle, func_get_mobjangle},
	{"health", offsetof(mobj_t, health), LUA_TNUMBER},
	{"damage", offsetof(mobj_t, damage), LUA_TNUMBER},
	{"damagetype", offsetof(mobj_t, damagetype), LUA_TNUMBER},
	{"armor", offsetof(mobj_t, armorpoints), LUA_TNUMBER},
	{"armortype", offsetof(mobj_t, armortype), LUA_TLIGHTUSERDATA, func_set_mobjtype, func_get_ptr},
	{"pitch", offsetof(mobj_t, pitch), LUA_TNUMBER, func_set_mobjpitch, func_get_fixedt},
	{"floorz", offsetof(mobj_t, floorz), LUA_TNUMBER, func_set_fixedt, func_get_fixedt},
	{"ceilingz", offsetof(mobj_t, ceilingz), LUA_TNUMBER, func_set_fixedt, func_get_fixedt},
	{"radius", offsetof(mobj_t, radius), LUA_TNUMBER, func_set_radius, func_get_fixedt},
	{"height", offsetof(mobj_t, height), LUA_TNUMBER, func_set_fixedt, func_get_fixedt},
	{"momx", offsetof(mobj_t, momx), LUA_TNUMBER, func_set_fixedt, func_get_fixedt},
	{"momy", offsetof(mobj_t, momy), LUA_TNUMBER, func_set_fixedt, func_get_fixedt},
	{"momz", offsetof(mobj_t, momz), LUA_TNUMBER, func_set_fixedt, func_get_fixedt},
	{"gravity", offsetof(mobj_t, gravity), LUA_TNUMBER, func_set_fixedt, func_get_fixedt},
	{"bounce", offsetof(mobj_t, bounce), LUA_TNUMBER, func_set_fixedt, func_get_fixedt},
	{"movedir", offsetof(mobj_t, movedir), LUA_TNUMBER},
	{"movecount", offsetof(mobj_t, movecount), LUA_TNUMBER},
	{"target", offsetof(mobj_t, target), LUA_TLIGHTUSERDATA, func_set_mobj, func_get_ptr},
	{"source", offsetof(mobj_t, source), LUA_TLIGHTUSERDATA, func_set_mobj, func_get_ptr},
	{"attacker", offsetof(mobj_t, attacker), LUA_TLIGHTUSERDATA, func_set_mobj, func_get_ptr},
	{"mobj", offsetof(mobj_t, mobj), LUA_TLIGHTUSERDATA, func_set_mobj, func_get_ptr},
	{"reactiontime", offsetof(mobj_t, reactiontime), LUA_TNUMBER},
	{"threshold", offsetof(mobj_t, threshold), LUA_TNUMBER},
	{"tics", offsetof(mobj_t, tics), LUA_TNUMBER},
	{"tag", offsetof(mobj_t, tag), LUA_TNUMBER},
	{"material", offsetof(mobj_t, material), LUA_TNUMBER},
	{"speed", offsetof(mobj_t, speed), LUA_TNUMBER, func_set_fixedt, func_get_fixedt},
	{"mass", offsetof(mobj_t, mass), LUA_TNUMBER},
	{"render", offsetof(mobj_t, render), LUA_TSTRING, func_set_renderstyle, func_get_writeonly},
	{"block", offsetof(mobj_t, blocking), LUA_TNUMBER},
	{"pass", offsetof(mobj_t, canpass), LUA_TNUMBER},
	{"attacktype", offsetof(mobj_t, damagercv), LUA_TNUMBER},
	// read only
	{"x", offsetof(mobj_t, x), LUA_TNUMBER, func_set_readonly, func_get_fixedt},
	{"y", offsetof(mobj_t, y), LUA_TNUMBER, func_set_readonly, func_get_fixedt},
	{"z", offsetof(mobj_t, z), LUA_TNUMBER, func_set_readonly, func_get_fixedt},
	// special stuff
	{"player", offsetof(mobj_t, player), LUA_TLIGHTUSERDATA, func_set_readonly, func_get_ptr},
	{"sector", 0, LUA_TLIGHTUSERDATA, func_set_readonly, func_get_sector},
	{"info", offsetof(mobj_t, info), LUA_TLIGHTUSERDATA, func_set_readonly, func_get_ptr},
	// colors
	{"translation", offsetof(mobj_t, translation), LUA_TSTRING, func_set_colormap, func_get_colormap},
	{"colormap", offsetof(mobj_t, colormap), LUA_TSTRING, func_set_colormap, func_get_colormap},
	// functions
	{"Remove", 0, LUA_TFUNCTION, func_set_readonly, func_get_removemobj},
	{"Face", 0, LUA_TFUNCTION, func_set_readonly, func_get_facemobj},
	{"Teleport", 0, LUA_TFUNCTION, func_set_readonly, func_get_teleportmobj},
	{"CheckPosition", 0, LUA_TFUNCTION, func_set_readonly, func_get_checkmobjpos},
	{"SpawnMissile", 0, LUA_TFUNCTION, func_set_readonly, func_get_spawnmissile},
	{"AttackAim", 0, LUA_TFUNCTION, func_set_readonly, func_get_attackaim},
	{"LineTarget", 0, LUA_TFUNCTION, func_set_readonly, func_get_linetarget},
	{"MeleeRange", 0, LUA_TFUNCTION, func_set_readonly, func_get_meleerange},
	{"Distance", 0, LUA_TFUNCTION, func_set_readonly, func_get_distance},
	{"Damage", 0, LUA_TFUNCTION, func_set_readonly, func_get_damage},
	{"LineAttack", 0, LUA_TFUNCTION, func_set_readonly, func_get_lineattack},
	{"Thrust", 0, LUA_TFUNCTION, func_set_readonly, func_get_thrustmobj},
	{"CheckSight", 0, LUA_TFUNCTION, func_set_readonly, func_get_checkmobjsight},
	{"RadiusDamage", 0, LUA_TFUNCTION, func_set_readonly, func_get_mobjradiusdmg},
	{"DamageScale", 0, LUA_TFUNCTION, func_set_readonly, func_get_mobjdmgscale},
	{"ResetFlags", 0, LUA_TFUNCTION, func_set_readonly, func_get_mobjresetflags},
	{"SoundBody", 0, LUA_TFUNCTION, func_set_readonly, func_get_mobjsound_body},
	{"SoundWeapon", 0, LUA_TFUNCTION, func_set_readonly, func_get_mobjsound_weapon},
	{"SoundPickup", 0, LUA_TFUNCTION, func_set_readonly, func_get_mobjsound_pickup},
	// inventory functions
	{"InventoryGive", 0, LUA_TFUNCTION, func_set_readonly, func_get_mobj_invgive},
	{"InventoryTake", 0, LUA_TFUNCTION, func_set_readonly, func_get_mobj_invtake},
	{"InventoryCheck", 0, LUA_TFUNCTION, func_set_readonly, func_get_mobj_invcheck},
	{"InventorySetMax", 0, LUA_TFUNCTION, func_set_readonly, func_get_mobj_invsetmax},
	// generic tickers
	{"TickerSet", 0, LUA_TFUNCTION, func_set_readonly, func_get_mobj_tickerset},
	{"TickerRemove", 0, LUA_TFUNCTION, func_set_readonly, func_get_mobj_tickerdel},
	{"TickerCheck", 0, LUA_TFUNCTION, func_set_readonly, func_get_mobj_tickerchk},
};

// all player values
static const lua_table_model_t lua_player[] =
{
	{"mo", offsetof(player_t, mo), LUA_TLIGHTUSERDATA, func_set_playermobj, func_get_ptr},
	{"camera", offsetof(player_t, camera), LUA_TLIGHTUSERDATA, func_set_mobj, func_get_ptr},
	{"refire", offsetof(player_t, refire), LUA_TNUMBER},
	{"colormap", offsetof(player_t, viewmap), LUA_TSTRING, func_set_colormap, func_get_colormap},
	{"extralight", offsetof(player_t, extralight), LUA_TNUMBER},
	{"deltaviewheight", offsetof(player_t, deltaviewheight), LUA_TNUMBER, func_set_fixedt, func_get_fixedt},
	{"map", offsetof(player_t, automap), LUA_TNUMBER},
	{"hideStatusBar", offsetof(player_t, hide_stbar), LUA_TBOOLEAN},
	{"forceWeapon", offsetof(player_t, force_weapon), LUA_TBOOLEAN},
	{"isMoving", 0, LUA_TBOOLEAN, func_set_readonly, func_get_playermoving},
	// functions
	{"Message", 0, LUA_TFUNCTION, func_set_readonly, func_setplayermessage},
	{"HudMessage", 0, LUA_TFUNCTION, func_set_readonly, func_playerhudmessage},
	{"HudMessageAlign", 0, LUA_TFUNCTION, func_set_readonly, func_playerhudalign},
	{"HudMessageScale", 0, LUA_TFUNCTION, func_set_readonly, func_playerhudscale},
	{"SetWeapon", 0, LUA_TFUNCTION, func_set_readonly, func_setplayerweapon},
	{"WeaponRefire", 0, LUA_TFUNCTION, func_set_readonly, func_playerrefire},
	{"WeaponFlash", 0, LUA_TFUNCTION, func_set_readonly, func_playerwflash},
};

// all sector values
static const lua_table_model_t lua_sector[] =
{
	// soundorg?
	{"floorheight", offsetof(sector_t, floorheight), LUA_TNUMBER, func_set_sectorheight, func_get_fixedt},
	{"ceilingheight", offsetof(sector_t, ceilingheight), LUA_TNUMBER, func_set_sectorheight, func_get_fixedt},
	{"spawnFloorheight", offsetof(sector_t, Sfloorheight), LUA_TNUMBER, func_set_fixedt, func_get_fixedt},
	{"spawnCeilingheight", offsetof(sector_t, Sceilingheight), LUA_TNUMBER, func_set_fixedt, func_get_fixedt},
	{"liquid", offsetof(sector_t, liquid), LUA_TNUMBER, func_set_fixedt, func_get_fixedt},
	{"floorpic", offsetof(sector_t, floorpic), LUA_TSTRING, func_set_flattexture, func_get_flattexture},
	{"ceilingpic", offsetof(sector_t, ceilingpic), LUA_TSTRING, func_set_flattexture, func_get_flattexture},
	{"foglevel", offsetof(sector_t, lightlevel), LUA_TNUMBER, func_set_byte, func_get_byte},
	{"shade", offsetof(sector_t, lightlevel)+1, LUA_TNUMBER, func_set_shade, func_get_shade},
	{"deepFog", offsetof(sector_t, lightlevel)+1, LUA_TBOOLEAN, func_set_deepfog, func_get_deepfog},
	{"special", offsetof(sector_t, special), LUA_TNUMBER, func_set_short, func_get_short},
	{"tag", offsetof(sector_t, tag), LUA_TNUMBER, func_set_short, func_get_short},
	{"isSecret", offsetof(sector_t, floordata), LUA_TBOOLEAN, func_set_secretsector, func_get_secretsector},
	{"color", offsetof(sector_t, colormap), LUA_TSTRING, func_set_colormap, func_get_colormap},
	{"colormap", offsetof(sector_t, fogmap), LUA_TSTRING, func_set_colorfog, func_get_colorfog},
	// action
	{"funcFloor", offsetof(sector_t, floordata), LUA_TLIGHTUSERDATA, func_set_readonly, func_get_ptr},
	{"funcCeiling", offsetof(sector_t, ceilingdata), LUA_TLIGHTUSERDATA, func_set_readonly, func_get_ptr},
	{"funcCustom", offsetof(sector_t, customdata), LUA_TLIGHTUSERDATA, func_set_readonly, func_get_ptr},
	// functions
	{"FindLowestFloor", 0, LUA_TFUNCTION, func_set_readonly, func_get_lowefloorsector},
	{"FindHighestFloor", 0, LUA_TFUNCTION, func_set_readonly, func_get_highfloorsector},
	{"FindNextFloor", 0, LUA_TFUNCTION, func_set_readonly, func_get_nextfloorsector},
	{"FindLowestCeiling", 0, LUA_TFUNCTION, func_set_readonly, func_get_loweceilingsector},
	{"FindHighestCeiling", 0, LUA_TFUNCTION, func_set_readonly, func_get_highceilingsector},
	{"FindMaximalFog", 0, LUA_TFUNCTION, func_set_readonly, func_get_highlightsector},
	{"FindMinimalFog", 0, LUA_TFUNCTION, func_set_readonly, func_get_lowlightsector},
	{"GetShortestTexture", 0, LUA_TFUNCTION, func_set_readonly, func_get_shortexfloorsector},
	{"GenericFloor", 0, LUA_TFUNCTION, func_set_readonly, func_get_genericfloorsector},
	{"GenericCeiling", 0, LUA_TFUNCTION, func_set_readonly, func_get_genericceilingsector},
	{"GenericCallerFloor", 0, LUA_TFUNCTION, func_set_readonly, func_get_genericcallsectorfloor},
	{"GenericCallerCeiling", 0, LUA_TFUNCTION, func_set_readonly, func_get_genericcallsectorceiling},
	{"GenericCaller", 0, LUA_TFUNCTION, func_set_readonly, func_get_genericcallsector},
	{"ThingIterator", 0, LUA_TFUNCTION, func_set_readonly, func_get_thingitersector},
	{"LineIterator", 0, LUA_TFUNCTION, func_set_readonly, func_get_lineitersector},
	{"SetDamage", 0, LUA_TFUNCTION, func_set_readonly, func_get_setdamagesector},
	{"AddFloor", 0, LUA_TFUNCTION, func_set_readonly, func_get_add3dfloorsector},
	{"SoundBody", 0, LUA_TFUNCTION, func_set_readonly, func_get_mobjsound_body},
	{"SoundWeapon", 0, LUA_TFUNCTION, func_set_readonly, func_get_mobjsound_weapon},
	{"SoundPickup", 0, LUA_TFUNCTION, func_set_readonly, func_get_mobjsound_pickup},
};

// all line values
static const lua_table_model_t lua_linedef[] =
{
	// soundorg?
	{"midtexture", offsetof(side_t, midtexture), LUA_TSTRING, func_set_walltexture, func_get_walltexture},
	{"toptexture", offsetof(side_t, toptexture), LUA_TSTRING, func_set_walltexture, func_get_walltexture},
	{"bottexture", offsetof(side_t, bottomtexture), LUA_TSTRING, func_set_walltexture, func_get_walltexture},
	{"special", offsetof(line_t, special), LUA_TNUMBER, func_set_byte, func_get_byte},
	{"arg0", offsetof(line_t, arg)+0, LUA_TNUMBER, func_set_byte, func_get_byte},
	{"arg1", offsetof(line_t, arg)+1, LUA_TNUMBER, func_set_byte, func_get_byte},
	{"arg2", offsetof(line_t, arg)+2, LUA_TNUMBER, func_set_byte, func_get_byte},
	{"arg3", offsetof(line_t, arg)+3, LUA_TNUMBER, func_set_byte, func_get_byte},
	{"arg4", offsetof(line_t, arg)+4, LUA_TNUMBER, func_set_byte, func_get_byte},
	{"tag", offsetof(line_t, tag), LUA_TNUMBER, func_set_short, func_get_short},
	{"angle", offsetof(line_t, angle), LUA_TNUMBER, func_set_readonly, func_get_mobjangle},
	{"render", offsetof(line_t, render), LUA_TSTRING, func_set_renderstyle, func_get_writeonly},
	{"horizon", 0, LUA_TBOOLEAN, func_set_line_horizon, func_get_line_horizon},
	{"block", offsetof(line_t, blocking), LUA_TNUMBER},
	// sectors
	{"sectorFront", 0, LUA_TLIGHTUSERDATA, func_set_readonly, func_get_linedefsectorF},
	{"sectorBack", 0, LUA_TLIGHTUSERDATA, func_set_readonly, func_get_linedefsectorB},
	{"frontsector", offsetof(line_t, frontsector), LUA_TLIGHTUSERDATA, func_set_readonly, func_get_ptr},
	{"backsector", offsetof(line_t, backsector), LUA_TLIGHTUSERDATA, func_set_readonly, func_get_ptr},
	// action
	{"func", 0, LUA_TLIGHTUSERDATA, func_set_readonly, func_getlinefunc_ptr},
	{"funcFront", offsetof(line_t, specialdata), LUA_TLIGHTUSERDATA, func_set_readonly, func_get_ptr},
	{"funcBack", offsetof(line_t, specialdata) + sizeof(void*), LUA_TLIGHTUSERDATA, func_set_readonly, func_get_ptr},
	// functions
	{"DoButton", 0, LUA_TFUNCTION, func_set_readonly, func_get_doswitchline},
	{"ToggleButton", 0, LUA_TFUNCTION, func_set_readonly, func_get_togswitchline},
	{"SetScroller", 0, LUA_TFUNCTION, func_set_readonly, func_get_scrollerline},
	{"SoundBody", 0, LUA_TFUNCTION, func_set_readonly, func_get_mobjsound_body},
	{"SoundWeapon", 0, LUA_TFUNCTION, func_set_readonly, func_get_mobjsound_weapon},
	{"SoundPickup", 0, LUA_TFUNCTION, func_set_readonly, func_get_mobjsound_pickup},
	// extra
	{"side", 0, LUA_TBOOLEAN, func_set_sideset, func_get_sideset},
};

// all generic plane values
static const lua_table_model_t lua_genericplane[] =
{
	{"sector", offsetof(generic_plane_t, info) + offsetof(generic_info_t, sector), LUA_TLIGHTUSERDATA, func_set_readonly, func_get_ptr},
	{"action", offsetof(generic_plane_t, lua_action), LUA_TFUNCTION, func_set_lua_regfunc, func_get_lua_registry},
	{"crush", offsetof(generic_plane_t, lua_crush), LUA_TFUNCTION, func_set_lua_regfunc, func_get_lua_registry},
	{"arg", offsetof(generic_plane_t, lua_arg), LUA_TNIL, func_set_lua_registry, func_get_lua_registry},
	// special
	{"isFloor", 0, LUA_TBOOLEAN, func_set_readonly, func_get_genericpltypef},
	{"isCeiling", 0, LUA_TBOOLEAN, func_set_readonly, func_get_genericpltypec},
	{"isCaller", 0, LUA_TBOOLEAN, func_set_readonly, func_get_genericpltypel},
	{"isSuspended", 0, LUA_TBOOLEAN, func_set_readonly, func_get_genericplsuspended},
	// plane movement
	{"isRaising", 0, LUA_TBOOLEAN, func_set_readonly, func_get_genericplraising},
	{"isLowering", 0, LUA_TBOOLEAN, func_set_readonly, func_get_genericpllowering},
	// functions
	{"Stop", 0, LUA_TFUNCTION, func_set_readonly, func_get_genericstop},
	{"Suspend", 0, LUA_TFUNCTION, func_set_readonly, func_get_genericsleep},
	{"Reverse", 0, LUA_TFUNCTION, func_set_readonly, func_get_genericreverse},
};

// all sector caller values; shares a lot with generic plane
static const lua_table_model_t lua_sectorcall[] =
{
	{"sector", offsetof(generic_plane_t, info) + offsetof(generic_info_t, sector), LUA_TLIGHTUSERDATA, func_set_readonly, func_get_ptr},
	{"ticrate", offsetof(generic_plane_t, info) + offsetof(generic_call_t, ticrate), LUA_TNUMBER},
	{"action", offsetof(generic_plane_t, lua_action), LUA_TFUNCTION, func_set_lua_regfunc, func_get_lua_registry},
	{"arg", offsetof(generic_plane_t, lua_arg), LUA_TNIL, func_set_lua_registry, func_get_lua_registry},
	// special
	{"isFloor", 0, LUA_TBOOLEAN, func_set_readonly, func_get_genericpltypef},
	{"isCeiling", 0, LUA_TBOOLEAN, func_set_readonly, func_get_genericpltypec},
	{"isCaller", 0, LUA_TBOOLEAN, func_set_readonly, func_get_genericpltypel},
	{"isSuspended", 0, LUA_TBOOLEAN, func_set_readonly, func_get_genericplsuspended},
	// plane movement
	{"isRaising", 0, LUA_TBOOLEAN, func_set_readonly, func_get_genericplraising},
	{"isLowering", 0, LUA_TBOOLEAN, func_set_readonly, func_get_genericpllowering},
	// functions
	{"Stop", 0, LUA_TFUNCTION, func_set_readonly, func_get_genericstop},
	{"Suspend", 0, LUA_TFUNCTION, func_set_readonly, func_get_genericsleep},
};

// all texture scroller values
static const lua_table_model_t lua_texturescroll[] =
{
	{"x", offsetof(generic_plane_t, speed), LUA_TNUMBER, func_set_fixedt, func_get_fixedt},
	{"y", offsetof(generic_plane_t, speed), LUA_TNUMBER, func_set_fixedt, func_get_fixedt},
	{"Stop", 0, LUA_TFUNCTION, func_set_readonly, func_get_texfuncstop},
};

// all pickup options
lua_intvalue_t lua_pickups[] =
{
	{"doNotPickup", SPECIAL_DONTPICKUP},
	{"item", SPECIAL_ITEM},
	{"ammo", SPECIAL_AMMO},
	{"weapon", SPECIAL_WEAPON},
	{"key", SPECIAL_KEY},
	{"power", SPECIAL_POWER},
	{"superPower", SPECIAL_SUPERPOWER},
	{"remove", SPECIAL_REMOVE},
};

// all line special options
lua_intvalue_t lua_linespec[] =
{
	{"use", EXTRA_USE},
	{"cross", EXTRA_CROSS},
	{"hit", EXTRA_HITSCAN},
	{"bump", EXTRA_BUMP},
};

//
// debug

//#define LUA_DebugRef(r, f)	LUA_DebugRefTxt(r, f, __FUNCTION__)
#define LUA_DebugRef(r, f)

void LUA_DebugRefTxt(int ref, boolean free, const char *func)
{
	const char *text;

	if(free)
		text = "LuaRef: free %i at %s\n";
	else
		text = "LuaRef: ref %i at %s\n";

	printf(text, ref, func);
}

static void LUA_DumpTable(lua_State *L, int depth)
{
	char temp[depth*2+1];

	if(depth)
		memset(temp, ' ', depth*2);
	temp[depth*2] = 0;
	// assuming table is on top of the stack
	lua_pushnil(L);
	while(lua_next(L, -2) != 0)
	{
		// key
		switch(lua_type(L, -2))
		{
			case LUA_TSTRING:
				printf("%s%s:", temp, lua_tostring(L, -2));
			break;
			case LUA_TNUMBER:
				printf("%s[%i]:", temp, (int)lua_tonumber(L, -2));
			break;
			default:
				printf("%s%s:", temp, lua_typename(L, lua_type(L, -2)));
			break;
		}
		// value
		switch(lua_type(L, -1))
		{
			case LUA_TSTRING:
				printf(" %s\n", lua_tostring(L, -1));
			break;
			case LUA_TNUMBER:
				printf(" %i\n", (int)lua_tonumber(L, -1));
			break;
			default:
				printf("\n");
				LUA_DumpTable(L, depth+1);
			break;
		}
		lua_pop(L, 1);
	}
}

//
// mobj states

static int state_mobjt; // for final fixup
static int state_state; // for 'loop'

static int state_getSprite(const char *spr)
{
	int i;

	// search for this name
	for(i = 0; i < numsnames; i++)
	{
		if(sprnames[i].u == *(const uint32_t*)spr)
			return i;
	}
	// add this name
	if((numsnames+1) / INFO_SPRITE_ALLOC > numsnames / INFO_SPRITE_ALLOC)
	{
		int asize = (1 + numsnames + INFO_SPRITE_ALLOC) * sizeof(sprname_t);
		// realloc buffer
		sprname_t *temp = realloc(sprnames, asize);
		if(!temp)
			I_Error("out of memory\n");
		sprnames = temp;
	}
	sprnames[numsnames].u = *(const uint32_t*)spr;
	numsnames++;
	// done
	return numsnames - 1;
}

static int state_add(lua_State *L)
{
	int len;
	state_t st;
	const char *sf;
	int num;

	len = lua_rawlen(L, -1);
	if(len <= 0)
		return 0;

	memset(&st, 0, sizeof(state_t));
	st.func = LUA_REFNIL;

	//
	// sprite + frame

	lua_pushinteger(L, 1);
	lua_gettable(L, -2);
	if(lua_type(L, -1) != LUA_TSTRING)
		return luaL_error(L, "invalid sprite / frame");
	sf = lua_tostring(L, -1);
	lua_pop(L, 1);
	// fullbright?
	if(sf[0] == '*')
	{
		st.frame = FF_FULLBRIGHT;
		sf++;
	} else
	if(sf[0] == '*')
	{
		st.frame = FF_FULLBRIGHT;
		sf++;
	}
	// check
	if(strlen(sf) < 5)
		return luaL_error(L, "invalid sprite / frame");
	// parse sprite
	st.sprite = state_getSprite(sf);
	// parse frame
	if(sf[4] < 'A' || sf[4] > '_')
		return luaL_error(L, "invalid sprite frame");
	st.frame |= sf[4] - 'A';

	//
	// tics

	if(len < 2)
		goto finish;

	lua_pushinteger(L, 2);
	lua_gettable(L, -2);
	if(lua_type(L, -1) != LUA_TNUMBER)
		return luaL_error(L, "invalid state duration");
	st.tics = lua_tointeger(L, -1);
	lua_pop(L, 1);

	//
	// function

	if(len < 3)
		goto finish;

	lua_pushinteger(L, 3);
	lua_gettable(L, -2);
	switch(lua_type(L, -1))
	{
		case LUA_TNIL:
			lua_pop(L, 1);
		break;
		case LUA_TFUNCTION:
			st.func = luaL_ref(L, LUA_REGISTRYINDEX);
			LUA_DebugRef(st.func, false);
		break;
		default:
			return luaL_error(L, "invalid state function");
		break;
	}

	//
	// done
finish:
	// check previous frame for special S_NULL
	if(numstates > state_state && states[numstates-1].nextstate == S_NULL)
		states[numstates-1].nextstate = STATE_NULL_NEXT;

	// set next frame
	st.nextstate = numstates + 1;
	// add new state
	if((numstates+1) / INFO_STATE_ALLOC > numstates / INFO_STATE_ALLOC)
	{
		int asize = (1 + numstates + INFO_STATE_ALLOC) * sizeof(state_t);
		// realloc buffer
		state_t *temp = realloc(states, asize);
		if(!temp)
			I_Error("out of memory\n");
		states = temp;
	}
	memcpy(states + numstates, &st, sizeof(state_t));
	numstates++;

	return 1; // state was added
}

void L_StateCall(state_t *st, mobj_t *mo)
{
	if(st->func == LUA_REFNIL)
		return;
	// function to call
	lua_rawgeti(luaS_game, LUA_REGISTRYINDEX, st->func);
	// mobj to pass
	lua_pushlightuserdata(luaS_game, mo);
	// do the call
	if(lua_pcall(luaS_game, 1, 0, 0))
		// script error
		I_Error("L_StateCall: %s", lua_tostring(luaS_game, -1));
}

statenum_t L_StateFromAlias(mobjinfo_t *info, statenum_t state)
{
	int anim;
	int offs;

	// animation ID
	anim = state & STATE_AMASK;
	offs = (state & STATE_OMASK) >> STATE_OFFSHIFT;
	// set this animation
	return *(&info->spawnstate + anim) + offs;
}

//
// mobj types

static int func_set_fixedt(lua_State *L, void *dst, void *o)
{
	lua_Number num;

	num = lua_tonumber(L, -1);
	*(fixed_t*)dst = (fixed_t)(num * (lua_Number)FRACUNIT);
	return 0;
}

static int func_get_fixedt(lua_State *L, void *dst, void *o)
{
	lua_pushnumber(L, *(fixed_t*)dst / (lua_Number)FRACUNIT);
	return 1;
}

static int func_set_short(lua_State *L, void *dst, void *o)
{
	*(short*)dst = lua_tointeger(L, -1);
	return 0;
}

static int func_get_short(lua_State *L, void *dst, void *o)
{
	lua_pushinteger(L, *(short*)dst);
	return 1;
}

static int func_set_byte(lua_State *L, void *dst, void *o)
{
	*(uint8_t*)dst = lua_tointeger(L, -1);
	return 0;
}

static int func_get_byte(lua_State *L, void *dst, void *o)
{
	lua_pushinteger(L, *(uint8_t*)dst);
	return 1;
}

static int func_set_lua_regfunc(lua_State *L, void *dst, void *o)
{
	int ref = *(int*)dst;

	LUA_DebugRef(ref, true);
	luaL_unref(L, LUA_REGISTRYINDEX, ref);

	if(lua_type(L, -1) != LUA_TNIL)
		luaL_checktype(L, -1, LUA_TFUNCTION);

	ref = luaL_ref(L, LUA_REGISTRYINDEX);
	LUA_DebugRef(ref, false);
	*(int*)dst = ref;
	lua_pushnil(L);

	return 0;
}

static int func_set_lua_registry(lua_State *L, void *dst, void *o)
{
	int ref = *(int*)dst;

	LUA_DebugRef(ref, true);
	luaL_unref(L, LUA_REGISTRYINDEX, ref);

	ref = luaL_ref(L, LUA_REGISTRYINDEX);
	LUA_DebugRef(ref, false);
	*(int*)dst = ref;
	lua_pushnil(L);

	return 0;
}

static int func_get_lua_registry(lua_State *L, void *dst, void *o)
{
	lua_rawgeti(luaS_game, LUA_REGISTRYINDEX, *(int*)dst);
	return 1;
}

static int func_set_states(lua_State *L, void *dst, void *o)
{
	size_t i, len;
	const char *jump;
	int finished = 0;

	state_state = numstates;
	len = lua_rawlen(L, -1);

	for(i = 1; i <= len; i++)
	{
		lua_pushinteger(L, i);
		lua_gettable(L, -2);
		switch(lua_type(L, -1))
		{
			case LUA_TTABLE:
				if(state_add(L))
					finished = 0;
			break;
			case LUA_TSTRING:
				jump = lua_tostring(L, -1);
				if(jump[0] == '_' && jump[1] != '_')
				{
					int j;
					int k = 0; // k is animation ID
					int offs = 0;
					char temp[16];
					char *ptr = temp;

					// check for offset
					strncpy(temp, jump, sizeof(temp)-1);
					temp[sizeof(temp)-1] = 0;

					ptr = temp;
					while(*ptr)
					{
						if(*ptr == '+')
						{
							*ptr = 0;
							if(sscanf(ptr + 1, "%d", &offs) != 1)
								offs = 0;
							break;
						}
						ptr++;
					}

					// find this animation
					for(j = 0; j < sizeof(lua_mobjtype)/sizeof(lua_table_model_t); j++)
					{
						if(lua_mobjtype[j].name[0] == '_')
						{
							if(!strcmp(temp, lua_mobjtype[j].name))
							{
								// set next state 'alias' with optional offset
								finished = 1;
								states[numstates-1].nextstate = STATE_ANIMATION | k | (offs << STATE_OFFSHIFT);
								break;
							}
							k++;
						}
					}
					if(finished)
						break;
				}
				if(!strcmp(jump, "loop") && state_state != numstates)
				{
					finished = 1;
					states[numstates-1].nextstate = state_state;
					break;
				}
				if(!strcmp(jump, "stop") && state_state != numstates)
				{
					finished = 1;
					states[numstates-1].nextstate = S_NULL;
					break;
				}
				luaL_error(L, "invalid type for state");
			break;
			default:
				luaL_error(L, "invalid type for state");
			break;
		}
		lua_pop(L, 1);
	}

	if(state_state == numstates)
		return 0;

	if(!finished)
		states[numstates-1].nextstate = S_NULL;

	*(int*)dst = state_state;

	return 0;
}

static int func_set_damagescale(lua_State *L, void *dst, void *o)
{
	size_t i, len;
	mobjinfo_t *info = o;

	len = lua_rawlen(L, -1);
	if(len > NUMDAMAGETYPES)
		len = NUMDAMAGETYPES;
	for(i = 1; i <= len; i++)
	{
		lua_pushinteger(L, i);
		lua_gettable(L, -2);

		if(lua_type(L, -1) == LUA_TBOOLEAN)
		{
			// instant gib
			info->damagescale[i-1] = 255;
		} else
		{
			int num;

			luaL_checktype(L, -1, LUA_TNUMBER);
			num = (DEFAULT_DAMAGE_SCALE-DAMAGE_SCALE) + (lua_tonumber(L, -1) * (lua_Number)DAMAGE_SCALE);
			if(num < 0)
				num = 0;
			if(num > 254)
				num = 254;
			info->damagescale[i-1] = num;
		}

		lua_pop(L, 1);
	}

	return 0;
}

//
// mobj set/get functions

// read-only value
static int func_set_readonly(lua_State *L, void *dst, void *o)
{
	return luaL_error(L, "tried to modify read-only value");
}

// write-only value
static int func_get_writeonly(lua_State *L, void *dst, void *o)
{
	return luaL_error(L, "tried read write-only value");
}

// special decimal handling for angles
static int func_set_mobjangle(lua_State *L, void *dst, void *o)
{
	*(angle_t*)dst = lua_tonumber(L, -1) * (lua_Number)(1 << ANGLETOFINESHIFT);
	return 0;
}

static int func_get_mobjangle(lua_State *L, void *dst, void *o)
{
	lua_pushnumber(L, *(angle_t*)dst / (lua_Number)(1 << ANGLETOFINESHIFT));
	return 1;
}

// assigning mobj info
static int func_set_mobjtype(lua_State *L, void *dst, void *o)
{
	mobjinfo_t *type;

	if(lua_type(L, -1) == LUA_TNIL)
		type = NULL;
	else
	{
		LUA_GetMobjTypeParam(L, -1);
		type = lua_touserdata(L, -1);
	}

	*(void**)dst = type;

	return 0;
}

// pitch values have to be in certain range and in fixed_t format
static int func_set_mobjpitch(lua_State *L, void *dst, void *o)
{
	fixed_t pitch = lua_tonumber(L, -1) * (lua_Number)FRACUNIT;

	if(pitch > 50000)
	    pitch = 50000;
	if(pitch < -50000)
	    pitch = -50000;

	*(int*)dst = pitch;
	return 0;
}

// radius changes have to update blockmap links
static int func_set_radius(lua_State *L, void *dst, void *o)
{
	mobj_t *mo = o;
	int radius = lua_tonumber(L, -1) * (lua_Number)FRACUNIT;

	P_UnsetThingPosition(mo);
	*(fixed_t*)dst = radius;
	P_SetThingPosition(mo);

	return 0;
}

// special mobj type
static int func_set_mobj(lua_State *L, void *dst, void *o)
{
	thinker_t *th;

	th = lua_touserdata(L, -1);
	if(th && th->lua_type != TT_MOBJ)
		return luaL_error(L, "invalid thinker type, mobj expected");

	*(void**)dst = th;
	return 0;
}

// special player mobj changes
static int func_set_playermobj(lua_State *L, void *dst, void *o)
{
	thinker_t *th;
	mobj_t *mo = *(void**)dst;
	player_t *pl = mo->player;

	th = lua_touserdata(L, -1);
	if(th && th->lua_type != TT_MOBJ)
		return luaL_error(L, "invalid thinker type, mobj expected");

	// remove player from original mobj
	mo->player = NULL;
	// add player to new mobj
	mo = (mobj_t*)th;
	mo->player = pl;
	// fix viewheight
	pl->viewheight = mo->info->viewz;

	// check playerstate
	if(mo->health > 0)
		pl->playerstate = PST_LIVE;
	else
		pl->playerstate = PST_DEAD;

	// TODO: server; tell clients about this

	*(void**)dst = th;
	return 0;
}

// get player movement status
static int func_get_playermoving(lua_State *L, void *dst, void *o)
{
	player_t *pl = o;

	lua_pushboolean(L, pl->cmd.forwardmove || pl->cmd.sidemove);

	return 1;
}

// get special type (mobj, player, sector ...)
static int func_get_ptr(lua_State *L, void *dst, void *o)
{
	if(*(void**)dst)
		lua_pushlightuserdata(L, *(void**)dst);
	else
		lua_pushnil(L);
	return 1;
}

// return sector from mobj
static int func_get_sector(lua_State *L, void *dst, void *o)
{
	mobj_t *mo = o;

	if(mo->subsector && mo->subsector->sector)
		lua_pushlightuserdata(L, mo->subsector->sector);
	else
		lua_pushnil(L);
	return 1;
}

// return mobj removal function
static int func_get_removemobj(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_removeMobjFromMobj, 1);
	return 1;
}

// return looking function
static int func_get_facemobj(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_faceFromMobj, 1);
	return 1;
}

// return fuction for teleporting mobj
static int func_get_teleportmobj(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_teleportMobj, 1);
	return 1;
}

// return function for mobj position check
static int func_get_checkmobjpos(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_checkMobjPos, 1);
	return 1;
}

// return fuction for missile shooting
static int func_get_spawnmissile(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_spawnMissile, 1);
	return 1;
}

// return monster aiming function
static int func_get_attackaim(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_attackAim, 1);
	return 1;
}

// return linetarget function
static int func_get_linetarget(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_lineTarget, 1);
	return 1;
}

// return melee range check function
static int func_get_meleerange(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_meleeRange, 1);
	return 1;
}

// return distance calculation function
static int func_get_distance(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_mobjDistance, 1);
	return 1;
}

// return damage function
static int func_get_damage(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_damageFromMobj, 1);
	return 1;
}

// return hitscan function
static int func_get_lineattack(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_lineAttack, 1);
	return 1;
}

// return mobj thrust function
static int func_get_thrustmobj(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_thrustFromMobj, 1);
	return 1;
}

// return mobj sight check function
static int func_get_checkmobjsight(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_sightCheckFromMobj, 1);
	return 1;
}

// return radius damage function
static int func_get_mobjradiusdmg(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_radiusDamageFromMobj, 1);
	return 1;
}

// return damage scale setter / getter
static int func_get_mobjdmgscale(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_damageScaleFromMobj, 1);
	return 1;
}

// return flag reset function
static int func_get_mobjresetflags(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_resetFlagsFromMobj, 1);
	return 1;
}

// return sound playback function
static int func_get_mobjsound_body(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushinteger(L, SOUND_BODY);
	lua_pushcclosure(L, LUA_soundFromMobj, 2);
	return 1;
}

// return sound playback function
static int func_get_mobjsound_weapon(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushinteger(L, SOUND_WEAPON);
	lua_pushcclosure(L, LUA_soundFromMobj, 2);
	return 1;
}

// return sound playback function
static int func_get_mobjsound_pickup(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushinteger(L, SOUND_PICKUP);
	lua_pushcclosure(L, LUA_soundFromMobj, 2);
	return 1;
}

// return inventory give function
static int func_get_mobj_invgive(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_mobjInventoryGive, 1);
	return 1;
}

// return inventory take function
static int func_get_mobj_invtake(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_mobjInventoryTake, 1);
	return 1;
}

// return inventory check function
static int func_get_mobj_invcheck(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_mobjInventoryCheck, 1);
	return 1;
}

// return inventory maximum set function
static int func_get_mobj_invsetmax(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_mobjInventorySetMax, 1);
	return 1;
}

// return generic ticker set function
static int func_get_mobj_tickerset(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_mobjTickerSet, 1);
	return 1;
}

// return generic ticker remove function
static int func_get_mobj_tickerdel(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_mobjTickerDel, 1);
	return 1;
}

// return generic ticker check function
static int func_get_mobj_tickerchk(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_mobjTickerCheck, 1);
	return 1;
}

// change colormap
static int func_set_colormap(lua_State *L, void *dst, void *o)
{
	char temp[8];
	const char *src;
	int i, lump;
	uint32_t idx = 0;
	colormap_t *map = (colormap_t *)dst;
	degenthinker_t *th = o;

	src = lua_tostring(L, -1);

	if(*src != '-' || th->lua_type == TT_SECTOR)
	{
		for(i = 0; i < sizeof(temp); i++)
		{
			if(src[i] == ':')
			{
				temp[i] = 0;
				break;
			}
			temp[i] = src[i];
			if(!src[i])
				break;
		}
		if(src[i] == ':')
		{
			if(sscanf(src+i+1, "%u", &idx) != 1 || idx > 1024)
				return luaL_error(L, "invalid colormap index");
		}
	} else
	{
		map->lump = -1;
		map->idx = 0;
		map->data = NULL;
		return 0;
	}

	lump = W_GetNumForName(temp);
	i = W_LumpLength(lump);

	if(i & 0xFF)
		return luaL_error(L, "invalid colormap lump");

	if(idx * 256 > i)
		return luaL_error(L, "invalid colormap index");

	map->lump = lump;
	map->idx = idx;
	map->data = W_CacheLumpNum(lump) + idx * 256;

	return 0;
}

// return colormap
static int func_get_colormap(lua_State *L, void *dst, void *o)
{
	colormap_t *map = (colormap_t *)dst;

	if(map->data)
	{
		char temp[16];
		char *name;

		name = W_LumpNumName(map->lump);
		if(map->idx)
			sprintf(temp, "%.8s:%i", name, map->idx);
		else
			sprintf(temp, "%.8s", name);

		lua_pushstring(L, temp);
	} else
		lua_pushstring(L, "-");

	return 1;
}

// change fog in sector
static int func_set_colorfog(lua_State *L, void *dst, void *o)
{
	char temp[8];
	const char *src;
	colormap_t *map = (colormap_t *)dst;

	src = lua_tostring(L, -1);
	if(*src == '-')
	{
		map->lump = -1;
		map->idx = 0;
		map->data = NULL;
		return 0;
	}

	strncpy(temp, src, 8);
	map->lump = W_GetNumForName(temp);
	map->idx = 0;
	map->data = W_CacheLumpNum(map->lump);

	if(W_LumpLength(map->lump) < 256*32)
		return luaL_error(L, "invalid colormap lump '%s'", src);

	return 0;
}

// return fog in sector
static int func_get_colorfog(lua_State *L, void *dst, void *o)
{
	colormap_t *map = (colormap_t *)dst;

	if(map->data)
	{
		char *name;
		char temp[16];
		name = W_LumpNumName(map->lump);
		sprintf(temp, "%.8s", name);
		lua_pushstring(L, temp);
	} else
		lua_pushstring(L, "-");

	return 1;
}

// return message set function
static int func_setplayermessage(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_setPlayerMessage, 1);
	return 1;
}

// return HUD message function
static int func_playerhudmessage(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_playerHudMessage, 1);
	return 1;
}

// return HUD message alignment set function
static int func_playerhudalign(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_playerHudAlign, 1);
	return 1;
}

//
static int func_playerhudscale(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_playerHudScale, 1);
	return 1;
}

// return weapon set function
static int func_setplayerweapon(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_setPlayerWeapon, 1);
	return 1;
}

// return player refire function
static int func_playerrefire(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_playerRefireWeapon, 1);
	return 1;
}

// return weapon flash function
static int func_playerwflash(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_playerFlashWeapon, 1);
	return 1;
}

// set shade value
static int func_set_shade(lua_State *L, void *dst, void *o)
{
	int val;
	uint8_t *tmp = dst;

	val = lua_tointeger(L, -1);

	if(val > 31)
		val = 31;
	if(val < 0)
		val = 0;

	*tmp = (*tmp & 0xC0) | val;

	return 0;
}

// get shade value
static int func_get_shade(lua_State *L, void *dst, void *o)
{
	lua_pushnumber(L, *(uint8_t*)dst & 31);
	return 1;
}

// set sector deep fog
static int func_set_deepfog(lua_State *L, void *dst, void *o)
{
	uint8_t *tmp = dst;

	if(lua_toboolean(L, -1))
		*tmp |= 0x80;
	else
		*tmp &= ~0x80;
	return 1;
}

// get sector deep fog
static int func_get_deepfog(lua_State *L, void *dst, void *o)
{
	lua_pushboolean(L, !!(*(uint8_t*)dst & 0x80));
	return 1;
}

// set sector secret status
static int func_set_secretsector(lua_State *L, void *dst, void *o)
{
	sector_t *sec = o;
	boolean secret;

	secret = lua_toboolean(L, -1);

	if(!secret)
	{
		if(sec->flags & (SF_SECRET | SF_WAS_SECRET))
			totalsecret--;
		sec->flags &= ~(SF_SECRET | SF_WAS_SECRET);
	} else
	if(!(sec->flags & SF_WAS_SECRET))
	{
		sec->flags |= SF_SECRET;
		totalsecret++;
	}

	return 0;
}

// get sector secret status
static int func_get_secretsector(lua_State *L, void *dst, void *o)
{
	sector_t *sec = o;
	lua_pushboolean(L, !!(sec->flags & (SF_SECRET | SF_WAS_SECRET)));
	return 1;
}

// return floor search function
static int func_get_lowefloorsector(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushlightuserdata(L, P_FindLowestFloorSurrounding);
	lua_pushcclosure(L, LUA_heightFromSector, 2);
	return 1;
}

// return floor search function
static int func_get_highfloorsector(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushlightuserdata(L, P_FindHighestFloorSurrounding);
	lua_pushcclosure(L, LUA_heightFromSector, 2);
	return 1;
}

// return floor search function
static int func_get_nextfloorsector(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushlightuserdata(L, P_FindNextHighestFloor);
	lua_pushcclosure(L, LUA_heightFromSector, 2);
	return 1;
}

// return ceiling search function
static int func_get_loweceilingsector(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushlightuserdata(L, P_FindLowestCeilingSurrounding);
	lua_pushcclosure(L, LUA_heightFromSector, 2);
	return 1;
}

// return ceiling search function
static int func_get_highceilingsector(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushlightuserdata(L, P_FindHighestCeilingSurrounding);
	lua_pushcclosure(L, LUA_heightFromSector, 2);
	return 1;
}

// return light search function
static int func_get_highlightsector(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushlightuserdata(L, P_FindMaxSurroundingLight);
	lua_pushcclosure(L, LUA_lightFromSector, 2);
	return 1;
}

// return light search function
static int func_get_lowlightsector(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushlightuserdata(L, P_FindMinSurroundingLight);
	lua_pushcclosure(L, LUA_lightFromSector, 2);
	return 1;
}

// return texture height search function
static int func_get_shortexfloorsector(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_shortTexFromSector, 1);
	return 1;
}

// return generic floor move function
static int func_get_genericfloorsector(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushboolean(L, true);
	lua_pushcclosure(L, LUA_genericPlaneFromSector, 2);
	return 1;
}

// return generic ceiling move function
static int func_get_genericceilingsector(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushboolean(L, false);
	lua_pushcclosure(L, LUA_genericPlaneFromSector, 2);
	return 1;
}

// return generic sector call function (floor)
static int func_get_genericcallsectorfloor(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushinteger(L, 0);
	lua_pushcclosure(L, LUA_genericCallFromSector, 2);
	return 1;
}

// return generic sector call function (ceiling)
static int func_get_genericcallsectorceiling(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushinteger(L, 1);
	lua_pushcclosure(L, LUA_genericCallFromSector, 2);
	return 1;
}

// return generic sector call function (custom)
static int func_get_genericcallsector(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushinteger(L, 2);
	lua_pushcclosure(L, LUA_genericCallFromSector, 2);
	return 1;
}

// return sector line iterator function
static int func_get_thingitersector(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_sectorThingIterator, 1);
	return 1;
}

// return sector line iterator function
static int func_get_lineitersector(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_sectorLineIterator, 1);
	return 1;
}

// return sector damage set function
static int func_get_setdamagesector(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_sectorSetDamage, 1);
	return 1;
}

// return 3D floor add function
static int func_get_add3dfloorsector(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_sectorAdd3DFloor, 1);
	return 1;
}

// change sector heights
static int func_set_sectorheight(lua_State *L, void *dst, void *o)
{
	lua_Number num;

	num = lua_tonumber(L, -1);
	*(int*)dst = (int)(num * (lua_Number)FRACUNIT);

	P_ChangeSector(o, LUA_REFNIL, LUA_REFNIL);

	return 0;
}

// 'flat' textures
static int func_set_flattexture(lua_State *L, void *dst, void *o)
{
	const char *tex;
	char temp[8];
	int lump;

	tex = lua_tostring(L, -1);
	strncpy(temp, tex, sizeof(temp));
	lump = R_FlatNumForName(temp);

	*(uint16_t*)dst = lump;

	return 0;
}

// get wall texture
static int func_get_flattexture(lua_State *L, void *dst, void *o)
{
	short texture;

	texture = *(uint16_t*)dst;

	if(texture)
	{
		char tex[9];
		memcpy(tex, textures[texture].name, 8);
		tex[8] = 0;
		lua_pushstring(L, tex);
	} else
		lua_pushstring(L, "-");

	return 1;
}

// set working side
static int func_set_sideset(lua_State *L, void *dst, void *o)
{
	linedef_side = !lua_toboolean(L, -1);
	return 0;
}

// set working side
static int func_get_sideset(lua_State *L, void *dst, void *o)
{
	lua_pushboolean(L, !linedef_side);
	return 1;
}

// change wall texture
static int func_set_walltexture(lua_State *L, void *dst, void *o)
{
	const char *tex;
	char temp[8];
	short *texture;
	side_t *side;
	line_t *line = o;
	int sidenum = line->sidenum[linedef_side];

	if(sidenum < 0)
		return 0;

	side = &sides[sidenum];

	// move offset into sidedef
	texture = (short*)((uint8_t*)side + (dst - o));

	tex = lua_tostring(L, -1);
	strncpy(temp, tex, sizeof(temp));
	*texture = R_TextureNumForName(temp);

	return 0;
}

// get wall texture
static int func_get_walltexture(lua_State *L, void *dst, void *o)
{
	short *texture;
	side_t *side;
	line_t *line = o;
	int sidenum = line->sidenum[linedef_side];

	if(sidenum < 0)
	{
		lua_pushnil(L);
		return 1;
	}

	side = &sides[sidenum];

	// move offset into sidedef
	texture = (short*)((uint8_t*)side + (dst - o));

	if(*texture)
	{
		char tex[9];
		memcpy(tex, textures[*texture].name, 8);
		tex[8] = 0;
		lua_pushstring(L, tex);
	} else
		lua_pushstring(L, "-");

	return 1;
}

// set line 'horizon' effect
static int func_set_line_horizon(lua_State *L, void *dst, void *o)
{
	line_t *l = o;

	if(l->sidenum[1] >= 0)
		return 0;

	if(lua_toboolean(L, -1))
		l->sidenum[1] = -2;
	else
		l->sidenum[1] = -1;

	return 0;
}

// get line 'horizon' effect
static int func_get_line_horizon(lua_State *L, void *dst, void *o)
{
	line_t *l = o;

	lua_pushboolean(L, l->sidenum[1] == -2);
	return 1;
}

// return line function from selected side
static int func_getlinefunc_ptr(lua_State *L, void *dst, void *o)
{
	void *func = ((line_t*)o)->specialdata[linedef_side];
	if(func)
		lua_pushlightuserdata(L, func);
	else
		lua_pushnil(L);
	return 1;
}

// return switch function
static int func_get_doswitchline(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_doSwitchTextureLine, 1);
	return 1;
}

// return switch function
static int func_get_togswitchline(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_togSwitchTextureLine, 1);
	return 1;
}

// return scroller add function
static int func_get_scrollerline(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_scrollFromLine, 1);
	return 1;
}

// return sector of linedef from selected side
static int func_get_linedefsectorF(lua_State *L, void *dst, void *o)
{
	line_t *l = o;
	sector_t *s;

	if(linedef_side)
		s = l->backsector;
	else
		s = l->frontsector;

	if(s)
		lua_pushlightuserdata(L, s);
	else
		lua_pushnil(L);

	return 1;
}

// return sector of linedef from selected side; return other sector
static int func_get_linedefsectorB(lua_State *L, void *dst, void *o)
{
	line_t *l = o;
	sector_t *s;

	if(!linedef_side)
		s = l->backsector;
	else
		s = l->frontsector;

	if(s)
		lua_pushlightuserdata(L, s);
	else
		lua_pushnil(L);

	return 1;
}

// return generic plane type match
static int func_get_genericpltypef(lua_State *L, void *dst, void *o)
{
	lua_pushboolean(L, ((generic_plane_t*)o)->thinker.function.acp1 == (actionf_p1)T_GenericFloor);
	return 1;
}

// return generic plane type match
static int func_get_genericpltypec(lua_State *L, void *dst, void *o)
{
	lua_pushboolean(L, ((generic_plane_t*)o)->thinker.function.acp1 == (actionf_p1)T_GenericCeiling);
	return 1;
}

// return generic caller type match
static int func_get_genericpltypel(lua_State *L, void *dst, void *o)
{
	lua_pushboolean(L, ((generic_plane_t*)o)->thinker.function.acp1 == (actionf_p1)T_GenericCaller);
	return 1;
}

// return suspension flag
static int func_get_genericplsuspended(lua_State *L, void *dst, void *o)
{
	lua_pushboolean(L, !((generic_plane_t*)o)->speed);
	return 1;
}

// return plane movement match
static int func_get_genericplraising(lua_State *L, void *dst, void *o)
{
	generic_plane_t *gp = o;
	if(gp->thinker.lua_type == TT_SECCALL)
		lua_pushboolean(L, false);
	else
		lua_pushboolean(L, gp->info.startz < gp->info.stopz);
	return 1;
}

// return plane movement match
static int func_get_genericpllowering(lua_State *L, void *dst, void *o)
{
	generic_plane_t *gp = o;
	if(gp->thinker.lua_type == TT_SECCALL)
		lua_pushboolean(L, false);
	else
		lua_pushboolean(L, gp->info.startz > gp->info.stopz);
	return 1;
}

// return generic plane / caller removal function
static int func_get_genericstop(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_stopFromGeneric, 1);
	return 1;
}

// return sleep / wakeup function
static int func_get_genericsleep(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_sleepFromGeneric, 1);
	return 1;
}

// return reversal function
static int func_get_genericreverse(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_reverseFromGeneric, 1);
	return 1;
}

// return scroller removal function
static int func_get_texfuncstop(lua_State *L, void *dst, void *o)
{
	lua_pushlightuserdata(L, o);
	lua_pushcclosure(L, LUA_stopFromLine, 1);
	return 1;
}

static int func_set_lumpname_optional(lua_State *L, void *dst, void *o)
{
	*(int*)dst = W_GetNumForNameLua(lua_tostring(L, -1), true);
	return 0;
}

static int func_get_lumpname(lua_State *L, void *dst, void *o)
{
	const char *tex;

	tex = W_LumpNumName(*(int*)dst);
	if(!tex)
		lua_pushstring(L, "-");
	else
	{
		char temp[9];
		memcpy(temp, tex, 8);
		temp[8] = 0;
		lua_pushstring(L, temp);
	}

	return 1;
}

static int func_set_renderstyle(lua_State *L, void *dst, void *o)
{
	render_t *render = dst;
	const char *tmp;

	tmp = lua_tostring(L, -1);

	if(tmp[0] == '+' || tmp[0] == '-')
	{
		int lump;
		char name[8];
		const char *sname = tmp + 1;

		if(*sname == '!')
			sname++;

		strncpy(name, sname, 8);
		lump = W_GetNumForName(name);
		if(W_LumpLength(lump) < 0x10000)
			return luaL_error(L, "invalid render table '%s'", tmp);

		// 64k table
		if(tmp[1] == '!')
		{
			if(tmp[0] == '-')
				render->renderstyle = RENDER_FUZZ_TABLEI;
			else
				render->renderstyle = RENDER_FUZZ_TABLE;
		} else
		{
			if(tmp[0] == '-')
				render->renderstyle = RENDER_TABLEI;
			else
				render->renderstyle = RENDER_TABLE;
		}
		render->rendertable = W_CacheLumpNum(lump);

		return 0;
	}

	if(!strcmp(tmp, "!NORMAL"))
	{
		render->renderstyle = RENDER_NORMAL;
		render->rendertable = NULL;
		return 0;
	}

	if(!strncmp(tmp, "!FUZZ", 5))
	{
		render->renderstyle = RENDER_FUZZ;
		render->rendertable = colormaps + 256 * 8;
		if(tmp[5] == ':')
		{
			int lump, i;
			char name[8];
			int idx = 0;
			const char *src = tmp + 6;

			for(i = 0; i < 9; i++)
			{
				if(*src == ':')
				{
					name[i] = 0;
					src++;
					sscanf(src, "%d", &idx);
					break;
				}
				if(i == 8)
					break;
				name[i] = *src;
				if(!*src)
					break;
				src++;
			}

			lump = W_GetNumForName(name);
			if(W_LumpLength(lump) < idx * 256 + 256)
				return luaL_error(L, "invalid render table '%s'", tmp);
			render->rendertable = W_CacheLumpNum(lump) + 256 * idx;
		}
		return 0;
	}

	if(!strcmp(tmp, "!HOLEY0"))
	{
		render->renderstyle = RENDER_HOLEY0;
		render->rendertable = NULL;
		return 0;
	}

	if(!strcmp(tmp, "!HOLEY1"))
	{
		render->renderstyle = RENDER_HOLEY1;
		render->rendertable = NULL;
		return 0;
	}

	return luaL_error(L, "invalid render style '%s'", tmp);
}
/*
static int func_get_renderstyle(lua_State *L, void *dst, void *o)
{
	render_t *render = dst;

	switch(render->renderstyle)
	{
		case RENDER_NORMAL:
			lua_pushstring(L, "!NORMAL");
		break;
		case RENDER_FUZZ:
			lua_pushstring(L, "!FUZZ");
		break;
		case RENDER_HOLEY0:
			lua_pushstring(L, "!HOLEY0");
		break;
		case RENDER_HOLEY1:
			lua_pushstring(L, "!HOLEY1");
		break;
	}

	return 1;
}*/

//
// common functions

const lua_table_model_t *LUA_FieldByName(const char *name, const lua_table_model_t *model, int modelsize)
{
	int i;

	for(i = 0; i < modelsize; i++, model++)
	{
		if(!strcmp(model->name, name))
			return model;
	}
	return NULL;
}

int LUA_MobjTypeStruct(lua_State *L, void *dst)
{
	int i;
	int modelsize = sizeof(lua_mobjtype) / sizeof(lua_table_model_t);
	int fodelsize = sizeof(lua_mobjflags) / sizeof(lua_mobjflag_t);
	// assuming table is on top of the stack
	lua_pushnil(L);
	while(lua_next(L, -2) != 0)
	{
		if(lua_type(L, -2) == LUA_TSTRING)
		{
			const char *key = lua_tostring(L, -2);
			int type = lua_type(L, -1);

			if(key[0] == '_' && key[1] == '_')
			for(i = 0; i < fodelsize; i++)
			{
				// it's a flag
				if(!strcmp(key + 2, lua_mobjflags[i].name))
				{
					luaL_checktype(L, -1, LUA_TBOOLEAN);
					if(lua_toboolean(L, -1))
						// set
						((mobjinfo_t*)dst)->flags |= lua_mobjflags[i].value;
					else
						// unset
						((mobjinfo_t*)dst)->flags &= ~lua_mobjflags[i].value;
					i = -1;
					break;
				}
			} else
			for(i = 0; i < modelsize; i++)
			{
				// it's a value
				if((type == lua_mobjtype[i].ltype || lua_mobjtype[i].ltype == LUA_TNIL) && !strcmp(key, lua_mobjtype[i].name))
				{
					if(lua_mobjtype[i].set)
						lua_mobjtype[i].set(L, ((void*)dst) + lua_mobjtype[i].offset, dst);
					else
					{
						// right now only numbers are supported
						int num = lua_tointeger(L, -1);
						*(int*)(dst + lua_mobjtype[i].offset) = num;
					}
					i = -1;
					break;
				}
			}
			if(i >= 0)
				return luaL_error(L, "key '%s' is not expected", key);
		} else
			return luaL_error(L, "invalid key for structure");
		lua_pop(L, 1);
	}
	return 0;
}

mobj_t *LUA_GetMobjParam(lua_State *L, int idx, boolean allownull)
{
	mobj_t *dest;

	if(allownull && lua_type(L, idx) == LUA_TNIL)
		return NULL;

	luaL_checktype(L, idx, LUA_TLIGHTUSERDATA);

	if(!lua_getmetatable(L, idx))
	{
		luaL_error(L, "mobj expected");
		return 0;
	}

	lua_pushstring(L, "__index");
	lua_rawget(L, -2);

	if(lua_tocfunction(L, -1) != LUA_ThinkerIndex)
	{
		luaL_error(L, "mobj expected");
		return 0;
	}

	lua_pop(L, 2);

	dest = lua_touserdata(L, idx);

	if(dest->thinker.lua_type != TT_MOBJ)
	{
		luaL_error(L, "mobj expected");
		return 0;
	}

	return dest;
}

int LUA_GetMobjTypeParam(lua_State *L, int idx)
{
	mobjinfo_t *dest;

	luaL_checktype(L, idx, LUA_TLIGHTUSERDATA);

	if(!lua_getmetatable(L, idx))
		return luaL_error(L, "mobj type expected");

	lua_pushstring(L, "__index");
	lua_rawget(L, -2);

	if(lua_tocfunction(L, -1) != LUA_ThinkerIndex)
		return luaL_error(L, "mobj type expected");

	lua_pop(L, 2);

	dest = lua_touserdata(L, idx);

	if(dest->dthink.lua_type != TT_MOBJINFO)
		return luaL_error(L, "mobj type expected");

	return dest - mobjinfo;
}

sector_t *LUA_GetSectorParam(lua_State *L, int idx, boolean allownull)
{
	sector_t *dest;

	if(allownull && lua_type(L, idx) == LUA_TNIL)
		return NULL;

	luaL_checktype(L, idx, LUA_TLIGHTUSERDATA);

	if(!lua_getmetatable(L, idx))
	{
		luaL_error(L, "sector expected");
		return 0;
	}

	lua_pushstring(L, "__index");
	lua_rawget(L, -2);

	if(lua_tocfunction(L, -1) != LUA_ThinkerIndex)
	{
		luaL_error(L, "sector expected");
		return 0;
	}

	lua_pop(L, 2);

	dest = lua_touserdata(L, idx);

	if(dest->soundorg.thinker.lua_type != TT_SECTOR)
	{
		luaL_error(L, "sector expected");
		return 0;
	}

	return dest;
}

line_t *LUA_GetLineParam(lua_State *L, int idx, boolean allownull)
{
	line_t *dest;

	if(allownull && lua_type(L, idx) == LUA_TNIL)
		return NULL;

	luaL_checktype(L, idx, LUA_TLIGHTUSERDATA);

	if(!lua_getmetatable(L, idx))
	{
		luaL_error(L, "line expected");
		return 0;
	}

	lua_pushstring(L, "__index");
	lua_rawget(L, -2);

	if(lua_tocfunction(L, -1) != LUA_ThinkerIndex)
	{
		luaL_error(L, "line expected");
		return 0;
	}

	lua_pop(L, 2);

	dest = lua_touserdata(L, idx);

	if(dest->soundorg.thinker.lua_type != TT_LINE)
	{
		luaL_error(L, "line expected");
		return 0;
	}

	return dest;
}

//
// LUA functions

static int LUA_print(lua_State *L)
{
	// arg0 - debug text
	if(lua_gettop(L) == 1)
		printf("LUA: %s\n", lua_tostring(L, -1));
	return 0;
}

static int LUA_createMobjType(lua_State *L)
{
	if(!lua_setup)
		return luaL_error(L, "createMobjType: you are too late now ...");

	if(lua_gettop(L) == 1 && lua_type(L, -1) == LUA_TTABLE)
	{
		int i, j;
		int k = 0;
		mobjinfo_t temp;

		// set type defaults
		memset(&temp, 0, sizeof(mobjinfo_t));
		memset(&temp.damagescale, DEFAULT_DAMAGE_SCALE, NUMDAMAGETYPES);
		temp.damagetype = NUMDAMAGETYPES;
		temp.lua_action = LUA_REFNIL;
		temp.lua_arg = LUA_REFNIL;
		temp.crash_action = LUA_REFNIL;
		temp.stepheight = 24 * FRACUNIT;
		temp.blocking = 0xFFFF;
		temp.icon = -1;
		temp.gravity = GRAVITY;
		// get states
		state_mobjt = numstates;
		LUA_MobjTypeStruct(L, &temp);
		// get memory
		Z_Enlarge(mobjinfo, sizeof(mobjinfo_t));
		// finish mobj type
		temp.dthink.lua_type = TT_MOBJINFO;
		memcpy(mobjinfo + numobjtypes, &temp, sizeof(mobjinfo_t));
		lua_pushlightuserdata(L, mobjinfo + numobjtypes);
		numobjtypes++;
	} else
		return luaL_error(L, "createMobjType: single table required");

	return 1;
}

static int LUA_setPlayerType(lua_State *L)
{
	MT_PLAYER = LUA_GetMobjTypeParam(L, 1);
	return 0;
}

static int LUA_addWeaponType(lua_State *L)
{
	int type, amm0, amm1;
	int top;

	// type; required
	type = LUA_GetMobjTypeParam(L, 1);

	top = lua_gettop(L);

	// primary ammo type; optional
	if(top > 1)
		amm0 = LUA_GetMobjTypeParam(L, 2);
	else
		amm0 = 0;

	// secondary ammo type; optional
	if(top > 2)
		amm1 = LUA_GetMobjTypeParam(L, 3);
	else
		amm1 = 0;

	ST_AddWeaponType(type, amm0, amm1);

	return 0;
}

static int LUA_addKeyType(lua_State *L)
{
	int type;
	type = LUA_GetMobjTypeParam(L, 1);
	ST_AddKeyType(type);
	return 0;
}

static int LUA_addAnimation(lua_State *L)
{
	int top = lua_gettop(L);
	int ticrate, i;
	animdef_t *anim;
	int (*func)(char*);
	int arg = 1;
	int target = -1;

	// target; optional
	if(lua_type(L, 1) == LUA_TBOOLEAN)
	{
		top--;
		arg++;
		target = lua_toboolean(L, 1);
	}

	// ticrate; required
	luaL_checktype(L, arg, LUA_TNUMBER);
	ticrate = lua_tointeger(L, arg);
	if(ticrate <= 0)
		ticrate = 1;

	// texture list; at least 2 required
	top--;
	if(top < 2)
		return luaL_error(L, "addAnimation: incorrect number of arguments");
	arg++;

	func = R_TextureNumForName;
	if(target < 0)
		isHexen = 1;
	else
	if(target)
		func = R_FlatNumForName;

	anim = P_AddAnimation(target, ticrate, top);
	for(i = 0; i < top; i++)
	{
		char name[8];
		const char *str;

		luaL_checktype(L, arg+i, LUA_TSTRING);
		str = lua_tostring(L, arg+i);
		strncpy(name, str, 8);
		anim->anim[i] = func(name);
	}

	return 0;
}

static int LUA_doomRandom(lua_State *L)
{
	int top = lua_gettop(L);
	int from, to;

	if(!top)
	{
		lua_pushinteger(L, P_Random());
		return 1;
	}

	luaL_checktype(L, 1, LUA_TNUMBER);

	if(top < 2)
	{
		from = 0;
		to = lua_tointeger(L, 1);
	} else
	{
		luaL_checktype(L, 2, LUA_TNUMBER);
		from = lua_tointeger(L, 1);
		to = lua_tointeger(L, 2);
	}

	if(to < from)
	{
		top = to;
		to = from;
		from = top;
	}
	from = from + (F_Random() % ((to - from)+1));
	lua_pushinteger(L, from);
	return 1;
}

static int LUA_spawnMobj(lua_State *L)
{
	mobj_t *mo;
	fixed_t x, y, z;
	angle_t ang;
	int type;
	boolean action;
	int top = lua_gettop(L);

	// type; required
	type = LUA_GetMobjTypeParam(L, 1);

	// x and y; required
	luaL_checktype(L, 2, LUA_TNUMBER); // x
	luaL_checktype(L, 3, LUA_TNUMBER); // y
	x = lua_tonumber(L, 2) * (lua_Number)FRACUNIT;
	y = lua_tonumber(L, 3) * (lua_Number)FRACUNIT;

	// z; optional
	if(top > 3)
	{
		luaL_checktype(L, 4, LUA_TNUMBER);
		z = lua_tonumber(L, 4) * (lua_Number)FRACUNIT;
	} else
		z = ONFLOORZ;

	// angle; optional
	if(top > 4)
	{
		luaL_checktype(L, 5, LUA_TNUMBER);
		ang = lua_tonumber(L, 5) * (lua_Number)(1 << ANGLETOFINESHIFT);
	} else
		ang = 0;

	// skip first action; optional
	if(top > 5)
	{
		luaL_checktype(L, 6, LUA_TBOOLEAN);
		action = lua_toboolean(L, 6);
	} else
		action = true;

	// spawn
	mo = P_SpawnMobj(x, y, z, type);
	mo->angle = ang;

	// play first action
	if(action)
		P_SetMobjAnimation(mo, ANIM_SPAWN, 0);

	lua_pushlightuserdata(L, mo);

	return 1;
}

static int LUA_blockThingsIterator(lua_State *L)
{
	fixed_t x, y, xe, ye, t;
	int bx, by;
	int top = lua_gettop(L);
	int ftype = lua_type(L, 1);

	if(top == 2 || (top == 3 && ftype == LUA_TLIGHTUSERDATA))
	{
		// mobj, func, [arg]
		mobj_t *mo;

		luaL_checktype(L, 2, LUA_TFUNCTION);

		mo = LUA_GetMobjParam(L, 1, false);

		if(top > 2)
		{
			block_lua_arg = luaL_ref(L, LUA_REGISTRYINDEX);
			LUA_DebugRef(block_lua_arg, false);
		} else
			block_lua_arg = LUA_REFNIL;

		block_lua_func = luaL_ref(L, LUA_REGISTRYINDEX);
		LUA_DebugRef(block_lua_func, false);

		x = mo->x;
		xe = x + mo->radius;
		x -= mo->radius;
		y = mo->y;
		ye = y + mo->radius;
		y -= mo->radius;
	} else
	if(top == 3 || top == 4)
	{
		// x, y, func, [arg]
		luaL_checktype(L, 1, LUA_TNUMBER);
		x = (fixed_t)(lua_tonumber(L, 1) * (lua_Number)FRACUNIT);
		luaL_checktype(L, 2, LUA_TNUMBER);
		y = (fixed_t)(lua_tonumber(L, 2) * (lua_Number)FRACUNIT);

		xe = x;
		ye = y;

		luaL_checktype(L, 3, LUA_TFUNCTION);

		if(top > 3)
		{
			block_lua_arg = luaL_ref(L, LUA_REGISTRYINDEX);
			LUA_DebugRef(block_lua_arg, false);
		} else
			block_lua_arg = LUA_REFNIL;

		block_lua_func = luaL_ref(L, LUA_REGISTRYINDEX);
		LUA_DebugRef(block_lua_func, false);

	} else
	if(top == 5 || top == 6)
	{
		// x1, y1, x2, y2, func, [arg]
		luaL_checktype(L, 1, LUA_TNUMBER);
		x = (fixed_t)(lua_tonumber(L, 1) * (lua_Number)FRACUNIT);
		luaL_checktype(L, 2, LUA_TNUMBER);
		y = (fixed_t)(lua_tonumber(L, 2) * (lua_Number)FRACUNIT);

		luaL_checktype(L, 3, LUA_TNUMBER);
		xe = (fixed_t)(lua_tonumber(L, 3) * (lua_Number)FRACUNIT);
		luaL_checktype(L, 4, LUA_TNUMBER);
		ye = (fixed_t)(lua_tonumber(L, 4) * (lua_Number)FRACUNIT);

		luaL_checktype(L, 5, LUA_TFUNCTION);

		if(top > 5)
		{
			block_lua_arg = luaL_ref(L, LUA_REGISTRYINDEX);
			LUA_DebugRef(block_lua_arg, false);
		} else
			block_lua_arg = LUA_REFNIL;

		block_lua_func = luaL_ref(L, LUA_REGISTRYINDEX);
		LUA_DebugRef(block_lua_func, false);
	} else
		return luaL_error(L, "blockThingsIterator: incorrect number of arguments");

	lua_settop(L, 0);

	if(x > xe)
	{
		t = x;
		x = xe;
		xe = t;
	}

	if(y > ye)
	{
		t = y;
		y = ye;
		ye = t;
	}

	block_lua_x0 = x;
	block_lua_x1 = xe;
	block_lua_y0 = y;
	block_lua_y1 = ye;

	x = (x - bmaporgx)>>MAPBLOCKSHIFT;
	xe = (xe - bmaporgx)>>MAPBLOCKSHIFT;
	y = (y - bmaporgy)>>MAPBLOCKSHIFT;
	ye = (ye - bmaporgy)>>MAPBLOCKSHIFT;

	luaS_PIT = L;

	for(bx = x; bx <= xe; bx++)
	{
		for(by = y; by <= ye; by++)
			if(!P_BlockThingsIterator(bx, by, PIT_LuaCheckThing))
				break;
		if(by <= ye)
			break;
	}

	LUA_DebugRef(block_lua_func, true);
	LUA_DebugRef(block_lua_arg, true);

	luaL_unref(L, LUA_REGISTRYINDEX, block_lua_func);
	luaL_unref(L, LUA_REGISTRYINDEX, block_lua_arg);

	return lua_gettop(L);
}

static int LUA_globalPlayersIterator(lua_State *L)
{
	int i;
	int arg, func;
	int top = lua_gettop(L);

	if(top > 2)
		return luaL_error(L, "globalPlayersIterator: incorrect number of arguments");

	luaL_checktype(L, 1, LUA_TFUNCTION);

	if(top > 1)
	{
		arg = luaL_ref(L, LUA_REGISTRYINDEX);
		LUA_DebugRef(arg, false);
	} else
		arg = LUA_REFNIL;

	func = luaL_ref(L, LUA_REGISTRYINDEX);
	LUA_DebugRef(func, false);

	lua_settop(L, 0);

	for(i = 0; i < MAXPLAYERS; i++)
	{
		if(!playeringame[i] || players[i].playerstate != PST_LIVE)
			continue;
		// function to call
		lua_rawgeti(L, LUA_REGISTRYINDEX, func);
		// player to pass
		lua_pushlightuserdata(L, players + i);
		// parameter to pass
		lua_rawgeti(L, LUA_REGISTRYINDEX, arg);
		// do the call
		if(lua_pcall(L, 2, LUA_MULTRET, 0))
			// script error
			return luaL_error(L, "PlayersIterator: %s", lua_tostring(L, -1));

		if(lua_gettop(L) == 0)
			// no return means continue
			continue;

		// first return has to be continue flag
		luaL_checktype(L, 1, LUA_TBOOLEAN);

		if(lua_toboolean(L, 1))
			// iteration will continue, discard all returns
			lua_settop(L, 0);
		else
		{
			// remove only this flag, rest will get returned
			lua_remove(L, 1);
			break;
		}
	}
	LUA_DebugRef(func, true);
	LUA_DebugRef(arg, true);
	luaL_unref(L, LUA_REGISTRYINDEX, func);
	luaL_unref(L, LUA_REGISTRYINDEX, arg);
	return lua_gettop(L);
}

static int LUA_globalThingsIterator(lua_State *L)
{
	int arg, func;
	thinker_t *think;
	int top = lua_gettop(L);

	if(top > 2)
		return luaL_error(L, "globalThingsIterator: incorrect number of arguments");

	luaL_checktype(L, 1, LUA_TFUNCTION);

	if(top > 1)
	{
		arg = luaL_ref(L, LUA_REGISTRYINDEX);
		LUA_DebugRef(arg, false);
	} else
		arg = LUA_REFNIL;

	func = luaL_ref(L, LUA_REGISTRYINDEX);
	LUA_DebugRef(func, false);

	lua_settop(L, 0);

	for(think = thinkercap.next; think != &thinkercap; think = think->next)
	{
		if(think->lua_type != TT_MOBJ)
			// not a mobj thinker
			continue;
		// function to call
		lua_rawgeti(L, LUA_REGISTRYINDEX, func);
		// mobj to pass
		lua_pushlightuserdata(L, think);
		// parameter to pass
		lua_rawgeti(L, LUA_REGISTRYINDEX, arg);
		// do the call
		if(lua_pcall(L, 2, LUA_MULTRET, 0))
			// script error
			return luaL_error(L, "ThingsIterator: %s", lua_tostring(L, -1));

		if(lua_gettop(L) == 0)
			// no return means continue
			continue;

		// first return has to be continue flag
		luaL_checktype(L, 1, LUA_TBOOLEAN);

		if(lua_toboolean(L, 1))
			// iteration will continue, discard all returns
			lua_settop(L, 0);
		else
		{
			// remove only this flag, rest will get returned
			lua_remove(L, 1);
			break;
		}
	}
	LUA_DebugRef(func, true);
	LUA_DebugRef(arg, true);
	luaL_unref(L, LUA_REGISTRYINDEX, func);
	luaL_unref(L, LUA_REGISTRYINDEX, arg);
	return lua_gettop(L);
}

static int LUA_globalSectorsIterator(lua_State *L)
{
	int i;
	int arg, func;
	int top = lua_gettop(L);

	if(top > 2)
		return luaL_error(L, "globalSectorsIterator: incorrect number of arguments");

	luaL_checktype(L, 1, LUA_TFUNCTION);

	if(top > 1)
	{
		arg = luaL_ref(L, LUA_REGISTRYINDEX);
		LUA_DebugRef(arg, false);
	} else
		arg = LUA_REFNIL;

	func = luaL_ref(L, LUA_REGISTRYINDEX);
	LUA_DebugRef(func, false);

	lua_settop(L, 0);

	for(i = 0; i < numsectors; i++)
	{
		// function to call
		lua_rawgeti(L, LUA_REGISTRYINDEX, func);
		// sector to pass
		lua_pushlightuserdata(L, sectors + i);
		// parameter to pass
		lua_rawgeti(L, LUA_REGISTRYINDEX, arg);
		// do the call
		if(lua_pcall(L, 2, LUA_MULTRET, 0))
			// script error
			return luaL_error(L, "SectorsIterator: %s", lua_tostring(L, -1));

		if(lua_gettop(L) == 0)
			// no return means continue
			continue;

		// first return has to be continue flag
		luaL_checktype(L, 1, LUA_TBOOLEAN);

		if(lua_toboolean(L, 1))
			// iteration will continue, discard all returns
			lua_settop(L, 0);
		else
		{
			// remove only this flag, rest will get returned
			lua_remove(L, 1);
			break;
		}
	}
	LUA_DebugRef(func, true);
	LUA_DebugRef(arg, true);
	luaL_unref(L, LUA_REGISTRYINDEX, func);
	luaL_unref(L, LUA_REGISTRYINDEX, arg);
	return lua_gettop(L);
}

static int LUA_globalLinesIterator(lua_State *L)
{
	int i;
	int arg, func;
	int top = lua_gettop(L);

	if(top > 2)
		return luaL_error(L, "globalLinesIterator: incorrect number of arguments");

	luaL_checktype(L, 1, LUA_TFUNCTION);

	if(top > 1)
	{
		arg = luaL_ref(L, LUA_REGISTRYINDEX);
		LUA_DebugRef(arg, false);
	} else
		arg = LUA_REFNIL;

	func = luaL_ref(L, LUA_REGISTRYINDEX);
	LUA_DebugRef(func, false);

	lua_settop(L, 0);

	for(i = 0; i < numlines; i++)
	{
		// function to call
		lua_rawgeti(L, LUA_REGISTRYINDEX, func);
		// sector to pass
		lua_pushlightuserdata(L, lines + i);
		// parameter to pass
		lua_rawgeti(L, LUA_REGISTRYINDEX, arg);
		// do the call
		if(lua_pcall(L, 2, LUA_MULTRET, 0))
			// script error
			return luaL_error(L, "LinesIterator: %s", lua_tostring(L, -1));

		if(lua_gettop(L) == 0)
			// no return means continue
			continue;

		// first return has to be continue flag
		luaL_checktype(L, 1, LUA_TBOOLEAN);

		if(lua_toboolean(L, 1))
			// iteration will continue, discard all returns
			lua_settop(L, 0);
		else
		{
			// remove only this flag, rest will get returned
			lua_remove(L, 1);
			break;
		}
	}
	LUA_DebugRef(func, true);
	LUA_DebugRef(arg, true);
	luaL_unref(L, LUA_REGISTRYINDEX, func);
	luaL_unref(L, LUA_REGISTRYINDEX, arg);
	return lua_gettop(L);
}

static int LUA_sectorTagIterator(lua_State *L)
{
	int i, tag;
	int arg, func;
	int top = lua_gettop(L);

	if(top > 3)
		return luaL_error(L, "sectorTagIterator: incorrect number of arguments");

	// tag; required
	luaL_checktype(L, 1, LUA_TNUMBER);
	// function; required
	luaL_checktype(L, 2, LUA_TFUNCTION);

	tag = lua_tointeger(L, 1);

	// arg; optional
	if(top > 2)
	{
		arg = luaL_ref(L, LUA_REGISTRYINDEX);
		LUA_DebugRef(arg, false);
	} else
		arg = LUA_REFNIL;

	func = luaL_ref(L, LUA_REGISTRYINDEX);
	LUA_DebugRef(func, false);

	lua_settop(L, 0);

	for(i = 0; i < numsectors; i++)
	{
		if(sectors[i].tag != tag)
			continue;
		// function to call
		lua_rawgeti(L, LUA_REGISTRYINDEX, func);
		// mobj to pass
		lua_pushlightuserdata(L, &sectors[i]);
		// parameter to pass
		lua_rawgeti(L, LUA_REGISTRYINDEX, arg);
		// do the call
		if(lua_pcall(L, 2, LUA_MULTRET, 0))
			// script error
			return luaL_error(L, "SectorIterator: %s", lua_tostring(L, -1));

		if(lua_gettop(L) == 0)
			// no return means continue
			continue;

		// first return has to be continue flag
		luaL_checktype(L, 1, LUA_TBOOLEAN);

		if(lua_toboolean(L, 1))
			// iteration will continue, discard all returns
			lua_settop(L, 0);
		else
		{
			// remove only this flag, rest will get returned
			lua_remove(L, 1);
			break;
		}
	}
	LUA_DebugRef(func, true);
	LUA_DebugRef(arg, true);
	luaL_unref(L, LUA_REGISTRYINDEX, func);
	luaL_unref(L, LUA_REGISTRYINDEX, arg);
	return lua_gettop(L);
}

static int LUA_thingTagIterator(lua_State *L)
{
	int i, tag;
	int arg, func;
	int top = lua_gettop(L);
	thinker_t *think;

	if(top > 3)
		return luaL_error(L, "thingTagIterator: incorrect number of arguments");

	// tag; required
	luaL_checktype(L, 1, LUA_TNUMBER);
	// function; required
	luaL_checktype(L, 2, LUA_TFUNCTION);

	tag = lua_tointeger(L, 1);

	// arg; optional
	if(top > 2)
	{
		arg = luaL_ref(L, LUA_REGISTRYINDEX);
		LUA_DebugRef(arg, false);
	} else
		arg = LUA_REFNIL;

	func = luaL_ref(L, LUA_REGISTRYINDEX);
	LUA_DebugRef(func, false);

	lua_settop(L, 0);

	for(think = thinkercap.next; think != &thinkercap; think = think->next)
	{
		if(think->lua_type != TT_MOBJ)
			continue;
		if(((mobj_t*)think)->tag != tag)
			continue;
		// function to call
		lua_rawgeti(L, LUA_REGISTRYINDEX, func);
		// mobj to pass
		lua_pushlightuserdata(L, think);
		// parameter to pass
		lua_rawgeti(L, LUA_REGISTRYINDEX, arg);
		// do the call
		if(lua_pcall(L, 2, LUA_MULTRET, 0))
			// script error
			return luaL_error(L, "ThingIterator: %s", lua_tostring(L, -1));

		if(lua_gettop(L) == 0)
			// no return means continue
			continue;

		// first return has to be continue flag
		luaL_checktype(L, 1, LUA_TBOOLEAN);

		if(lua_toboolean(L, 1))
			// iteration will continue, discard all returns
			lua_settop(L, 0);
		else
		{
			// remove only this flag, rest will get returned
			lua_remove(L, 1);
			break;
		}
	}
	LUA_DebugRef(func, true);
	LUA_DebugRef(arg, true);
	luaL_unref(L, LUA_REGISTRYINDEX, func);
	luaL_unref(L, LUA_REGISTRYINDEX, arg);
	return lua_gettop(L);
}

static int LUA_setFakeContrast(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TBOOLEAN);
	r_fakecontrast = lua_toboolean(L, 1);
	return 0;
}

static int LUA_setSky(lua_State *L)
{
	const char *tex;
	char temp[8];
	int num;

	tex = lua_tostring(L, -1);
	strncpy(temp, tex, sizeof(temp));
	num = R_TextureNumForName(temp);
	if(num > 0)
		skytexture = num;

	return 0;
}

static int LUA_setBackground(lua_State *L)
{
	const char *tex;
	char temp[8];
	int lump;

	tex = lua_tostring(L, -1);
	strncpy(temp, tex, sizeof(temp));
	lump = W_CheckNumForName(temp);
	if(lump >= 0)
		game_backpatch = W_CacheLumpNum(lump);
	else
		game_backpatch = NULL;
	return 0;
}

//
// indirect LUA functions

static int LUA_luaExit(lua_State *L)
{
	const char *tmp;

	luaL_checktype(L, 1, LUA_TSTRING);
	tmp = lua_tostring(L, 1);
	strncpy(level_name, tmp, 8);
	G_ExitLevel();
	return 0;
}

static int LUA_doomExit(lua_State *L)
{
	level_name[0] = 0;
	if(lua_gettop(L) > 0)
	{
		luaL_checktype(L, 1, LUA_TBOOLEAN);
		if(lua_toboolean(L, 1))
		{
			G_SecretExitLevel();
			return 0;
		}
	}
	G_ExitLevel();
	return 0;
}

static int LUA_TableReadOnly(lua_State *L)
{
	return luaL_error(L, "tried to modify read-only table");
}

static int LUA_ThinkerIndex(lua_State *L)
{
	int i;
	const char *idx;
	const lua_table_model_t *field;
	thinker_t *th;
	int top = lua_gettop(L);

	// get index
	luaL_checktype(L, 2, LUA_TSTRING);
	idx = lua_tostring(L, 2);

	// get thinker type
	th = lua_touserdata(L, 1);
	switch(th->lua_type)
	{
		case TT_MOBJ:
			// special check
			field = NULL;
			if(idx[0] == '_')
			{
				if(idx[1] == '_')
				{
					// flag name
					for(i = 0; i < sizeof(lua_mobjflags) / sizeof(lua_mobjflag_t); i++)
					{
						if(!strcmp(idx + 2, lua_mobjflags[i].name))
						{
							mobj_t *mo = (mobj_t*)th;
							if(top != 2)
							{
								uint64_t new;

								luaL_checktype(L, 3, LUA_TBOOLEAN);
								if(lua_toboolean(L, 3))
									// set
									new = mo->flags | lua_mobjflags[i].value;
								else
									// unset
									new = mo->flags & ~lua_mobjflags[i].value;

								// special check
								if((mo->flags ^ new) & (MF_NOBLOCKMAP | MF_NOSECTOR))
								{
									// these can't be just changed
									P_UnsetThingPosition(mo);
									mo->flags = new;
									P_SetThingPosition(mo);
								} else
									// set flag
									mo->flags = new;

								return 0;
							} else
							{
								lua_pushboolean(L, !!(mo->flags & lua_mobjflags[i].value));
								return 1;
							}
						}
					}
				} else
				{
					// animation change
					if(top != 2)
						return func_set_readonly(L, th, th);
					int k = 0;
					for(i = 0; i < sizeof(lua_mobjtype) / sizeof(lua_table_model_t); i++)
					{
						if(lua_mobjtype[i].name[0] == '_')
						{
							if(!strcmp(idx, lua_mobjtype[i].name))
							{
								lua_pushlightuserdata(L, th);
								lua_pushinteger(L, k);
								lua_pushcclosure(L, LUA_animationFromMobj, 2);
								return 1;
							}
							k++;
						}
					}
				}
			} else
			// normal check
			field = LUA_FieldByName(idx, lua_mobj, sizeof(lua_mobj) / sizeof(lua_table_model_t));
		break;
		case TT_MOBJINFO:
			field = LUA_FieldByName(idx, lua_mobjtype, sizeof(lua_mobjtype) / sizeof(lua_table_model_t));
		break;
		case TT_PLAYER:
			field = LUA_FieldByName(idx, lua_player, sizeof(lua_player) / sizeof(lua_table_model_t));
		break;
		case TT_SECTOR:
			field = LUA_FieldByName(idx, lua_sector, sizeof(lua_sector) / sizeof(lua_table_model_t));
		break;
		case TT_LINE:
			field = LUA_FieldByName(idx, lua_linedef, sizeof(lua_linedef) / sizeof(lua_table_model_t));
		break;
		case TT_GENPLANE:
			field = LUA_FieldByName(idx, lua_genericplane, sizeof(lua_genericplane) / sizeof(lua_table_model_t));
		break;
		case TT_SECCALL:
			field = LUA_FieldByName(idx, lua_sectorcall, sizeof(lua_sectorcall) / sizeof(lua_table_model_t));
		break;
		case TT_SCROLLTEX:
			field = LUA_FieldByName(idx, lua_texturescroll, sizeof(lua_texturescroll) / sizeof(lua_table_model_t));
		break;
		default:
			return luaL_error(L, "unknown thinker type");
	}

	// check field
	if(!field)
		return luaL_error(L, "thinker '%s' does not contain '%s'", thinker_names[th->lua_type], idx);

	if(top == 2)
	{
		// index
		if(field->get)
			return field->get(L, ((void*)th) + field->offset, th);
		if(field->ltype == LUA_TBOOLEAN)
			lua_pushboolean(L, *(boolean*)(((void*)th) + field->offset));
		else
			lua_pushinteger(L, *(int*)(((void*)th) + field->offset));
		return 1;
	}

	// newindex
	if(field->ltype != LUA_TLIGHTUSERDATA && field->ltype != LUA_TNIL)
		luaL_checktype(L, -1, field->ltype);

	if(field->set)
		return field->set(L, ((void*)th) + field->offset, th);
	if(field->ltype == LUA_TBOOLEAN)
		*(boolean*)(((void*)th) + field->offset) = lua_toboolean(L, -1);
	else
		*(int*)(((void*)th) + field->offset) = lua_tointeger(L, -1);

	return 0;
}

static int LUA_removeMobjFromMobj(lua_State *L)
{
	mobj_t *mo;

	mo = lua_touserdata(L, lua_upvalueindex(1));

	if(mo->player)
		return luaL_error(L, "atempt to remove player mobj");

	P_RemoveMobj(mo);

	return 0;
}

static int LUA_faceFromMobj(lua_State *L)
{
	mobj_t *mo, *dest;
	fixed_t x, y, z, slope;
	int top = lua_gettop(L);

	mo = lua_touserdata(L, lua_upvalueindex(1));

	if(top > 1)
	{
		luaL_checktype(L, 1, LUA_TNUMBER);
		luaL_checktype(L, 2, LUA_TNUMBER);

		x = (fixed_t)(lua_tonumber(L, 1) * (lua_Number)FRACUNIT);
		y = (fixed_t)(lua_tonumber(L, 2) * (lua_Number)FRACUNIT);

		mo->angle = R_PointToAngle2(mo->x, mo->y, x, y);

		if(top > 2)
		{
			z = (fixed_t)(lua_tonumber(L, 3) * (lua_Number)FRACUNIT);

			slope = P_AproxDistance(x - mo->x, y - mo->y);
			slope = P_AproxDistance(slope, z - mo->z) / FRACUNIT;

			if(slope < 1)
				slope = 1;

			mo->pitch = (z - mo->z) / slope;
		}
	} else
	{
		dest = LUA_GetMobjParam(L, 1, true);
		if(dest)
		{
			mo->angle = R_PointToAngle2(mo->x, mo->y, dest->x, dest->y);
			mo->pitch = P_AimLineAttack(mo, mo->angle, MISSILERANGE, dest);
		}
	}

	return 0;
}

static int LUA_teleportMobj(lua_State *L)
{
	mobj_t *mo;
	fixed_t x, y, z;

	luaL_checktype(L, 1, LUA_TNUMBER);
	luaL_checktype(L, 2, LUA_TNUMBER);
	if(lua_type(L, 3) == LUA_TBOOLEAN)
	{
		if(lua_toboolean(L, 3))
			z = ONCEILINGZ;
		else
			z = ONFLOORZ;
	} else
	{
		luaL_checktype(L, 3, LUA_TNUMBER);
		z = lua_tonumber(L, 3) * (lua_Number)FRACUNIT;
	}

	x = lua_tonumber(L, 1) * (lua_Number)FRACUNIT;
	y = lua_tonumber(L, 2) * (lua_Number)FRACUNIT;

	mo = lua_touserdata(L, lua_upvalueindex(1));

	P_UnsetThingPosition(mo);
	mo->x = x;
	mo->y = y;
	mo->z = z;
	P_SetThingPosition(mo);

	mo->floorz = mo->subsector->sector->floorheight;
	mo->ceilingz = mo->subsector->sector->ceilingheight;

	if(mo->z == ONCEILINGZ)
		mo->z = mo->ceilingz - mo->height;

	if(mo->z < mo->floorz)
		mo->z = mo->floorz;

	if(mo->player)
		mo->player->viewz = mo->z + mo->player->viewheight;

	return 0;
}

static int LUA_checkMobjPos(lua_State *L)
{
	mobj_t *mo;
	fixed_t x, y, z, oz;

	mo = lua_touserdata(L, lua_upvalueindex(1));

	if(lua_gettop(L) == 0)
	{
		x = mo->x;
		y = mo->y;
		z = mo->z;
	} else
	{
		luaL_checktype(L, 1, LUA_TNUMBER);
		luaL_checktype(L, 2, LUA_TNUMBER);
		luaL_checktype(L, 3, LUA_TNUMBER);
		x = lua_tonumber(L, 1) * (lua_Number)FRACUNIT;
		y = lua_tonumber(L, 2) * (lua_Number)FRACUNIT;
		z = lua_tonumber(L, 3) * (lua_Number)FRACUNIT;
	}

	mo->flags |= MF_TELEPORT;
	oz = mo->z;
	mo->z = z;
	lua_pushboolean(L, P_CheckPosition(mo, x, y));
	mo->z = oz;
	mo->flags &= ~MF_TELEPORT;

	return 1;
}

static int LUA_spawnMissile(lua_State *L)
{
	mobj_t *mo;
	mobj_t *mi;
	int type;
	angle_t a = 0;
	fixed_t x = 0;
	fixed_t y = 0;
	fixed_t z = 0;
	fixed_t s = 0;
	int top = lua_gettop(L);

	// type, must be present
	type = LUA_GetMobjTypeParam(L, 1);

	// absolute angle
	if(top > 1)
	{
		luaL_checktype(L, 2, LUA_TNUMBER);
		a = lua_tonumber(L, 2) * (lua_Number)(1 << ANGLETOFINESHIFT);
	}

	// absolute pitch
	if(top > 2)
	{
		luaL_checktype(L, 3, LUA_TNUMBER);
		s = (fixed_t)(lua_tonumber(L, 3) * (lua_Number)FRACUNIT);
	}

	// z offset
	if(top > 3)
	{
		luaL_checktype(L, 4, LUA_TNUMBER);
		z = (fixed_t)(lua_tonumber(L, 4) * (lua_Number)FRACUNIT);
	}

	// x offset
	if(top > 4)
	{
		luaL_checktype(L, 5, LUA_TNUMBER);
		x = (fixed_t)(lua_tonumber(L, 5) * (lua_Number)FRACUNIT);
	}

	// y offset
	if(top > 5)
	{
		luaL_checktype(L, 6, LUA_TNUMBER);
		y = (fixed_t)(lua_tonumber(L, 6) * (lua_Number)FRACUNIT);
	}

	// spawn
	mo = lua_touserdata(L, lua_upvalueindex(1));
	mi = P_SpawnMissile(mo, type, a, s, z, x, y);

	lua_pushlightuserdata(L, mi);

	return 1;
}

static int LUA_attackAim(lua_State *L)
{
	mobj_t *source;
	mobj_t *dest;
	angle_t angle;
	fixed_t slope;
	fixed_t z;
	boolean hitscan = 0;
	int top = lua_gettop(L);

	source = lua_touserdata(L, lua_upvalueindex(1));

	// optional type
	if(top > 0)
	{
		luaL_checktype(L, 1, LUA_TBOOLEAN);
		hitscan = lua_toboolean(L, 1);
	}

	if(top > 1)
	{
		// optional target
		dest = LUA_GetMobjParam(L, 2, true);
	} else
		// use mobj target
		dest = source->target;

	if(source->player && !dest)
	{
		// player aim
		if(sv_freeaim)
		{
			slope = source->pitch;
		} else
		{
			angle = source->angle;
			slope = P_AimLineAttack (source, angle, 16*64*FRACUNIT, NULL);

			if (!linetarget)
			{
				angle += 1<<26;
				slope = P_AimLineAttack (source, angle, 16*64*FRACUNIT, NULL);
				if (!linetarget)
				{
					angle -= 2<<26;
					slope = P_AimLineAttack (source, angle, 16*64*FRACUNIT, NULL);
				}
			}
		}
		angle = source->angle;
	} else
	{
		// other aim

		if(!dest)
		{
			// find first target
			angle = source->angle;
			slope = P_AimLineAttack(source, angle, MISSILERANGE, NULL);
			if(!linetarget)
				slope = 0;
			hitscan = 0;
		} else
			angle = R_PointToAngle2(source->x, source->y, dest->x, dest->y);

		if(hitscan)
		{
			slope = P_AimLineAttack(source, angle, MISSILERANGE, dest);
			if(!linetarget)
				hitscan = false;
		}

		if(!hitscan && dest)
		{
			slope = P_AproxDistance(dest->x - source->x, dest->y - source->y);
			slope /= FRACUNIT;

			if(slope < 1)
				slope = 1;

			slope = (dest->z - source->z) / slope;
		}
	}

	lua_pushnumber(L, angle / (lua_Number)(1 << ANGLETOFINESHIFT));
	lua_pushnumber(L, slope / (lua_Number)FRACUNIT);

	return 2;
}

static int LUA_lineTarget(lua_State *L)
{
	mobj_t *mo;
	angle_t angle;
	fixed_t slope;

	mo = lua_touserdata(L, lua_upvalueindex(1));

	// optional angle
	if(lua_gettop(L))
	{
		luaL_checktype(L, 1, LUA_TNUMBER);
		angle = lua_tonumber(L, 1) * (lua_Number)(1 << ANGLETOFINESHIFT);
	} else
		angle = mo->angle;

	slope = P_AimLineAttack(mo, angle, MISSILERANGE, NULL);

	if(linetarget)
		lua_pushlightuserdata(L, linetarget);
	else
		lua_pushnil(L);
	lua_pushnumber(L, slope / (lua_Number)FRACUNIT);

	return 2;
}

static int LUA_meleeRange(lua_State *L)
{
	mobj_t *source;
	mobj_t *dest;
	boolean zCheck;
	int top = lua_gettop(L);

	source = lua_touserdata(L, lua_upvalueindex(1));

	if(top > 0)
		// optional target
		dest = LUA_GetMobjParam(L, 1, false);
	else
		// use mobj target
		dest = source->target;

	if(top > 1)
	{
		luaL_checktype(L, 2, LUA_TBOOLEAN);
		zCheck = lua_toboolean(L, 2);
	} else
		zCheck = true;

	if(P_CheckMeleeRange(source, dest, zCheck))
		lua_pushboolean(L, true);
	else
		lua_pushboolean(L, false);

	return 1;
}

static int LUA_mobjDistance(lua_State *L)
{
	mobj_t *source;
	mobj_t *dest;
	boolean zCheck;
	fixed_t dist;
	int top = lua_gettop(L);

	source = lua_touserdata(L, lua_upvalueindex(1));

	if(top > 0)
		// optional target
		dest = LUA_GetMobjParam(L, 1, false);
	else
		// use mobj target
		dest = source->target;

	if(top > 1)
	{
		luaL_checktype(L, 2, LUA_TBOOLEAN);
		zCheck = lua_toboolean(L, 2);
	} else
		zCheck = true;

	dist = P_AproxDistance(dest->x - source->x, dest->y - source->y);
	if(zCheck)
		dist = P_AproxDistance(dest->z - source->z, dist);

	lua_pushnumber(L, dist / (lua_Number)FRACUNIT);

	return 1;
}

static int LUA_damageFromMobj(lua_State *L)
{
	int damage, type;
	mobj_t *dest;
	mobj_t *inflictor = NULL;
	mobj_t *source = NULL;
	int top = lua_gettop(L);

	// damage; required
	if(lua_type(L, 1) == LUA_TBOOLEAN)
	{
		if(lua_toboolean(L, 1))
			damage = INSTANTGIB;
		else
			damage = INSTANTKILL;
	} else
	{
		luaL_checktype(L, 1, LUA_TNUMBER);
		damage = lua_tointeger(L, 1);
	}

	// damage type; required
	luaL_checktype(L, 2, LUA_TNUMBER);
	type = lua_tointeger(L, 1) - 1;
	if(type < 0)
		type = NUMDAMAGETYPES;

	// optional source
	if(top > 2)
		source = LUA_GetMobjParam(L, 3, true);

	// optional inflictor
	if(top > 3)
		inflictor = LUA_GetMobjParam(L, 4, true);

	dest = lua_touserdata(L, lua_upvalueindex(1));

	P_DamageMobj(dest, inflictor, source, damage, type);

	if(dest->health > 0)
		lua_pushboolean(L, false);
	else
		lua_pushboolean(L, true);

	return 1;
}

static int LUA_lineAttack(lua_State *L)
{
	mobj_t *mo;
	int type, damage;
	angle_t a = 0;
	fixed_t x = 0;
	fixed_t z = 0;
	fixed_t s = 0;
	fixed_t r = MISSILERANGE;
	int top = lua_gettop(L);

	// type, must be present
	type = LUA_GetMobjTypeParam(L, 1);

	// damage, must be present
	luaL_checktype(L, 2, LUA_TNUMBER);
	damage = lua_tointeger(L, 2);

	// absolute angle
	if(top > 2)
	{
		luaL_checktype(L, 3, LUA_TNUMBER);
		a = lua_tonumber(L, 3) * (lua_Number)(1 << ANGLETOFINESHIFT);
	}

	// absolute pitch
	if(top > 3)
	{
		luaL_checktype(L, 4, LUA_TNUMBER);
		s = (fixed_t)(lua_tonumber(L, 4) * (lua_Number)FRACUNIT);
	}

	// z offset
	if(top > 4)
	{
		luaL_checktype(L, 5, LUA_TNUMBER);
		z = (fixed_t)(lua_tonumber(L, 5) * (lua_Number)FRACUNIT);
	}

	// x offset
	if(top > 5)
	{
		luaL_checktype(L, 6, LUA_TNUMBER);
		x = (fixed_t)(lua_tonumber(L, 6) * (lua_Number)FRACUNIT);
	}

	// range
	if(top > 6)
	{
		luaL_checktype(L, 7, LUA_TNUMBER);
		r = (fixed_t)(lua_tonumber(L, 7) * (lua_Number)FRACUNIT);
	}

	// spawn
	mo = lua_touserdata(L, lua_upvalueindex(1));

	la_pufftype = type;
	P_LineAttack(mo, a, r, s, damage, z, x);

	if(linetarget)
		lua_pushlightuserdata(L, linetarget);
	else
		lua_pushnil(L);

	return 1;
}

static int LUA_thrustFromMobj(lua_State *L)
{
	mobj_t *mo;
	fixed_t speed;
	angle_t a = 0;
	int top = lua_gettop(L);

	// speed, must be present
	luaL_checktype(L, 1, LUA_TNUMBER);
	speed = (fixed_t)(lua_tonumber(L, 1) * (lua_Number)FRACUNIT);

	mo = lua_touserdata(L, lua_upvalueindex(1));

	// angle, optional
	if(top > 1)
	{
		luaL_checktype(L, 2, LUA_TNUMBER);
		a = lua_tonumber(L, 2) * (lua_Number)(1 << ANGLETOFINESHIFT);
	} else
		a = mo->angle;

	mo->momx = FixedMul(speed, finecosine[a>>ANGLETOFINESHIFT]);
	mo->momy = FixedMul(speed, finesine[a>>ANGLETOFINESHIFT]);

	return 0;
}

static int LUA_sightCheckFromMobj(lua_State *L)
{
	mobj_t *source;
	mobj_t *dest;

	source = lua_touserdata(L, lua_upvalueindex(1));

	if(lua_gettop(L) > 0)
		// optional target
		dest = LUA_GetMobjParam(L, -1, false);
	else
		// use mobj target
		dest = source->target;

	if(dest && P_CheckSight(source, dest))
		lua_pushboolean(L, true);
	else
		lua_pushboolean(L, false);

	return 1;
}

static int LUA_radiusDamageFromMobj(lua_State *L)
{
	mobj_t *source;
	int damage, type;
	fixed_t range;
	mobj_t *from = NULL;
	boolean hurt = true;
	int top = lua_gettop(L);

	// range; required
	luaL_checktype(L, 1, LUA_TNUMBER);
	range = (fixed_t)(lua_tonumber(L, 1) * (lua_Number)FRACUNIT);

	// damage; required
	luaL_checktype(L, 2, LUA_TNUMBER);
	damage = lua_tointeger(L, 2);

	// damage type; required
	luaL_checktype(L, 3, LUA_TNUMBER);
	type = lua_tointeger(L, 3) - 1;
	if(type < 0)
		type = NUMDAMAGETYPES;

	// attacker; optional
	if(top > 3)
		from = LUA_GetMobjParam(L, 4, true);

	// hurt flag; optional
	if(top > 4)
	{
		luaL_checktype(L, 5, LUA_TBOOLEAN);
		hurt = lua_toboolean(L, 5);
	}

	source = lua_touserdata(L, lua_upvalueindex(1));

	P_RadiusAttack(source, from, range, damage, hurt, type);

	return 0;
}

static int LUA_damageScaleFromMobj(lua_State *L)
{
	mobj_t *mo;
	int type;
	int num;

	mo = lua_touserdata(L, lua_upvalueindex(1));

	// damage type; required
	luaL_checktype(L, 1, LUA_TNUMBER);
	type = lua_tointeger(L, 1) - 1;
	if(type < 0)
		type = NUMDAMAGETYPES;

	// damage scaler; optional
	if(lua_gettop(L) > 1)
	{
		// set
		if(type >= NUMDAMAGETYPES)
			return luaL_error(L, "invalid damage type");

		if(lua_type(L, 2) == LUA_TBOOLEAN)
		{
			// instant gib
			mo->damagescale[type] = 255;
		} else
		{
			luaL_checktype(L, 2, LUA_TNUMBER);
			num = (DEFAULT_DAMAGE_SCALE-DAMAGE_SCALE) + (lua_tonumber(L, -1) * (lua_Number)DAMAGE_SCALE);
			if(num < 0)
				num = 0;
			if(num > 254)
				num = 254;
			mo->damagescale[type] = num;
		}

		return 0;
	} else
	{
		// get
		if(type >= NUMDAMAGETYPES)
			num = DEFAULT_DAMAGE_SCALE;
		else
			num = mo->damagescale[type];

		lua_pushnumber(L, (lua_Number)(num - (DEFAULT_DAMAGE_SCALE-DAMAGE_SCALE)) / DAMAGE_SCALE);
		return 1;
	}
}

static int LUA_resetFlagsFromMobj(lua_State *L)
{
	mobj_t *mo;
	uint64_t new;
	boolean defa = false;

	// reset to type defaults; optional
	if(lua_gettop(L) > 0)
	{
		luaL_checktype(L, 1, LUA_TBOOLEAN);
		defa = lua_toboolean(L, 1);
	}

	mo = lua_touserdata(L, lua_upvalueindex(1));

	if(defa)
		new = mo->info->flags;
	else
		new = 0;

	if((mo->flags ^ new) & (MF_NOBLOCKMAP | MF_NOSECTOR))
	{
		P_UnsetThingPosition(mo);
		mo->flags = new;
		P_SetThingPosition(mo);
	} else
		mo->flags = new;

	return 0;
}

static int LUA_soundFromMobj(lua_State *L)
{
	const char *tex;
	char temp[8];
	int lump;
	int chan, idx;
	mobj_t *source;
	line_t *line;
	int top = lua_gettop(L);

	// at least one sound
	luaL_checktype(L, 1, LUA_TSTRING);

	// sound(s) to play
	// if multiple sounds are specified, pick random one
	if(top > 1)
		idx = 1 + (P_Random() % top);
	else
		idx = 1;

	tex = lua_tostring(L, idx);

	if(tex[0] == '-' && tex[1] == 0)
	{
		// support for 'no sound'
		return 0;
	} else
	{
		strncpy(temp, tex, sizeof(temp));
		lump = W_GetNumForName(temp);
	}

	source = lua_touserdata(L, lua_upvalueindex(1));
	chan = lua_tointeger(L, lua_upvalueindex(2));

	S_StartSound(source, lump, chan);

	return 0;
}

static int LUA_mobjInventoryGive(lua_State *L)
{
	mobj_t *mo;
	mobjinfo_t *type;
	int num;

	// amount; optional
	if(lua_gettop(L) > 1)
	{
		luaL_checktype(L, 2, LUA_TNUMBER);
		num = lua_tointeger(L, 2);
	} else
		num = 1;

	mo = lua_touserdata(L, lua_upvalueindex(1));

	// type; required
	LUA_GetMobjTypeParam(L, 1);
	type = lua_touserdata(L, 1);

	if(num >= 0) // allow to give 0 for depletable items
		num = P_GiveInventory(mo, type, num);
	else
		num = 0;

	lua_pushinteger(L, num);

	return 1;
}

static int LUA_mobjInventoryTake(lua_State *L)
{
	mobj_t *mo;
	mobjinfo_t *type;
	int num;

	// amount; optional
	if(lua_gettop(L) > 1)
	{
		luaL_checktype(L, 2, LUA_TNUMBER);
		num = lua_tointeger(L, 2);
	} else
		num = 1;

	mo = lua_touserdata(L, lua_upvalueindex(1));

	// type; required
	LUA_GetMobjTypeParam(L, 1);
	type = lua_touserdata(L, 1);

	if(num > 0)
		num = P_GiveInventory(mo, type, -num);
	else
		num = 0;

	lua_pushinteger(L, num);

	return 1;
}

static int LUA_mobjInventoryCheck(lua_State *L)
{
	mobj_t *mo;
	mobjinfo_t *type;
	int num, max;

	mo = lua_touserdata(L, lua_upvalueindex(1));

	// type; required
	LUA_GetMobjTypeParam(L, 1);
	type = lua_touserdata(L, 1);

	num = P_CheckInventory(mo, type, &max);

	if(max < 0)
		max = -max;

	// return count and maxcount
	lua_pushinteger(L, num);
	lua_pushinteger(L, max);

	return 2;
}

static int LUA_mobjInventorySetMax(lua_State *L)
{
	mobj_t *mo;
	mobjinfo_t *type;
	int num;

	// max amount; required
	luaL_checktype(L, 2, LUA_TNUMBER);

	mo = lua_touserdata(L, lua_upvalueindex(1));

	// type; required
	LUA_GetMobjTypeParam(L, 1);
	type = lua_touserdata(L, 1);

	num = lua_tointeger(L, 2);
	num = P_MaxInventory(mo, type, num);

	lua_pushinteger(L, num);

	return 1;
}

static int LUA_mobjTickerSet(lua_State *L)
{
	mobj_t *mo;
	int func, id, rate;
	int arg = LUA_REFNIL;
	int patch = -1;

	// ID; required
	luaL_checktype(L, 1, LUA_TNUMBER);
	// ticrate; required
	luaL_checktype(L, 2, LUA_TNUMBER);
	// icon; can be nil
	if(lua_type(L, 3) != LUA_TNIL)
	{
		const char *tmp;
		char name[8];

		luaL_checktype(L, 3, LUA_TSTRING);
		tmp = lua_tostring(L, 3);
		strncpy(name, tmp, 8);
		patch = W_CheckNumForName(name);
	}
	// function; required
	luaL_checktype(L, 4, LUA_TFUNCTION);

	id = lua_tointeger(L, 1);
	rate = lua_tointeger(L, 2);

	// argument; optional
	if(lua_gettop(L) > 4)
	{
		arg = luaL_ref(L, LUA_REGISTRYINDEX);
		LUA_DebugRef(arg, false);
	}

	// set function
	func = luaL_ref(L, LUA_REGISTRYINDEX);
	LUA_DebugRef(func, false);

	mo = lua_touserdata(L, lua_upvalueindex(1));

	P_AddMobjTicker(mo, id, rate, func, arg, patch);

	return 0;
}

static int LUA_mobjTickerDel(lua_State *L)
{
	mobj_t *mo;
	int id;

	// ID; required
	luaL_checktype(L, 1, LUA_TNUMBER);
	id = lua_tointeger(L, 1);

	mo = lua_touserdata(L, lua_upvalueindex(1));
	P_RemoveMobjTicker(mo, id);

	return 0;
}

static int LUA_mobjTickerCheck(lua_State *L)
{
	generic_ticker_t *gt;
	mobj_t *mo;
	int id;

	// ID; required
	luaL_checktype(L, 1, LUA_TNUMBER);
	id = lua_tointeger(L, 1);

	mo = lua_touserdata(L, lua_upvalueindex(1));
	gt = mo->generic_ticker;

	while(gt)
	{
		if(gt->id == id)
		{
			lua_pushboolean(L, true);
			return 1;
		}
		gt = gt->next;
	}

	lua_pushboolean(L, false);
	return 1;
}

static int LUA_setPlayerMessage(lua_State *L)
{
	static char msg[64];
	const char *text;
	player_t *pl;

	pl = lua_touserdata(L, lua_upvalueindex(1));
	if(pl != &players[displayplayer])
		return 0;

	luaL_checktype(L, 1, LUA_TSTRING);

	text = lua_tostring(L, 1);

	strncpy(msg, text, sizeof(msg)-1);
	msg[sizeof(msg)-1] = 0;

	pl->message = msg;

	return 0;
}

static int LUA_playerHudMessage(lua_State *L)
{
	int id, x, y;
	const char *txt;
	int tics = 0;
	player_t *pl;

	pl = lua_touserdata(L, lua_upvalueindex(1));
	if(pl != &players[displayplayer])
		return 0;

	// ID; required
	luaL_checktype(L, 1, LUA_TNUMBER);
	id = lua_tointeger(L, 1);

	// X; required
	luaL_checktype(L, 2, LUA_TNUMBER);
	x = lua_tointeger(L, 2);

	// Y; required
	luaL_checktype(L, 3, LUA_TNUMBER);
	y = lua_tointeger(L, 3);

	// text; required
	luaL_checktype(L, 4, LUA_TSTRING);
	txt = lua_tostring(L, 4);

	// tics; optional
	if(lua_gettop(L) > 4)
	{
		luaL_checktype(L, 5, LUA_TNUMBER);
		tics = lua_tointeger(L, 5);
	}

	HU_MessagePlain(id, x, y, tics, txt);

	return 0;
}

static int LUA_playerHudAlign(lua_State *L)
{
	int in;

	// align; required
	luaL_checktype(L, 1, LUA_TNUMBER);
	in = lua_tointeger(L, 1);
	if(in < 0 || in > 2)
		return luaL_error(L, "Invalid alignment %i.", in);

	hudmsg_align = in;

	return 0;
}

static int LUA_playerHudScale(lua_State *L)
{
	int in;

	// scale; required
	luaL_checktype(L, 1, LUA_TNUMBER);
	in = lua_tointeger(L, 1);
	if(in < 1 || in > 4)
		return luaL_error(L, "Invalid alignment %i.", in);

	hudmsg_scale = in;

	return 0;
}

static int LUA_setPlayerWeapon(lua_State *L)
{
	player_t *pl;
	boolean forced = false;

	pl = lua_touserdata(L, lua_upvalueindex(1));

	if(lua_type(L, 1) == LUA_TNIL)
	{
		pl->psprites[ps_weapon].state = NULL;
		pl->psprites[ps_flash].state = NULL;
		pl->readyweapon = wp_nochange;
		return 0;
	}

	pl->pendingweapon = LUA_GetMobjTypeParam(L, 1);
	pl->lua_weapon_change = 1;

	if(lua_gettop(L) > 1)
	{
		luaL_checktype(L, 2, LUA_TBOOLEAN);
		forced = lua_toboolean(L, 2);
	}

	if(forced || !pl->psprites[ps_weapon].state)
		P_BringUpWeapon(pl);

	return 0;
}

static int LUA_playerRefireWeapon(lua_State *L)
{
	player_t *player;
	int offset = 0;

	player = lua_touserdata(L, lua_upvalueindex(1));

	if(lua_gettop(L) && lua_type(L, 1) != LUA_TNIL)
	{
		luaL_checktype(L, 1, LUA_TNUMBER);
		offset = lua_tonumber(L, 1);
		if(offset < 0)
			offset = 0;
	}

	P_WeaponRefire(player, offset);

	return 0;
}

static int LUA_playerFlashWeapon(lua_State *L)
{
	player_t *player;
	int offset = 0;

	player = lua_touserdata(L, lua_upvalueindex(1));

	if(lua_gettop(L) && lua_type(L, 1) != LUA_TNIL)
	{
		luaL_checktype(L, 1, LUA_TNUMBER);
		offset = lua_tonumber(L, 1);
		if(offset < 0)
			offset = 0;
	}

	P_WeaponFlash(player, offset);

	return 0;
}

static int LUA_genericPlaneFromSector(lua_State *L)
{
	generic_plane_t *gp;
	generic_info_t gen;
	int top = lua_gettop(L);
	boolean floor;

	gen.sector = lua_touserdata(L, lua_upvalueindex(1));
	floor = lua_toboolean(L, lua_upvalueindex(2));

	// stopz; required
	luaL_checktype(L, 1, LUA_TNUMBER);
	gen.stopz = (fixed_t)(lua_tonumber(L, 1) * (lua_Number)FRACUNIT);

	// speed; required
	luaL_checktype(L, 2, LUA_TNUMBER);
	gen.speed = (fixed_t)(lua_tonumber(L, 2) * (lua_Number)FRACUNIT);

	// crush speed; optional
	if(top > 2 && lua_type(L, 3) != LUA_TNIL)
	{
		luaL_checktype(L, 3, LUA_TNUMBER);
		gen.crushspeed = (fixed_t)(lua_tonumber(L, 3) * (lua_Number)FRACUNIT);
		if(gen.crushspeed < 0)
			gen.crushspeed = -gen.crushspeed;
	}

	// sounds; optional
	if(top > 3 && lua_type(L, 4) != LUA_TNIL)
	{
		luaL_checktype(L, 4, LUA_TSTRING);
		gen.startsound = W_GetNumForNameLua(lua_tostring(L, 4), true);
	} else
		gen.startsound = 0;

	if(top > 4 && lua_type(L, 5) != LUA_TNIL)
	{
		luaL_checktype(L, 5, LUA_TSTRING);
		gen.stopsound = W_GetNumForNameLua(lua_tostring(L, 5), true);
	} else
		gen.stopsound = 0;

	if(top > 5 && lua_type(L, 6) != LUA_TNIL)
	{
		luaL_checktype(L, 6, LUA_TSTRING);
		gen.movesound = W_GetNumForNameLua(lua_tostring(L, 6), true);
	} else
		gen.movesound = 0;

	// texture; optional
	if(top > 6 && lua_type(L, 7) != LUA_TNIL)
	{
		luaL_checktype(L, 7, LUA_TSTRING);
		char temp[8];
		strncpy(temp, lua_tostring(L, 7), sizeof(temp));
		gen.stoppic = R_FlatNumForName(temp);
	} else
	if(floor)
		gen.stoppic = gen.sector->floorpic;
	else
		gen.stoppic = gen.sector->ceilingpic;

	if(!gen.speed)
		gen.speed = gen.startz - gen.stopz;
	if(gen.speed < 0)
		gen.speed = -gen.speed;

	if(floor)
	{
		gen.startpic = gen.sector->floorpic;
		gen.startz = gen.sector->floorheight;
		gp = P_GenericSectorFloor(&gen);
	} else
	{
		gen.startpic = gen.sector->ceilingpic;
		gen.startz = gen.sector->ceilingheight;
		gp = P_GenericSectorCeiling(&gen);
	}

	// return generic plane
	if(gp)
		lua_pushlightuserdata(L, gp);
	else
		lua_pushnil(L);

	return 1;
}

static int LUA_genericCallFromSector(lua_State *L)
{
	generic_call_t gen;
	int act, arg, dest;
	generic_plane_t *gp;

	gen.sector = lua_touserdata(L, lua_upvalueindex(1));
	dest = lua_tointeger(L, lua_upvalueindex(2));

	// ticrate; required
	luaL_checktype(L, 1, LUA_TNUMBER);
	gen.ticrate = lua_tointeger(L, 1);

	// function; required
	luaL_checktype(L, 2, LUA_TFUNCTION);

	// create Generic caller
	gp = P_GenericSectorCaller(&gen, dest);

	if(!gp)
	{
		lua_pushnil(L);
		return 1;
	}

	// argument; optional
	if(lua_gettop(L) > 2)
	{
		gp->lua_arg = luaL_ref(L, LUA_REGISTRYINDEX);
		LUA_DebugRef(gp->lua_arg, false);
	}

	// set function
	gp->lua_action = luaL_ref(L, LUA_REGISTRYINDEX);
	LUA_DebugRef(gp->lua_action, false);

	// return generic caller
	lua_pushlightuserdata(L, gp);

	return 1;
}

static int LUA_sectorLineIterator(lua_State *L)
{
	int i, ret;
	sector_t *sec;
	int lua_func;
	int lua_arg = LUA_REFNIL;

	// function; required
	luaL_checktype(L, 1, LUA_TFUNCTION);

	// argument; optional
	if(lua_gettop(L) > 1)
	{
		lua_arg = luaL_ref(L, LUA_REGISTRYINDEX);
		LUA_DebugRef(lua_arg, false);
	}

	// set function
	lua_func = luaL_ref(L, LUA_REGISTRYINDEX);
	LUA_DebugRef(lua_func, false);

	sec = lua_touserdata(L, lua_upvalueindex(1));

	// reset stack
	lua_settop(L, 0);

	for(i = 0; i < sec->linecount; i++)
	{
		// function to call
		lua_rawgeti(L, LUA_REGISTRYINDEX, lua_func);
		// line to pass
		lua_pushlightuserdata(L, sec->lines[i]);
		// parameter to pass
		lua_rawgeti(L, LUA_REGISTRYINDEX, lua_arg);
		// do the call
		if(lua_pcall(L, 2, LUA_MULTRET, 0))
			// script error
			return luaL_error(L, "LineIterator: %s", lua_tostring(luaS_game, -1));

		if(lua_gettop(L) == 0)
			// no return means continue
			continue;

		// first return has to be continue flag
		luaL_checktype(L, 1, LUA_TBOOLEAN);
		ret = lua_toboolean(L, 1);

		if(ret)
			// iteration will continue, discard all returns
			lua_settop(L, 0);
		else
		{
			// remove only this flag, rest will get returned
			lua_remove(L, 1);
			break;
		}
	}

	LUA_DebugRef(lua_func, true);
	LUA_DebugRef(lua_arg, true);

	luaL_unref(L, LUA_REGISTRYINDEX, lua_func);
	luaL_unref(L, LUA_REGISTRYINDEX, lua_arg);

	return lua_gettop(L);
}

static int LUA_sectorSetDamage(lua_State *L)
{
	sector_t *sec;
	int top = lua_gettop(L);

	sec = lua_touserdata(L, lua_upvalueindex(1));

	if(top == 1)
	{
		// source sector; optional
		sector_t *src = LUA_GetSectorParam(L, 1, false);
		sec->damage = src->damage;
		sec->damagetick = src->damagetick;
		sec->damagetype = src->damagetype;
	} else
	{
		int damage;
		int ticks;
		int type;
		boolean in_air = false;

		// damage; required
		luaL_checktype(L, 1, LUA_TNUMBER);
		// damage type; required
		luaL_checktype(L, 2, LUA_TNUMBER);
		// ticrate; required
		luaL_checktype(L, 3, LUA_TNUMBER);
		// in air; optional
		if(top > 3)
		{
			luaL_checktype(L, 4, LUA_TBOOLEAN);
			in_air = lua_toboolean(L, 4);
		}

		damage = lua_tointeger(L, 1);
		type = lua_tointeger(L, 2) - 1;
		ticks = lua_tointeger(L, 3);

		if(type < 0)
			type = NUMDAMAGETYPES;

		if(in_air)
			ticks |= 0x8000;

		sec->damage = damage;
		sec->damagetick = ticks;
		sec->damagetype = type;
	}

	return 0;
}

static int LUA_sectorAdd3DFloor(lua_State *L)
{
	sector_t *dst;
	sector_t *src = LUA_GetSectorParam(L, 1, false); // source (sector model); required
	line_t *line = LUA_GetLineParam(L, 2, false); // source (line model); required
	int block = 0xFFFF;

	// blocking; optional
	if(lua_gettop(L) > 2)
	{
		luaL_checktype(L, 3, LUA_TNUMBER);
		block = lua_tointeger(L, 3);
	}

	dst = lua_touserdata(L, lua_upvalueindex(1));

	e3d_AddExtraFloor(dst, src, line);

	return 0;
}

static int LUA_sectorThingIterator(lua_State *L)
{
	mobj_t *mobj;
	sector_t *sec;
	int lua_func, ret;
	int lua_arg = LUA_REFNIL;
	int top = lua_gettop(L);
	boolean touching = false;

	// function; required
	luaL_checktype(L, 1, LUA_TFUNCTION);

	// argument; optional
	if(top > 1)
	{
		lua_arg = luaL_ref(L, LUA_REGISTRYINDEX);
		LUA_DebugRef(lua_arg, false);
	}

	// touching flag; optional
	if(top > 2)
	{
		luaL_checktype(L, 3, LUA_TBOOLEAN);
		touching = lua_toboolean(L, 3);
		lua_pop(L, 1);
	}	

	// set function
	lua_func = luaL_ref(L, LUA_REGISTRYINDEX);
	LUA_DebugRef(lua_func, false);

	// pop everything
	lua_settop(L, 0);

	sec = lua_touserdata(L, lua_upvalueindex(1));

	if(touching)
		return luaL_error(L, "ThingIterator: TODO touching");
	else
	{
		mobj = sec->thinglist;
		while(mobj)
		{
			// function to call
			lua_rawgeti(L, LUA_REGISTRYINDEX, lua_func);
			// mobj to pass
			lua_pushlightuserdata(L, mobj);
			// parameter to pass
			lua_rawgeti(L, LUA_REGISTRYINDEX, lua_arg);
			// do the call
			if(lua_pcall(L, 2, LUA_MULTRET, 0))
				// script error
				return luaL_error(L, "ThingIterator: %s", lua_tostring(luaS_game, -1));

			if(lua_gettop(L) == 0)
			{
				// no return means continue
				mobj = mobj->snext;
				continue;
			}

			// first return has to be continue flag
			luaL_checktype(L, 1, LUA_TBOOLEAN);
			ret = lua_toboolean(L, 1);

			if(ret)
				// iteration will continue, discard all returns
				lua_settop(L, 0);
			else
			{
				// remove only this flag, rest will get returned
				lua_remove(L, 1);
				break;
			}
			mobj = mobj->snext;
		}
	}

	LUA_DebugRef(lua_func, true);
	LUA_DebugRef(lua_arg, true);

	luaL_unref(L, LUA_REGISTRYINDEX, lua_func);
	luaL_unref(L, LUA_REGISTRYINDEX, lua_arg);

	return lua_gettop(L);
}

static int LUA_stopFromGeneric(lua_State *L)
{
	generic_plane_t *gp;
	sector_t *sec;

	gp = lua_touserdata(L, lua_upvalueindex(1));
	sec = gp->info.sector;

	// remove action
	L_FinishGeneric(*gp->gp, true);
	*gp->gp = NULL;
	P_RemoveThinker(&gp->thinker);

	return 0;
}

static int LUA_sleepFromGeneric(lua_State *L)
{
	generic_plane_t *gp;
	boolean sleep;

	luaL_checktype(L, 1, LUA_TBOOLEAN);
	sleep = lua_toboolean(L, 1);

	gp = lua_touserdata(L, lua_upvalueindex(1));

	if(sleep)
		gp->speed = 0;
	else
	{
		if(gp->thinker.lua_type == TT_SECCALL)
			gp->speed = gp->call.ticrate;
		else
			gp->speed = gp->info.speed;
	}

	return 0;
}

static int LUA_reverseFromGeneric(lua_State *L)
{
	generic_plane_t *gp;
	fixed_t tmp;
	int temp;

	gp = lua_touserdata(L, lua_upvalueindex(1));

	// reverse motion
	tmp = gp->info.startz;
	gp->info.startz = gp->info.stopz;
	gp->info.stopz = tmp;

	// reverse textures
	temp = gp->info.stoppic;
	gp->info.stoppic = gp->info.startpic;
	gp->info.startpic = temp;

	return 0;
}

static int LUA_scrollFromLine(lua_State *L)
{
	generic_line_t *ga;
	fixed_t x, y;
	line_t *line;
	int side = linedef_side;

	// x; required
	luaL_checktype(L, 1, LUA_TNUMBER);
	// y; required
	luaL_checktype(L, 2, LUA_TNUMBER);

	x = (fixed_t)(lua_tonumber(L, 1) * (lua_Number)FRACUNIT);
	y = (fixed_t)(lua_tonumber(L, 2) * (lua_Number)FRACUNIT);

	// side; optional
	if(lua_gettop(L) > 2)
	{
		luaL_checktype(L, 3, LUA_TBOOLEAN);
		side = !lua_toboolean(L, 3);
	}

	line = lua_touserdata(L, lua_upvalueindex(1));

	ga = P_TextureScroller(line, x, y, side);

	if(ga)
		lua_pushlightuserdata(L, ga);
	else
		lua_pushnil(L);

	return 1;
}

static int LUA_stopFromLine(lua_State *L)
{
	generic_line_t *ga;

	ga = lua_touserdata(L, lua_upvalueindex(1));

	ga->ga = NULL;

	P_RemoveThinker(&ga->thinker);

	return 0;
}

static int LUA_doSwitchTextureLine(lua_State *L)
{
	line_t *line;
	int sound0 = 0;
	int sound1 = 0;
	int btntime = BUTTONTIME;
	int top = lua_gettop(L);

	// optional press sound
	if(top > 0)
	{
		const char *tex;

		luaL_checktype(L, 1, LUA_TSTRING);
		tex = lua_tostring(L, 1);

		if(tex[0] == '-' && tex[1] == 0)
		{
			// support for 'no sound'
		} else
		{
			char temp[8];
			strncpy(temp, tex, sizeof(temp));
			sound0 = W_GetNumForName(temp);
		}
	}

	// optional release sound
	if(top > 1)
	{
		const char *tex;

		luaL_checktype(L, 2, LUA_TSTRING);
		tex = lua_tostring(L, 2);

		if(tex[0] == '-' && tex[1] == 0)
		{
			// support for 'no sound'
		} else
		{
			char temp[8];
			strncpy(temp, tex, sizeof(temp));
			sound1 = W_GetNumForName(temp);
		}
	}

	// optional timer
	if(top > 2)
	{
		luaL_checktype(L, 3, LUA_TNUMBER);
		btntime = lua_tonumber(L, 3);
		if(btntime <= 0)
			btntime = BUTTONTIME;
	}

	line = lua_touserdata(L, lua_upvalueindex(1));
	P_ChangeSwitchTexture(line, sound0, sound1, btntime);

	return 0;
}

static int LUA_togSwitchTextureLine(lua_State *L)
{
	line_t *line;
	int sound = 0;
	int top = lua_gettop(L);

	// optional press sound
	if(top > 0)
	{
		const char *tex;

		luaL_checktype(L, 1, LUA_TSTRING);
		tex = lua_tostring(L, 1);

		if(tex[0] == '-' && tex[1] == 0)
		{
			// support for 'no sound'
		} else
		{
			char temp[8];
			strncpy(temp, tex, sizeof(temp));
			sound = W_GetNumForName(temp);
		}
	}

	line = lua_touserdata(L, lua_upvalueindex(1));
	P_ChangeSwitchTexture(line, sound, 0, -1);

	return 0;
}

static int LUA_heightFromSector(lua_State *L)
{
	fixed_t (*func)(sector_t*);
	sector_t *sec;
	fixed_t height;

	sec = lua_touserdata(L, lua_upvalueindex(1));
	func = lua_touserdata(L, lua_upvalueindex(2));

	height = func(sec);

	lua_pushnumber(L, height / (lua_Number)FRACUNIT);

	return 1;
}

static int LUA_lightFromSector(lua_State *L)
{
	int (*func)(sector_t*);
	sector_t *sec;
	int level;

	sec = lua_touserdata(L, lua_upvalueindex(1));
	func = lua_touserdata(L, lua_upvalueindex(2));

	level = func(sec);

	lua_pushnumber(L, level);

	return 1;
}

static int LUA_shortTexFromSector(lua_State *L)
{
	int i;
	sector_t *sec, *next;
	side_t *side;
	fixed_t height = ONCEILINGZ;

	luaL_checktype(L, 1, LUA_TBOOLEAN);

	sec = lua_touserdata(L, lua_upvalueindex(1));

	if(!sec->linecount)
	{
		lua_pushnumber(L, 0);
		return 1;
	}

	if(lua_toboolean(L, 1))
	{
		for(i = 0; i < sec->linecount; i++)
		{
			if(sec->lines[i]->flags & LF_TWOSIDED)
			{
				side = &sides[sec->lines[i]->sidenum[0]];
				if(side->toptexture >= 0)
					if(textures[side->toptexture].height < height)
						height = textures[side->toptexture].height;
				side = &sides[sec->lines[i]->sidenum[1]];
				if(side->toptexture >= 0)
					if(textures[side->toptexture].height < height)
						height = textures[side->toptexture].height;
			}
		}
	} else
	{
		for(i = 0; i < sec->linecount; i++)
		{
			if(sec->lines[i]->flags & LF_TWOSIDED)
			{
				side = &sides[sec->lines[i]->sidenum[0]];
				if(side->bottomtexture >= 0)
					if(textures[side->bottomtexture].height < height)
						height = textures[side->bottomtexture].height;
				side = &sides[sec->lines[i]->sidenum[1]];
				if(side->bottomtexture >= 0)
					if(textures[side->bottomtexture].height < height)
						height = textures[side->bottomtexture].height;
			}
		}
	}

	if(height == ONCEILINGZ)
		height = 0;

	lua_pushnumber(L, height);

	return 1;
}

static int LUA_animationFromMobj(lua_State *L)
{
	mobj_t *mo;
	int *st;
	int an;
	int skip = 0;

	mo = lua_touserdata(L, lua_upvalueindex(1));
	an = lua_tointeger(L, lua_upvalueindex(2));

	st = &mo->info->spawnstate;

	if(lua_gettop(L) > 0)
	{
		luaL_checktype(L, 1, LUA_TNUMBER);
		skip = lua_tointeger(L, 1);
	}

	if(st[an])
	{
		P_SetMobjAnimation(mo, an, skip);
		lua_pushboolean(L, true);
	} else
		lua_pushboolean(L, false);

	return 1;
}

static int LUA_doMobjAction(lua_State *L)
{
	void (*func)(mobj_t*);
	mobj_t *mo;

	luaL_checktype(L, -1, LUA_TLIGHTUSERDATA);

	if(!lua_getmetatable(L, -1))
		return luaL_error(L, "doMobjAction: mobj expected");

	lua_pushstring(L, "__index");
	lua_rawget(L, -2);

	if(lua_tocfunction(L, -1) != LUA_ThinkerIndex)
		return luaL_error(L, "doMobjAction: mobj expected");

	mo = lua_touserdata(L, -3);

	if(mo->thinker.lua_type != TT_MOBJ)
		return luaL_error(L, "doMobjAction: mobj expected");

	func = lua_touserdata(L, lua_upvalueindex(1));
	func(mo);

	return 0;
}

static int LUA_SinIndex(lua_State *L)
{
	angle_t ang;

	luaL_checktype(L, 2, LUA_TNUMBER);
	ang = (int)lua_tonumber(L, 2) & FINEMASK;
	lua_pushnumber(L, finesine[ang] / (lua_Number)FRACUNIT);
	return 1;
}

static int LUA_CosIndex(lua_State *L)
{
	angle_t ang;

	luaL_checktype(L, 2, LUA_TNUMBER);
	ang = (int)lua_tonumber(L, 2) & FINEMASK;
	lua_pushnumber(L, finecosine[ang] / (lua_Number)FRACUNIT);
	return 1;
}

static int LUA_LineSpecIndex(lua_State *L)
{
	int spec;

	luaL_checktype(L, 2, LUA_TNUMBER);
	spec = lua_tointeger(L, 2);

	if(spec < 0 || spec > 255)
		return luaL_error(L, "invalid index");

	if(lua_gettop(L) > 2)
	{
		// set func
		luaL_checktype(L, 3, LUA_TFUNCTION);
		LUA_DebugRef(linespec_table[spec], true);
		luaL_unref(L, LUA_REGISTRYINDEX, linespec_table[spec]);
		linespec_table[spec] = luaL_ref(L, LUA_REGISTRYINDEX);
		LUA_DebugRef(linespec_table[spec], false);
		return 0;
	}

	// get func
	lua_rawgeti(L, LUA_REGISTRYINDEX, linespec_table[spec]);

	return 1;
}

static int LUA_GameInfoIndex(lua_State *L)
{
	const char *par;

	if(lua_gettop(L) > 2)
		return luaL_error(L, "you can't modify this");

	luaL_checktype(L, 2, LUA_TSTRING);
	par = lua_tostring(L, 2);

	if(!strcmp(par, "map"))
	{
		if(level_name[0])
			lua_pushstring(L, level_name);
		else
			lua_pushnil(L);
	} else
	if(!strcmp(par, "time"))
	{
		lua_pushinteger(L, leveltime);
	} else
	if(!strcmp(par, "DoomExit"))
	{
		lua_pushcfunction(L, LUA_doomExit);
	} else
	if(!strcmp(par, "Exit"))
	{
		lua_pushcfunction(L, LUA_luaExit);
	} else
	if(!strcmp(par, "deathMatch"))
	{
		lua_pushboolean(L, !!sv_deathmatch);
	} else
	if(!strcmp(par, "doomEpisode"))
	{
		lua_pushinteger(L, gameepisode);
	} else
		lua_pushnil(L);

	return 1;
}

//
// LUA state

void L_ExportFlags(lua_State *L)
{
	int i;

	lua_createtable(L, 0, sizeof(lua_mobjflags) / sizeof(lua_mobjflag_t)); // 'mf' table
	for(i = 0; i < sizeof(lua_mobjflags) / sizeof(lua_mobjflag_t); i++)
	{
		lua_pushinteger(L, lua_mobjflags[i].value);
		lua_setfield(L, -2, lua_mobjflags[i].name);
	}
	lua_setglobal(L, "mf");
}

void L_ExportMobjActions(lua_State *L)
{
	int i;

	lua_createtable(L, 0, sizeof(lua_mobjactions) / sizeof(lua_mobjaction_t)); // 'a' table
	for(i = 0; i < sizeof(lua_mobjactions) / sizeof(lua_mobjaction_t); i++)
	{
		lua_pushlightuserdata(L, lua_mobjactions[i].func);
		lua_pushcclosure(L, LUA_doMobjAction, 1);
		lua_setfield(L, -2, lua_mobjactions[i].name);
	}
	lua_setglobal(L, "a");
}

void L_ExportThinkerMetatable(lua_State *L)
{
	// userdata
	lua_pushlightuserdata(L, NULL);
	// metatable; all userdata share single metatable
	lua_createtable(L, 0, 2);
	// newindex
	lua_pushcfunction(L, LUA_ThinkerIndex);
	lua_setfield(L, -2, "__newindex");
	// index
	lua_pushcfunction(L, LUA_ThinkerIndex);
	lua_setfield(L, -2, "__index");
	// set metatable
	lua_setmetatable(L, -2);
	// pop userdata
	lua_pop(L, 1);
}

void L_ExportSinTable(lua_State *L)
{
	// variable 'finesine'
	lua_createtable(L, 0, 0);
	// metatable
	lua_createtable(L, 0, 2);
	// newindex
	lua_pushcfunction(L, LUA_TableReadOnly);
	lua_setfield(L, -2, "__newindex");
	// index
	lua_pushcfunction(L, LUA_SinIndex);
	lua_setfield(L, -2, "__index");
	// add metatable
	lua_setmetatable(L, -2);
	// finish
	lua_setglobal(L, "finesine");
}

void L_ExportCosTable(lua_State *L)
{
	// variable 'finesine'
	lua_createtable(L, 0, 0);
	// metatable
	lua_createtable(L, 0, 2);
	// newindex
	lua_pushcfunction(L, LUA_TableReadOnly);
	lua_setfield(L, -2, "__newindex");
	// index
	lua_pushcfunction(L, LUA_CosIndex);
	lua_setfield(L, -2, "__index");
	// add metatable
	lua_setmetatable(L, -2);
	// finish
	lua_setglobal(L, "finecosine");
}

void L_ExportIntegers(lua_State *L, const char *tablename, lua_intvalue_t *table, int count)
{
	int i;

	lua_createtable(L, 0, count); // table
	for(i = 0; i < count; i++)
	{
		lua_pushinteger(L, table[i].value);
		lua_setfield(L, -2, table[i].name);
	}
	lua_setglobal(L, tablename);
}

void L_ExportLineSpecTable(lua_State *L)
{
	// variable 'linefunc'
	lua_createtable(L, 0, 0);
	// metatable
	lua_createtable(L, 0, 2);
	// newindex
	lua_pushcfunction(L, LUA_LineSpecIndex);
	lua_setfield(L, -2, "__newindex");
	// index
	lua_pushcfunction(L, LUA_LineSpecIndex);
	lua_setfield(L, -2, "__index");
	// add metatable
	lua_setmetatable(L, -2);
	// finish
	lua_setglobal(L, "linefunc");
}

void L_ExportGameInfoTable(lua_State *L)
{
	// variable 'linefunc'
	lua_createtable(L, 0, 0);
	// metatable
	lua_createtable(L, 0, 2);
	// newindex
	lua_pushcfunction(L, LUA_GameInfoIndex);
	lua_setfield(L, -2, "__newindex");
	// index
	lua_pushcfunction(L, LUA_GameInfoIndex);
	lua_setfield(L, -2, "__index");
	// add metatable
	lua_setmetatable(L, -2);
	// finish
	lua_setglobal(L, "game");
}

void L_ExportFunctions(lua_State *L, int mask)
{
	int i;
	for(i = 0; i < sizeof(lua_functions) / sizeof(luafunc_t); i++)
	{
		if(lua_functions[i].mask & mask)
			lua_register(L, lua_functions[i].name, lua_functions[i].func);
		else
		{
			lua_pushnil(L);
			lua_setglobal(L, lua_functions[i].name);
		}
	}
}

void L_InitState()
{
	// new lua state
	if(!luaS_game)
	{
		luaS_game = luaL_newstate();
		// export some Lua libraries
		luaL_requiref(luaS_game, "table", luaopen_table, true);
		luaL_requiref(luaS_game, "math", luaopen_math, true);
		luaL_requiref(luaS_game, "string", luaopen_string, true);
		// export all functions
		L_ExportFunctions(luaS_game, LUA_EXPORT_SETUP);
		// export all mobj flags
		L_ExportFlags(luaS_game);
		// export all state functions
		L_ExportMobjActions(luaS_game);
		// export thinker table (for any lightuserdata)
		L_ExportThinkerMetatable(luaS_game);
		// export Doom sin / cos tables
		L_ExportSinTable(luaS_game);
		L_ExportCosTable(luaS_game);
		// export game info table
		L_ExportGameInfoTable(luaS_game);
		// export line special table
		L_ExportLineSpecTable(luaS_game);
		// export pickup returns
		L_ExportIntegers(luaS_game, "pickup", lua_pickups, sizeof(lua_pickups) / sizeof(lua_intvalue_t));
		// export line special activation type
		L_ExportIntegers(luaS_game, "lnspec", lua_linespec, sizeof(lua_linespec) / sizeof(lua_intvalue_t));
		// done
		lua_settop(luaS_game, 0);
	}
}

void L_LoadScript(int lump)
{
	char scriptname[32];
	char *scriptdata;

	// setup script name
	scriptdata = W_CacheLumpNum(lump);
	if(scriptdata[0] == '-' && scriptdata[1] == '-' && scriptdata[2] == '-')
	{
		// get name from first comment line
		char *dst = scriptname;
		char *src = scriptdata + 3;
		int i;

		for(i = 0; i < sizeof(scriptname) - 1; i++, dst++, src++)
		{
			if(*src == '\n' || *src == '\r')
				break;
			*dst = *src;
		}
		*dst = 0;
	} else
		// generate name; GAMELUA:wadnum:lumpnum
		sprintf(scriptname, "%.8s:%i:%i", W_LumpNumName(lump), lump >> 24, lump & 0xFFFFFF);
	// load lua script
	luaL_loadbuffer(luaS_game, scriptdata, W_LumpLength(lump), scriptname);
	// call (initialize) script
	if(lua_pcall(luaS_game, 0, 0, 0))
	{
		I_Error("L_LoadScript: [%s] %s", scriptname, lua_tostring(luaS_game, -1));
		lua_close(luaS_game);
		return;
	}
}

static boolean cb_LuaLoad(int lump)
{
	if(W_LumpLength(lump) > 3)
		L_LoadScript(lump);
	return false;
}

int L_NoRef()
{
	return LUA_REFNIL;
}

void L_Unref(int *ref)
{
	LUA_DebugRef(*ref, true);
	luaL_unref(luaS_game, LUA_REGISTRYINDEX, *ref);
	*ref = LUA_REFNIL;
}

void L_Init()
{
	int i;

	lua_setup = 1;

	// init Lua state
	L_InitState();

	// clear line functions
	for(i = 0; i < 256; i++)
		linespec_table[i] = LUA_REFNIL;

	// create sprite names
	sprnames = malloc(INFO_SPRITE_ALLOC * sizeof(sprname_t));
	if(!sprnames)
		I_Error("out of memory\n");
	// add default names
	memcpy(sprnames, info_def_sprnames, sizeof(info_def_sprnames));
	numsnames = NUM_DEF_SPRITES;

	// create mobj states
	states = malloc(INFO_STATE_ALLOC * sizeof(state_t));
	if(!states)
		I_Error("out of memory\n");
	// add default states
	memcpy(states, info_def_states, sizeof(info_def_states));
	numstates = NUM_DEF_STATES;
	// also clear functions of these states
	for(i = 0; i < NUM_DEF_STATES; i++)
		states[i].func = LUA_REFNIL;

	// create mobj types; use zone memory - for enlarge
	mobjinfo = Z_Malloc(NUM_DEF_MOBJTYPES * sizeof(mobjinfo_t), PU_STATIC, NULL);
	// add default types
	memcpy(mobjinfo, info_def_mobjinfo, sizeof(info_def_mobjinfo));
	numobjtypes = NUM_DEF_MOBJTYPES;

	// load script from all WADs
	W_ForEachName("GAMELUA", cb_LuaLoad);

	lua_setup = 0;

	// export level functions
	L_ExportFunctions(luaS_game, LUA_EXPORT_LEVEL);

	// just to be safe
	Z_CheckHeap();
}

//
// game callbacks

void L_SetupMap()
{
	// some resets
	game_backpatch = NULL;
	// start map load scripts
	lua_getglobal(luaS_game, "mapLoaded");
	if(lua_type(luaS_game, -1) == LUA_TFUNCTION)
	{
		if(lua_pcall(luaS_game, 0, 0, 0))
			// script error
			I_Error("L_SetupMap: %s", lua_tostring(luaS_game, -1));
	}
	lua_settop(luaS_game, 0);
}

void L_SpawnPlayer(player_t *pl)
{
	pl->psprites[ps_flash].state = NULL;
	pl->psprites[ps_weapon].state = NULL;
	pl->readyweapon = wp_nochange;
	lua_getglobal(luaS_game, "playerSpawn");
	if(lua_type(luaS_game, -1) == LUA_TFUNCTION)
	{
		lua_pushlightuserdata(luaS_game, pl);
		if(lua_pcall(luaS_game, 1, 0, 0))
			// script error
			I_Error("L_SpawnPlayer: %s", lua_tostring(luaS_game, -1));
	}
	lua_settop(luaS_game, 0);
//	P_DumpInventory(pl->mo);
}

int L_TouchSpecial(mobj_t *special, mobj_t *toucher)
{
	int top;
	int ret = SPECIAL_REMOVE;

	if(special->info->lua_action == LUA_REFNIL)
		return SPECIAL_REMOVE | SPECIAL_NOFLASH_FLAG;

	// function to call
	lua_rawgeti(luaS_game, LUA_REGISTRYINDEX, special->info->lua_action);
	// toucher mobj to pass
	lua_pushlightuserdata(luaS_game, toucher);
	// special mobj to pass
	lua_pushlightuserdata(luaS_game, special);
	// parameter to pass
	lua_rawgeti(luaS_game, LUA_REGISTRYINDEX, special->info->lua_arg);
	// do the call
	if(lua_pcall(luaS_game, 3, 2, 0))
		// script error
		I_Error("L_TouchSpecial: %s", lua_tostring(luaS_game, -1));

	top = lua_gettop(luaS_game);

	if(top > 0)
	{
		if(lua_type(luaS_game, 1) == LUA_TNUMBER)
			ret = lua_tointeger(luaS_game, 1);
		if(top > 1)
		{
			if(lua_type(luaS_game, 2) == LUA_TBOOLEAN && lua_toboolean(luaS_game, 2))
				ret |= SPECIAL_NOFLASH_FLAG;
		}
	}

	lua_settop(luaS_game, 0);

	return ret;
}

void L_FinishGeneric(generic_plane_t *gp, boolean forced)
{
	if(crush_gp == gp)
		crush_gp = NULL;
	if(!forced && gp->lua_action != LUA_REFNIL)
	{
		generic_sector_removed = false;
		// function to call
		lua_rawgeti(luaS_game, LUA_REGISTRYINDEX, gp->lua_action);
		// sector to pass
		lua_pushlightuserdata(luaS_game, gp->info.sector);
		// parameter to pass
		lua_rawgeti(luaS_game, LUA_REGISTRYINDEX, gp->lua_arg);
		// do the call
		if(lua_pcall(luaS_game, 2, 0, 0))
			// script error
			I_Error("L_FinishGeneric: %s", lua_tostring(luaS_game, -1));
		// check removal
		if(generic_sector_removed)
			// it was already removed and freed
			return;
	}
	// unref action and argument
	LUA_DebugRef(gp->lua_action, true);
	LUA_DebugRef(gp->lua_arg, true);
	LUA_DebugRef(gp->lua_crush, true);
	luaL_unref(luaS_game, LUA_REGISTRYINDEX, gp->lua_action);
	luaL_unref(luaS_game, LUA_REGISTRYINDEX, gp->lua_arg);
	luaL_unref(luaS_game, LUA_REGISTRYINDEX, gp->lua_crush);
	gp->lua_action = LUA_REFNIL;
	gp->lua_arg = LUA_REFNIL;
	gp->lua_crush = LUA_REFNIL;
	// mark as removed
	generic_sector_removed = true;
}

void T_GenericCaller(generic_plane_t *gp)
{
	if(gp->speed <= 0)
		// suspended
		return;
	gp->call.curtics--;
	if(!gp->call.curtics)
	{
		sector_t *sec = gp->info.sector;

		generic_sector_removed = false;
		// function to call
		lua_rawgeti(luaS_game, LUA_REGISTRYINDEX, gp->lua_action);
		// caller to pass
		lua_pushlightuserdata(luaS_game, gp);
		// parameter to pass
		lua_rawgeti(luaS_game, LUA_REGISTRYINDEX, gp->lua_arg);
		// do the call
		if(lua_pcall(luaS_game, 2, 1, 0))
			// script error
			I_Error("T_GenericCaller: %s", lua_tostring(luaS_game, -1));
		// check removal
		if(generic_sector_removed)
		{
			// it was already removed and freed
			lua_settop(luaS_game, 0);
			return;
		}
		// check continue
		if(lua_type(luaS_game, 1) != LUA_TBOOLEAN || !lua_toboolean(luaS_game, 1))
		{
			// remove caller
			L_FinishGeneric(*gp->gp, true);
			*gp->gp = NULL;
			P_RemoveThinker(&gp->thinker);
		} else
		{
			// reset timer, ticrate might be different
			gp->speed = gp->call.ticrate;
			gp->call.curtics = gp->speed;
		}
		lua_settop(luaS_game, 0);
	}
}

boolean L_CrushThing(mobj_t *th, sector_t *sec, int lfunc, int larg)
{
	int type;
	boolean ret = false;

	// function to call
	lua_rawgeti(luaS_game, LUA_REGISTRYINDEX, lfunc);
	// thing to pass
	lua_pushlightuserdata(luaS_game, th);
	// caller to pass
	if(crush_gp)
		lua_pushlightuserdata(luaS_game, crush_gp);
	else
		lua_pushnil(luaS_game);
	// parameter to pass
	lua_rawgeti(luaS_game, LUA_REGISTRYINDEX, larg);
	// do the call
	if(lua_pcall(luaS_game, 3, 1, 0))
		// script error
		I_Error("L_CrushThing: %s", lua_tostring(luaS_game, -1));

	type = lua_type(luaS_game, 1);

	if(type == LUA_TNIL)
		// no return means continue
		return true;

	// first return has to be continue flag
	if(type != LUA_TBOOLEAN)
		I_Error("L_CrushThing: boolean or no return expected");
	ret = lua_toboolean(luaS_game, 1);

	lua_settop(luaS_game, 0);

	return ret;
}

void L_CrashThing(mobj_t *th)
{
	if(th->info->crash_action == LUA_REFNIL)
		return;
	// function to call
	lua_rawgeti(luaS_game, LUA_REGISTRYINDEX, th->info->crash_action);
	// thing to pass
	lua_pushlightuserdata(luaS_game, th);
	// do the call
	if(lua_pcall(luaS_game, 1, 0, 0))
		// script error
		I_Error("L_CrashThing: %s", lua_tostring(luaS_game, -1));
}

void L_RunGenericTickers(mobj_t *mo)
{
	generic_ticker_t *gt = mo->generic_ticker;
	generic_ticker_t **last = &mo->generic_ticker;

	while(gt)
	{
		generic_ticker_t *cur = gt;
		gt = gt->next;

		// tick
		cur->curtics--;
		if(cur->curtics)
		{
			last = &cur->next;
			continue;
		}

		// function to call
		lua_rawgeti(luaS_game, LUA_REGISTRYINDEX, cur->lua_action);
		// thing to pass
		lua_pushlightuserdata(luaS_game, mo);
		// parameter to pass
		lua_rawgeti(luaS_game, LUA_REGISTRYINDEX, cur->lua_arg);
		// do the call
		if(lua_pcall(luaS_game, 2, 1, 0))
			// script error
			I_Error("L_RunGenericTickers: ID %i; %s", cur->id, lua_tostring(luaS_game, -1));

		// check continue
		if(lua_type(luaS_game, 1) != LUA_TBOOLEAN || !lua_toboolean(luaS_game, 1))
		{
			// remove this ticker
			*last = cur->next;
			last = &cur->next;
			// unref
			LUA_DebugRef(cur->lua_action, true);
			luaL_unref(luaS_game, LUA_REGISTRYINDEX, cur->lua_action);
			LUA_DebugRef(cur->lua_arg, true);
			luaL_unref(luaS_game, LUA_REGISTRYINDEX, cur->lua_arg);
			// free
			Z_Free(cur);
		} else
		{
			// reset timer
			cur->curtics = cur->ticrate;
			last = &cur->next;
		}

		lua_settop(luaS_game, 0);
	}
}

//
// other functions

boolean P_SetMobjAnimation(mobj_t *mo, int anim, int skip)
{
	int state;
	int *st;

	if(anim < 0)
	{
		// only skip some states
		state = mo->state - states;
	} else
	{
		// change animation
		st = &mo->info->spawnstate;
		state = st[anim];
	}

	while(1)
	{
		if(state == S_NULL)
			// nope
			return true;

		if(!skip)
		{
			mo->animation = anim;
			return P_ForceMobjState(mo, state);
		}
		skip--;

		if(states[state].nextstate == STATE_NULL_NEXT)
			state++;
		else
			state = states[state].nextstate;
		if(state & STATE_ANIMATION)
			// nope
			return true;
	}
	mo->animation = anim;
	return P_ForceMobjState(mo, state);
}

boolean P_ExtraLineSpecial(mobj_t *mobj, line_t *line, int side, int act)
{
	int spec = line->special;
	boolean ret = false;
	boolean reuse = false;

	// NULL check
	if(spec > 255 || linespec_table[spec] == LUA_REFNIL)
		spec = 0;

	// again
	if(linespec_table[spec] == LUA_REFNIL)
		return false;

	// HEXEN mode activation check
	if(isHexen && (line->flags & ELF_ACT_TYPE_MASK) != ELF_ACT_PASSLUA)
	{
		switch(act)
		{
			case EXTRA_USE:
				switch(line->flags & ELF_ACT_TYPE_MASK)
				{
					case ELF_ACT_PLAYER:
						if(!mobj->player && !(line->flags & ELF_ANY_ACT))
							return false;
					break;
					default:
					return false;
				}
			break;
			case EXTRA_CROSS:
				switch(line->flags & ELF_ACT_TYPE_MASK)
				{
					case ELF_CROSS_PLAYER:
						if(mobj->flags & MF_MISSILE)
							return false;
						if(!mobj->player && !(line->flags & ELF_ANY_ACT))
							return false;
					break;
					case ELF_CROSS_MONSTER:
						if(mobj->flags & MF_MISSILE)
							return false;
						if(mobj->player && !(line->flags & ELF_ANY_ACT))
							return false;
					break;
					case ELF_CROSS_PROJECTILE:
						if(!(mobj->flags & MF_MISSILE))
							return false;
					break;
					default:
					return false;
				}
			break;
			case EXTRA_HITSCAN:
				switch(line->flags & ELF_ACT_TYPE_MASK)
				{
					case ELF_HIT_PROJECTILE:
						if(!mobj->player && !(line->flags & ELF_ANY_ACT))
							return false;
					break;
					default:
					return false;
				}
			break;
			case EXTRA_BUMP:
				if((line->flags & ELF_ACT_TYPE_MASK) != ELF_HIT_PLAYER || (!mobj->player && !(line->flags & ELF_ANY_ACT)))
					return false;
			break;
		}
	}

	// preset side
	linedef_side = side;

	// function to call
	lua_rawgeti(luaS_game, LUA_REGISTRYINDEX, linespec_table[spec]);
	// mobj to pass
	lua_pushlightuserdata(luaS_game, mobj);
	// line to pass
	lua_pushlightuserdata(luaS_game, line);
	// side to pass
	lua_pushboolean(luaS_game, !side);
	// activation to pass
	lua_pushinteger(luaS_game, act);
	// do the call
	if(lua_pcall(luaS_game, 4, 1, 0))
		// script error
		I_Error("P_ExtraLineSpecial: %s", lua_tostring(luaS_game, -1));

	if(isHexen && !(line->flags & ELF_ACT_REPEAT))
		line->special = 0;

	if(lua_type(luaS_game, -1) == LUA_TBOOLEAN)
		ret = lua_toboolean(luaS_game, -1);

	lua_settop(luaS_game, 0);

	return ret;
}

boolean PIT_LuaCheckThing(mobj_t *thing)
{
	boolean ret;
	fixed_t tbx0 = thing->x - thing->radius;
	fixed_t tbx1 = thing->x + thing->radius;
	fixed_t tby0 = thing->y - thing->radius;
	fixed_t tby1 = thing->y + thing->radius;

	// range check
	if(tbx0 > block_lua_x1)
		return true;
	if(tbx1 < block_lua_x0)
		return true;
	if(tby0 > block_lua_y1)
		return true;
	if(tby1 < block_lua_y0)
		return true;

	// function to call
	lua_rawgeti(luaS_PIT, LUA_REGISTRYINDEX, block_lua_func);
	// mobj to pass
	lua_pushlightuserdata(luaS_PIT, thing);
	// parameter to pass
	lua_rawgeti(luaS_PIT, LUA_REGISTRYINDEX, block_lua_arg);
	// do the call
	if(lua_pcall(luaS_PIT, 2, LUA_MULTRET, 0))
		// script error
		return luaL_error(luaS_PIT, "ThingsIterator: %s", lua_tostring(luaS_PIT, -1));

	if(lua_gettop(luaS_PIT) == 0)
		// no return means continue
		return true;

	// first return has to be continue flag
	luaL_checktype(luaS_PIT, 1, LUA_TBOOLEAN);
	ret = lua_toboolean(luaS_PIT, 1);

	if(ret)
		// iteration will continue, discard all returns
		lua_settop(luaS_PIT, 0);
	else
		// remove only this flag, rest will get returned
		lua_remove(luaS_PIT, 1);

	return ret;
}

