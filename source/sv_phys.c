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

	$Id: sv_phys.c,v 1.19 2007/02/13 14:18:16 tonik Exp $
*/
// sv_phys.c

#include "qwsvdef.h"
//#define MVDSV_MOVEMENT

/*


pushmove objects do not obey gravity, and do not interact with each other or trigger fields, but block normal movement and push normal objects when they move.

onground is set for toss objects when they come to a complete rest.  it is set for steping or walking objects 

doors, plats, etc are SOLID_BSP, and MOVETYPE_PUSH
bonus items are SOLID_TRIGGER touch, and MOVETYPE_TOSS
corpses are SOLID_NOT and MOVETYPE_TOSS
crates are SOLID_BBOX and MOVETYPE_TOSS
walking monsters are SOLID_SLIDEBOX and MOVETYPE_STEP
flying/floating monsters are SOLID_SLIDEBOX and MOVETYPE_FLY

solid_edge items only clip against bsp models.

*/

cvar_t	sv_maxvelocity		= { "sv_maxvelocity","2000"};

cvar_t	sv_gravity		= { "sv_gravity", "800"};
cvar_t	sv_stopspeed		= { "sv_stopspeed", "100"};
cvar_t	sv_maxspeed		= { "sv_maxspeed", "320"};
cvar_t	sv_spectatormaxspeed 	= { "sv_spectatormaxspeed", "500"};
cvar_t	sv_accelerate		= { "sv_accelerate", "10"};
cvar_t	sv_airaccelerate	= { "sv_airaccelerate", "10"};

cvar_t	sv_wateraccelerate	= { "sv_wateraccelerate", "10"};
cvar_t	sv_friction		= { "sv_friction", "4"};
cvar_t	sv_waterfriction	= { "sv_waterfriction", "4"};
cvar_t	pm_ktjump		= { "pm_ktjump", "1", CVAR_SERVERINFO};
cvar_t	pm_bunnyspeedcap	= { "pm_bunnyspeedcap", "", CVAR_SERVERINFO};
cvar_t	pm_slidefix		= { "pm_slidefix", "", CVAR_SERVERINFO};
cvar_t	pm_airstep		= { "pm_airstep", "", CVAR_SERVERINFO};
cvar_t	pm_pground		= { "pm_pground", "", CVAR_SERVERINFO};

double	sv_frametime;


void SV_Physics_Toss (edict_t *ent);


/*
================
SV_CheckVelocity
================
*/
void SV_CheckVelocity (edict_t *ent)
{
	int i;
	float wishspeed;

	//
	// bound velocity
	//
	for (i=0 ; i<3 ; i++)
	{
		if (IS_NAN(ent->v.velocity[i]))
		{
			Con_DPrintf ("Got a NaN velocity on %s\n",
#ifdef USE_PR2
			             PR2_GetString(ent->v.classname)
#else
			             PR_GetString(ent->v.classname)
#endif
			            );
			ent->v.velocity[i] = 0;
		}
		if (IS_NAN(ent->v.origin[i]))
		{
			Con_DPrintf ("Got a NaN origin on %s\n",
#ifdef USE_PR2
			             PR2_GetString(ent->v.classname)
#else
			             PR_GetString(ent->v.classname)
#endif
			            );
			ent->v.origin[i] = 0;
		}
/*		if (ent->v.velocity[i] > sv_maxvelocity.value)
			ent->v.velocity[i] = sv_maxvelocity.value;
		else if (ent->v.velocity[i] < -sv_maxvelocity.value)
			ent->v.velocity[i] = -sv_maxvelocity.value;
*/
	}

	// SV_MAXVELOCITY fix by Maddes
	wishspeed = VectorLength(ent->v.velocity);
	if (wishspeed > sv_maxvelocity.value)
	{
		VectorScale (ent->v.velocity, sv_maxvelocity.value/wishspeed, ent->v.velocity);
		wishspeed = sv_maxvelocity.value;
	}
}

#ifndef newphys
/*
=============
SV_RunThink

Runs thinking code if time.  There is some play in the exact time the think
function will be called, because it is called before any movement is done
in a frame.  Not used for pushmove objects, because they must be exact.
Returns false if the entity removed itself.
=============
*/
qbool SV_RunThink (edict_t *ent)
{
	float thinktime;

	do
	{
		thinktime = ent->v.nextthink;
		if (thinktime <= 0)
			return true;
		if (thinktime > sv.time + sv_frametime)
			return true;

		if (thinktime < sv.time)
			thinktime = sv.time; // don't let things stay in the past.
		// it is possible to start that way
		// by a trigger with a local time.
		ent->v.nextthink = 0;
		pr_global_struct->time = thinktime;
		pr_global_struct->self = EDICT_TO_PROG(ent);
		pr_global_struct->other = EDICT_TO_PROG(sv.edicts);
#ifdef USE_PR2
		if ( sv_vm )
			PR2_EdictThink();
		else
#endif
			PR_ExecuteProgram (ent->v.think);

		if (ent->free)
			return false;

		if (ent->v.nextthink <= thinktime)	//hmm... infinate loop was possible here.. Quite a few non-QW mods do this.
			return true;
	} while (1);

	return true;
}

/*
==================
SV_Impact

Two entities have touched, so run their touch functions
==================
*/
void SV_Impact (edict_t *e1, edict_t *e2)
{
	int old_self, old_other;

	old_self = pr_global_struct->self;
	old_other = pr_global_struct->other;

	pr_global_struct->time = sv.time;
	if (e1->v.touch && e1->v.solid != SOLID_NOT)
	{
		pr_global_struct->self = EDICT_TO_PROG(e1);
		pr_global_struct->other = EDICT_TO_PROG(e2);
#ifdef USE_PR2
		if ( sv_vm )
			PR2_EdictTouch();
		else
#endif
			PR_ExecuteProgram (e1->v.touch);
	}

	if (e2->v.touch && e2->v.solid != SOLID_NOT)
	{
		pr_global_struct->self = EDICT_TO_PROG(e2);
		pr_global_struct->other = EDICT_TO_PROG(e1);
#ifdef USE_PR2
		if( sv_vm )
			PR2_EdictTouch();
		else
#endif
			PR_ExecuteProgram (e2->v.touch);
	}

	pr_global_struct->self = old_self;
	pr_global_struct->other = old_other;
}


/*
==================
ClipVelocity

Slide off of the impacting object
==================
*/
#define	STOP_EPSILON	0.1

// xavior: fixme: add def
#ifdef MVDSV_MOVEMENT
void ClipVelocity (vec3_t in, vec3_t normal, vec3_t out, float overbounce)
{
	float backoff;
	float change;
	int i;


	backoff = DotProduct (in, normal) * overbounce;

	for (i=0 ; i<3 ; i++)
	{
		change = normal[i]*backoff;
		out[i] = in[i] - change;
		if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON)
			out[i] = 0;
	}
}
#else
#define	STOP_EPSILON	0.1

int ClipVelocity (vec3_t in, vec3_t normal, vec3_t out, float overbounce)
{
	float	backoff;
	float	change;
	int		i, blocked;
	
	blocked = 0;
	if (normal[2] > 0)
		blocked |= 1;		// floor
	if (!normal[2])
		blocked |= 2;		// step
	
	backoff = DotProduct (in, normal) * overbounce;

	for (i=0 ; i<3 ; i++)
	{
		change = normal[i]*backoff;
		out[i] = in[i] - change;
		if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON)
			out[i] = 0;
	}
	
	return blocked;
}
#endif


