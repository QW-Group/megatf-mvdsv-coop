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

	$Id: pmove.h,v 1.13 2006/07/06 00:08:15 disconn3ct Exp $
*/

#ifndef __PMOVE_H__
#define __PMOVE_H__

#define MAX_PHYSENTS 64 // 32
typedef struct
{
	vec3_t		origin;
	cmodel_t	*model;		// only for bsp models
	vec3_t		mins, maxs;	// only for non-bsp models

	vec3_t		angles;
	vec3_t		oldangles;
	//model_t	*model; // only for bsp models
	int			solid;	// xavior hax
	int			rotated;		// BIGGER HAX
	qbyte		nonsolid;

	int			info;		// for client or server to identify
} physent_t;

typedef enum
{
	PM_NORMAL,			// normal ground movement
	PM_OLD_SPECTATOR,	// fly, no clip to world (QW bug)
	PM_SPECTATOR,		// fly, no clip to world
	PM_DEAD,			// no acceleration
	PM_FLY,				// fly, bump into walls
	PM_NONE,			// can't move
	PM_FREEZE			// can't move or look around (TODO)
} pmtype_t;

typedef struct
{
	// player state
	vec3_t		origin;
	vec3_t		angles;
	vec3_t		velocity;
	qbool		jump_held;

	float		waterjumptime;
	int			pm_type;

	// world state
	int			numphysent;
	physent_t	physents[MAX_PHYSENTS]; // 0 should be the world

	// input
	usercmd_t	cmd;

	// results
	int			numtouch;
	int			touchindex[MAX_PHYSENTS];
	qbool		onground;
	int			groundent; // index in physents array, only valid when onground is true
	int			waterlevel;
	int			watertype;
} playermove_t;

typedef struct {
	float	gravity;
	float	stopspeed;
	float	maxspeed;
	float	spectatormaxspeed;
	float	accelerate;
	float	airaccelerate;
	float	wateraccelerate;
	float	friction;
	float	waterfriction;
	float	entgravity;
	float	bunnyspeedcap;
	float	ktjump;
	qbool	slidefix; // NQ-style movement down ramps
	qbool	airstep;
	qbool	pground; // NQ-style "onground" flag handling.
} movevars_t;


extern	movevars_t movevars;
extern	playermove_t pmove;

void PM_PlayerMove (void);

int PM_PointContents (vec3_t point);
qbool PM_TestPlayerPosition (vec3_t point);
trace_t PM_PlayerTrace (vec3_t start, vec3_t end);

#endif /* !__PMOVE_H__ */
