#include "doomdef.h"

#include "m_bbox.h"

#include "i_system.h"

#include "r_main.h"
#include "r_plane.h"
#include "r_things.h"

// State.
#include "doomstat.h"
#include "r_state.h"

#include "kg_3dfloor.h"

//#include "r_local.h"



seg_t*		curline;
side_t*		sidedef;
line_t*		linedef;
sector_t*	frontsector;
sector_t*	backsector;

drawseg_t	drawsegs[MAXDRAWSEGS];
drawseg_t*	ds_p;


void R_StoreWallRange(int start, int stop);
void R_StoreWallRangeFake(int start, int stop);

//
// R_ClearDrawSegs
//
void R_ClearDrawSegs (void)
{
    ds_p = drawsegs;
}



//
// ClipWallSegment
// Clips the given range of columns
// and includes it in the new clip list.
//
typedef	struct
{
    int	first;
    int last;
    
} cliprange_t;


#define MAXSEGS		32

// newend is one past the last valid seg
cliprange_t*	newend;
cliprange_t	solidsegs[MAXSEGS];




//
// R_ClipSolidWallSegment
// Does handle solid walls,
//  e.g. single sided LineDefs (middle texture)
//  that entirely block the view.
// 
void
R_ClipSolidWallSegment
( int			first,
  int			last )
{
    cliprange_t*	next;
    cliprange_t*	start;

    // Find the first range that touches the range
    //  (adjacent pixels are touching).
    start = solidsegs;
    while (start->last < first-1)
	start++;

    if (first < start->first)
    {
	if (last < start->first-1)
	{
	    // Post is entirely visible (above start),
	    //  so insert a new clippost.
	    R_StoreWallRange (first, last);
	    next = newend;
	    newend++;
	    
	    while (next != start)
	    {
		*next = *(next-1);
		next--;
	    }
	    next->first = first;
	    next->last = last;
	    return;
	}
		
	// There is a fragment above *start.
	R_StoreWallRange (first, start->first - 1);
	// Now adjust the clip size.
	start->first = first;	
    }

    // Bottom contained in start?
    if (last <= start->last)
	return;			
		
    next = start;
    while (last >= (next+1)->first-1)
    {
	// There is a fragment between two posts.
	R_StoreWallRange (next->last + 1, (next+1)->first - 1);
	next++;
	
	if (last <= next->last)
	{
	    // Bottom is contained in next.
	    // Adjust the clip size.
	    start->last = next->last;	
	    goto crunch;
	}
    }
	
    // There is a fragment after *next.
    R_StoreWallRange (next->last + 1, last);
    // Adjust the clip size.
    start->last = last;
	
    // Remove start+1 to next from the clip list,
    // because start now covers their area.
  crunch:
    if (next == start)
    {
	// Post just extended past the bottom of one post.
	return;
    }
    

    while (next++ != newend)
    {
	// Remove a post.
	*++start = *next;
    }

    newend = start+1;
}

// [kg] this one is for fake rendering
void
R_ClipSolidWallSegmentFake
( int			first,
  int			last )
{
    cliprange_t*	next;
    cliprange_t*	start;

    // Find the first range that touches the range
    //  (adjacent pixels are touching).
    start = solidsegs;
    while (start->last < first-1)
	start++;

    if (first < start->first)
    {
	if (last < start->first-1)
	{
	    // Post is entirely visible (above start),
	    //  so insert a new clippost.
	    R_StoreWallRangeFake (first, last);
	    return;
	}
	// There is a fragment above *start.
	R_StoreWallRangeFake (first, start->first - 1);
    }

    // Bottom contained in start?
    if (last <= start->last)
	return;			

    next = start;
    while (last >= (next+1)->first-1)
    {
	// There is a fragment between two posts.
	R_StoreWallRangeFake (next->last + 1, (next+1)->first - 1);
	next++;
	
	if (last <= next->last)
	{
	    // Bottom is contained in next.
	    return;
	}
    }

    // There is a fragment after *next.
    R_StoreWallRangeFake (next->last + 1, last);
}