/*
============
SV_FlyMove

The basic solid body movement clip that slides along multiple planes
Returns the clipflags if the velocity was modified (hit something solid)
1 = floor
2 = wall / step
4 = dead stop
If steptrace is not NULL, the trace of any vertical wall hit will be stored
============
*/
#define MAX_CLIP_PLANES	5
int SV_FlyMove (edict_t *ent, float time1, trace_t *steptrace, int type)
{
#ifdef MVDSV_MOVEMENT
	int		bumpcount, numbumps;
	vec3_t	dir;
	float	d;
	int		numplanes;
	vec3_t	planes[MAX_CLIP_PLANES];
	vec3_t	primal_velocity, original_velocity, new_velocity;
	int		i, j;
	trace_t	trace;
	vec3_t	end;
	float	time_left;
	int		blocked;

	numbumps = 4;

	blocked = 0;
	VectorCopy (ent->v.velocity, original_velocity);
	VectorCopy (ent->v.velocity, primal_velocity);
	numplanes = 0;

	time_left = time1;

	for (bumpcount=0 ; bumpcount<numbumps ; bumpcount++)
	{
		for (i=0 ; i<3 ; i++)
			end[i] = ent->v.origin[i] + time_left * ent->v.velocity[i];

		trace = SV_Trace (ent->v.origin, ent->v.mins, ent->v.maxs, end, type, ent);

		if (trace.allsolid)
		{	// entity is trapped in another solid
			VectorClear (ent->v.velocity);
			return 3;
		}

		if (trace.fraction > 0)
		{	// actually covered some distance
			VectorCopy (trace.endpos, ent->v.origin);
			VectorCopy (ent->v.velocity, original_velocity);
			numplanes = 0;
		}

		if (trace.fraction == 1)
			break; // moved the entire distance

		if (!trace.e.ent)
			SV_Error ("SV_FlyMove: !trace.e.ent");

		if (trace.plane.normal[2] > 0.7)
		{
			blocked |= 1; // floor
			if (trace.e.ent->v.solid == SOLID_BSP)
			{
				ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
				ent->v.groundentity = EDICT_TO_PROG(trace.e.ent);
			}
		}
		if (!trace.plane.normal[2])
		{
			blocked |= 2; // step
			if (steptrace)
				*steptrace = trace; // save for player extrafriction
		}

		//
		// run the impact function
		//
		SV_Impact (ent, trace.e.ent);
		if (ent->free)
			break;	 // removed by the impact function


		time_left -= time_left * trace.fraction;

		// cliped to another plane
		if (numplanes >= MAX_CLIP_PLANES)
		{	// this shouldn't really happen
			VectorClear (ent->v.velocity);
			return 3;
		}

		VectorCopy (trace.plane.normal, planes[numplanes]);
		numplanes++;

		//
		// modify original_velocity so it parallels all of the clip planes
		//
		for (i=0 ; i<numplanes ; i++)
		{
			ClipVelocity (original_velocity, planes[i], new_velocity, 1);
			for (j=0 ; j<numplanes ; j++)
				if (j != i)
				{
					if (DotProduct (new_velocity, planes[j]) < 0)
						break; // not ok
				}
			if (j == numplanes)
				break;
		}

		if (i != numplanes)
		{	// go along this plane
			VectorCopy (new_velocity, ent->v.velocity);
		}
		else
		{	// go along the crease
			if (numplanes != 2)
			{
				// Con_Printf ("clip velocity, numplanes == %i\n",numplanes);
				VectorClear (ent->v.velocity);
				return 7;
			}
			CrossProduct (planes[0], planes[1], dir);
			d = DotProduct (dir, ent->v.velocity);
			VectorScale (dir, d, ent->v.velocity);
		}

		//
		// if original velocity is against the original velocity, stop dead
		// to avoid tiny occilations in sloping corners
		//
		if (DotProduct (ent->v.velocity, primal_velocity) <= 0)
		{
			VectorClear (ent->v.velocity);
			return blocked;
		}
	}

	return blocked;
#else
	int			bumpcount, numbumps;
	vec3_t		dir;
	float		d;
	int			numplanes;
	vec3_t		planes[MAX_CLIP_PLANES];
	vec3_t		primal_velocity, original_velocity, new_velocity;
	int			i, j;
	trace_t		trace;
	vec3_t		end;
	float		time_left;
	int			blocked;
	
	numbumps = 4;
	
	blocked = 0;
	VectorCopy (ent->v.velocity, original_velocity);
	VectorCopy (ent->v.velocity, primal_velocity);
	numplanes = 0;
	
	time_left = time1;

	for (bumpcount=0 ; bumpcount<numbumps ; bumpcount++)
	{
		for (i=0 ; i<3 ; i++)
			end[i] = ent->v.origin[i] + time_left * ent->v.velocity[i];

//		trace = SV_Move (ent->v.origin, ent->v.mins, ent->v.maxs, end, false, ent);
		trace = SV_Trace (ent->v.origin, ent->v.mins, ent->v.maxs, end, type, ent);

		if (trace.allsolid)
		{	// entity is trapped in another solid
			VectorCopy (vec3_origin, ent->v.velocity);
			return 3;
		}

		if (trace.fraction > 0)
		{	// actually covered some distance
			VectorCopy (trace.endpos, ent->v.origin);
			VectorCopy (ent->v.velocity, original_velocity);
			numplanes = 0;
		}

		if (trace.fraction == 1)
			 break;		// moved the entire distance

		if (!trace.e.ent)
			SV_Error ("SV_FlyMove: !trace.ent");

		if (trace.plane.normal[2] > 0.7)
		{
			blocked |= 1;		// floor
			if (trace.e.ent->v.solid == SOLID_BSP)
			{
				ent->v.flags =	(int)ent->v.flags | FL_ONGROUND;
				ent->v.groundentity = EDICT_TO_PROG(trace.e.ent);
			}
		}
		if (!trace.plane.normal[2])
		{
			blocked |= 2;		// step
			if (steptrace)
				*steptrace = trace;	// save for player extrafriction
		}

//
// run the impact function
//
		SV_Impact (ent, trace.e.ent);
		if (ent->free)
			break;		// removed by the impact function

		
		time_left -= time_left * trace.fraction;
		
	// cliped to another plane
		if (numplanes >= MAX_CLIP_PLANES)
		{	// this shouldn't really happen
			VectorCopy (vec3_origin, ent->v.velocity);
			return 3;
		}

		VectorCopy (trace.plane.normal, planes[numplanes]);
		numplanes++;

//
// modify original_velocity so it parallels all of the clip planes
//
		for (i=0 ; i<numplanes ; i++)
		{
			ClipVelocity (original_velocity, planes[i], new_velocity, 1);
			for (j=0 ; j<numplanes ; j++)
				if (j != i)
				{
					if (DotProduct (new_velocity, planes[j]) < 0)
						break;	// not ok
				}
			if (j == numplanes)
				break;
		}
		
		if (i != numplanes)
		{	// go along this plane
			VectorCopy (new_velocity, ent->v.velocity);
		}
		else
		{	// go along the crease
			if (numplanes != 2)
			{
//				Con_Printf ("clip velocity, numplanes == %i\n",numplanes);
				VectorCopy (vec3_origin, ent->v.velocity);
				return 7;
			}
			CrossProduct (planes[0], planes[1], dir);
			d = DotProduct (dir, ent->v.velocity);
			VectorScale (dir, d, ent->v.velocity);
		}

//
// if original velocity is against the original velocity, stop dead
// to avoid tiny occilations in sloping corners
//
		if (DotProduct (ent->v.velocity, primal_velocity) <= 0)
		{
			VectorCopy (vec3_origin, ent->v.velocity);
			return blocked;
		}
	}

	return blocked;
#endif
}


/*
============
SV_AddGravity
============
*/
void SV_AddGravity (edict_t *ent, float scale)
{
	ent->v.velocity[2] -= scale * movevars.gravity * sv_frametime;
}

/*
===============================================================================

PUSHMOVE

===============================================================================
*/

/*
============
SV_PushEntity

Does not change the entities velocity at all
============
*/
trace_t SV_PushEntity (edict_t *ent, vec3_t push)
{
	trace_t	trace;
	vec3_t	end;

	VectorAdd (ent->v.origin, push, end);

	if (ent->v.movetype == MOVETYPE_FLYMISSILE)
		trace = SV_Trace (ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_MISSILE, ent);
	else if (ent->v.solid == SOLID_TRIGGER || ent->v.solid == SOLID_NOT)
		// only clip against bmodels
		trace = SV_Trace (ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_NOMONSTERS, ent);
	else
		trace = SV_Trace (ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_NORMAL, ent);

	VectorCopy (trace.endpos, ent->v.origin);
	SV_LinkEdict (ent, true);

	if (trace.e.ent)
		SV_Impact (ent, trace.e.ent);

	return trace;
}

typedef struct
{
	edict_t	*ent;
	vec3_t	origin;
	vec3_t	angles;
//	float	deltayaw;
} pushed_t;
pushed_t	pushed[MAX_EDICTS], *pushed_p;

void SV_TouchLinks ( edict_t *ent, areanode_t *node );
qbool SV_PushAngles (edict_t *pusher, vec3_t move, vec3_t amove)
{
	int			i, e;
	edict_t		*check, *block;
	vec3_t		mins, maxs;
	float oldsolid;
	pushed_t	*p;
	vec3_t		org, org2, move2, forward, right, up;

	pushed_p = pushed;

	// find the bounding box
	for (i=0 ; i<3 ; i++)
	{
		mins[i] = pusher->v.absmin[i] + move[i];
		maxs[i] = pusher->v.absmax[i] + move[i];
	}

// we need this for pushing things later
	VectorNegate (amove, org);
	AngleVectors (org, forward, right, up);

// save the pusher's original position
	pushed_p->ent = pusher;
	VectorCopy (pusher->v.origin, pushed_p->origin);
	VectorCopy (pusher->v.angles, pushed_p->angles);
//	if (pusher->client)
//		pushed_p->deltayaw = pusher->client->ps.pmove.delta_angles[YAW];
	pushed_p++;

// move the pusher to it's final position
	VectorAdd (pusher->v.origin, move, pusher->v.origin);
	VectorAdd (pusher->v.angles, amove, pusher->v.angles);
	SV_LinkEdict (pusher, false);

// see if any solid entities are inside the final position
	for (e = 1; e < sv.num_edicts; e++)
	{
		check = EDICT_NUM(e);
		if (check->free)
			continue;

		if (check->v.movetype == MOVETYPE_PUSH
		|| check->v.movetype == MOVETYPE_NONE
		|| check->v.movetype == MOVETYPE_NOCLIP)
			continue;

#if 1
		oldsolid = pusher->v.solid;
		pusher->v.solid = SOLID_NOT;
		block = SV_TestEntityPosition (check);
		pusher->v.solid = oldsolid;
		if (block)
			continue;
#else
		if (!check->area.prev)
			continue;		// not linked in anywhere
#endif

	// if the entity is standing on the pusher, it will definitely be moved
		// if the entity is standing on the pusher, it will definately be moved
		if ( ! ( ((int)check->v.flags & FL_ONGROUND)
		&& PROG_TO_EDICT(check->v.groundentity) == pusher) )
		{
			if ( check->v.absmin[0] >= maxs[0]
			|| check->v.absmin[1] >= maxs[1]
			|| check->v.absmin[2] >= maxs[2]
			|| check->v.absmax[0] <= mins[0]
			|| check->v.absmax[1] <= mins[1]
			|| check->v.absmax[2] <= mins[2] )
				continue;

			// see if the ent's bbox is inside the pusher's final position
			if (!SV_TestEntityPosition (check))
				continue;
		}

		if ((pusher->v.movetype == MOVETYPE_PUSH) || (PROG_TO_EDICT(check->v.groundentity) == pusher))
		{
			// move this entity
			pushed_p->ent = check;
			VectorCopy (check->v.origin, pushed_p->origin);
			VectorCopy (check->v.angles, pushed_p->angles);
			pushed_p++;

			// try moving the contacted entity
			VectorAdd (check->v.origin, move, check->v.origin);
//			if (check->client)
//			{	// FIXME: doesn't rotate monsters?
//				check->client->ps.pmove.delta_angles[YAW] += amove[YAW];
//			}
			VectorAdd (check->v.angles, amove, check->v.angles);

			// figure movement due to the pusher's amove
			VectorSubtract (check->v.origin, pusher->v.origin, org);
			org2[0] = DotProduct (org, forward);
			org2[1] = -DotProduct (org, right);
			org2[2] = DotProduct (org, up);
			VectorSubtract (org2, org, move2);
			VectorAdd (check->v.origin, move2, check->v.origin);

			check->v.flags = (int)check->v.flags & ~FL_ONGROUND;

			// may have pushed them off an edge
			if (PROG_TO_EDICT(check->v.groundentity) != pusher)
				check->v.groundentity = 0;

			block = SV_TestEntityPosition (check);
			if (!block)
			{	// pushed ok
				SV_LinkEdict (check, false);
				// impact?
				continue;
			}



			// if it is ok to leave in the old position, do it
			// this is only relevent for riding entities, not pushed
			// FIXME: this doesn't acount for rotation
			VectorSubtract (check->v.origin, move, check->v.origin);
			block = SV_TestEntityPosition (check);
			if (!block)
			{
				pushed_p--;
				continue;
			}
		}

		// if it is sitting on top. Do not block.
		if (check->v.mins[0] == check->v.maxs[0])
		{
			SV_LinkEdict (check, false);
			continue;
		}

//		Con_Printf("Pusher hit %s\n", PR_GetString(svprogfuncs, check->v.classname));
		if (pusher->v.blocked)
		{
			pr_global_struct->self = EDICT_TO_PROG(pusher);
			pr_global_struct->other = EDICT_TO_PROG(check);
#ifdef VM_Q1
			if (svs.gametype == GT_Q1QVM)
				Q1QVM_Blocked();
			else
#endif
				PR_ExecuteProgram (pusher->v.blocked);
		}

		// move back any entities we already moved
		// go backwards, so if the same entity was pushed
		// twice, it goes back to the original position
		for (p=pushed_p-1 ; p>=pushed ; p--)
		{
			VectorCopy (p->origin, p->ent->v.origin);
			VectorCopy (p->angles, p->ent->v.angles);
//			if (p->ent->client)
//			{
//				p->ent->client->ps.pmove.delta_angles[YAW] = p->deltayaw;
//			}
			SV_LinkEdict (p->ent, false);
		}
		return false;
	}

//FIXME: is there a better way to handle this?
	// see if anything we moved has touched a trigger
	for (p=pushed_p-1 ; p>=pushed ; p--)
		SV_TouchLinks ( p->ent, sv_areanodes );

	return true;
}

