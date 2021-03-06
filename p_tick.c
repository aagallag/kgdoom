#include "z_zone.h"
#include "p_local.h"

#include "doomstat.h"

#include "p_inventory.h"

#include "kg_record.h"

#ifdef SERVER
#include "sv_cmds.h"
#else
#include "cl_cmds.h"
#endif


int	leveltime;

//
// THINKERS
// All thinkers should be allocated by Z_Malloc
// so they can be operated on uniformly.
// The actual structures will vary in size,
// but the first element must be thinker_t.
//



// Both the head and tail of the thinker list.
thinker_t	thinkercap;


//
// P_InitThinkers
//
void P_InitThinkers (void)
{
    thinkercap.prev = thinkercap.next  = &thinkercap;
}




//
// P_AddThinker
// Adds a new thinker at the end of the list.
//
void P_AddThinker (thinker_t* thinker, luathinker_t type)
{
	thinker->lua_type = type;
	thinkercap.prev->next = thinker;
	thinker->next = &thinkercap;
	thinker->prev = thinkercap.prev;
	thinkercap.prev = thinker;
}



//
// P_RemoveThinker
// Deallocation is lazy -- it will not actually be freed
// until its thinking turn comes up.
//
void P_RemoveThinker (thinker_t* thinker)
{
  // FIXME: NOP.
  thinker->function.acv = (actionf_v)(-1);
}



//
// P_AllocateThinker
// Allocates memory and adds a new thinker at the end of the list.
//
void P_AllocateThinker (thinker_t*	thinker)
{
}



//
// P_RunThinkers
//
void P_RunThinkers (void)
{
    thinker_t*	currentthinker;

    currentthinker = thinkercap.next;
    while (currentthinker != &thinkercap)
    {
	if ( currentthinker->function.acv == (actionf_v)(-1) )
	{
	    // time to remove it
	    currentthinker->next->prev = currentthinker->prev;
	    currentthinker->prev->next = currentthinker->next;
	    Z_Free (currentthinker);
	}
	else
	{
	    if (currentthinker->function.acp1)
		currentthinker->function.acp1 (currentthinker);
	}
	currentthinker = currentthinker->next;
    }
}



//
// P_Ticker
//

void P_Ticker (void)
{
    int		i;
    
    // run the tic
    if (paused)
	return;
#ifndef SERVER
    // pause if in menu and at least one tic has been run
    if ( !netgame
	 && (menuactive && !rec_is_playback)
	 && players[consoleplayer].playerstate != PST_DEAD)
    {
	return;
    }
#endif

#ifndef SERVER
    // [kg] local player prediction
    if(netgame)
	CL_PredictPlayer();
#endif

#ifdef SERVER
    disable_player_think = 1;
#else
    for (i=0 ; i<=MAXPLAYERS ; i++)
	if (playeringame[i])
	    P_PlayerThink (&players[i]);
#endif

    P_RunThinkers ();
    P_UpdateSpecials ();

    // for par times
    leveltime++;	
}

