/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

	$Id: pmovetst.c,v 1.10 2006/07/05 17:07:18 disconn3ct Exp $
*/

#include "qwsvdef.h"

extern	vec3_t player_mins;
extern	vec3_t player_maxs;


static void PM_TraceBounds (vec3_t start, vec3_t end, vec3_t boxmins, vec3_t boxmaxs)
{
	int i;

	for (i = 0; i < 3; i++) {
		if (end[i] > start[i]) {
			boxmins[i] = start[i] - 1;
			boxmaxs[i] = end[i] + 1;
		} else {
			boxmins[i] = end[i] - 1;
			boxmaxs[i] = start[i] + 1;
		}
	}
}

static qbool PM_CullTraceBox(vec3_t mins, vec3_t maxs, vec3_t offset, vec3_t emins, vec3_t emaxs, vec3_t hullmins, vec3_t hullmaxs) {
	return
		(	mins[0] + hullmins[0] > offset[0] + emaxs[0] || maxs[0] + hullmaxs[0] < offset[0] + emins[0] ||
			mins[1] + hullmins[1] > offset[1] + emaxs[1] || maxs[1] + hullmaxs[1] < offset[1] + emins[1] ||
			mins[2] + hullmins[2] > offset[2] + emaxs[2] || maxs[2] + hullmaxs[2] < offset[2] + emins[2]
		);
}


/*
==================
PM_PointContents
==================
*/
int PM_PointContents (vec3_t p)
{
	hull_t *hull = &pmove.physents[0].model->hulls[0];
	return CM_HullPointContents (hull, hull->firstclipnode, p);
}

static hull_t trace_hull;
//static qbool RecursiveHullTrace (int num, float p1f, float p2f, vec3_t p1, vec3_t p2);
#define DIST_EPSILON (0.03125)
static trace_t trace_trace;

static	hull_t		box_hull;
static	dclipnode_t	box_clipnodes[6];
static	mplane_t	box_planes[6];
qbool RecursiveHullTracet (int num, float p1f, float p2f, vec3_t p1, vec3_t p2);
qbool Q1BSP_RecursiveHullCheck (hull_t *hull, int num, float p1f, float p2f, vec3_t p1, vec3_t p2, trace_t *trace);

qbool PM_TransformedHullCheck (cmodel_t *model, vec3_t start, vec3_t end, trace_t *trace, vec3_t origin, vec3_t angles)
{
	vec3_t		start_l, end_l;
	vec3_t		a;
	vec3_t		forward, right, up;
	vec3_t		temp;
	qbool	rotated;
	qbool	result;

	// subtract origin offset
	VectorSubtract (start, origin, start_l);
	VectorSubtract (end, origin, end_l);

	// rotate start and end into the models frame of reference
	if (model && 
	(angles[0] || angles[1] || angles[2]) )
		rotated = true;
	else
		rotated = false;

	if (rotated)
	{
		AngleVectors (angles, forward, right, up);

		VectorCopy (start_l, temp);
		start_l[0] = DotProduct (temp, forward);
		start_l[1] = -DotProduct (temp, right);
		start_l[2] = DotProduct (temp, up);

		VectorCopy (end_l, temp);
		end_l[0] = DotProduct (temp, forward);
		end_l[1] = -DotProduct (temp, right);
		end_l[2] = DotProduct (temp, up);
	}
	// sweep the box through the model
/*
	if (model && model->funcs.Trace)
		result = model->funcs.Trace(model, 0, 0, start_l, end_l, player_mins, player_maxs, trace);
	else
	{
		result = Q1BSP_RecursiveHullCheck (&box_hull, box_hull.firstclipnode, 0, 1, start_l, end_l, trace);
	}
	*/
	result = Q1BSP_RecursiveHullCheck (&box_hull, box_hull.firstclipnode, 0, 1, start_l, end_l, trace);

	//result = RecursiveHullTrace(/*&box_hull,*/ box_hull.firstclipnode, 0, 1, start_l, end_l/*, trace*/);

	if (rotated)
	{
		// FIXME: figure out how to do this with existing angles
//		VectorNegate (angles, a);

		if (trace->fraction != 1.0)
		{
			a[0] = -angles[0];
			a[1] = -angles[1];
			a[2] = -angles[2];
			AngleVectors (a, forward, right, up);

			VectorCopy (trace->plane.normal, temp);
			trace->plane.normal[0] = DotProduct (temp, forward);
			trace->plane.normal[1] = -DotProduct (temp, right);
			trace->plane.normal[2] = DotProduct (temp, up);
		}
		trace->endpos[0] = start[0] + trace->fraction * (end[0] - start[0]);
		trace->endpos[1] = start[1] + trace->fraction * (end[1] - start[1]);
		trace->endpos[2] = start[2] + trace->fraction * (end[2] - start[2]);
	}
	else
	{
		trace->endpos[0] += origin[0];
		trace->endpos[1] += origin[1];
		trace->endpos[2] += origin[2];
	}

	return result;
}

hull_t	*PM_HullForBox (vec3_t mins, vec3_t maxs)
{
	box_planes[0].dist = maxs[0];
	box_planes[1].dist = mins[0];
	box_planes[2].dist = maxs[1];
	box_planes[3].dist = mins[1];
	box_planes[4].dist = maxs[2];
	box_planes[5].dist = mins[2];

	return &box_hull;
}

/*
================
PM_TestPlayerPosition

Returns false if the given player position is not valid (in solid)
================
*/