/*
============
SV_Push

============
*/
#ifndef mvdsv_move
qbool SV_Push (edict_t *pusher, vec3_t move, vec3_t amove)
{
	int			i, e;
	edict_t		*check, *block;
	vec3_t		mins, maxs;
	vec3_t		pushorig;
	int			num_moved;
	edict_t		*moved_edict[MAX_EDICTS];
	vec3_t		moved_from[MAX_EDICTS];
	float oldsolid;

	if (amove[0] || amove[1] || amove[2])
	{
		return SV_PushAngles(pusher, move, amove);
	}

	for (i=0 ; i<3 ; i++)
	{
		mins[i] = pusher->v.absmin[i] + move[i];
		maxs[i] = pusher->v.absmax[i] + move[i];
	}

	VectorCopy (pusher->v.origin, pushorig);

// move the pusher to it's final position

	VectorAdd (pusher->v.origin, move, pusher->v.origin);
	SV_LinkEdict (pusher, false);

// see if any solid entities are inside the final position
	num_moved = 0;
	for (e=1 ; e<sv.num_edicts ; e++)
	{
		check = EDICT_NUM(e);
		if (check->free)
			continue;
		if (check->v.movetype == MOVETYPE_PUSH
		|| check->v.movetype == MOVETYPE_NONE
		//|| check->v.movetype == MOVETYPE_FOLLOW
		|| check->v.movetype == MOVETYPE_NOCLIP)
			continue;

		oldsolid = pusher->v.solid;
		pusher->v.solid = SOLID_NOT;
		block = SV_TestEntityPosition (check);
		pusher->v.solid = oldsolid;
		if (block)
			continue;

	// if the entity is standing on the pusher, it will definately be moved
		if ( ! ( ((int)check->v.flags & FL_ONGROUND)
		&&
			PROG_TO_EDICT(check->v.groundentity) == pusher) )
		{
			if ( check->v.absmin[0] >= maxs[0]
			|| check->v.absmin[1] >= maxs[1]
			|| check->v.absmin[2] >= maxs[2]
			|| check->v.absmax[0] <= mins[0]
			|| check->v.absmax[1] <= mins[1]
			|| check->v.absmax[2] <= mins[2] )
				continue;

		// see if the ent's bbox is inside the pusher's final position
			if (!SV_TestEntityPosition (check))
				continue;
		}

		VectorCopy (check->v.origin, moved_from[num_moved]);
		moved_edict[num_moved] = check;
		num_moved++;

//		check->v.flags = (int)check->v.flags & ~FL_ONGROUND;

		// try moving the contacted entity
		VectorAdd (check->v.origin, move, check->v.origin);
		block = SV_TestEntityPosition (check);
		if (!block)
		{	// pushed ok
			SV_LinkEdict (check, false);
			continue;
		}

		// if it is ok to leave in the old position, do it
		VectorSubtract (check->v.origin, move, check->v.origin);
		block = SV_TestEntityPosition (check);
		if (!block)
		{
			//if leaving it where it was, allow it to drop to the floor again (useful for plats that move downward)
			check->v.flags = (int)check->v.flags & ~FL_ONGROUND;

			num_moved--;
			continue;
		}

	// if it is still inside the pusher, block
		if (check->v.mins[0] == check->v.maxs[0])
		{
			SV_LinkEdict (check, false);
			continue;
		}
		if (check->v.solid == SOLID_NOT || check->v.solid == SOLID_TRIGGER)
		{	// corpse
			check->v.mins[0] = check->v.mins[1] = 0;
			VectorCopy (check->v.mins, check->v.maxs);
			SV_LinkEdict (check, false);
			continue;
		}

		VectorCopy (pushorig, pusher->v.origin);
		SV_LinkEdict (pusher, false);

		// if the pusher has a "blocked" function, call it
		// otherwise, just stay in place until the obstacle is gone
		if (pusher->v.blocked)
		{
			pr_global_struct->self = EDICT_TO_PROG(pusher);
			pr_global_struct->other = EDICT_TO_PROG(check);
#ifdef VM_Q1
			if (svs.gametype == GT_Q1QVM)
				Q1QVM_Blocked();
			else
#endif
				PR_ExecuteProgram (pusher->v.blocked);
		}

	// move back any entities we already moved
		for (i=0 ; i<num_moved ; i++)
		{
			VectorCopy (moved_from[i], moved_edict[i]->v.origin);
			SV_LinkEdict (moved_edict[i], false);
		}
		return false;
	}

	return true;
}
#else
qbool SV_Push (edict_t *pusher, vec3_t move, vec3_t amove)
{
	int			i, e;
	edict_t		*check, *block;
	vec3_t		mins, maxs;
	vec3_t		pushorig;
	int			num_moved;
	edict_t		*moved_edict[MAX_EDICTS];
	vec3_t		moved_from[MAX_EDICTS];
	float		solid_save;

	if (amove[0] || amove[1] || amove[2])
	{
		return SV_PushAngles(pusher, move, amove);
	}

	for (i=0 ; i<3 ; i++)
	{
		mins[i] = pusher->v.absmin[i] + move[i];
		maxs[i] = pusher->v.absmax[i] + move[i];
	}

	VectorCopy (pusher->v.origin, pushorig);

	// move the pusher to its final position

	VectorAdd (pusher->v.origin, move, pusher->v.origin);
	SV_LinkEdict (pusher, false);

	// see if any solid entities are inside the final position
	num_moved = 0;
	check = NEXT_EDICT(sv.edicts);
	for (e=1 ; e<sv.num_edicts ; e++, check = NEXT_EDICT(check))
	{
		if (check->free)
			continue;
		if (check->v.movetype == MOVETYPE_PUSH
		|| check->v.movetype == MOVETYPE_NONE
		|| check->v.movetype == MOVETYPE_NOCLIP)
			continue;

		solid_save = pusher->v.solid;
		pusher->v.solid = SOLID_NOT;
		block = SV_TestEntityPosition (check);
		pusher->v.solid = solid_save;
		if (block)
			continue;

		// if the entity is standing on the pusher, it will definately be moved
		if ( ! ( ((int)check->v.flags & FL_ONGROUND)
		&& PROG_TO_EDICT(check->v.groundentity) == pusher) )
		{
			if ( check->v.absmin[0] >= maxs[0]
			|| check->v.absmin[1] >= maxs[1]
			|| check->v.absmin[2] >= maxs[2]
			|| check->v.absmax[0] <= mins[0]
			|| check->v.absmax[1] <= mins[1]
			|| check->v.absmax[2] <= mins[2] )
				continue;

			// see if the ent's bbox is inside the pusher's final position
			if (!SV_TestEntityPosition (check))
				continue;
		}

		// remove the onground flag for non-players
		if (check->v.movetype != MOVETYPE_WALK)
			check->v.flags = (int)check->v.flags & ~FL_ONGROUND;

		VectorCopy (check->v.origin, moved_from[num_moved]);
		moved_edict[num_moved] = check;
		num_moved++;

		// try moving the contacted entity
		VectorAdd (check->v.origin, move, check->v.origin);
		block = SV_TestEntityPosition (check);
		if (!block)
		{	// pushed ok
			SV_LinkEdict (check, false);
			continue;
		}

		// if it is ok to leave in the old position, do it
		VectorSubtract (check->v.origin, move, check->v.origin);
		block = SV_TestEntityPosition (check);
		if (!block)
		{
			//if leaving it where it was, allow it to drop to the floor again (useful for plats that move downward)
			//check->v->flags = (int)check->v->flags & ~FL_ONGROUND; // disconnect: is it needed?

			num_moved--;
			continue;
		}

		// if it is still inside the pusher, block
		if (check->v.mins[0] == check->v.maxs[0])
		{
			SV_LinkEdict (check, false);
			continue;
		}
		if (check->v.solid == SOLID_NOT || check->v.solid == SOLID_TRIGGER)
		{	// corpse
			check->v.mins[0] = check->v.mins[1] = 0;
			VectorCopy (check->v.mins, check->v.maxs);
			SV_LinkEdict (check, false);
			continue;
		}

		VectorCopy (pushorig, pusher->v.origin);
		SV_LinkEdict (pusher, false);

		// if the pusher has a "blocked" function, call it
		// otherwise, just stay in place until the obstacle is gone
#ifdef USE_PR2
		if ( sv_vm )
		{
			pr_global_struct->self = EDICT_TO_PROG(pusher);
			pr_global_struct->other = EDICT_TO_PROG(check);
			PR2_EdictBlocked();
		}
		else
#endif
			if (pusher->v.blocked)
			{
				pr_global_struct->self = EDICT_TO_PROG(pusher);
				pr_global_struct->other = EDICT_TO_PROG(check);
				PR_ExecuteProgram (pusher->v.blocked);
			}

		// move back any entities we already moved
		for (i=0 ; i<num_moved ; i++)
		{
			VectorCopy (moved_from[i], moved_edict[i]->v.origin);
			SV_LinkEdict (moved_edict[i], false);
		}
		return false;
	}

	return true;
}
#endif
/*
============
SV_PushMove

============
*/
#ifdef mvd_pushmove
void SV_PushMove (edict_t *pusher, float movetime)
{
	int			i, e;
	edict_t		*check, *block;
	vec3_t		mins, maxs, move;
	vec3_t		entorig, pushorig;
	int			num_moved;
	edict_t		*moved_edict[MAX_EDICTS];
	vec3_t		moved_from[MAX_EDICTS];

	if (!pusher->v.velocity[0] && !pusher->v.velocity[1] && !pusher->v.velocity[2])
	{
		pusher->v.ltime += movetime;
		return;
	}

	for (i=0 ; i<3 ; i++)
	{
		move[i] = pusher->v.velocity[i] * movetime;
		mins[i] = pusher->v.absmin[i] + move[i];
		maxs[i] = pusher->v.absmax[i] + move[i];
	}

	VectorCopy (pusher->v.origin, pushorig);
	
// move the pusher to it's final position

	VectorAdd (pusher->v.origin, move, pusher->v.origin);
	pusher->v.ltime += movetime;
	SV_LinkEdict (pusher, false);


// see if any solid entities are inside the final position
	num_moved = 0;
	check = NEXT_EDICT(sv.edicts);
	for (e=1 ; e<sv.num_edicts ; e++, check = NEXT_EDICT(check))
	{
		if (check->free)
			continue;
		if (check->v.movetype == MOVETYPE_PUSH
		|| check->v.movetype == MOVETYPE_NONE
#ifdef QUAKE2
		|| check->v.movetype == MOVETYPE_FOLLOW
#endif
		|| check->v.movetype == MOVETYPE_NOCLIP)
			continue;

	// if the entity is standing on the pusher, it will definately be moved
		if ( ! ( ((int)check->v.flags & FL_ONGROUND)
		&& PROG_TO_EDICT(check->v.groundentity) == pusher) )
		{
			if ( check->v.absmin[0] >= maxs[0]
			|| check->v.absmin[1] >= maxs[1]
			|| check->v.absmin[2] >= maxs[2]
			|| check->v.absmax[0] <= mins[0]
			|| check->v.absmax[1] <= mins[1]
			|| check->v.absmax[2] <= mins[2] )
				continue;

		// see if the ent's bbox is inside the pusher's final position
			if (!SV_TestEntityPosition (check))
				continue;
		}

	// remove the onground flag for non-players
		if (check->v.movetype != MOVETYPE_WALK)
			check->v.flags = (int)check->v.flags & ~FL_ONGROUND;
		
		VectorCopy (check->v.origin, entorig);
		VectorCopy (check->v.origin, moved_from[num_moved]);
		moved_edict[num_moved] = check;
		num_moved++;

		// try moving the contacted entity 
		pusher->v.solid = SOLID_NOT;
		SV_PushEntity (check, move);
		pusher->v.solid = SOLID_BSP;

	// if it is still inside the pusher, block
		block = SV_TestEntityPosition (check);
		if (block)
		{	// fail the move
			if (check->v.mins[0] == check->v.maxs[0])
				continue;
			if (check->v.solid == SOLID_NOT || check->v.solid == SOLID_TRIGGER)
			{	// corpse
				check->v.mins[0] = check->v.mins[1] = 0;
				VectorCopy (check->v.mins, check->v.maxs);
				continue;
			}
			
			VectorCopy (entorig, check->v.origin);
			SV_LinkEdict (check, true);

			VectorCopy (pushorig, pusher->v.origin);
			SV_LinkEdict (pusher, false);
			pusher->v.ltime -= movetime;

			// if the pusher has a "blocked" function, call it
			// otherwise, just stay in place until the obstacle is gone
			if (pusher->v.blocked)
			{
				pr_global_struct->self = EDICT_TO_PROG(pusher);
				pr_global_struct->other = EDICT_TO_PROG(check);
				PR_ExecuteProgram (pusher->v.blocked);
			}
			
		// move back any entities we already moved
			for (i=0 ; i<num_moved ; i++)
			{
				VectorCopy (moved_from[i], moved_edict[i]->v.origin);
				SV_LinkEdict (moved_edict[i], false);
			}
			return;
		}	
	}
	/*
	int i;
	vec3_t move;

	if (!pusher->v.velocity[0] && !pusher->v.velocity[1] && !pusher->v.velocity[2])
	{
		pusher->v.ltime += movetime;
		return;
	}

	for (i=0 ; i<3 ; i++)
		move[i] = pusher->v.velocity[i] * movetime;

	if (SV_Push (pusher, move))
		pusher->v.ltime += movetime;
		*/
}
#else
void SV_PushMove (edict_t *pusher, float movetime)
{
	int			i;
	vec3_t		move;
	vec3_t		amove;

	if (!pusher->v.velocity[0] && !pusher->v.velocity[1] && !pusher->v.velocity[2]
		&& !pusher->v.avelocity[0] && !pusher->v.avelocity[1] && !pusher->v.avelocity[2])
	{
		pusher->v.ltime += movetime;
		return;
	}

	for (i=0 ; i<3 ; i++)
	{
		move[i] = pusher->v.velocity[i] * movetime;
		amove[i] = pusher->v.avelocity[i] * movetime;
	}

	if (SV_Push (pusher, move, amove))
		pusher->v.ltime += movetime;
}
#endif
/*
============
SV_PushRotate

============
*/
void SV_PushRotate (edict_t *pusher, float movetime)
{
	int			i, e;
	edict_t		*check, *block;
	vec3_t		move, a, amove;
	vec3_t		entorig, pushorig;
	int			num_moved;
	edict_t		*moved_edict[MAX_EDICTS];
	vec3_t		moved_from[MAX_EDICTS];
	vec3_t		org, org2;
	vec3_t		forward, right, up;

	if (!pusher->v.avelocity[0] && !pusher->v.avelocity[1] && !pusher->v.avelocity[2])
	{
		pusher->v.ltime += movetime;
		return;
	}

	for (i=0 ; i<3 ; i++)
		amove[i] = pusher->v.avelocity[i] * movetime;

	VectorSubtract (vec3_origin, amove, a);
	AngleVectors (a, forward, right, up);

	VectorCopy (pusher->v.angles, pushorig);
	
// move the pusher to it's final position

	VectorAdd (pusher->v.angles, amove, pusher->v.angles);
	pusher->v.ltime += movetime;
	SV_LinkEdict (pusher, false);


// see if any solid entities are inside the final position
	num_moved = 0;
	check = NEXT_EDICT(sv.edicts);
	for (e=1 ; e<sv.num_edicts ; e++, check = NEXT_EDICT(check))
	{
		if (check->free)
			continue;
		if (check->v.movetype == MOVETYPE_PUSH
		|| check->v.movetype == MOVETYPE_NONE
		//|| check->v.movetype == MOVETYPE_FOLLOW
		|| check->v.movetype == MOVETYPE_NOCLIP)
			continue;

	// if the entity is standing on the pusher, it will definately be moved
		if ( ! ( ((int)check->v.flags & FL_ONGROUND)
		&& PROG_TO_EDICT(check->v.groundentity) == pusher) )
		{
			if ( check->v.absmin[0] >= pusher->v.absmax[0]
			|| check->v.absmin[1] >= pusher->v.absmax[1]
			|| check->v.absmin[2] >= pusher->v.absmax[2]
			|| check->v.absmax[0] <= pusher->v.absmin[0]
			|| check->v.absmax[1] <= pusher->v.absmin[1]
			|| check->v.absmax[2] <= pusher->v.absmin[2] )
				continue;

		// see if the ent's bbox is inside the pusher's final position
			if (!SV_TestEntityPosition (check))
				continue;
		}

	// remove the onground flag for non-players
		if (check->v.movetype != MOVETYPE_WALK)
			check->v.flags = (int)check->v.flags & ~FL_ONGROUND;
		
		VectorCopy (check->v.origin, entorig);
		VectorCopy (check->v.origin, moved_from[num_moved]);
		moved_edict[num_moved] = check;
		num_moved++;

		// calculate destination position
		VectorSubtract (check->v.origin, pusher->v.origin, org);
		org2[0] = DotProduct (org, forward);
		org2[1] = -DotProduct (org, right);
		org2[2] = DotProduct (org, up);
		VectorSubtract (org2, org, move);

		// try moving the contacted entity 
		pusher->v.solid = SOLID_NOT;
		SV_PushEntity (check, move);
		pusher->v.solid = SOLID_BSP;

	// if it is still inside the pusher, block
		block = SV_TestEntityPosition (check);
		if (block)
		{	// fail the move
			if (check->v.mins[0] == check->v.maxs[0])
				continue;
			if (check->v.solid == SOLID_NOT || check->v.solid == SOLID_TRIGGER)
			{	// corpse
				check->v.mins[0] = check->v.mins[1] = 0;
				VectorCopy (check->v.mins, check->v.maxs);
				continue;
			}
			
			VectorCopy (entorig, check->v.origin);
			SV_LinkEdict (check, true);

			VectorCopy (pushorig, pusher->v.angles);
			SV_LinkEdict (pusher, false);
			pusher->v.ltime -= movetime;

			// if the pusher has a "blocked" function, call it
			// otherwise, just stay in place until the obstacle is gone
			if (pusher->v.blocked)
			{
				pr_global_struct->self = EDICT_TO_PROG(pusher);
				pr_global_struct->other = EDICT_TO_PROG(check);
				PR_ExecuteProgram (pusher->v.blocked);
			}
			
		// move back any entities we already moved
			for (i=0 ; i<num_moved ; i++)
			{
				VectorCopy (moved_from[i], moved_edict[i]->v.origin);
				VectorSubtract (moved_edict[i]->v.angles, amove, moved_edict[i]->v.angles);
				SV_LinkEdict (moved_edict[i], false);
			}
			return;
		}
		else
		{
			VectorAdd (check->v.angles, amove, check->v.angles);
		}
	}

	
}

