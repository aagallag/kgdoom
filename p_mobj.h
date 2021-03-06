#ifndef __P_MOBJ__
#define __P_MOBJ__

// Basics.
#include "tables.h"
#include "m_fixed.h"

// We need the thinker_t stuff.
#include "d_think.h"

// We need the WAD data structure for Map things,
// from the THINGS lump.
#include "doomdata.h"

// States are tied to finite states are
//  tied to animation frames.
// Needs precompiled tables/data structures.
#include "info.h"

// [kg] new way to handle links
// based on zdoom
typedef struct blocklink_s
{
	struct blocklink_s **prev_block;
	struct blocklink_s *next_block;
	struct blocklink_s **prev_mobj;
	struct blocklink_s *next_mobj;
	struct mobj_s *mobj;
} blocklink_t;

#ifdef __GNUG__
#pragma interface
#endif



//
// NOTES: mobj_t
//
// mobj_ts are used to tell the refresh where to draw an image,
// tell the world simulation when objects are contacted,
// and tell the sound driver how to position a sound.
//
// The refresh uses the next and prev links to follow
// lists of things in sectors as they are being drawn.
// The sprite, frame, and angle elements determine which patch_t
// is used to draw the sprite if it is visible.
// The sprite and frame values are allmost allways set
// from state_t structures.
// The statescr.exe utility generates the states.h and states.c
// files that contain the sprite/frame numbers from the
// statescr.txt source file.
// The xyz origin point represents a point at the bottom middle
// of the sprite (between the feet of a biped).
// This is the default origin position for patch_ts grabbed
// with lumpy.exe.
// A walking creature will have its z equal to the floor
// it is standing on.
//
// The sound code uses the x,y, and subsector fields
// to do stereo positioning of any sound effited by the mobj_t.
//
// The play simulation uses the blocklinks, x,y,z, radius, height
// to determine when mobj_ts are touching each other,
// touching lines in the map, or hit by trace lines (gunshots,
// lines of sight, etc).
// The mobj_t->flags element has various bit flags
// used by the simulation.
//
// Every mobj_t is linked into a single sector
// based on its origin coordinates.
// The subsector_t is found with R_PointInSubsector(x,y),
// and the sector_t can be found with subsector->sector.
// The sector links are only used by the rendering code,
// the play simulation does not care about them at all.
//
// Any mobj_t that needs to be acted upon by something else
// in the play world (block movement, be shot, etc) will also
// need to be linked into the blockmap.
// If the thing has the MF_NOBLOCK flag set, it will not use
// the block links. It can still interact with other things,
// but only as the instigator (missiles will run into other
// things, but nothing can run into a missile).
// Each block in the grid is 128*128 units, and knows about
// every line_t that it contains a piece of, and every
// interactable mobj_t that has its origin contained.  
//
// A valid mobj_t is a mobj_t that has the proper subsector_t
// filled in for its xy coordinates and is linked into the
// sector from which the subsector was made, or has the
// MF_NOSECTOR flag set (the subsector_t needs to be valid
// even if MF_NOSECTOR is set), and is linked into a blockmap
// block or has the MF_NOBLOCKMAP flag set.
// Links should only be modified by the P_[Un]SetThingPosition()
// functions.
// Do not change the MF_NO? flags while a thing is valid.
//
// Any questions?
//

//
// Misc. mobj flags
//
typedef enum
{
    // Call P_SpecialThing when touched.
    MF_SPECIAL		= 1,
    // Blocks.
    MF_SOLID		= 2,
    // Can be hit.
    MF_SHOOTABLE	= 4,
    // Don't use the sector links (invisible but touchable).
    MF_NOSECTOR		= 8,
    // Don't use the blocklinks (inert but displayable)
    MF_NOBLOCKMAP	= 16,                    

    // Not to be activated by sound, deaf monster.
    MF_AMBUSH		= 32,
    // Will try to attack right back.
    MF_JUSTHIT		= 64,
    // Will take at least one step before attacking.
    MF_JUSTATTACKED	= 128,
    // On level spawning (initial position),
    //  hang from ceiling instead of stand on floor.
    MF_SPAWNCEILING	= 256,

    // [kg] can be pushed when bumped
    // transfer momentnum by mass
    MF_PUSHABLE		= 512,

    // Movement flags.
    // This allows jumps from high places.
    MF_DROPOFF		= 0x400,
    // For players, will pick up items.
    MF_PICKUP		= 0x800,
    // Player cheat. ???
    MF_NOCLIP		= 0x1000,
    // Player: keep info about sliding along walls.
    MF_SLIDE		= 0x2000,
    // Allow moves to any height, no gravity.
    // For active floaters, e.g. cacodemons, pain elementals.
    MF_FLOAT		= 0x4000,
    // Don't cross lines
    //   ??? or look at heights on teleport.
    MF_TELEPORT		= 0x8000,
    // Don't hit same species, explode on block.
    // Player missiles as well as fireballs of various kinds.
    MF_MISSILE		= 0x10000,	
    // Dropped by a demon, not level spawned.
    // E.g. ammo clips dropped by dying former humans.
    MF_DROPPED		= 0x20000,
    // [kg] this is a monster
    MF_ISMONSTER	= 0x40000,
    // Flag: don't bleed when shot (use puff),
    //  barrels and shootable furniture shall not bleed.
    MF_NOBLOOD		= 0x80000,
    // Don't stop moving halfway off a step,
    //  that is, have dead bodies slide down all the way.
    MF_CORPSE		= 0x100000,
    // Floating to a height for a move, ???
    //  don't auto float to target's height.
    MF_INFLOAT		= 0x200000,

    // On kill, count this enemy object
    //  towards intermission kill total.
    // Happy gathering.
    MF_COUNTKILL	= 0x400000,
    
    // On picking up, count this item object
    //  towards intermission item total.
    MF_COUNTITEM	= 0x800000,

    // Special handling: skull in flight.
    // Neither a cacodemon nor a missile.
    MF_SKULLFLY		= 0x1000000,

    // Don't spawn this object
    //  in death match mode (e.g. key cards).
    MF_NOTDMATCH    	= 0x2000000,

    // [kg] immune to explosions
    MF_NORADIUSDMG	= 0x4000000,

    // [kg] disable thing Z collision updates
    // used on dead projectiles
    MF_NOZCHANGE	= 0x8000000,

    // [kg] disable thing Z checking when spawning
    // used on puffs, puffs can spawn inside walls
    MF_NOZSPAWNCHECK	= 0x10000000,

    // [kg] ignore thing colisions
    MF_TROUGHMOBJ	= 0x20000000,

    // [kg] full volume body sounds
    MF_FULLVOLUME	= 0x40000000,

    // [kg] disable death 'pull'
    MF_NODEATHPULL = 0x80000000,

    // [kg] invulnerability
    MF_INVULNERABLE = 0x100000000,

    // [kg] no Z check for explosions
    MF_NORADIUSZ	= 0x200000000,

    // [kg] bounce
    MF_WALLBOUNCE	= 0x400000000,
    MF_MOBJBOUNCE	= 0x800000000,

    // [kg] can't be targetted by internal AI
    MF_NOTARGET		= 0x1000000000,

    // [kg] explode projectiles on sky
    MF_SKYEXPLODE	= 0x2000000000,

    // [kg] mobj can't activate 'use' lines
    MF_CANTUSE		= 0x4000000000,
    // [kg] mobj can't activate 'bump' lines
    MF_CANTBUMP		= 0x8000000000,

    // [kg] few custom flags for Lua
    MF_CUSTOM0		= 0x1000000000000000,
    MF_CUSTOM1		= 0x2000000000000000,
    MF_CUSTOM2		= 0x4000000000000000,
    MF_CUSTOM3		= 0x8000000000000000,

} mobjflag_t;