//
// R_ClipPassWallSegment
// Clips the given range of columns,
//  but does not includes it in the clip list.
// Does handle windows,
//  e.g. LineDefs with upper and lower texture.
//
void
R_ClipPassWallSegment
( int	first,
  int	last )
{
    cliprange_t*	start;

    // Find the first range that touches the range
    //  (adjacent pixels are touching).
    start = solidsegs;
    while (start->last < first-1)
	start++;

    if (first < start->first)
    {
	if (last < start->first-1)
	{
	    // Post is entirely visible (above start).
	    R_StoreWallRange (first, last);
	    return;
	}
		
	// There is a fragment above *start.
	R_StoreWallRange (first, start->first - 1);
    }

    // Bottom contained in start?
    if (last <= start->last)
	return;			
		
    while (last >= (start+1)->first-1)
    {
	// There is a fragment between two posts.
	R_StoreWallRange (start->last + 1, (start+1)->first - 1);
	start++;
	
	if (last <= start->last)
	    return;
    }
	
    // There is a fragment after *next.
    R_StoreWallRange (start->last + 1, last);
}



//
// R_ClearClipSegs
//
void R_ClearClipSegs (void)
{
    solidsegs[0].first = -0x7fffffff;
    solidsegs[0].last = -1;
    solidsegs[1].first = viewwidth;
    solidsegs[1].last = 0x7fffffff;
    newend = solidsegs+2;
}

//
// R_AddLine
// Clips the given segment
// and adds any visible pieces to the line list.
//
void R_AddLine (seg_t*	line)
{
    int			x1;
    int			x2;
    angle_t		angle1;
    angle_t		angle2;
    angle_t		span;
    angle_t		tspan;
    
    curline = line;

    // OPTIMIZE: quickly reject orthogonal back sides.
    angle1 = R_PointToAngle (line->v1->x, line->v1->y);
    angle2 = R_PointToAngle (line->v2->x, line->v2->y);
    
    // Clip to view edges.
    // OPTIMIZE: make constant out of 2*clipangle (FIELDOFVIEW).
    span = angle1 - angle2;
    
    // Back side? I.e. backface culling?
    if (span >= ANG180)
	return;		

    // Global angle needed by segcalc.
    rw_angle1 = angle1;
    angle1 -= viewangle;
    angle2 -= viewangle;
	
    tspan = angle1 + clipangle;
    if (tspan > 2*clipangle)
    {
	tspan -= 2*clipangle;

	// Totally off the left edge?
	if (tspan >= span)
	    return;
	
	angle1 = clipangle;
    }
    tspan = clipangle - angle2;
    if (tspan > 2*clipangle)
    {
	tspan -= 2*clipangle;

	// Totally off the left edge?
	if (tspan >= span)
	    return;	
	angle2 = -clipangle;
    }
    
    // The seg is in the view range,
    // but not necessarily visible.
    angle1 = (angle1+ANG90)>>ANGLETOFINESHIFT;
    angle2 = (angle2+ANG90)>>ANGLETOFINESHIFT;
    x1 = viewangletox[angle1];
    x2 = viewangletox[angle2];

    // Does not cross a pixel?
    if (x1 >= x2)
	return;

    if(fakeplane || fakeclip)
    {
	R_ClipSolidWallSegmentFake(x1, x2-1);
	return;
    }
	
    backsector = line->backsector;

    // Single sided line?
    if (!backsector)
	goto clipsolid;

    // Closed door.
    if (backsector->ceilingheight <= frontsector->floorheight
	|| backsector->floorheight >= frontsector->ceilingheight)
	goto clipsolid;

    // Window.
    if (backsector->ceilingheight != frontsector->ceilingheight
	|| backsector->floorheight != frontsector->floorheight)
	goto clippass;
		
    // Reject empty lines used for triggers
    //  and special events.
    // Identical floor and ceiling on both sides,
    // identical light levels on both sides,
    // and no middle texture.
    if (backsector->ceilingpic == frontsector->ceilingpic
	&& backsector->floorpic == frontsector->floorpic
	&& backsector->lightlevel == frontsector->lightlevel
	&& curline->sidedef->midtexture == 0
	// [kg] 3D floor check
	&& !backsector->exfloor && !frontsector->exfloor
	&& !backsector->exceiling && !frontsector->exceiling
	// [kg] color check
	&& backsector->colormap.data != frontsector->colormap.data
	&& backsector->fogmap.data != frontsector->fogmap.data
    )
    {
	return;
    }
    
				
  clippass:
    R_ClipPassWallSegment (x1, x2-1);	
    return;
		
  clipsolid:
    R_ClipSolidWallSegment (x1, x2-1);
}