/*
================
SV_Physics_Pusher

================
*/
void SV_Physics_Pusher (edict_t *ent)
{
	float	thinktime;
	float	oldltime;
	float	movetime;

	oldltime = ent->v.ltime;
	
	thinktime = ent->v.nextthink;
	if (thinktime < ent->v.ltime + sv_frametime)
	{
		movetime = thinktime - ent->v.ltime;
		if (movetime < 0)
			movetime = 0;
	}
	else
		movetime = sv_frametime;

	if (movetime)
	{
//#ifdef QUAKE2
		/*
		if (ent->v.avelocity[0] || ent->v.avelocity[1] || ent->v.avelocity[2])
			SV_PushRotate (ent, sv_frametime);
		else
		*/
		// use FTE's SV_PushMove code instead:
//#endif
			SV_PushMove (ent, movetime);	// advances ent->v.ltime if not blocked
	}
		
	if (thinktime > oldltime && thinktime <= ent->v.ltime)
	{
		ent->v.nextthink = 0;
		pr_global_struct->time = sv.time;
		pr_global_struct->self = EDICT_TO_PROG(ent);
		pr_global_struct->other = EDICT_TO_PROG(sv.edicts);
		PR_ExecuteProgram (ent->v.think);
		if (ent->free)
			return;
	}

}


/*
=============
SV_Physics_None

Non moving objects can only think
=============
*/
void SV_Physics_None (edict_t *ent)
{
	// regular thinking
	SV_RunThink (ent);
}

/*
=============
SV_Physics_Noclip

A moving object that doesn't obey physics
=============
*/
void SV_Physics_Noclip (edict_t *ent)
{
	// regular thinking
	if (!SV_RunThink (ent))
		return;

	VectorMA (ent->v.angles, sv_frametime, ent->v.avelocity, ent->v.angles);
	VectorMA (ent->v.origin, sv_frametime, ent->v.velocity, ent->v.origin);

	SV_LinkEdict (ent, false);
}

/*
==============================================================================

TOSS / BOUNCE

==============================================================================
*/

/*
=============
SV_CheckWaterTransition

=============
*/
void SV_CheckWaterTransition (edict_t *ent)
{
	int cont;

	cont = SV_PointContents (ent->v.origin);
	if (!ent->v.watertype)
	{	// just spawned here
		ent->v.watertype = cont;
		ent->v.waterlevel = 1;
		return;
	}

	if (cont <= CONTENTS_WATER)
	{
		if (ent->v.watertype == CONTENTS_EMPTY)
		{	// just crossed into water
			SV_StartSound (ent, 0, "misc/h2ohit1.wav", 255, 1);
		}
		ent->v.watertype = cont;
		ent->v.waterlevel = 1;
	}
	else
	{
		if (ent->v.watertype != CONTENTS_EMPTY)
		{	// just crossed into water
			SV_StartSound (ent, 0, "misc/h2ohit1.wav", 255, 1);
		}
		ent->v.watertype = CONTENTS_EMPTY;
		ent->v.waterlevel = cont;
	}
}

