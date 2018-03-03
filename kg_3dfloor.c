// 3D floors
// by kgsws
#include "z_zone.h"
#include "doomdef.h"
#include "p_local.h"
#include "doomstat.h"
#include "r_state.h"
#include "p_generic.h"

#include "kg_3dfloor.h"

#include "kg_lua.h"

typedef struct clip_s
{
	struct clip_s *next;
	short clip[];
} clip_t;

boolean fakeclip;
short *fakecliptop;
short *fakeclipbot;
extraplane_t *fakeplane;

clip_t *topclip;

height3d_t height3top = {.height = ONCEILINGZ};
height3d_t height3bot = {.height = ONFLOORZ};

extraplane_t *e3d_AddFloorPlane(extraplane_t **dest, sector_t *sec, line_t *line)
{
	extraplane_t *pl = *dest;
	extraplane_t *new;

	while(pl)
	{
		if(sec->ceilingheight < *pl->height)
			break;
		dest = &pl->next;
		pl = pl->next;
	}

	new = Z_Malloc(sizeof(extraplane_t), PU_LEVEL, NULL);
	*dest = new;
	new->next = pl;
	new->line = line;
	new->source = sec;
	new->height = &sec->ceilingheight;
	new->pic = &sec->ceilingpic;
	new->lightlevel = &sec->lightlevel;
	new->validcount = 0;

	return new;
}

extraplane_t *e3d_AddCeilingPlane(extraplane_t **dest, sector_t *sec, line_t *line)
{
	extraplane_t *pl = *dest;
	extraplane_t *new;

	while(pl)
	{
		if(sec->floorheight > *pl->height)
			break;
		dest = &pl->next;
		pl = pl->next;
	}

	new = Z_Malloc(sizeof(extraplane_t), PU_LEVEL, NULL);
	*dest = new;
	new->next = pl;
	new->line = line;
	new->source = sec;
	new->height = &sec->floorheight;
	new->pic = &sec->floorpic;
	new->lightlevel = &sec->lightlevel;
	new->validcount = 0;

	return new;
}

void e3d_AddExtraFloor(sector_t *dst, sector_t *src, line_t *line)
{
	// check
	extraplane_t *pl = dst->exfloor;
	while(pl)
	{
		if(pl->source == src)
			// already added
			return;
		pl = pl->next;
	}
	// add planes
	e3d_AddFloorPlane(&dst->exfloor, src, line);
	e3d_AddCeilingPlane(&dst->exceiling, src, line);
}

void e3d_CleanPlanes()
{
	int i;

	for(i = 0; i < numsectors; i++)
	{
		extraplane_t *pl;
		sector_t *sec = sectors + i;

		pl = sec->exfloor;
		while(pl)
		{
			extraplane_t *fr = pl;
			pl = pl->next;
			free(fr);
		}

		pl = sec->exceiling;
		while(pl)
		{
			extraplane_t *fr = pl;
			pl = pl->next;
			free(fr);
		}
	}
}

void e3d_Reset()
{
	clip_t *cl = topclip;

	while(cl)
	{
		clip_t *ff = cl;
		cl = cl->next;
		Z_Free(ff);
	}
	topclip = NULL;

	height3d_t *hh = height3bot.next;

	while(hh && hh != &height3top)
	{
		height3d_t *hf = hh;
		hh = hh->next;
		Z_Free(hf);
	}

	height3top.prev = &height3bot;
	height3bot.next = &height3top;
}

short *e3d_NewClip(short *source)
{
	clip_t *new;

	new = Z_Malloc(sizeof(clip_t) + SCREENWIDTH * sizeof(short), PU_STATIC, NULL);
	new->next = topclip;
	memcpy(new->clip, source, SCREENWIDTH * sizeof(short));

	topclip = new;

	return new->clip;
}

void e3d_NewHeight(fixed_t height)
{
	height3d_t *hh = &height3bot;
	height3d_t *new;

	while(hh)
	{
		if(hh->height == height)
			return;
		if(hh->height > height)
			break;
		hh = hh->next;
	}

	new = Z_Malloc(sizeof(height3d_t), PU_STATIC, NULL);
	new->next = hh;
	new->prev = hh->prev;
	hh->prev->next = new;
	hh->prev = new;
	new->height = height;
}