//
// R_CheckBBox
// Checks BSP node/subtree bounding box.
// Returns true
//  if some part of the bbox might be visible.
//
int	checkcoord[12][4] =
{
    {3,0,2,1},
    {3,0,2,0},
    {3,1,2,0},
    {0},
    {2,0,2,1},
    {0,0,0,0},
    {3,1,3,0},
    {0},
    {2,0,3,1},
    {2,1,3,1},
    {2,1,3,0}
};


boolean R_CheckBBox (fixed_t*	bspcoord)
{
    int			boxx;
    int			boxy;
    int			boxpos;

    fixed_t		x1;
    fixed_t		y1;
    fixed_t		x2;
    fixed_t		y2;
    
    angle_t		angle1;
    angle_t		angle2;
    angle_t		span;
    angle_t		tspan;
    
    cliprange_t*	start;

    int			sx1;
    int			sx2;
    
    // Find the corners of the box
    // that define the edges from current viewpoint.
    if (viewx <= bspcoord[BOXLEFT])
	boxx = 0;
    else if (viewx < bspcoord[BOXRIGHT])
	boxx = 1;
    else
	boxx = 2;
		
    if (viewy >= bspcoord[BOXTOP])
	boxy = 0;
    else if (viewy > bspcoord[BOXBOTTOM])
	boxy = 1;
    else
	boxy = 2;
		
    boxpos = (boxy<<2)+boxx;
    if (boxpos == 5)
	return true;
	
    x1 = bspcoord[checkcoord[boxpos][0]];
    y1 = bspcoord[checkcoord[boxpos][1]];
    x2 = bspcoord[checkcoord[boxpos][2]];
    y2 = bspcoord[checkcoord[boxpos][3]];
    
    // check clip list for an open space
    angle1 = R_PointToAngle (x1, y1) - viewangle;
    angle2 = R_PointToAngle (x2, y2) - viewangle;
	
    span = angle1 - angle2;

    // Sitting on a line?
    if (span >= ANG180)
	return true;
    
    tspan = angle1 + clipangle;

    if (tspan > 2*clipangle)
    {
	tspan -= 2*clipangle;

	// Totally off the left edge?
	if (tspan >= span)
	    return false;	

	angle1 = clipangle;
    }
    tspan = clipangle - angle2;
    if (tspan > 2*clipangle)
    {
	tspan -= 2*clipangle;

	// Totally off the left edge?
	if (tspan >= span)
	    return false;
	
	angle2 = -clipangle;
    }


    // Find the first clippost
    //  that touches the source post
    //  (adjacent pixels are touching).
    angle1 = (angle1+ANG90)>>ANGLETOFINESHIFT;
    angle2 = (angle2+ANG90)>>ANGLETOFINESHIFT;
    sx1 = viewangletox[angle1];
    sx2 = viewangletox[angle2];

    // Does not cross a pixel.
    if (sx1 == sx2)
	return false;			
    sx2--;
	
    start = solidsegs;
    while (start->last < sx2)
	start++;
    
    if (sx1 >= start->first
	&& sx2 <= start->last)
    {
	// The clippost contains the new span.
	return false;
    }

    return true;
}

render_t render_normal = {RENDER_NORMAL, NULL};