/*
=============
SV_Physics_Toss

Toss, bounce, and fly movement.  When onground, do nothing.
=============
*/
void SV_Physics_Toss (edict_t *ent)
{
#ifndef MVDSV_MOVEMENT
	trace_t	trace;
	vec3_t	move;
	float	backoff;

	// regular thinking
	if (!SV_RunThink (ent))
		return;

	if (ent->v.velocity[2] > 0)
		ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;

	// if onground, return without moving
	if ( ((int)ent->v.flags & FL_ONGROUND) )
		return;

	SV_CheckVelocity (ent);

	// add gravity
	if (ent->v.movetype != MOVETYPE_FLY
	&& ent->v.movetype != MOVETYPE_FLYMISSILE)
		SV_AddGravity (ent, 1.0);

	// move angles
	VectorMA (ent->v.angles, sv_frametime, ent->v.avelocity, ent->v.angles);

	// move origin
	VectorScale (ent->v.velocity, sv_frametime, move);
	trace = SV_PushEntity (ent, move);
	if (trace.fraction == 1)
		return;
	if (ent->free)
		return;

	if (ent->v.movetype == MOVETYPE_BOUNCE)
		backoff = 1.5;
	else
		backoff = 1;

	ClipVelocity (ent->v.velocity, trace.plane.normal, ent->v.velocity, backoff);

	// stop if on ground
	if (trace.plane.normal[2] > 0.7)
	{
		if (ent->v.velocity[2] < 60 || ent->v.movetype != MOVETYPE_BOUNCE )
		{
			ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
			ent->v.groundentity = EDICT_TO_PROG(trace.e.ent);
			VectorClear (ent->v.velocity);
			VectorClear (ent->v.avelocity);
		}
	}

	// check for in water
	SV_CheckWaterTransition (ent);
#else
	trace_t	trace;
	vec3_t	move;
	float	backoff;

// regular thinking
	if (!SV_RunThink (ent))
		return;

	if (ent->v.velocity[2] > 0)
		ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;

// if onground, return without moving
	if ( ((int)ent->v.flags & FL_ONGROUND) )
		return;

	SV_CheckVelocity (ent);

// add gravity
	if (ent->v.movetype != MOVETYPE_FLY
	&& ent->v.movetype != MOVETYPE_FLYMISSILE)
		SV_AddGravity (ent, 1.0);

// move angles
	VectorMA (ent->v.angles, sv_frametime, ent->v.avelocity, ent->v.angles);
	//VectorMA (ent->v.angles, host_frametime, ent->v.avelocity, ent->v.angles);

// move origin
	VectorScale (ent->v.velocity, sv_frametime, move);
	trace = SV_PushEntity (ent, move);
	if (trace.fraction == 1)
		return;
	if (ent->free)
		return;
	
	if (ent->v.movetype == MOVETYPE_BOUNCE)
		backoff = 1.5;
	else
		backoff = 1;

	ClipVelocity (ent->v.velocity, trace.plane.normal, ent->v.velocity, backoff);

// stop if on ground
	if (trace.plane.normal[2] > 0.7)
	{		
		if (ent->v.velocity[2] < 60 || ent->v.movetype != MOVETYPE_BOUNCE )
		{
			ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
			ent->v.groundentity = EDICT_TO_PROG(trace.e.ent);
			VectorCopy (vec3_origin, ent->v.velocity);
			VectorCopy (vec3_origin, ent->v.avelocity);
		}
	}
	
// check for in water
	SV_CheckWaterTransition (ent);
#endif
}

/*
===============================================================================

STEPPING MOVEMENT

===============================================================================
*/

/*
=============
SV_Physics_Step

Monsters freefall when they don't have a ground entity, otherwise
all movement is done with discrete steps.

This is also used for objects that have become still on the ground, but
will fall if the floor is pulled out from under them.
FIXME: is this true?
=============
*/
void SV_Physics_Step (edict_t *ent)
{
	qbool hitsound;

	// frefall if not onground
	if ( ! ((int)ent->v.flags & (FL_ONGROUND | FL_FLY | FL_SWIM) ) )
	{
		if (ent->v.velocity[2] < movevars.gravity*-0.1)
			hitsound = true;
		else
			hitsound = false;

		SV_AddGravity (ent, 1.0);
		SV_CheckVelocity (ent);
		// Tonik: the check for SOLID_NOT is to fix the way dead bodies and
		// gibs behave (should not be blocked by players & monsters);
		// The SOLID_TRIGGER check is disabled lest we break frikbots
		if (ent->v.solid == SOLID_NOT /* || ent->v.solid == SOLID_TRIGGER*/)
			SV_FlyMove (ent, sv_frametime, NULL, MOVE_NOMONSTERS);
		else
			SV_FlyMove (ent, sv_frametime, NULL, MOVE_NORMAL);
		SV_LinkEdict (ent, true);

		if ( (int)ent->v.flags & FL_ONGROUND ) // just hit ground
		{
			if (hitsound)
				SV_StartSound (ent, 0, "demon/dland2.wav", 255, 1);
		}
	}

	// regular thinking
	SV_RunThink (ent);

	SV_CheckWaterTransition (ent);
}

//============================================================================

void SV_ProgStartFrame (void)
{
	// let the progs know that a new frame has started
	pr_global_struct->self = EDICT_TO_PROG(sv.edicts);
	pr_global_struct->other = EDICT_TO_PROG(sv.edicts);
	pr_global_struct->time = sv.time;
#ifdef USE_PR2
	if ( sv_vm )
		PR2_GameStartFrame();
	else
#endif
		PR_ExecuteProgram (pr_global_struct->StartFrame);
}

/*
================
SV_RunEntity
================
*/
void SV_RunEntity (edict_t *ent)
{
	if (ent->lastruntime == sv.time)
		return;
	ent->lastruntime = sv.time;

	switch ((int)ent->v.movetype)
	{
	case MOVETYPE_PUSH:
		SV_Physics_Pusher (ent);
		break;
	case MOVETYPE_NONE:
		SV_Physics_None (ent);
		break;
	case MOVETYPE_NOCLIP:
		SV_Physics_Noclip (ent);
		break;
	case MOVETYPE_STEP:
		SV_Physics_Step (ent);
		break;
	case MOVETYPE_TOSS:
	case MOVETYPE_BOUNCE:
	case MOVETYPE_FLY:
	case MOVETYPE_FLYMISSILE:
		SV_Physics_Toss (ent);
		break;
	default:
		SV_Error ("SV_Physics: bad movetype %i", (int)ent->v.movetype);
	}
}

/*
================
SV_RunNewmis
================
*/
void SV_RunNewmis (void)
{
	edict_t	*ent;
	double save_frametime;

	if (!pr_global_struct->newmis)
		return;

	ent = PROG_TO_EDICT(pr_global_struct->newmis);
	pr_global_struct->newmis = 0;

	save_frametime = sv_frametime;
	sv_frametime = 0.05;

	SV_RunEntity (ent);

	sv_frametime = save_frametime;
}

/*
================
SV_Physics
================
*/
#ifdef USE_PR2
void SV_PreRunCmd(void);
void SV_RunCmd (usercmd_t *ucmd, qbool inside);
void SV_PostRunCmd(void);
#endif
void SV_Physics (void)
{
	int i;
	edict_t *ent;
#ifdef USE_PR2
	client_t *cl,*savehc;
	edict_t *savesvpl;
#endif

	if (sv.state != ss_active)
		return;

	if (sv.old_time)
	{
		// don't bother running a frame if sv_mintic seconds haven't passed
		sv_frametime = sv.time - sv.old_time;
		if (sv_frametime < (double) sv_mintic.value)
			return;
		if (sv_frametime > (double) sv_maxtic.value)
			sv_frametime = (double) sv_maxtic.value;
		sv.old_time = sv.time;
	}
	else
		sv_frametime = 0.1; // initialization frame

	pr_global_struct->frametime = sv_frametime;

	SV_ProgStartFrame ();

	//
	// treat each object in turn
	// even the world gets a chance to think
	//
	ent = sv.edicts;
	for (i=0 ; i<sv.num_edicts ; i++, ent = NEXT_EDICT(ent))
	{
		if (ent->free)
			continue;

		if (pr_global_struct->force_retouch)
			SV_LinkEdict (ent, true);	// force retouch even for stationary

		if (i > 0 && i <= MAX_CLIENTS)
			continue;		// clients are run directly from packets

		SV_RunEntity (ent);
		SV_RunNewmis ();
	}

	if (pr_global_struct->force_retouch)
		pr_global_struct->force_retouch--;

#ifdef USE_PR2
	savesvpl = sv_player;
	savehc = sv_client;

	// so spec will have right goalentity - if speccing someone
	// qqshka {
	if ( sv_vm ) // don't fix .qc based mods
		for ( i = 0, cl = svs.clients; i < MAX_CLIENTS; i++, cl++ )
		{
			if ( cl->state == cs_free )
				continue;

			sv_client = cl;
			ent = cl->edict;

			if( sv_client->spectator && sv_client->spec_track > 0 )
				ent->v.goalentity = EDICT_TO_PROG(svs.clients[sv_client->spec_track-1].edict);
		}
	// }

	for ( i = 0, cl = svs.clients; i < MAX_CLIENTS; i++, cl++ )
	{
		if ( cl->state == cs_free )
			continue;
		if ( !cl->isBot )
			continue;

		sv_client = cl;
		sv_player = cl->edict;

		SV_PreRunCmd();
		SV_RunCmd (&cl->botcmd, false);
		SV_PostRunCmd();

		cl->lastcmd = cl->botcmd;
		cl->lastcmd.buttons = 0;

		memset(&cl->botcmd,0,sizeof(cl->botcmd));

		cl->localtime = sv.time;
		cl->delta_sequence = -1;	// no delta unless requested
	}
	sv_player = savesvpl;
	sv_client = savehc;
#endif
}

void SV_SetMoveVars(void)
{
	movevars.gravity		= sv_gravity.value;
	movevars.stopspeed		= sv_stopspeed.value;
	movevars.maxspeed		= sv_maxspeed.value;
	movevars.spectatormaxspeed	= sv_spectatormaxspeed.value;
	movevars.accelerate		= sv_accelerate.value;
	movevars.airaccelerate		= sv_airaccelerate.value;
	movevars.wateraccelerate	= sv_wateraccelerate.value;
	movevars.friction		= sv_friction.value;
	movevars.waterfriction		= sv_waterfriction.value;
	movevars.entgravity		= 1.0;
}

#else
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

	$Id: sv_phys.c,v 1.19 2007/02/13 14:18:16 tonik Exp $
*/
// sv_phys.c

#include "qwsvdef.h"


/*


pushmove objects do not obey gravity, and do not interact with each other or trigger fields, but block normal movement and push normal objects when they move.

onground is set for toss objects when they come to a complete rest.  it is set for steping or walking objects 

doors, plats, etc are SOLID_BSP, and MOVETYPE_PUSH
bonus items are SOLID_TRIGGER touch, and MOVETYPE_TOSS
corpses are SOLID_NOT and MOVETYPE_TOSS
crates are SOLID_BBOX and MOVETYPE_TOSS
walking monsters are SOLID_SLIDEBOX and MOVETYPE_STEP
flying/floating monsters are SOLID_SLIDEBOX and MOVETYPE_FLY

solid_edge items only clip against bsp models.

*/

cvar_t	sv_maxvelocity		= { "sv_maxvelocity","2000"};

cvar_t	sv_gravity		= { "sv_gravity", "800"};
cvar_t	sv_stopspeed		= { "sv_stopspeed", "100"};
cvar_t	sv_maxspeed		= { "sv_maxspeed", "320"};
cvar_t	sv_spectatormaxspeed 	= { "sv_spectatormaxspeed", "500"};
cvar_t	sv_accelerate		= { "sv_accelerate", "10"};
cvar_t	sv_airaccelerate	= { "sv_airaccelerate", "10"};