// Map Object definition.
typedef struct mobj_s
{
    // List: thinker links.
    thinker_t		thinker;

    // Info for drawing: position.
    fixed_t		x;
    fixed_t		y;
    fixed_t		z;

    // More list: links in sector (if needed)
    struct mobj_s*	snext;
    struct mobj_s*	sprev;

    //More drawing info: to determine current sprite.
    angle_t		angle;	// orientation
    fixed_t		pitch;	// [kg] vertical 'orientation'
    spritenum_t		sprite;	// used to find patch_t and flip value
    int			frame;	// might be ORed with FF_FULLBRIGHT

    // Interaction info, by BLOCKMAP.
    // Links in blocks (if needed).
    // [kg] new way to handle links
    blocklink_t		*blocklink;
    
    struct subsector_s*	subsector;

    // The closest interval over all contacted Sectors.
    fixed_t		floorz;
    fixed_t		ceilingz;

    // For movement checking.
    fixed_t		radius;
    fixed_t		height;	

    // Momentums, used to update position.
    fixed_t		momx;
    fixed_t		momy;
    fixed_t		momz;

    // If == validcount, already checked.
    int			validcount;

    mobjtype_t		type;
    mobjinfo_t*		info;	// &mobjinfo[mobj->type]
    
    int			tics;	// state tic counter
    state_t*		state;
    // [kg] current animation ID
    int			animation;

    // [kg] can be modified
    fixed_t		speed;
    int			mass;
    fixed_t		gravity;
    fixed_t		bounce;

    uint64_t		flags;
    int			health;
    int			damage;
    int			damagetype;

    // [kg] current liquid state
    fixed_t		liquid;
    fixed_t		liquidip;

    // [kg] moved here from player
    int			armorpoints;
    mobjinfo_t*		armortype;
    // [kg] damage resistance
    uint8_t		damagescale[NUMDAMAGETYPES];
    int			damagercv; // received damage type

    // Movement direction, movement generation (zig-zagging).
    int			movedir;	// 0-7
    int			movecount;	// when 0, select a new dir

    // [kg] pointer rewrite
    // Thing being chased/attacked (or NULL)
    struct mobj_s*	target;
    // originator for missiles
    struct mobj_s*	source;
    // [kg] attacker; set in DamageMobj
    struct mobj_s*	attacker;
    // [kg] gerenal purpose mobj
    struct mobj_s*	mobj;

    // Reaction time: if non 0, don't attack yet.
    // Used by player to freeze a bit after teleporting.
    int			reactiontime;   

    // If >0, the target will be chased
    // no matter what (even if shot)
    int			threshold;

    // Additional info record for player avatars only.
    // Only valid if type == MT_PLAYER
    struct player_s*	player;

    // Player number last looked for.
    int			lastlook;	

    // For nightmare respawn.
    mapthing_hexen_t		spawnpoint;

    // [kg] tag
    int tag;

    // [kg] material
    int material;

    // [kg] blocking
    int blocking;
    int canpass;

    // [kg] colors
    // translation is applied first, colormap second
    colormap_t	translation;
    colormap_t	colormap;

    // [kg] renderer
    render_t render;

    // [kg] inventory
    struct inventory_s *inventory;

    // [kg] Lua ticker
    struct generic_ticker_s *generic_ticker;

    // [kg] network ID
    int netid;

    // [kg] for Z movement
    boolean onground;

} mobj_t;



#endif