qbool PM_TestPlayerPosition (vec3_t pos)
{
	int			i;
	physent_t	*pe;
	vec3_t		mins, maxs, offset, test;
	hull_t		*hull;

	for (i=0 ; i< pmove.numphysent ; i++)
	{
		pe = &pmove.physents[i];
	// get the clipping hull
		if (pe->model)
		{
			// todo: make this based on player hull size
			//VectorSubtract (sv_player->v.mins, player_mins, offset); // spike <-
			if (sv_client->is_crouching == 1)
				hull = &pmove.physents[i].model->hulls[3];
			else
				hull = &pmove.physents[i].model->hulls[1];
			VectorSubtract (hull->clip_mins, player_mins, offset);
			VectorAdd (offset, pe->origin, offset);
		}
		else
		{
			VectorSubtract (pe->mins, player_maxs, mins);
			VectorSubtract (pe->maxs, player_mins, maxs);
			hull = CM_HullForBox (mins, maxs);
			VectorCopy (pe->origin, offset);
		}

		VectorSubtract (pos, offset, test);

		if (CM_HullPointContents (hull, hull->firstclipnode, test) == CONTENTS_SOLID)
			return false;
	}

	return true;
}

#define QUAKE2
/*
================
PM_PlayerTrace
================
*/

trace_t PM_PlayerTrace (vec3_t start, vec3_t end)
{
	trace_t		trace, total;
	vec3_t		offset;
	vec3_t		start_l, end_l;
	hull_t		*hull;
	int			i;
	physent_t	*pe;
	vec3_t		mins, maxs, tracemins, tracemaxs;

// fill in a default trace
	memset (&total, 0, sizeof(trace_t));
	total.fraction = 1;
	total.e.entnum = -1;
	VectorCopy (end, total.endpos);

	PM_TraceBounds(start, end, tracemins, tracemaxs);

	for (i=0 ; i< pmove.numphysent ; i++)
	{
		pe = &pmove.physents[i];

	// get the clipping hull
		if (pe->model)
		{
			if (sv_client->is_crouching == 1)
				hull = &pmove.physents[i].model->hulls[3];
			else
				hull = &pmove.physents[i].model->hulls[1];

			//if (i > 0 && PM_CullTraceBox(tracemins, tracemaxs, pe->origin, pe->model->mins, pe->model->maxs, hull->clip_mins, hull->clip_maxs))
			//	continue;

			VectorSubtract (hull->clip_mins, player_mins, offset);
			VectorAdd (offset, pe->origin, offset);
		}
		else
		{
			VectorSubtract (pe->mins, player_maxs, mins);
			VectorSubtract (pe->maxs, player_mins, maxs);

			if (PM_CullTraceBox(tracemins, tracemaxs, pe->origin, mins, maxs, vec3_origin, vec3_origin))
				continue;

			hull = CM_HullForBox (mins, maxs);
			VectorCopy (pe->origin, offset);
		}

		VectorSubtract (start, offset, start_l);
		VectorSubtract (end, offset, end_l);


	// rotate start and end into the models frame of reference
		if (pe->solid == SOLID_BSP &&
	(pe->angles[0] || pe->angles[1] || pe->angles[2]) )
	{
		vec3_t	a;
		vec3_t	forward, right, up;
		vec3_t	temp;

		AngleVectors (pe->angles, forward, right, up);

		VectorCopy (start_l, temp);
		start_l[0] = DotProduct (temp, forward);
		start_l[1] = -DotProduct (temp, right);
		start_l[2] = DotProduct (temp, up);

		VectorCopy (end_l, temp);
		end_l[0] = DotProduct (temp, forward);
		end_l[1] = -DotProduct (temp, right);
		end_l[2] = DotProduct (temp, up);
	}


		// trace a line through the apropriate clipping hull
		trace = CM_HullTrace (hull, start_l, end_l);

	// rotate endpos back to world frame of reference
	if (pe->solid == SOLID_BSP &&
	(pe->angles[0] || pe->angles[1] || pe->angles[2]) )
	{
		vec3_t	a;
		vec3_t	forward, right, up;
		vec3_t	temp;

		if (trace.fraction != 1)
		{
			VectorSubtract (vec3_origin, pe->angles, a);
			AngleVectors (a, forward, right, up);

			VectorCopy (trace.endpos, temp);
			trace.endpos[0] = DotProduct (temp, forward);
			trace.endpos[1] = -DotProduct (temp, right);
			trace.endpos[2] = DotProduct (temp, up);

			VectorCopy (trace.plane.normal, temp);
			trace.plane.normal[0] = DotProduct (temp, forward);
			trace.plane.normal[1] = -DotProduct (temp, right);
			trace.plane.normal[2] = DotProduct (temp, up);
		}
	}


		// fix trace up by the offset
		VectorAdd (trace.endpos, offset, trace.endpos);

		if (trace.allsolid)
			trace.startsolid = true;
		if (trace.startsolid)
			trace.fraction = 0;

		// did we clip the move?
		if (trace.fraction < total.fraction)
		{
			total = trace;
			total.e.entnum = i;
		}

	}

	return total;
}


void PM_InitBoxHull (void)
{
	int		i;
	int		side;

	box_hull.clipnodes = box_clipnodes;
	box_hull.planes = box_planes;
	box_hull.firstclipnode = 0;
	box_hull.lastclipnode = 5;

	//Q1BSP_SetHullFuncs(&box_hull);

	for (i=0 ; i<6 ; i++)
	{
		box_clipnodes[i].planenum = i;
		
		side = i&1;
		
		box_clipnodes[i].children[side] = CONTENTS_EMPTY;
		if (i != 5)
			box_clipnodes[i].children[side^1] = i + 1;
		else
			box_clipnodes[i].children[side^1] = CONTENTS_SOLID;
		
		box_planes[i].type = i>>1;
		box_planes[i].normal[i>>1] = 1;
	}
	
}