cvar_t	sv_wateraccelerate	= { "sv_wateraccelerate", "10"};
cvar_t	sv_friction		= { "sv_friction", "4"};
cvar_t	sv_waterfriction	= { "sv_waterfriction", "4"};
cvar_t	pm_ktjump		= { "pm_ktjump", "1", CVAR_SERVERINFO};
cvar_t	pm_bunnyspeedcap	= { "pm_bunnyspeedcap", "", CVAR_SERVERINFO};
cvar_t	pm_slidefix		= { "pm_slidefix", "", CVAR_SERVERINFO};
cvar_t	pm_airstep		= { "pm_airstep", "", CVAR_SERVERINFO};
cvar_t	pm_pground		= { "pm_pground", "", CVAR_SERVERINFO};

double	sv_frametime;


void SV_Physics_Toss (edict_t *ent);


/*
================
SV_CheckVelocity
================
*/
void SV_CheckVelocity (edict_t *ent)
{
	int i;
	float wishspeed;

	//
	// bound velocity
	//
	for (i=0 ; i<3 ; i++)
	{
		if (IS_NAN(ent->v.velocity[i]))
		{
			Con_DPrintf ("Got a NaN velocity on %s\n",
#ifdef USE_PR2
			             PR2_GetString(ent->v.classname)
#else
			             PR_GetString(ent->v.classname)
#endif
			            );
			ent->v.velocity[i] = 0;
		}
		if (IS_NAN(ent->v.origin[i]))
		{
			Con_DPrintf ("Got a NaN origin on %s\n",
#ifdef USE_PR2
			             PR2_GetString(ent->v.classname)
#else
			             PR_GetString(ent->v.classname)
#endif
			            );
			ent->v.origin[i] = 0;
		}
/*		if (ent->v.velocity[i] > sv_maxvelocity.value)
			ent->v.velocity[i] = sv_maxvelocity.value;
		else if (ent->v.velocity[i] < -sv_maxvelocity.value)
			ent->v.velocity[i] = -sv_maxvelocity.value;
*/
	}

	// SV_MAXVELOCITY fix by Maddes
	wishspeed = VectorLength(ent->v.velocity);
	if (wishspeed > sv_maxvelocity.value)
	{
		VectorScale (ent->v.velocity, sv_maxvelocity.value/wishspeed, ent->v.velocity);
		wishspeed = sv_maxvelocity.value;
	}
}

/*
=============
SV_RunThink

Runs thinking code if time.  There is some play in the exact time the think
function will be called, because it is called before any movement is done
in a frame.  Not used for pushmove objects, because they must be exact.
Returns false if the entity removed itself.
=============
*/
qbool SV_RunThink (edict_t *ent)
{
	float thinktime;

	do
	{
		thinktime = ent->v.nextthink;
		if (thinktime <= 0)
			return true;
		if (thinktime > sv.time + sv_frametime)
			return true;

		if (thinktime < sv.time)
			thinktime = sv.time; // don't let things stay in the past.
		// it is possible to start that way
		// by a trigger with a local time.
		ent->v.nextthink = 0;
		pr_global_struct->time = thinktime;
		pr_global_struct->self = EDICT_TO_PROG(ent);
		pr_global_struct->other = EDICT_TO_PROG(sv.edicts);
#ifdef USE_PR2
		if ( sv_vm )
			PR2_EdictThink();
		else
#endif
			PR_ExecuteProgram (ent->v.think);

		if (ent->free)
			return false;

		if (ent->v.nextthink <= thinktime)	//hmm... infinate loop was possible here.. Quite a few non-QW mods do this.
			return true;
	} while (1);

	return true;
}

/*
==================
SV_Impact

Two entities have touched, so run their touch functions
==================
*/
void SV_Impact (edict_t *e1, edict_t *e2)
{
	int old_self, old_other;

	old_self = pr_global_struct->self;
	old_other = pr_global_struct->other;

	pr_global_struct->time = sv.time;
	if (e1->v.touch && e1->v.solid != SOLID_NOT)
	{
		pr_global_struct->self = EDICT_TO_PROG(e1);
		pr_global_struct->other = EDICT_TO_PROG(e2);
#ifdef USE_PR2
		if ( sv_vm )
			PR2_EdictTouch();
		else
#endif
			PR_ExecuteProgram (e1->v.touch);
	}

	if (e2->v.touch && e2->v.solid != SOLID_NOT)
	{
		pr_global_struct->self = EDICT_TO_PROG(e2);
		pr_global_struct->other = EDICT_TO_PROG(e1);
#ifdef USE_PR2
		if( sv_vm )
			PR2_EdictTouch();
		else
#endif
			PR_ExecuteProgram (e2->v.touch);
	}

	pr_global_struct->self = old_self;
	pr_global_struct->other = old_other;
}


/*
==================
ClipVelocity

Slide off of the impacting object
==================
*/
#define	STOP_EPSILON	0.1

// xavior: fixme: add def
#ifdef MVDSV_MOVEMENT
void ClipVelocity (vec3_t in, vec3_t normal, vec3_t out, float overbounce)
{
	float backoff;
	float change;
	int i;


	backoff = DotProduct (in, normal) * overbounce;

	for (i=0 ; i<3 ; i++)
	{
		change = normal[i]*backoff;
		out[i] = in[i] - change;
		if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON)
			out[i] = 0;
	}
}
#else
#define	STOP_EPSILON	0.1

int ClipVelocity (vec3_t in, vec3_t normal, vec3_t out, float overbounce)
{
	float	backoff;
	float	change;
	int		i, blocked;
	
	blocked = 0;
	if (normal[2] > 0)
		blocked |= 1;		// floor
	if (!normal[2])
		blocked |= 2;		// step
	
	backoff = DotProduct (in, normal) * overbounce;

	for (i=0 ; i<3 ; i++)
	{
		change = normal[i]*backoff;
		out[i] = in[i] - change;
		if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON)
			out[i] = 0;
	}
	
	return blocked;
}
#endif


/*
============
SV_FlyMove

The basic solid body movement clip that slides along multiple planes
Returns the clipflags if the velocity was modified (hit something solid)
1 = floor
2 = wall / step
4 = dead stop
If steptrace is not NULL, the trace of any vertical wall hit will be stored
============
*/
#define MAX_CLIP_PLANES	5
int SV_FlyMove (edict_t *ent, float time1, trace_t *steptrace, int type)
{
#ifdef MVDSV_MOVEMENT
	int		bumpcount, numbumps;
	vec3_t	dir;
	float	d;
	int		numplanes;
	vec3_t	planes[MAX_CLIP_PLANES];
	vec3_t	primal_velocity, original_velocity, new_velocity;
	int		i, j;
	trace_t	trace;
	vec3_t	end;
	float	time_left;
	int		blocked;

	numbumps = 4;

	blocked = 0;
	VectorCopy (ent->v.velocity, original_velocity);
	VectorCopy (ent->v.velocity, primal_velocity);
	numplanes = 0;

	time_left = time1;

	for (bumpcount=0 ; bumpcount<numbumps ; bumpcount++)
	{
		for (i=0 ; i<3 ; i++)
			end[i] = ent->v.origin[i] + time_left * ent->v.velocity[i];

		trace = SV_Trace (ent->v.origin, ent->v.mins, ent->v.maxs, end, type, ent);

		if (trace.allsolid)
		{	// entity is trapped in another solid
			VectorClear (ent->v.velocity);
			return 3;
		}

		if (trace.fraction > 0)
		{	// actually covered some distance
			VectorCopy (trace.endpos, ent->v.origin);
			VectorCopy (ent->v.velocity, original_velocity);
			numplanes = 0;
		}

		if (trace.fraction == 1)
			break; // moved the entire distance

		if (!trace.e.ent)
			SV_Error ("SV_FlyMove: !trace.e.ent");

		if (trace.plane.normal[2] > 0.7)
		{
			blocked |= 1; // floor
			if (trace.e.ent->v.solid == SOLID_BSP)
			{
				ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
				ent->v.groundentity = EDICT_TO_PROG(trace.e.ent);
			}
		}
		if (!trace.plane.normal[2])
		{
			blocked |= 2; // step
			if (steptrace)
				*steptrace = trace; // save for player extrafriction
		}

		//
		// run the impact function
		//
		SV_Impact (ent, trace.e.ent);
		if (ent->free)
			break;	 // removed by the impact function


		time_left -= time_left * trace.fraction;

		// cliped to another plane
		if (numplanes >= MAX_CLIP_PLANES)
		{	// this shouldn't really happen
			VectorClear (ent->v.velocity);
			return 3;
		}

		VectorCopy (trace.plane.normal, planes[numplanes]);
		numplanes++;

		//
		// modify original_velocity so it parallels all of the clip planes
		//
		for (i=0 ; i<numplanes ; i++)
		{
			ClipVelocity (original_velocity, planes[i], new_velocity, 1);
			for (j=0 ; j<numplanes ; j++)
				if (j != i)
				{
					if (DotProduct (new_velocity, planes[j]) < 0)
						break; // not ok
				}
			if (j == numplanes)
				break;
		}

		if (i != numplanes)
		{	// go along this plane
			VectorCopy (new_velocity, ent->v.velocity);
		}
		else
		{	// go along the crease
			if (numplanes != 2)
			{
				// Con_Printf ("clip velocity, numplanes == %i\n",numplanes);
				VectorClear (ent->v.velocity);
				return 7;
			}
			CrossProduct (planes[0], planes[1], dir);
			d = DotProduct (dir, ent->v.velocity);
			VectorScale (dir, d, ent->v.velocity);
		}

		//
		// if original velocity is against the original velocity, stop dead
		// to avoid tiny occilations in sloping corners
		//
		if (DotProduct (ent->v.velocity, primal_velocity) <= 0)
		{
			VectorClear (ent->v.velocity);
			return blocked;
		}
	}

	return blocked;
#else
	int			bumpcount, numbumps;
	vec3_t		dir;
	float		d;
	int			numplanes;
	vec3_t		planes[MAX_CLIP_PLANES];
	vec3_t		primal_velocity, original_velocity, new_velocity;
	int			i, j;
	trace_t		trace;
	vec3_t		end;
	float		time_left;
	int			blocked;
	
	numbumps = 4;
	
	blocked = 0;
	VectorCopy (ent->v.velocity, original_velocity);
	VectorCopy (ent->v.velocity, primal_velocity);
	numplanes = 0;
	
	time_left = time1;

	for (bumpcount=0 ; bumpcount<numbumps ; bumpcount++)
	{
		for (i=0 ; i<3 ; i++)
			end[i] = ent->v.origin[i] + time_left * ent->v.velocity[i];

//		trace = SV_Move (ent->v.origin, ent->v.mins, ent->v.maxs, end, false, ent);
		trace = SV_Trace (ent->v.origin, ent->v.mins, ent->v.maxs, end, type, ent);

		if (trace.allsolid)
		{	// entity is trapped in another solid
			VectorCopy (vec3_origin, ent->v.velocity);
			return 3;
		}

		if (trace.fraction > 0)
		{	// actually covered some distance
			VectorCopy (trace.endpos, ent->v.origin);
			VectorCopy (ent->v.velocity, original_velocity);
			numplanes = 0;
		}

		if (trace.fraction == 1)
			 break;		// moved the entire distance

		if (!trace.e.ent)
			SV_Error ("SV_FlyMove: !trace.ent");

		if (trace.plane.normal[2] > 0.7)
		{
			blocked |= 1;		// floor
			if (trace.e.ent->v.solid == SOLID_BSP)
			{
				ent->v.flags =	(int)ent->v.flags | FL_ONGROUND;
				ent->v.groundentity = EDICT_TO_PROG(trace.e.ent);
			}
		}
		if (!trace.plane.normal[2])
		{
			blocked |= 2;		// step
			if (steptrace)
				*steptrace = trace;	// save for player extrafriction
		}

//
// run the impact function
//
		SV_Impact (ent, trace.e.ent);
		if (ent->free)
			break;		// removed by the impact function

		
		time_left -= time_left * trace.fraction;
		
	// cliped to another plane
		if (numplanes >= MAX_CLIP_PLANES)
		{	// this shouldn't really happen
			VectorCopy (vec3_origin, ent->v.velocity);
			return 3;
		}

		VectorCopy (trace.plane.normal, planes[numplanes]);
		numplanes++;

//
// modify original_velocity so it parallels all of the clip planes
//
		for (i=0 ; i<numplanes ; i++)
		{
			ClipVelocity (original_velocity, planes[i], new_velocity, 1);
			for (j=0 ; j<numplanes ; j++)
				if (j != i)
				{
					if (DotProduct (new_velocity, planes[j]) < 0)
						break;	// not ok
				}
			if (j == numplanes)
				break;
		}
		
		if (i != numplanes)
		{	// go along this plane
			VectorCopy (new_velocity, ent->v.velocity);
		}
		else
		{	// go along the crease
			if (numplanes != 2)
			{
//				Con_Printf ("clip velocity, numplanes == %i\n",numplanes);
				VectorCopy (vec3_origin, ent->v.velocity);
				return 7;
			}
			CrossProduct (planes[0], planes[1], dir);
			d = DotProduct (dir, ent->v.velocity);
			VectorScale (dir, d, ent->v.velocity);
		}

//
// if original velocity is against the original velocity, stop dead
// to avoid tiny occilations in sloping corners
//
		if (DotProduct (ent->v.velocity, primal_velocity) <= 0)
		{
			VectorCopy (vec3_origin, ent->v.velocity);
			return blocked;
		}
	}

	return blocked;
#endif
}