//
// R_Subsector
// Determine floor/ceiling planes.
// Add sprites of things in sector.
// Draw one or more line segments.
//
void R_Subsector (int num)
{
	static sector_t fakeback;
	int		count;
	seg_t*		line;
	subsector_t*	sub;
	extraplane_t	*pl;
	
#ifdef RANGECHECK
	if(num >= numsubsectors)
		I_Error ("R_Subsector: ss %i with numss = %i", num, numsubsectors);
#endif

	sscount++;
	sub = &subsectors[num];
	frontsector = sub->sector;

	// [kg] add 3D floors now
	ceilingplane = NULL;
	pl = frontsector->exfloor;
	while(pl)
	{
		fixed_t lightlevel;
		void *colormap;
		void *fogmap;

		if(*pl->height < frontsector->floorheight && frontsector->floorpic != skyflatnum)
		{
			pl = pl->next;
			continue;
		}
		if(*pl->height >= viewz)
		{
			// out of sight; but must add height for light effects and sides
			e3d_NewHeight(*pl->height);
			pl = pl->next;
			continue;
		}
		if(pl->next)
		{
			lightlevel = *pl->next->lightlevel;
			colormap = pl->next->source->colormap.data;
			fogmap = pl->next->source->fogmap.data;
		} else
		{
			lightlevel = frontsector->lightlevel;
			colormap = frontsector->colormap.data;
			fogmap = frontsector->fogmap.data;
		}
		fakeplane = pl;
		floorplane = R_FindPlane(*pl->height, *pl->pic, lightlevel, colormap, fogmap, pl->render, fogmap && pl->source->fogmap.data != fogmap);
		if(floorplane)
		{
			e3d_NewHeight(*pl->height);
			count = sub->numlines;
			line = &segs[sub->firstline];
			if(pl->validcount != validcount)
				fakeclipbot = floorclip;
			else
				fakeclipbot = pl->clip;
			while(count--)
			{
				R_AddLine(line);
				line++;
			}
		}
		pl = pl->next;
	}
	fakeclipbot = NULL;
	// [kg] add 3D ceilings now
	floorplane = NULL;
	pl = frontsector->exceiling;
	while(pl)
	{
		if(*pl->height > frontsector->ceilingheight && frontsector->ceilingpic != skyflatnum)
		{
			pl = pl->next;
			continue;
		}
		if(*pl->height <= viewz)
		{
			// out of sight; but must add height for sides
			e3d_NewHeight(*pl->height);
			pl = pl->next;
			continue;
		}
		fakeplane = pl;
		ceilingplane = R_FindPlane(*pl->height, *pl->pic, *pl->lightlevel, pl->source->colormap.data, pl->source->fogmap.data, pl->render, !!pl->source->fogmap.data);
		if(ceilingplane)
		{
			e3d_NewHeight(*pl->height);
			count = sub->numlines;
			line = &segs[sub->firstline];
			if(pl->validcount != validcount)
				fakecliptop = ceilingclip;
			else
				fakecliptop = pl->clip;
			while(count--)
			{
				R_AddLine(line);
				line++;
			}
		}
		pl = pl->next;
	}
	fakecliptop = NULL;

	// [kg] add normal planes now
	fakeplane = NULL;
	count = sub->numlines;
	line = &segs[sub->firstline];

	if(frontsector->floorheight < viewz)
	{
		// [kg] go trough 3D floors to find correct light level
		fixed_t lightlevel = frontsector->lightlevel;
		void *colormap = frontsector->colormap.data;
		void *fogmap = frontsector->fogmap.data;

		pl = frontsector->exfloor;
		while(pl)
		{
			if(*pl->height >= frontsector->floorheight)
			{
				lightlevel = *pl->lightlevel;
				colormap = pl->source->colormap.data;
				fogmap = pl->source->fogmap.data;
				break;
			}
			pl = pl->next;
		}

		floorplane = R_FindPlane (frontsector->floorheight, frontsector->floorpic, lightlevel, colormap, fogmap, &render_normal, false);
	} else
		floorplane = NULL;


	if(frontsector->ceilingheight > viewz || frontsector->ceilingpic == skyflatnum)
	{
		// [kg] go trough 3D floors to find correct light level
		fixed_t lightlevel = frontsector->lightlevel;
		void *colormap = frontsector->colormap.data;
		void *fogmap = frontsector->fogmap.data;

		pl = frontsector->exfloor;
		while(pl)
		{
			if(*pl->height >= frontsector->ceilingheight)
			{
				lightlevel = *pl->lightlevel;
				colormap = pl->source->colormap.data;
				fogmap = pl->source->fogmap.data;
				break;
			}
			pl = pl->next;
		}

		ceilingplane = R_FindPlane (frontsector->ceilingheight, frontsector->ceilingpic, lightlevel, colormap, fogmap, &render_normal, false);
	} else
		ceilingplane = NULL;

	R_AddSprites (frontsector);

	while(count--)
	{
		sector_t *lbs = line->backsector;
		if(lbs && (lbs->exfloor || lbs->exceiling))
		{
			visplane_t *bfp, *bcp;
			boolean is_sky;
			// [kg] fake bounding lines
			bfp = floorplane;
			bcp = ceilingplane;
			fakeclip = true;
			floorplane = NULL;
			ceilingplane = NULL;
			// [kg] for each floor
			fakecliptop = NULL;
			is_sky = lbs->floorpic == frontsector->floorpic && lbs->floorpic == skyflatnum;
			pl = lbs->exfloor;
			while(pl)
			{
				if(*pl->height >= viewz)
					break;
				if(pl->validcount != validcount)
				{
					pl->clip = e3d_NewClip(floorclip);
					pl->validcount = validcount;
				}
				fakeclipbot = pl->clip;
				fakeback.ceilingheight = *pl->height + 1;
				fakeback.floorheight = *pl->height;
				backsector = &fakeback;
				if(is_sky)
				{
					fixed_t hhh = frontsector->floorheight;
					frontsector->floorheight = *pl->height;
					R_AddLine(line);
					frontsector->floorheight = hhh;
				} else
					R_AddLine(line);
				pl = pl->next;
			}
			// [kg] for each ceiling
			fakeclipbot = NULL;
			pl = lbs->exceiling;
			is_sky = lbs->ceilingpic == frontsector->ceilingpic && lbs->ceilingpic == skyflatnum;
			while(pl)
			{
				if(*pl->height <= viewz)
					break;
				if(pl->validcount != validcount)
				{
					pl->clip = e3d_NewClip(ceilingclip);
					pl->validcount = validcount;
				}
				fakecliptop = pl->clip;
				fakeback.floorheight = *pl->height - 1;
				fakeback.ceilingheight = *pl->height;
				backsector = &fakeback;
				if(is_sky)
				{
					fixed_t hhh = frontsector->ceilingheight;
					frontsector->ceilingheight = *pl->height;
					R_AddLine(line);
					frontsector->ceilingheight = hhh;
				} else
					R_AddLine(line);
				pl = pl->next;
			}
			// done
			fakeclipbot = NULL;
			fakecliptop = NULL;
			floorplane = bfp;
			ceilingplane = bcp;
			fakeclip = false;
		}
		// [kg] normal lines
		R_AddLine(line);
		line++;
	}
}


//
// RenderBSPNode
// Renders all subsectors below a given node,
//  traversing subtree recursively.
// Just call with BSP root.
void R_RenderBSPNode (int bspnum)
{
    node_t*	bsp;
    int		side;

    // Found a subsector?
    if (bspnum & NF_SUBSECTOR)
    {
	if (bspnum == -1)			
	    R_Subsector (0);
	else
	    R_Subsector (bspnum&(~NF_SUBSECTOR));
	return;
    }
		
    bsp = &nodes[bspnum];
    
    // Decide which side the view point is on.
    side = R_PointOnSide (viewx, viewy, bsp);

    // Recursively divide front space.
    R_RenderBSPNode (bsp->children[side]); 

    // Possibly divide back space.
    if (R_CheckBBox (bsp->bbox[side^1]))	
	R_RenderBSPNode (bsp->children[side^1]);
}