/*
============
SV_AddGravity
============
*/
void SV_AddGravity (edict_t *ent, float scale)
{
	ent->v.velocity[2] -= scale * movevars.gravity * sv_frametime;
}

/*
===============================================================================

PUSHMOVE

===============================================================================
*/

/*
============
SV_PushEntity

Does not change the entities velocity at all
============
*/
trace_t SV_PushEntity (edict_t *ent, vec3_t push)
{
	trace_t	trace;
	vec3_t	end;

	VectorAdd (ent->v.origin, push, end);

	if (ent->v.movetype == MOVETYPE_FLYMISSILE)
		trace = SV_Trace (ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_MISSILE, ent);
	else if (ent->v.solid == SOLID_TRIGGER || ent->v.solid == SOLID_NOT)
		// only clip against bmodels
		trace = SV_Trace (ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_NOMONSTERS, ent);
	else
		trace = SV_Trace (ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_NORMAL, ent);

	VectorCopy (trace.endpos, ent->v.origin);
	SV_LinkEdict (ent, true);

	if (trace.e.ent)
		SV_Impact (ent, trace.e.ent);

	return trace;
}


/*
============
SV_Push

============
*/
qbool SV_Push (edict_t *pusher, vec3_t move)
{
	int			i, e;
	edict_t		*check, *block;
	vec3_t		mins, maxs;
	vec3_t		pushorig;
	int			num_moved;
	edict_t		*moved_edict[MAX_EDICTS];
	vec3_t		moved_from[MAX_EDICTS];
	float		solid_save;

	for (i=0 ; i<3 ; i++)
	{
		mins[i] = pusher->v.absmin[i] + move[i];
		maxs[i] = pusher->v.absmax[i] + move[i];
	}

	VectorCopy (pusher->v.origin, pushorig);

	// move the pusher to its final position

	VectorAdd (pusher->v.origin, move, pusher->v.origin);
	SV_LinkEdict (pusher, false);

	// see if any solid entities are inside the final position
	num_moved = 0;
	check = NEXT_EDICT(sv.edicts);
	for (e=1 ; e<sv.num_edicts ; e++, check = NEXT_EDICT(check))
	{
		if (check->free)
			continue;
		if (check->v.movetype == MOVETYPE_PUSH
		|| check->v.movetype == MOVETYPE_NONE
		|| check->v.movetype == MOVETYPE_NOCLIP)
			continue;

		solid_save = pusher->v.solid;
		pusher->v.solid = SOLID_NOT;
		block = SV_TestEntityPosition (check);
		pusher->v.solid = solid_save;
		if (block)
			continue;

		// if the entity is standing on the pusher, it will definately be moved
		if ( ! ( ((int)check->v.flags & FL_ONGROUND)
		&& PROG_TO_EDICT(check->v.groundentity) == pusher) )
		{
			if ( check->v.absmin[0] >= maxs[0]
			|| check->v.absmin[1] >= maxs[1]
			|| check->v.absmin[2] >= maxs[2]
			|| check->v.absmax[0] <= mins[0]
			|| check->v.absmax[1] <= mins[1]
			|| check->v.absmax[2] <= mins[2] )
				continue;

			// see if the ent's bbox is inside the pusher's final position
			if (!SV_TestEntityPosition (check))
				continue;
		}

		// remove the onground flag for non-players
		if (check->v.movetype != MOVETYPE_WALK)
			check->v.flags = (int)check->v.flags & ~FL_ONGROUND;

		VectorCopy (check->v.origin, moved_from[num_moved]);
		moved_edict[num_moved] = check;
		num_moved++;

		// try moving the contacted entity
		VectorAdd (check->v.origin, move, check->v.origin);
		block = SV_TestEntityPosition (check);
		if (!block)
		{	// pushed ok
			SV_LinkEdict (check, false);
			continue;
		}

		// if it is ok to leave in the old position, do it
		VectorSubtract (check->v.origin, move, check->v.origin);
		block = SV_TestEntityPosition (check);
		if (!block)
		{
			//if leaving it where it was, allow it to drop to the floor again (useful for plats that move downward)
			//check->v->flags = (int)check->v->flags & ~FL_ONGROUND; // disconnect: is it needed?

			num_moved--;
			continue;
		}

		// if it is still inside the pusher, block
		if (check->v.mins[0] == check->v.maxs[0])
		{
			SV_LinkEdict (check, false);
			continue;
		}
		if (check->v.solid == SOLID_NOT || check->v.solid == SOLID_TRIGGER)
		{	// corpse
			check->v.mins[0] = check->v.mins[1] = 0;
			VectorCopy (check->v.mins, check->v.maxs);
			SV_LinkEdict (check, false);
			continue;
		}

		VectorCopy (pushorig, pusher->v.origin);
		SV_LinkEdict (pusher, false);

		// if the pusher has a "blocked" function, call it
		// otherwise, just stay in place until the obstacle is gone
#ifdef USE_PR2
		if ( sv_vm )
		{
			pr_global_struct->self = EDICT_TO_PROG(pusher);
			pr_global_struct->other = EDICT_TO_PROG(check);
			PR2_EdictBlocked();
		}
		else
#endif
			if (pusher->v.blocked)
			{
				pr_global_struct->self = EDICT_TO_PROG(pusher);
				pr_global_struct->other = EDICT_TO_PROG(check);
				PR_ExecuteProgram (pusher->v.blocked);
			}

		// move back any entities we already moved
		for (i=0 ; i<num_moved ; i++)
		{
			VectorCopy (moved_from[i], moved_edict[i]->v.origin);
			SV_LinkEdict (moved_edict[i], false);
		}
		return false;
	}

	return true;
}

/*
============
SV_PushMove

============
*/
void SV_PushMove (edict_t *pusher, float movetime)
{
	int i;
	vec3_t move;

	if (!pusher->v.velocity[0] && !pusher->v.velocity[1] && !pusher->v.velocity[2])
	{
		pusher->v.ltime += movetime;
		return;
	}

	for (i=0 ; i<3 ; i++)
		move[i] = pusher->v.velocity[i] * movetime;

	if (SV_Push (pusher, move))
		pusher->v.ltime += movetime;
}


/*
================
SV_Physics_Pusher

================
*/
void SV_Physics_Pusher (edict_t *ent)
{
	float thinktime;
	float oldltime;
	float movetime;
	float l;
	vec3_t oldorg, move;

	oldltime = ent->v.ltime;

	thinktime = ent->v.nextthink;
	if (thinktime < ent->v.ltime + sv_frametime)
	{
		movetime = thinktime - ent->v.ltime;
		if (movetime < 0)
			movetime = 0;
	}
	else
		movetime = sv_frametime;

	if (movetime)
	{
		SV_PushMove (ent, movetime);	// advances ent->v.ltime if not blocked
	}

	if (thinktime > oldltime && thinktime <= ent->v.ltime)
	{
		VectorCopy (ent->v.origin, oldorg);
		ent->v.nextthink = 0;
		pr_global_struct->time = sv.time;
		pr_global_struct->self = EDICT_TO_PROG(ent);
		pr_global_struct->other = EDICT_TO_PROG(sv.edicts);
#ifdef USE_PR2
		if ( sv_vm )
			PR2_EdictThink();
		else
#endif
			PR_ExecuteProgram (ent->v.think);
		if (ent->free)
			return;
		VectorSubtract (ent->v.origin, oldorg, move);

		l = VectorLength(move);
		if (l > 1.0/64)
		{
			// Con_Printf ("**** snap: %f\n", VectorLength (l));
			VectorCopy (oldorg, ent->v.origin);
			SV_Push (ent, move);
		}

	}

}


/*
=============
SV_Physics_None

Non moving objects can only think
=============
*/
void SV_Physics_None (edict_t *ent)
{
	// regular thinking
	SV_RunThink (ent);
}

/*
=============
SV_Physics_Noclip

A moving object that doesn't obey physics
=============
*/
void SV_Physics_Noclip (edict_t *ent)
{
	// regular thinking
	if (!SV_RunThink (ent))
		return;

	VectorMA (ent->v.angles, sv_frametime, ent->v.avelocity, ent->v.angles);
	VectorMA (ent->v.origin, sv_frametime, ent->v.velocity, ent->v.origin);

	SV_LinkEdict (ent, false);
}

/*
==============================================================================

TOSS / BOUNCE

==============================================================================
*/

/*
=============
SV_CheckWaterTransition

=============
*/
void SV_CheckWaterTransition (edict_t *ent)
{
	int cont;

	cont = SV_PointContents (ent->v.origin);
	if (!ent->v.watertype)
	{	// just spawned here
		ent->v.watertype = cont;
		ent->v.waterlevel = 1;
		return;
	}

	if (cont <= CONTENTS_WATER)
	{
		if (ent->v.watertype == CONTENTS_EMPTY)
		{	// just crossed into water
			SV_StartSound (ent, 0, "misc/h2ohit1.wav", 255, 1);
		}
		ent->v.watertype = cont;
		ent->v.waterlevel = 1;
	}
	else
	{
		if (ent->v.watertype != CONTENTS_EMPTY)
		{	// just crossed into water
			SV_StartSound (ent, 0, "misc/h2ohit1.wav", 255, 1);
		}
		ent->v.watertype = CONTENTS_EMPTY;
		ent->v.waterlevel = cont;
	}
}

/*
=============
SV_Physics_Toss

Toss, bounce, and fly movement.  When onground, do nothing.
=============
*/
void SV_Physics_Toss (edict_t *ent)
{
#ifndef MVDSV_MOVEMENT
	trace_t	trace;
	vec3_t	move;
	float	backoff;

	// regular thinking
	if (!SV_RunThink (ent))
		return;

	if (ent->v.velocity[2] > 0)
		ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;

	// if onground, return without moving
	if ( ((int)ent->v.flags & FL_ONGROUND) )
		return;

	SV_CheckVelocity (ent);

	// add gravity
	if (ent->v.movetype != MOVETYPE_FLY
	&& ent->v.movetype != MOVETYPE_FLYMISSILE)
		SV_AddGravity (ent, 1.0);

	// move angles
	VectorMA (ent->v.angles, sv_frametime, ent->v.avelocity, ent->v.angles);

	// move origin
	VectorScale (ent->v.velocity, sv_frametime, move);
	trace = SV_PushEntity (ent, move);
	if (trace.fraction == 1)
		return;
	if (ent->free)
		return;

	if (ent->v.movetype == MOVETYPE_BOUNCE)
		backoff = 1.5;
	else
		backoff = 1;

	ClipVelocity (ent->v.velocity, trace.plane.normal, ent->v.velocity, backoff);

	// stop if on ground
	if (trace.plane.normal[2] > 0.7)
	{
		if (ent->v.velocity[2] < 60 || ent->v.movetype != MOVETYPE_BOUNCE )
		{
			ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
			ent->v.groundentity = EDICT_TO_PROG(trace.e.ent);
			VectorClear (ent->v.velocity);
			VectorClear (ent->v.avelocity);
		}
	}

	// check for in water
	SV_CheckWaterTransition (ent);
#else
	trace_t	trace;
	vec3_t	move;
	float	backoff;

// regular thinking
	if (!SV_RunThink (ent))
		return;

	if (ent->v.velocity[2] > 0)
		ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;

// if onground, return without moving
	if ( ((int)ent->v.flags & FL_ONGROUND) )
		return;

	SV_CheckVelocity (ent);

// add gravity
	if (ent->v.movetype != MOVETYPE_FLY
	&& ent->v.movetype != MOVETYPE_FLYMISSILE)
		SV_AddGravity (ent, 1.0);

// move angles
	VectorMA (ent->v.angles, sv_frametime, ent->v.avelocity, ent->v.angles);
	//VectorMA (ent->v.angles, host_frametime, ent->v.avelocity, ent->v.angles);

// move origin
	VectorScale (ent->v.velocity, sv_frametime, move);
	trace = SV_PushEntity (ent, move);
	if (trace.fraction == 1)
		return;
	if (ent->free)
		return;
	
	if (ent->v.movetype == MOVETYPE_BOUNCE)
		backoff = 1.5;
	else
		backoff = 1;

	ClipVelocity (ent->v.velocity, trace.plane.normal, ent->v.velocity, backoff);

// stop if on ground
	if (trace.plane.normal[2] > 0.7)
	{		
		if (ent->v.velocity[2] < 60 || ent->v.movetype != MOVETYPE_BOUNCE )
		{
			ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
			ent->v.groundentity = EDICT_TO_PROG(trace.e.ent);
			VectorCopy (vec3_origin, ent->v.velocity);
			VectorCopy (vec3_origin, ent->v.avelocity);
		}
	}
	
// check for in water
	SV_CheckWaterTransition (ent);
#endif
}

/*
===============================================================================

STEPPING MOVEMENT

===============================================================================
*/

/*
=============
SV_Physics_Step

Monsters freefall when they don't have a ground entity, otherwise
all movement is done with discrete steps.

This is also used for objects that have become still on the ground, but
will fall if the floor is pulled out from under them.
FIXME: is this true?
=============
*/
void SV_Physics_Step (edict_t *ent)
{
	qbool hitsound;

	// frefall if not onground
	if ( ! ((int)ent->v.flags & (FL_ONGROUND | FL_FLY | FL_SWIM) ) )
	{
		if (ent->v.velocity[2] < movevars.gravity*-0.1)
			hitsound = true;
		else
			hitsound = false;

		SV_AddGravity (ent, 1.0);
		SV_CheckVelocity (ent);
		// Tonik: the check for SOLID_NOT is to fix the way dead bodies and
		// gibs behave (should not be blocked by players & monsters);
		// The SOLID_TRIGGER check is disabled lest we break frikbots
		if (ent->v.solid == SOLID_NOT /* || ent->v.solid == SOLID_TRIGGER*/)
			SV_FlyMove (ent, sv_frametime, NULL, MOVE_NOMONSTERS);
		else
			SV_FlyMove (ent, sv_frametime, NULL, MOVE_NORMAL);
		SV_LinkEdict (ent, true);

		if ( (int)ent->v.flags & FL_ONGROUND ) // just hit ground
		{
			if (hitsound)
				SV_StartSound (ent, 0, "demon/dland2.wav", 255, 1);
		}
	}

	// regular thinking
	SV_RunThink (ent);

	SV_CheckWaterTransition (ent);
}

//============================================================================

void SV_ProgStartFrame (void)
{
	// let the progs know that a new frame has started
	pr_global_struct->self = EDICT_TO_PROG(sv.edicts);
	pr_global_struct->other = EDICT_TO_PROG(sv.edicts);
	pr_global_struct->time = sv.time;
#ifdef USE_PR2
	if ( sv_vm )
		PR2_GameStartFrame();
	else
#endif
		PR_ExecuteProgram (pr_global_struct->StartFrame);
}

/*
================
SV_RunEntity
================
*/
void SV_RunEntity (edict_t *ent)
{
	if (ent->lastruntime == sv.time)
		return;
	ent->lastruntime = sv.time;

	switch ((int)ent->v.movetype)
	{
	case MOVETYPE_PUSH:
		SV_Physics_Pusher (ent);
		break;
	case MOVETYPE_NONE:
		SV_Physics_None (ent);
		break;
	case MOVETYPE_NOCLIP:
		SV_Physics_Noclip (ent);
		break;
	case MOVETYPE_STEP:
		SV_Physics_Step (ent);
		break;
	case MOVETYPE_TOSS:
	case MOVETYPE_BOUNCE:
	case MOVETYPE_FLY:
	case MOVETYPE_FLYMISSILE:
		SV_Physics_Toss (ent);
		break;
	default:
		SV_Error ("SV_Physics: bad movetype %i", (int)ent->v.movetype);
	}
}

/*
================
SV_RunNewmis
================
*/
void SV_RunNewmis (void)
{
	edict_t	*ent;
	double save_frametime;

	if (!pr_global_struct->newmis)
		return;

	ent = PROG_TO_EDICT(pr_global_struct->newmis);
	pr_global_struct->newmis = 0;

	save_frametime = sv_frametime;
	sv_frametime = 0.05;

	SV_RunEntity (ent);

	sv_frametime = save_frametime;
}

/*
================
SV_Physics
================
*/
#ifdef USE_PR2
void SV_PreRunCmd(void);
void SV_RunCmd (usercmd_t *ucmd, qbool inside);
void SV_PostRunCmd(void);
#endif
void SV_Physics (void)
{
	int i;
	edict_t *ent;
#ifdef USE_PR2
	client_t *cl,*savehc;
	edict_t *savesvpl;
#endif

	if (sv.state != ss_active)
		return;

	if (sv.old_time)
	{
		// don't bother running a frame if sv_mintic seconds haven't passed
		sv_frametime = sv.time - sv.old_time;
		if (sv_frametime < (double) sv_mintic.value)
			return;
		if (sv_frametime > (double) sv_maxtic.value)
			sv_frametime = (double) sv_maxtic.value;
		sv.old_time = sv.time;
	}
	else
		sv_frametime = 0.1; // initialization frame

	pr_global_struct->frametime = sv_frametime;

	SV_ProgStartFrame ();

	//
	// treat each object in turn
	// even the world gets a chance to think
	//
	ent = sv.edicts;
	for (i=0 ; i<sv.num_edicts ; i++, ent = NEXT_EDICT(ent))
	{
		if (ent->free)
			continue;

		if (pr_global_struct->force_retouch)
			SV_LinkEdict (ent, true);	// force retouch even for stationary

		if (i > 0 && i <= MAX_CLIENTS)
			continue;		// clients are run directly from packets

		SV_RunEntity (ent);
		SV_RunNewmis ();
	}

	if (pr_global_struct->force_retouch)
		pr_global_struct->force_retouch--;

#ifdef USE_PR2
	savesvpl = sv_player;
	savehc = sv_client;

	// so spec will have right goalentity - if speccing someone
	// qqshka {
	if ( sv_vm ) // don't fix .qc based mods
		for ( i = 0, cl = svs.clients; i < MAX_CLIENTS; i++, cl++ )
		{
			if ( cl->state == cs_free )
				continue;

			sv_client = cl;
			ent = cl->edict;

			if( sv_client->spectator && sv_client->spec_track > 0 )
				ent->v.goalentity = EDICT_TO_PROG(svs.clients[sv_client->spec_track-1].edict);
		}
	// }

	for ( i = 0, cl = svs.clients; i < MAX_CLIENTS; i++, cl++ )
	{
		if ( cl->state == cs_free )
			continue;
		if ( !cl->isBot )
			continue;

		sv_client = cl;
		sv_player = cl->edict;

		SV_PreRunCmd();
		SV_RunCmd (&cl->botcmd, false);
		SV_PostRunCmd();

		cl->lastcmd = cl->botcmd;
		cl->lastcmd.buttons = 0;

		memset(&cl->botcmd,0,sizeof(cl->botcmd));

		cl->localtime = sv.time;
		cl->delta_sequence = -1;	// no delta unless requested
	}
	sv_player = savesvpl;
	sv_client = savehc;
#endif
}

void SV_SetMoveVars(void)
{
	movevars.gravity		= sv_gravity.value;
	movevars.stopspeed		= sv_stopspeed.value;
	movevars.maxspeed		= sv_maxspeed.value;
	movevars.spectatormaxspeed	= sv_spectatormaxspeed.value;
	movevars.accelerate		= sv_accelerate.value;
	movevars.airaccelerate		= sv_airaccelerate.value;
	movevars.wateraccelerate	= sv_wateraccelerate.value;
	movevars.friction		= sv_friction.value;
	movevars.waterfriction		= sv_waterfriction.value;
	movevars.entgravity		= 1.0;
}

#endif