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

	$Id: pr_cmds.c,v 1.38 2007/01/14 20:02:33 tonik Exp $
*/

#include "qwsvdef.h"

//eqds
extern cvar_t	sv_MegaTFHacks;

#define	RETURN_EDICT(e) (((int *)pr_globals)[OFS_RETURN] = EDICT_TO_PROG(e))
#define	RETURN_STRING(s) (((int *)pr_globals)[OFS_RETURN] = PR_SetString(s))

#define PR_GetStringOfs(p,o) (G_INT_FTE(o)?G_INT_FTE(o) + p->stringtable:"")
#define PR_SetStringOfs(p,o,s) (G_INT(o) = s - p->stringtable)

/*
===============================================================================
 
						BUILT-IN FUNCTIONS
 
===============================================================================
*/

char *PF_VarString (int	first)
{
	int		i;
	static char out[2048];

	out[0] = 0;
	for (i=first ; i<pr_argc ; i++)
	{
		strlcat (out, G_STRING((OFS_PARM0+i*3)), sizeof(out));
	}
	return out;
}


/*
=================
PF_errror
 
This is a TERMINAL error, which will kill off the entire server.
Dumps self.
 
error(value)
=================
*/
void PF_error (void)
{
	char	*s;
	edict_t	*ed;

SV_EndRedirect();

	s = PF_VarString(0);
	Con_Printf ("======SERVER ERROR in %s:\n%s\n", PR_GetString(pr_xfunction->s_name) ,s);
	ed = PROG_TO_EDICT(pr_global_struct->self);
	ED_Print (ed);

	SV_Error ("Program error");
}

/*
=================
PF_objerror
 
Dumps out self, then an error message.  The program is aborted and self is
removed, but the level can continue.
 
objerror(value)
=================
*/
void PF_objerror (void)
{
	char	*s;
	edict_t	*ed;

	s = PF_VarString(0);
	Con_Printf ("======OBJECT ERROR in %s:\n%s\n", PR_GetString(pr_xfunction->s_name),s);
	ed = PROG_TO_EDICT(pr_global_struct->self);
	ED_Print (ed);
	ED_Free (ed);

	SV_Error ("Program error");
}



/*
==============
PF_makevectors
 
Writes new values for v_forward, v_up, and v_right based on angles
makevectors(vector)
==============
*/
void PF_makevectors (void)
{
	AngleVectors (G_VECTOR(OFS_PARM0), pr_global_struct->v_forward, pr_global_struct->v_right, pr_global_struct->v_up);
}

/*
=================
PF_setorigin
 
This is the only valid way to move an object without using the physics of the world (setting velocity and waiting).  Directly changing origin will not set internal links correctly, so clipping would be messed up.  This should be called when an object is spawned, and then only if it is teleported.
 
setorigin (entity, origin)
=================
*/
void PF_setorigin (void)
{
	edict_t	*e;
	float	*org;

	e = G_EDICT(OFS_PARM0);
	org = G_VECTOR(OFS_PARM1);
	VectorCopy (org, e->v.origin);
	SV_LinkEdict (e, false);
}


/*
=================
PF_setsize
 
the size box is rotated by the current angle
 
setsize (entity, minvector, maxvector)
=================
*/
void PF_setsize (void)
{
	edict_t	*e;
	float	*min, *max;

	e = G_EDICT(OFS_PARM0);
	min = G_VECTOR(OFS_PARM1);
	max = G_VECTOR(OFS_PARM2);
	VectorCopy (min, e->v.mins);
	VectorCopy (max, e->v.maxs);
	VectorSubtract (max, min, e->v.size);
	SV_LinkEdict (e, false);
}


/*
=================
PF_setmodel
 
setmodel(entity, model)
Also sets size, mins, and maxs for inline bmodels
=================
*/
static void PF_setmodel (void)
{
	int			i;
	edict_t		*e;
	char		*m, **check;
	cmodel_t	*mod;

	e = G_EDICT(OFS_PARM0);
	m = G_STRING(OFS_PARM1);

// check to see if model was properly precached
	for (i = 0, check = sv.model_precache; i < MAX_MODELS && *check ; i++, check++)
		if (!strcmp(*check, m))
			goto ok;
	PR_RunError ("PF_setmodel: no precache: %s\n", m);
ok:

		e->v.model = G_INT(OFS_PARM1);
	e->v.modelindex = i;

// if it is an inline model, get the size information for it
	if (m[0] == '*') {
		mod = CM_InlineModel (m);
		VectorCopy (mod->mins, e->v.mins);
		VectorCopy (mod->maxs, e->v.maxs);
		VectorSubtract (mod->maxs, mod->mins, e->v.size);
		SV_LinkEdict (e, false);
	}

}

/*
=================
PF_bprint
 
broadcast print to everyone on server
 
bprint(value)
=================
*/
void PF_bprint (void)
{
	char		*s;
	int			level;

	level = G_FLOAT(OFS_PARM0);

	s = PF_VarString(1);
	SV_BroadcastPrintf (level, "%s", s);
}

#define SPECPRINT_CENTERPRINT	0x1
#define SPECPRINT_SPRINT	0x2
#define SPECPRINT_STUFFCMD	0x4
/*
=================
PF_sprint
 
single print to a specific client
 
sprint(clientent, value)
=================
*/
void PF_sprint (void)
{
	char		*s;
	client_t	*client, *cl;
	int			entnum;
	int			level;
	int			i;
	edict_t		*ent;
	char		*sta,*stb;

	entnum = G_EDICTNUM(OFS_PARM0);
	level = G_FLOAT(OFS_PARM1);

	s = PF_VarString(2);

	if (entnum < 1 || entnum > MAX_CLIENTS)
	{
		ent = PROG_TO_EDICT(pr_global_struct->self);
		sta = PR2_GetString(ent->v.classname);
		stb = PR_GetString(ent->v.classname);
		if (!strcmp(sta, "bot") || !strcmp(stb, "bot")
			|| !strcmp(sta, "") || !strcmp(stb, "")
			|| !strcmp(stb, "countdown_timer") || !strcmp(stb, "timer")
			)
			return;
		else
			Con_Printf ("tried to sprint to a non-client class %s and netname %s that %s\n", PR_GetString(ent->v.classname), PR_GetString(ent->v.netname), s);
		return;
	}

	client = &svs.clients[entnum-1];

	SV_ClientPrintf (client, level, "%s", s);

	//bliP: spectator print ->
	if ((int)sv_specprint.value & SPECPRINT_SPRINT)
	{
		for (i = 0, cl = svs.clients; i < MAX_CLIENTS; i++, cl++)
		{
			if (!cl->state || !cl->spectator)
				continue;

			if ((cl->spec_track == entnum) && (cl->spec_print & SPECPRINT_SPRINT))
				SV_ClientPrintf (cl, level, "%s", s);
		}
	}
	//<-
}



/*
=================
PF_centerprint
 
single print to a specific client
 
centerprint(clientent, value)
=================
*/
void PF_centerprint (void)
{
	char		*s;
	int			entnum;
	client_t	*cl, *spec;
	int			i;
	edict_t		*ent;
	char	*sta,*stb;

	entnum = G_EDICTNUM(OFS_PARM0);
	s = PF_VarString(1);

	if (entnum < 1 || entnum > MAX_CLIENTS)
	{
		ent = PROG_TO_EDICT(pr_global_struct->self);
		sta = PR2_GetString(ent->v.classname);
		stb = PR_GetString(ent->v.classname);
		if (!strcmp(sta, "bot") || !strcmp(stb, "bot")
			|| !strcmp(sta, "") || !strcmp(stb, "")
			|| !strcmp(stb, "countdown_timer") || !strcmp(stb, "timer")
			)
			return;
		else
			//Con_Printf ("tried to sprint to a non-client class %s and netname %s\n", PR_GetString(ent->v.classname), PR_GetString(ent->v.netname));
			Con_Printf ("tried to sprint to a non-client class %s and netname %s that %s\n", PR_GetString(ent->v.classname), PR_GetString(ent->v.netname), s);
		return;
	}

	cl = &svs.clients[entnum-1];

	ClientReliableWrite_Begin (cl, svc_centerprint, 2 + strlen(s));
	ClientReliableWrite_String (cl, s);

	if (sv.mvdrecording)
	{
		if (MVDWrite_Begin (dem_single, entnum - 1, 2 + strlen(s)))
		{
			MVD_MSG_WriteByte (svc_centerprint);
			MVD_MSG_WriteString (s);
		}
	}

	//bliP: spectator print ->
	if ((int)sv_specprint.value & SPECPRINT_CENTERPRINT)
	{
		for (i = 0, spec = svs.clients; i < MAX_CLIENTS; i++, spec++)
		{
			if (!cl->state || !spec->spectator)
				continue;

			if ((spec->spec_track == entnum) && (cl->spec_print & SPECPRINT_CENTERPRINT))
			{
				ClientReliableWrite_Begin (spec, svc_centerprint, 2 + strlen(s));
				ClientReliableWrite_String (spec, s);
			}
		}
	}
	//<-
}


/*
=================
PF_normalize
 
vector normalize(vector)
=================
*/
void PF_normalize (void)
{
	float	*value1;
	vec3_t	newvalue;
	float	new;

	value1 = G_VECTOR(OFS_PARM0);

	new = value1[0] * value1[0] + value1[1] * value1[1] + value1[2]*value1[2];
	new = sqrt(new);

	if (new == 0)
		newvalue[0] = newvalue[1] = newvalue[2] = 0;
	else
	{
		new = 1/new;
		newvalue[0] = value1[0] * new;
		newvalue[1] = value1[1] * new;
		newvalue[2] = value1[2] * new;
	}

	VectorCopy (newvalue, G_VECTOR(OFS_RETURN));
}

/*
=================
PF_vlen
 
scalar vlen(vector)
=================
*/
void PF_vlen (void)
{
	float	*value1;
	float	new;

	value1 = G_VECTOR(OFS_PARM0);

	new = value1[0] * value1[0] + value1[1] * value1[1] + value1[2]*value1[2];
	new = sqrt(new);

	G_FLOAT(OFS_RETURN) = new;
}

/*
=================
PF_vectoyaw
 
float vectoyaw(vector)
=================
*/
void PF_vectoyaw (void)
{
	float	*value1;
	float	yaw;

	value1 = G_VECTOR(OFS_PARM0);

	if (value1[1] == 0 && value1[0] == 0)
		yaw = 0;
	else
	{
		yaw = (int) (atan2(value1[1], value1[0]) * 180 / M_PI);
		if (yaw < 0)
			yaw += 360;
	}

	G_FLOAT(OFS_RETURN) = yaw;
}


/*
=================
PF_vectoangles
 
vector vectoangles(vector)
=================
*/
void PF_vectoangles (void)
{
	float	*value1;
	float	forward;
	float	yaw, pitch;

	value1 = G_VECTOR(OFS_PARM0);

	if (value1[1] == 0 && value1[0] == 0)
	{
		yaw = 0;
		if (value1[2] > 0)
			pitch = 90;
		else
			pitch = 270;
	}
	else
	{
		yaw = (int) (atan2(value1[1], value1[0]) * 180 / M_PI);
		if (yaw < 0)
			yaw += 360;

		forward = sqrt (value1[0]*value1[0] + value1[1]*value1[1]);
		pitch = (int) (atan2(value1[2], forward) * 180 / M_PI);
		if (pitch < 0)
			pitch += 360;
	}

	G_FLOAT(OFS_RETURN+0) = pitch;
	G_FLOAT(OFS_RETURN+1) = yaw;
	G_FLOAT(OFS_RETURN+2) = 0;
}

/*
=================
PF_Random
 
Returns a number from 0<= num < 1
 
random()
=================
*/
void PF_random (void)
{
	float		num;

	num = (rand ()&0x7fff) / ((float)0x7fff);

	G_FLOAT(OFS_RETURN) = num;
}


/*
=================
PF_ambientsound
 
=================
*/
void PF_ambientsound (void)
{
	char		**check;
	char		*samp;
	float		*pos;
	float 		vol, attenuation;
	int			i, soundnum;

	pos = G_VECTOR (OFS_PARM0);
	samp = G_STRING(OFS_PARM1);
	vol = G_FLOAT(OFS_PARM2);
	attenuation = G_FLOAT(OFS_PARM3);

	// check to see if samp was properly precached
	for (soundnum=0, check = sv.sound_precache ; *check ; check++, soundnum++)
		if (!strcmp(*check,samp))
			break;

	if (!*check)
	{
		Con_Printf ("no precache: %s\n", samp);
		return;
	}

	// add an svc_spawnambient command to the level signon packet

	MSG_WriteByte (&sv.signon,svc_spawnstaticsound);
	for (i=0 ; i<3 ; i++)
		MSG_WriteCoord(&sv.signon, pos[i]);

	MSG_WriteByte (&sv.signon, soundnum);

	MSG_WriteByte (&sv.signon, vol*255);
	MSG_WriteByte (&sv.signon, attenuation*64);

}

/*
=================
PF_sound
 
Each entity can have eight independant sound sources, like voice,
weapon, feet, etc.
 
Channel 0 is an auto-allocate channel, the others override anything
already running on that entity/channel pair.
 
An attenuation of 0 will play full volume everywhere in the level.
Larger attenuations will drop off.
 
=================
*/
void PF_sound (void)
{
	char		*sample;
	int			channel;
	edict_t		*entity;
	int 		volume;
	float attenuation;

	entity = G_EDICT(OFS_PARM0);
	channel = G_FLOAT(OFS_PARM1);
	sample = G_STRING(OFS_PARM2);
	volume = G_FLOAT(OFS_PARM3) * 255;
	attenuation = G_FLOAT(OFS_PARM4);

	if (sv_MegaTFHacks.value) 
	{
	// switches jetpack channel to lesser used channel 5.
	// This fixes 'silent jetjumps' expolit in ALL versions of MegaTF
		if (!strcmp(sample, "weapons/jetjump.wav") &&
			channel == 1)
			channel = 5;
	}

	SV_StartSound (entity, channel, sample, volume, attenuation);
}

/*
=================
PF_break
 
break()
=================
*/
void PF_break (void)
{
	Con_Printf ("break statement\n");
	*(int *)-4 = 0;	// dump to debugger
	//	PR_RunError ("break statement");
}

/*
=================
PF_traceline
 
Used for use tracing and shot targeting
Traces are blocked by bbox and exact bsp entityes, and also slide box entities
if the tryents flag is set.
 
traceline (vector1, vector2, tryents)
=================
*/
void PF_traceline (void)
{
	float	*v1, *v2;
	trace_t	trace;
	int		nomonsters;
	edict_t	*ent;

	v1 = G_VECTOR(OFS_PARM0);
	v2 = G_VECTOR(OFS_PARM1);
	nomonsters = G_FLOAT(OFS_PARM2);
	ent = G_EDICT(OFS_PARM3);

	trace = SV_Trace (v1, vec3_origin, vec3_origin, v2, nomonsters, ent);

	pr_global_struct->trace_allsolid = trace.allsolid;
	pr_global_struct->trace_startsolid = trace.startsolid;
	pr_global_struct->trace_fraction = trace.fraction;
	pr_global_struct->trace_inwater = trace.inwater;
	pr_global_struct->trace_inopen = trace.inopen;
	VectorCopy (trace.endpos, pr_global_struct->trace_endpos);
	VectorCopy (trace.plane.normal, pr_global_struct->trace_plane_normal);
	pr_global_struct->trace_plane_dist =  trace.plane.dist;
	if (trace.e.ent)
		pr_global_struct->trace_ent = EDICT_TO_PROG(trace.e.ent);
	else
		pr_global_struct->trace_ent = EDICT_TO_PROG(sv.edicts);
}

/*
=================
PF_checkpos
 
Returns true if the given entity can move to the given position from it's
current position by walking or rolling.
FIXME: make work...
scalar checkpos (entity, vector)
=================
*/
void PF_checkpos (void)
{}

//============================================================================

// Unlike Quake's Mod_LeafPVS, CM_LeafPVS returns a pointer to static data
// uncompressed at load time, so it's safe to store for future use
static byte	*checkpvs;

int PF_newcheckclient (int check)
{
	int		i;
	edict_t	*ent;
	vec3_t	org;

// cycle to the next one

	if (check < 1)
		check = 1;
	if (check > MAX_CLIENTS)
		check = MAX_CLIENTS;

	if (check == MAX_CLIENTS)
		i = 1;
	else
		i = check + 1;

	for ( ;  ; i++)
	{
		if (i == MAX_CLIENTS+1)
			i = 1;

		ent = EDICT_NUM(i);

		if (i == check)
			break;	// didn't find anything else

		if (ent->free)
			continue;
		if (ent->v.health <= 0)
			continue;
		if ((int)ent->v.flags & FL_NOTARGET)
			continue;

	// anything that is a client, or has a client as an enemy
		break;
	}

// get the PVS for the entity
	VectorAdd (ent->v.origin, ent->v.view_ofs, org);
	checkpvs = CM_LeafPVS (CM_PointInLeaf(org));

	return i;
}


/*
=================
PF_checkclient

Returns a client (or object that has a client enemy) that would be a
valid target.

If there are more than one valid options, they are cycled each frame

If (self.origin + self.viewofs) is not in the PVS of the current target,
it is not returned at all.

entity checkclient() = #17
=================
*/
#define	MAX_CHECK	16
static void PF_checkclient (void)
{
	edict_t	*ent, *self;
	int		l;
	vec3_t	vieworg;
	
// find a new check if on a new frame
	if (sv.time - sv.lastchecktime >= 0.1)
	{
		sv.lastcheck = PF_newcheckclient (sv.lastcheck);
		sv.lastchecktime = sv.time;
	}

// return check if it might be visible	
	ent = EDICT_NUM(sv.lastcheck);
	if (ent->free || ent->v.health <= 0)
	{
		RETURN_EDICT(sv.edicts);
		return;
	}

// if current entity can't possibly see the check entity, return 0
	self = PROG_TO_EDICT(pr_global_struct->self);
	VectorAdd (self->v.origin, self->v.view_ofs, vieworg);
	l = CM_Leafnum(CM_PointInLeaf(vieworg)) - 1;
	if ( (l<0) || !(checkpvs[l>>3] & (1<<(l&7)) ) )
	{
		RETURN_EDICT(sv.edicts);
		return;
	}

// might be able to see it
	RETURN_EDICT(ent);
}

//============================================================================


/*
=================
PF_stuffcmd
 
Sends text over to the client's execution buffer
 
stuffcmd (clientent, value)
=================
*/
void PF_stuffcmd (void)
{
	int		entnum;
	char	*str;
	client_t	*cl, *spec;
	char	*buf;
	int j;

	entnum = G_EDICTNUM(OFS_PARM0);
	if (entnum < 1 || entnum > MAX_CLIENTS)
		PR_RunError ("Parm 0 not a client");
	str = G_STRING(OFS_PARM1);

	cl = &svs.clients[entnum-1];

	if (!strncmp(str, "disconnect\n", MAX_STUFFTEXT))
	{
		// so long and thanks for all the fish
		cl->drop = true;
		return;
	}

	buf = cl->stufftext_buf;
	if (strlen(buf) + strlen(str) >= MAX_STUFFTEXT)
		PR_RunError ("stufftext buffer overflow");
	strlcat (buf, str, MAX_STUFFTEXT);

	if( strchr( buf, '\n' ) )
	{
		ClientReliableWrite_Begin (cl, svc_stufftext, 2+strlen(buf));
		ClientReliableWrite_String (cl, buf);
		if (sv.mvdrecording)
		{
			if (MVDWrite_Begin ( dem_single, cl - svs.clients, 2+strlen(buf)))
			{
				MVD_MSG_WriteByte (svc_stufftext);
				MVD_MSG_WriteString (buf);
			}
		}

		//bliP: spectator print ->
		if ((int)sv_specprint.value & SPECPRINT_STUFFCMD)
		{
			for (j = 0, spec = svs.clients; j < MAX_CLIENTS; j++, spec++)
			{
				if (!cl->state || !spec->spectator)
					continue;

				if ((spec->spec_track == entnum) && (cl->spec_print & SPECPRINT_STUFFCMD))
				{
					ClientReliableWrite_Begin (spec, svc_stufftext, 2+strlen(buf));
					ClientReliableWrite_String (spec, buf);
				}
			}
		}
		//<-
	
	buf[0] = 0;
	}
}

/*
=================
PF_localcmd
 
Sends text over to the client's execution buffer
 
localcmd (string)
=================
*/
void PF_localcmd (void)
{
	char	*str;

	str = G_STRING(OFS_PARM0);

	if (sv_MegaTFHacks.value && !strcmp(str,"exec qwmcycle/map"))
		mtf.changing = true;

	Cbuf_AddText (str);
}

void PF_executecmd (void)
{
	int old_other, old_self; // mod_consolecmd will be executed, so we need to store this

	old_self = pr_global_struct->self;
	old_other = pr_global_struct->other;

	Cbuf_Execute();

	pr_global_struct->self = old_self;
	pr_global_struct->other = old_other;
}

#define MAX_PR_STRING_SIZE 2048

int		pr_string_index = 0;
char	pr_string_buf[8][MAX_PR_STRING_SIZE];
char	*pr_string_temp = pr_string_buf[0];

void PF_SetTempString(void)
{
	pr_string_temp = pr_string_buf[pr_string_index++&7];
}


/*
=================
PF_tokenize
 
float tokenize(string)
=================
*/

void PF_tokenize (void)
{
	char *str;

	str = G_STRING(OFS_PARM0);
	Cmd_TokenizeString(str);
	G_FLOAT(OFS_RETURN) = Cmd_Argc();
}

/*
=================
PF_argc
 
returns number of tokens (must be executed after PF_Tokanize!)
 
float argc(void)
=================
*/

void PF_argc (void)
{
	G_FLOAT(OFS_RETURN) = (float) Cmd_Argc();
}

/*
=================
PF_argv
 
returns token requested by user (must be executed after PF_Tokanize!)
 
string argc(float)
=================
*/

void PF_argv (void)
{
	int num;

	num = (int) G_FLOAT(OFS_PARM0);

//	if (num < 0 ) num = 0;
//	if (num > Cmd_Argc()-1) num = Cmd_Argc()-1;

	if (num < 0 || num >= Cmd_Argc())
		RETURN_STRING("");

	snprintf (pr_string_temp, MAX_PR_STRING_SIZE, "%s", Cmd_Argv(num));
	G_INT(OFS_RETURN) = PR_SetString(pr_string_temp);
	PF_SetTempString();
}

/*
=================
PF_teamfield
 
string teamfield(.string field)
=================
*/

void PF_teamfield (void)
{
	pr_teamfield = G_INT(OFS_PARM0);
}

/*
=================
PF_substr
 
string substr(string str, float start, float len)
=================
*/

void PF_substr (void)
{
	char *s;
	int start, len, l;

	s = G_STRING(OFS_PARM0);
	start = (int) G_FLOAT(OFS_PARM1);
	len = (int) G_FLOAT(OFS_PARM2);
	l = strlen(s);

	if (start >= l || !len || !*s)
	{
		G_INT(OFS_RETURN) = PR_SetTmpString("");
		return;
	}

	s += start;
	l -= start;

	if (len > l + 1)
		len = l + 1;

	strlcpy(pr_string_temp, s, len + 1);

	G_INT(OFS_RETURN) = PR_SetString(pr_string_temp);

	PF_SetTempString();
}

/*
=================
PF_strcat
 
string strcat(string str1, string str2)
=================
*/

void PF_strcat (void)
{
	/* FIXME */
	strcpy(pr_string_temp, PF_VarString(0)/*, MAX_PR_STRING_SIZE*/);
	G_INT(OFS_RETURN) = PR_SetString(pr_string_temp);

	PF_SetTempString();
}

/*
=================
PF_strlen
 
float strlen(string str)
=================
*/

void PF_strlen (void)
{
	G_FLOAT(OFS_RETURN) = (float) strlen(G_STRING(OFS_PARM0));
}

/*
=================
PF_str2byte
 
float str2byte (string str)
=================
*/

void PF_str2byte (void)
{
	G_FLOAT(OFS_RETURN) = (float) *G_STRING(OFS_PARM0);
}

/*
=================
PF_str2short
 
float str2short (string str)
=================
*/

void PF_str2short (void)
{
	G_FLOAT(OFS_RETURN) = (float) LittleShort(*(short*)G_STRING(OFS_PARM0));
}

/*
=================
PF_strzone

ZQ_QC_STRINGS
string newstr (string str [, float size])

The 'size' parameter is not QSG but an MVDSV extension
=================
*/

void PF_strzone (void)
{
	char *s;
	int i, size;

	s = G_STRING(OFS_PARM0);

	for (i = 0; i < MAX_PRSTR; i++)
	{
		if (!pr_newstrtbl[i] || pr_newstrtbl[i] == pr_strings)
			break;
	}

	if (i == MAX_PRSTR)
		PR_RunError("strzone: out of string memory");

	size = strlen(s) + 1;
	if (pr_argc == 2 && (int) G_FLOAT(OFS_PARM1) > size)
		size = (int) G_FLOAT(OFS_PARM1);

	pr_newstrtbl[i] = (char *) Q_malloc(size);
	strlcpy(pr_newstrtbl[i], s, size);

	G_INT(OFS_RETURN) = -(i+MAX_PRSTR);
}


//FTE_STRINGS
//strstr, without generating a new string. Use in conjunction with FRIK_FILE's substring for more similar strstr.
void  PF_strstrofs (void)
{
	char *instr = G_STRING(OFS_PARM0);
	char *match = G_STRING(OFS_PARM1);

	/*
	// Note from XavioR: this part makes strstr return negative values RANDOMLY. Causes much headache. Do avoid.
	int firstofs = G_FLOAT(OFS_PARM2);

	if (firstofs && (firstofs < 0 || firstofs > strlen(instr)))
	{
		G_FLOAT(OFS_RETURN) = -1;
		return;
	}
	*/

	match = strstr(instr/*+firstofs*/, match);
	if (!match)
		G_FLOAT(OFS_RETURN) = -1;
	else
		G_FLOAT(OFS_RETURN) = match - instr;
}


/*
void PF_strstr (void)
{
	char *str, *sub, *p;

	str = G_STRING(OFS_PARM0);
	sub = G_STRING(OFS_PARM1);

	if ((p = strstr(str, sub)) == NULL)
	{
		G_INT(OFS_RETURN) = 0;
		return;
	}

	RETURN_STRING(p);
}
*/
/*
=================
PF_strunzone

ZQ_QC_STRINGS
void strunzone (string str)
=================
*/

void PF_strunzone (void)
{
	int num;

	num = G_INT(OFS_PARM0);
	if (num > - MAX_PRSTR)
		PR_RunError("strunzone: not a dynamic string");

	if (num <= -(MAX_PRSTR * 2))
		PR_RunError ("strunzone: bad string");

	num = - (num + MAX_PRSTR);

	if (pr_newstrtbl[num] == pr_strings)
		return;	// allow multiple strunzone on the same string (like free in C)

	Q_free(pr_newstrtbl[num]);
	pr_newstrtbl[num] = pr_strings;
}

void PF_clear_strtbl(void)
{
	int i;

	for (i = 0; i < MAX_PRSTR; i++)
	{
		if (pr_newstrtbl[i] && pr_newstrtbl[i] != pr_strings)
		{
			Q_free(pr_newstrtbl[i]);
			pr_newstrtbl[i] = NULL;
		}
	}
}

/*
=================
PF_readcmd
 
string readmcmd (string str)
=================
*/

void PF_readcmd (void)
{
	char *s;
	static char output[OUTPUTBUF_SIZE];
	extern char outputbuf[];
	extern redirect_t sv_redirected;
	redirect_t old;

	s = G_STRING(OFS_PARM0);

	Cbuf_Execute();
	Cbuf_AddText (s);

	old = sv_redirected;
	if (old != RD_NONE)
		SV_EndRedirect();

	SV_BeginRedirect(RD_MOD);
	Cbuf_Execute();
	strlcpy(output, outputbuf, sizeof(output));
	SV_EndRedirect();

	if (old != RD_NONE)
		SV_BeginRedirect(old);


	G_INT(OFS_RETURN) = PR_SetString(output);
}

/*
=================
PF_redirectcmd
 
void redirectcmd (entity to, string str)
=================
*/

void PF_redirectcmd (void)
{
	char *s;
	int entnum;
	extern redirect_t sv_redirected;

	if (sv_redirected)
		return;

	entnum = G_EDICTNUM(OFS_PARM0);
	if (entnum < 1 || entnum > MAX_CLIENTS)
		PR_RunError ("Parm 0 not a client");

	s = G_STRING(OFS_PARM1);

	Cbuf_AddText (s);

	SV_BeginRedirect(RD_MOD + entnum);
	Cbuf_Execute();
	SV_EndRedirect();
}

dfunction_t *ED_FindFunction (char *name);

//FTE_CALLTIMEOFDAY
void PF_calltimeofday (void)
{
	date_t date;
	dfunction_t *f;

	if ((f = ED_FindFunction ("timeofday")) != NULL)
	{

		SV_TimeOfDay(&date);

		G_FLOAT(OFS_PARM0) = (float)date.sec;
		G_FLOAT(OFS_PARM1) = (float)date.min;
		G_FLOAT(OFS_PARM2) = (float)date.hour;
		G_FLOAT(OFS_PARM3) = (float)date.day;
		G_FLOAT(OFS_PARM4) = (float)date.mon;
		G_FLOAT(OFS_PARM5) = (float)date.year;
		G_INT(OFS_PARM6) = PR_SetTmpString(date.str);

		PR_ExecuteProgram((func_t)(f - pr_functions));
	}

}

/*
=================
PF_forcedemoframe
 
void PF_forcedemoframe(float now)
Forces demo frame
if argument 'now' is set, frame is written instantly
=================
*/

void PF_forcedemoframe (void)
{
	demo.forceFrame = 1;
	if (G_FLOAT(OFS_PARM0) == 1)
		SV_SendDemoMessage();
}


/*
=================
PF_strcpy
 
void strcpy(string dst, string src)
FIXME: check for null pointers first?
=================
*/

void PF_strcpy (void)
{
	strcpy(G_STRING(OFS_PARM0), G_STRING(OFS_PARM1));
}

/*
=================
PF_strncpy
 
void strcpy(string dst, string src, float count)
FIXME: check for null pointers first?
=================
*/

void PF_strncpy (void)
{
	strncpy(G_STRING(OFS_PARM0), G_STRING(OFS_PARM1), (int) G_FLOAT(OFS_PARM2));
}


/*
=================
PF_strstr
 
string strstr(string str, string sub)
=================
*/

void PF_strstr (void)
{
	char *str, *sub, *p;

	str = G_STRING(OFS_PARM0);
	sub = G_STRING(OFS_PARM1);

	if ((p = strstr(str, sub)) == NULL)
	{
		G_INT(OFS_RETURN) = 0;
		return;
	}

	RETURN_STRING(p);
}

/*
====================
SV_CleanName_Init
 
sets chararcter table to translate quake texts to more friendly texts
====================
*/

char chartbl2[256];

void PR_CleanLogText_Init (void)
{
	int i;

	for (i = 0; i < 32; i++)
		chartbl2[i] = chartbl2[i + 128] = '#';
	for (i = 32; i < 128; i++)
		chartbl2[i] = chartbl2[i + 128] = i;

	// special cases
	chartbl2[10] = 10;
	chartbl2[13] = 13;

	// dot
	chartbl2[5      ] = chartbl2[14      ] = chartbl2[15      ] = chartbl2[28      ] = chartbl2[46      ] = '.';
	chartbl2[5 + 128] = chartbl2[14 + 128] = chartbl2[15 + 128] = chartbl2[28 + 128] = chartbl2[46 + 128] = '.';

	// numbers
	for (i = 18; i < 28; i++)
		chartbl2[i] = chartbl2[i + 128] = i + 30;

	// brackets
	chartbl2[16] = chartbl2[16 + 128]= '[';
	chartbl2[17] = chartbl2[17 + 128] = ']';
	chartbl2[29] = chartbl2[29 + 128] = chartbl2[128] = '(';
	chartbl2[31] = chartbl2[31 + 128] = chartbl2[130] = ')';

	// left arrow
	chartbl2[127] = '>';
	// right arrow
	chartbl2[141] = '<';

	// '='
	chartbl2[30] = chartbl2[129] = chartbl2[30 + 128] = '=';
}

void PR_CleanText(unsigned char *text)
{
	for ( ; *text; text++)
		*text = chartbl2[*text];
}

/*
================
PF_log
 
void log(string name, float console, string text)
=================
*/

void PF_log(void)
{
	char name[MAX_OSPATH], *text;
	FILE *file;

	snprintf(name, MAX_OSPATH, "%s/%s.log", fs_gamedir, G_STRING(OFS_PARM0));
	text = PF_VarString(2);
	PR_CleanText((unsigned char*)text);

	if ((file = fopen(name, "a")) == NULL)
	{
		Sys_Printf("coldn't open log file %s\n", name);
	}
	else
	{
		fprintf (file, "%s", text);
		fflush (file);
		fclose(file);
	}

	if (G_FLOAT(OFS_PARM1))
		Sys_Printf("%s", text);

}

/*
=================
PF_cvar
 
float cvar (string)
=================
*/
void PF_cvar (void)
{
	char	*str;

	str = G_STRING(OFS_PARM0);

	if (!strcasecmp(str, "pr_checkextension") && !is_ktpro) {
		// we do support PF_checkextension
		G_FLOAT(OFS_RETURN) = 1.0;
		return;
	}

	G_FLOAT(OFS_RETURN) = Cvar_Value (str);
}

/*
=================
PF_cvar_set
 
float cvar (string)
=================
*/
void PF_cvar_set (void)
{
	char	*var_name, *val;
	cvar_t	*var;

	var_name = G_STRING(OFS_PARM0);
	val = G_STRING(OFS_PARM1);

	var = Cvar_FindVar(var_name);
	if (!var)
	{
		Con_Printf ("PF_cvar_set: variable %s not found\n", var_name);
		return;
	}

	Cvar_Set (var, val);
}

/*
=================
PF_findradius
 
Returns a chain of entities that have origins within a spherical area
 
findradius (origin, radius)
=================
*/
#ifndef mvdsv_findrad
vec_t Length(vec3_t v)
{
	int		i;
	float	length;
	
	length = 0;
	for (i=0 ; i< 3 ; i++)
		length += v[i]*v[i];
	length = sqrt (length);		// FIXME

	return length;
}
#endif

static void PF_findradius (void)
{
#ifdef mvdsv_findrad
	int			i, j, numtouch;
	edict_t		*touchlist[MAX_EDICTS], *ent, *chain;
	float		rad, rad_2, *org;
	vec3_t		mins, maxs, eorg;

	org = G_VECTOR(OFS_PARM0);
	rad = G_FLOAT(OFS_PARM1);
	rad_2 = rad * rad;

	for (i = 0; i < 3; i++)
	{
		mins[i] = org[i] - rad - 1;		// enlarge the bbox a bit
		maxs[i] = org[i] + rad + 1;
	}

	numtouch = SV_AreaEdicts (mins, maxs, touchlist, MAX_EDICTS, AREA_SOLID);
	numtouch += SV_AreaEdicts (mins, maxs, &touchlist[numtouch], MAX_EDICTS - numtouch, AREA_TRIGGERS);

	chain = (edict_t *)sv.edicts;

// touch linked edicts
	for (i = 0; i < numtouch; i++)
	{
		ent = touchlist[i];
//		if (ent->v.solid == SOLID_NOT)
//			continue;	// FIXME?
		if (ent->v.solid == SOLID_NOT && !((int)ent->v.flags & FL_FINDABLE_NONSOLID))
			continue;
		for (j = 0; j < 3; j++)
			eorg[j] = org[j] - (ent->v.origin[j] + (ent->v.mins[j] + ent->v.maxs[j]) * 0.5);
		if (DotProduct(eorg, eorg) > rad_2)
			continue;

		ent->v.chain = EDICT_TO_PROG(chain);
		chain = ent;
	}

	RETURN_EDICT(chain);
#else
// cp findradius()
	edict_t	*ent, *chain;
	float	rad;
	float	*org;
	vec3_t	eorg;
	int		i, j;

	chain = (edict_t *)sv.edicts;
	
	org = G_VECTOR(OFS_PARM0);
	rad = G_FLOAT(OFS_PARM1);

	ent = NEXT_EDICT(sv.edicts);
	for (i=1 ; i<sv.num_edicts ; i++, ent = NEXT_EDICT(ent))
	{
		if (ent->free)
			continue;                 //OfN Below, if FINDABLE_NONSOLID flag is set
		if (ent->v.solid == SOLID_NOT && !((int)ent->v.flags & FL_FINDABLE_NONSOLID))
			continue;
		for (j=0 ; j<3 ; j++)
			eorg[j] = org[j] - (ent->v.origin[j] + (ent->v.mins[j] + ent->v.maxs[j])*0.5);			
		if (Length(eorg) > rad)
			continue;
			
		ent->v.chain = EDICT_TO_PROG(chain);
		chain = ent;
	}

	RETURN_EDICT(chain);
#endif
}


/*
=========
PF_dprint
=========
*/
void PF_dprint (void)
{
	Con_Printf ("%s",PF_VarString(0));

	if (sv_MegaTFHacks.value && !strcmp(PF_VarString(0),
		"Intermission think.\n")) 
	{
		if (!mtf.intermission_thinks)
			mtf.intermission_thinks = 1;
		else
			mtf.intermission_thinks += 1;
	}
}

/*
=========
PF_conprint
=========
*/
void PF_conprint (void)
{
	Sys_Printf ("%s",PF_VarString(0));
}

//char	pr_string_temp[128];

void PF_ftos (void)
{
	float	v;
	v = G_FLOAT(OFS_PARM0);

	if (v == (int)v)
		snprintf (pr_string_temp, MAX_PR_STRING_SIZE, "%d",(int)v);
	else
		snprintf (pr_string_temp, MAX_PR_STRING_SIZE, "%5.1f",v);
	G_INT(OFS_RETURN) = PR_SetString(pr_string_temp);
	PF_SetTempString();
}

void PF_fabs (void)
{
	float	v;
	v = G_FLOAT(OFS_PARM0);
	G_FLOAT(OFS_RETURN) = fabs(v);
}

void PF_vtos (void)
{
	snprintf (pr_string_temp, MAX_PR_STRING_SIZE, "'%5.1f %5.1f %5.1f'", G_VECTOR(OFS_PARM0)[0], G_VECTOR(OFS_PARM0)[1], G_VECTOR(OFS_PARM0)[2]);
	G_INT(OFS_RETURN) = PR_SetString(pr_string_temp);

	PF_SetTempString();
}

void PF_Spawn (void)
{
	edict_t	*ed;
	ed = ED_Alloc();
	RETURN_EDICT(ed);
}

void PF_Remove (void)
{
	edict_t	*ed;

	ed = G_EDICT(OFS_PARM0);
	ED_Free (ed);
}


// entity (entity start, .string field, string match) find = #5;
void PF_Find (void)
{
	int		e;
	int		f;
	char	*s, *t;
	edict_t	*ed;

	e = G_EDICTNUM(OFS_PARM0);
	f = G_INT(OFS_PARM1);
	s = G_STRING(OFS_PARM2);
	if (!s)
		PR_RunError ("PF_Find: bad search string");

	for (e++ ; e < sv.num_edicts ; e++)
	{
		ed = EDICT_NUM(e);
		if (ed->free)
			continue;
		t = E_STRING(ed,f);
		if (!t)
			continue;
		if (!strcmp(t,s))
		{
			RETURN_EDICT(ed);
			return;
		}
	}

	RETURN_EDICT(sv.edicts);
}

void PR_CheckEmptyString (char *s)
{
	if (s[0] <= ' ')
		PR_RunError ("Bad string");
}

void PF_precache_file (void)
{	// precache_file is only used to copy files with qcc, it does nothing
	G_INT(OFS_RETURN) = G_INT(OFS_PARM0);
}

//#define fte_stuff
#ifdef fte_stuff
void PF_precache_sound (void)
{
	char	*s;
	int		i;

	//s = PR_GetStringOfs(prinst, OFS_PARM0);
	s = G_STRING(OFS_PARM0);
	G_INT(OFS_RETURN) = G_INT(OFS_PARM0);
	PR_CheckEmptyString (s);
/*
	if (sv.state != ss_loading)
	{
		PR_BIError (prinst, "PF_Precache_*: Precache can only be done in spawn functions (%s)", s);
		return;
	}
*/
	G_INT(OFS_RETURN) = G_INT(OFS_PARM0);

	if (s[0] <= ' ')
	{
		PR_RunError ("PF_precache_sound: Bad string");
		return;
	}

	for (i=1 ; i<MAX_SOUNDS ; i++)
	{
		if (!*sv.sound_precache[i])
		{
			strcpy(sv.sound_precache[i], s);


			if (sv.state != ss_loading)
			{
				sv.model_precache[i] = s;
				//MSG_WriteByte(&sv.reliable_datagram, 77/*svc_precache*/);
				//MSG_WriteShort(&sv.reliable_datagram, i+32768);
				//MSG_WriteString(&sv.reliable_datagram, s);
#ifdef NQPROT
				MSG_WriteByte(&sv.nqreliable_datagram, svcdp_precache);
				MSG_WriteShort(&sv.nqreliable_datagram, i+32768);
				MSG_WriteString(&sv.nqreliable_datagram, s);
#endif
			}
			return;
		}
		if (!strcmp(sv.sound_precache[i], s))
			return;
	}
	PR_RunError ("PF_precache_sound: overflow");
}
#else
void PF_precache_sound (void)
{
	char	*s;
	int		i;
	
	if (sv.state != ss_loading)
		PR_RunError ("PF_Precache_*: Precache can only be done in spawn functions");
		
	s = G_STRING(OFS_PARM0);
	G_INT(OFS_RETURN) = G_INT(OFS_PARM0);
	PR_CheckEmptyString (s);
	
	for (i=0 ; i<255/*MAX_SOUNDS*/ ; i++)	// xavior: screw 256, crashes clients
	{
		if (!sv.sound_precache[i])
		{
			//Con_Printf("%i. Precaching sound: %s\n", i, s);
			sv.sound_precache[i] = s;
			return;
		}
		if (!strcmp(sv.sound_precache[i], s))
			return;
	}
	Con_Printf("No room to precache SOUND: %s\n", s);
	//PR_RunError ("PF_precache_sound: overflow");
}
#endif

void PF_precache_model (void)
{
	char	*s;
	int		i;

	if (sv.state != ss_loading)
		PR_RunError ("PF_Precache_*: Precache can only be done in spawn functions");

	s = G_STRING(OFS_PARM0);
	G_INT(OFS_RETURN) = G_INT(OFS_PARM0);
	PR_CheckEmptyString (s);

	for (i=0 ; i<MAX_MODELS ; i++)
	{
		if (!sv.model_precache[i])
		{
			//Con_Printf("%i. Precaching model: %s\n", i, s);
			if (i > 255)
				Con_Printf("WARNING: Model count over 255 but precaching anyways: %s\n", s);
			sv.model_precache[i] = s;
			if (!strcmp(sv.model_precache[i],"sprites/doom/caco.spr") || !strcmp(sv.model_precache[i],"sprites/doom/doomg.spr")) {
				doom_map = 1;
			}
			return;
		}
		if (!strcmp(sv.model_precache[i], s))
			return;
	}
	Con_Printf("No room to precache model: %s\n", s);
	//PR_RunError ("PF_precache_model: overflow");
}

#ifdef VWEP_TEST
static void PF_precache_vwep_model (void)
{
	char	*s;
	int		i;
	
	if (sv.state != ss_loading)
		PR_RunError ("PF_Precache_*: Precache can only be done in spawn functions");
		
	s = G_STRING(OFS_PARM0);
	PR_CheckEmptyString (s);

	// the strings are transferred via the stufftext mechanism, hence the stringency
	if (strchr(s, '"') || strchr(s, ';') || strchr(s, '\n'  ) || strchr(s, '\t') || strchr(s, ' '))
		PR_RunError ("Bad string\n");

	for (i = 0; i < MAX_VWEP_MODELS; i++)
	{
		if (!sv.vw_model_name[i]) {
			sv.vw_model_name[i] = s;
			G_INT(OFS_RETURN) = i;
			return;
		}
	}
	PR_RunError ("PF_precache_vwep_model: overflow");
}
#endif


void PF_coredump (void)
{
	ED_PrintEdicts ();
}

void PF_traceon (void)
{
	pr_trace = true;
}

void PF_traceoff (void)
{
	pr_trace = false;
}

void PF_eprint (void)
{
	ED_PrintNum (G_EDICTNUM(OFS_PARM0));
}

/*
===============
PF_walkmove
 
float(float yaw, float dist) walkmove
===============
*/
void PF_walkmove (void)
{
	edict_t	*ent;
	float	yaw, dist;
	vec3_t	move;
	dfunction_t	*oldf;
	int 	oldself;

	ent = PROG_TO_EDICT(pr_global_struct->self);
	yaw = G_FLOAT(OFS_PARM0);
	dist = G_FLOAT(OFS_PARM1);

	if ( !( (int)ent->v.flags & (FL_ONGROUND|FL_FLY|FL_SWIM) ) )
	{
		G_FLOAT(OFS_RETURN) = 0;
		return;
	}

	yaw = yaw*M_PI*2 / 360;

	move[0] = cos(yaw)*dist;
	move[1] = sin(yaw)*dist;
	move[2] = 0;

	// save program state, because SV_movestep may call other progs
	oldf = pr_xfunction;
	oldself = pr_global_struct->self;

	G_FLOAT(OFS_RETURN) = SV_movestep(ent, move, true);


	// restore program state
	pr_xfunction = oldf;
	pr_global_struct->self = oldself;
}

/*
===============
PF_droptofloor
 
void() droptofloor
===============
*/
void PF_droptofloor (void)
{
	edict_t		*ent;
	vec3_t		end;
	trace_t		trace;

	ent = PROG_TO_EDICT(pr_global_struct->self);

	VectorCopy (ent->v.origin, end);
	end[2] -= 256;

	trace = SV_Trace (ent->v.origin, ent->v.mins, ent->v.maxs, end, false, ent);

	if (trace.fraction == 1 || trace.allsolid)
		G_FLOAT(OFS_RETURN) = 0;
	else
	{
		VectorCopy (trace.endpos, ent->v.origin);
		SV_LinkEdict (ent, false);
		ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
		ent->v.groundentity = EDICT_TO_PROG(trace.e.ent);
		G_FLOAT(OFS_RETURN) = 1;
	}
}

/*
===============
PF_lightstyle
 
void(float style, string value) lightstyle
===============
*/
void PF_lightstyle (void)
{
	int		style;
	char	*val;
	client_t	*client;
	int			j;

	style = G_FLOAT(OFS_PARM0);
	val = G_STRING(OFS_PARM1);

	// change the string in sv
	sv.lightstyles[style] = val;

	// send message to all clients on this server
	if (sv.state != ss_active)
		return;

	for (j=0, client = svs.clients ; j<MAX_CLIENTS ; j++, client++)
		if ( client->state == cs_spawned )
		{
			ClientReliableWrite_Begin (client, svc_lightstyle, strlen(val)+3);
			ClientReliableWrite_Char (client, style);
			ClientReliableWrite_String (client, val);
		}
	if (sv.mvdrecording)
	{
		if (MVDWrite_Begin( dem_all, 0, strlen(val)+3))
		{
			MVD_MSG_WriteByte(svc_lightstyle);
			MVD_MSG_WriteChar(style);
			MVD_MSG_WriteString(val);
		}
	}
}

void PF_rint (void)
{
	float	f;
	f = G_FLOAT(OFS_PARM0);
	if (f > 0)
		G_FLOAT(OFS_RETURN) = (int)(f + 0.5);
	else
		G_FLOAT(OFS_RETURN) = (int)(f - 0.5);
}
void PF_floor (void)
{
	G_FLOAT(OFS_RETURN) = floor(G_FLOAT(OFS_PARM0));
}
void PF_ceil (void)
{
	G_FLOAT(OFS_RETURN) = ceil(G_FLOAT(OFS_PARM0));
}


/*
=============
PF_checkbottom
=============
*/
void PF_checkbottom (void)
{
	edict_t	*ent;

	ent = G_EDICT(OFS_PARM0);

	G_FLOAT(OFS_RETURN) = SV_CheckBottom (ent);
}

/*
=============
PF_pointcontents
=============
*/
void PF_pointcontents (void)
{
	float	*v;

	v = G_VECTOR(OFS_PARM0);

	G_FLOAT(OFS_RETURN) = SV_PointContents (v);
}

/*
=============
PF_nextent
 
entity nextent(entity)
=============
*/
void PF_nextent (void)
{
	int		i;
	edict_t	*ent;

	i = G_EDICTNUM(OFS_PARM0);
	while (1)
	{
		i++;
		if (i == sv.num_edicts)
		{
			RETURN_EDICT(sv.edicts);
			return;
		}
		ent = EDICT_NUM(i);
		if (!ent->free)
		{
			RETURN_EDICT(ent);
			return;
		}
	}
}

/*
=============
PF_aim
 
Pick a vector for the player to shoot along
vector aim(entity, missilespeed)
=============
*/
void PF_aim (void)
{
	VectorCopy (pr_global_struct->v_forward, G_VECTOR(OFS_RETURN));
}

/*
==============
PF_changeyaw
 
This was a major timewaster in progs, so it was converted to C
==============
*/
void PF_changeyaw (void)
{
	edict_t		*ent;
	float		ideal, current, move, speed;

	ent = PROG_TO_EDICT(pr_global_struct->self);
	current = anglemod( ent->v.angles[1] );
	ideal = ent->v.ideal_yaw;
	speed = ent->v.yaw_speed;

	if (current == ideal)
		return;
	move = ideal - current;
	if (ideal > current)
	{
		if (move >= 180)
			move = move - 360;
	}
	else
	{
		if (move <= -180)
			move = move + 360;
	}
	if (move > 0)
	{
		if (move > speed)
			move = speed;
	}
	else
	{
		if (move < -speed)
			move = -speed;
	}

	ent->v.angles[1] = anglemod (current + move);
}

/*
===============================================================================
 
MESSAGE WRITING
 
===============================================================================
*/

#define	MSG_BROADCAST	0		// unreliable to all
#define	MSG_ONE			1		// reliable to one (msg_entity)
#define	MSG_ALL			2		// reliable to all
#define	MSG_INIT		3		// write to the init string
#define	MSG_MULTICAST	4		// for multicast()

sizebuf_t *WriteDest (void)
{
	int		dest;
	//	int		entnum;
	//	edict_t	*ent;

	dest = G_FLOAT(OFS_PARM0);
	switch (dest)
	{
	case MSG_BROADCAST:
		return &sv.datagram;

	case MSG_ONE:
		SV_Error("Shouldn't be at MSG_ONE");
#if 0
		ent = PROG_TO_EDICT(pr_global_struct->msg_entity);
		entnum = NUM_FOR_EDICT(ent);
		if (entnum < 1 || entnum > MAX_CLIENTS)
			PR_RunError ("WriteDest: not a client");
		return &svs.clients[entnum-1].netchan.message;
#endif

	case MSG_ALL:
		return &sv.reliable_datagram;

	case MSG_INIT:
		if (sv.state != ss_loading)
			PR_RunError ("PF_Write_*: MSG_INIT can only be written in spawn functions");
		return &sv.signon;

	case MSG_MULTICAST:
		return &sv.multicast;

	default:
		PR_RunError ("WriteDest: bad destination");
		break;
	}

	return NULL;
}

static client_t *Write_GetClient(void)
{
	int		entnum;
	edict_t	*ent;

	ent = PROG_TO_EDICT(pr_global_struct->msg_entity);
	entnum = NUM_FOR_EDICT(ent);
	if (entnum < 1 || entnum > MAX_CLIENTS)
		PR_RunError ("WriteDest: not a client");
	return &svs.clients[entnum-1];
}


void PF_WriteByte (void)
{
	if (G_FLOAT(OFS_PARM0) == MSG_ONE)
	{
		client_t *cl = Write_GetClient();
		ClientReliableCheckBlock(cl, 1);
		ClientReliableWrite_Byte(cl, G_FLOAT(OFS_PARM1));
		if (sv.mvdrecording)
		{
			if (MVDWrite_Begin(dem_single, cl - svs.clients, 1))
			{
				MVD_MSG_WriteByte(G_FLOAT(OFS_PARM1));
			}
		}
	}
	else
		MSG_WriteByte (WriteDest(), G_FLOAT(OFS_PARM1));
}

void PF_WriteChar (void)
{
	if (G_FLOAT(OFS_PARM0) == MSG_ONE)
	{
		client_t *cl = Write_GetClient();
		ClientReliableCheckBlock(cl, 1);
		ClientReliableWrite_Char(cl, G_FLOAT(OFS_PARM1));
		if (sv.mvdrecording)
		{
			if (MVDWrite_Begin(dem_single, cl - svs.clients, 1))
			{
				MVD_MSG_WriteByte(G_FLOAT(OFS_PARM1));
			}
		}
	}
	else
		MSG_WriteChar (WriteDest(), G_FLOAT(OFS_PARM1));
}

void PF_WriteShort (void)
{
	if (G_FLOAT(OFS_PARM0) == MSG_ONE)
	{
		client_t *cl = Write_GetClient();
		ClientReliableCheckBlock(cl, 2);
		ClientReliableWrite_Short(cl, G_FLOAT(OFS_PARM1));
		if (sv.mvdrecording)
		{
			if (MVDWrite_Begin(dem_single, cl - svs.clients, 2))
			{
				MVD_MSG_WriteShort(G_FLOAT(OFS_PARM1));
			}
		}
	}
	else
		MSG_WriteShort (WriteDest(), G_FLOAT(OFS_PARM1));
}

void PF_WriteLong (void)
{
	if (G_FLOAT(OFS_PARM0) == MSG_ONE)
	{
		client_t *cl = Write_GetClient();
		ClientReliableCheckBlock(cl, 4);
		ClientReliableWrite_Long(cl, G_FLOAT(OFS_PARM1));
		if (sv.mvdrecording)
		{
			if (MVDWrite_Begin(dem_single, cl - svs.clients, 4))
			{
				MVD_MSG_WriteLong(G_FLOAT(OFS_PARM1));
			}
		}
	}
	else
		MSG_WriteLong (WriteDest(), G_FLOAT(OFS_PARM1));
}

void PF_WriteAngle (void)
{
	if (G_FLOAT(OFS_PARM0) == MSG_ONE)
	{
		client_t *cl = Write_GetClient();
		ClientReliableCheckBlock(cl, 1);
		ClientReliableWrite_Angle(cl, G_FLOAT(OFS_PARM1));
		if (sv.mvdrecording)
		{
			if (MVDWrite_Begin(dem_single, cl - svs.clients, 1))
			{
				MVD_MSG_WriteByte(G_FLOAT(OFS_PARM1));
			}
		}
	}
	else
		MSG_WriteAngle (WriteDest(), G_FLOAT(OFS_PARM1));
}

void PF_WriteCoord (void)
{
	if (G_FLOAT(OFS_PARM0) == MSG_ONE)
	{
		client_t *cl = Write_GetClient();
		ClientReliableCheckBlock(cl, 2);
		ClientReliableWrite_Coord(cl, G_FLOAT(OFS_PARM1));
		if (sv.mvdrecording)
		{
			if (MVDWrite_Begin(dem_single, cl - svs.clients, 2))
			{
				MVD_MSG_WriteCoord(G_FLOAT(OFS_PARM1));
			}
		}
	}
	else
		MSG_WriteCoord (WriteDest(), G_FLOAT(OFS_PARM1));
}

void PF_WriteString (void)
{
	if (G_FLOAT(OFS_PARM0) == MSG_ONE)
	{
		client_t *cl = Write_GetClient();
		ClientReliableCheckBlock(cl, 1+strlen(G_STRING(OFS_PARM1)));
		ClientReliableWrite_String(cl, G_STRING(OFS_PARM1));
		if (sv.mvdrecording)
		{
			if (MVDWrite_Begin(dem_single, cl - svs.clients, 1 + strlen(G_STRING(OFS_PARM1))))
			{
				MVD_MSG_WriteString(G_STRING(OFS_PARM1));
			}
		}
	}
	else
		MSG_WriteString (WriteDest(), G_STRING(OFS_PARM1));
}


void PF_WriteEntity (void)
{
	if (G_FLOAT(OFS_PARM0) == MSG_ONE)
	{
		client_t *cl = Write_GetClient();
		ClientReliableCheckBlock(cl, 2);
		ClientReliableWrite_Short(cl, G_EDICTNUM(OFS_PARM1));
		if (sv.mvdrecording)
		{
			if (MVDWrite_Begin(dem_single, cl - svs.clients, 2))
			{
				MVD_MSG_WriteShort(G_EDICTNUM(OFS_PARM1));
			}
		}
	}
	else
		MSG_WriteShort (WriteDest(), G_EDICTNUM(OFS_PARM1));
}

//=============================================================================

int SV_ModelIndex (char *name);
#define FTE_METHOD
void PF_makestatic (void)
{
#ifdef FTE_METHOD
	edict_t	*ent;
	int		mdlindex, i;
	entity_state_t *state;

	ent = G_EDICT(OFS_PARM0);

	SV_FlushSignon ();

	mdlindex = SV_ModelIndex(PR_GetString(ent->v.model));

	if (mdlindex > 255 || ent->v.frame > 255)
	{
		if (sv.numextrastatics==sizeof(sv.extendedstatics)/sizeof(sv.extendedstatics[0]))
			return;	//fail the whole makestatic thing.

		state = &sv.extendedstatics[sv.numextrastatics++];
		memset(state, 0, sizeof(*state));
		state->number = sv.numextrastatics;
		state->flags = 0;
		VectorCopy (ent->v.origin, state->origin);
		VectorCopy (ent->v.angles, state->angles);
		state->modelindex = mdlindex;//ent->v->modelindex;
		state->frame = ent->v.frame;
		state->colormap = ent->v.colormap;
		state->skinnum = ent->v.skin;
		state->effects = ent->v.effects;
	}
	else
	{
		MSG_WriteByte (&sv.signon,svc_spawnstatic);

		MSG_WriteByte (&sv.signon, mdlindex&255);

		MSG_WriteByte (&sv.signon, ent->v.frame);
		MSG_WriteByte (&sv.signon, (int)ent->v.colormap);
		MSG_WriteByte (&sv.signon, (int)ent->v.skin);
		for (i=0 ; i<3 ; i++)
		{
			MSG_WriteCoord(&sv.signon, ent->v.origin[i]);
			MSG_WriteAngle(&sv.signon, ent->v.angles[i]);
		}
	}

// throw the entity away now
	ED_Free (ent);
#else
	edict_t	*ent;
	int		i;

	ent = G_EDICT(OFS_PARM0);
	//bliP: for maps with null models which crash clients (nmtrees.bsp) ->
	if (!SV_ModelIndex(PR_GetString(ent->v.model)))
		return;
	//<-

	MSG_WriteByte (&sv.signon,svc_spawnstatic);

	MSG_WriteByte (&sv.signon, SV_ModelIndex(PR_GetString(ent->v.model)));

	MSG_WriteByte (&sv.signon, ent->v.frame);
	MSG_WriteByte (&sv.signon, ent->v.colormap);
	MSG_WriteByte (&sv.signon, ent->v.skin);
	for (i=0 ; i<3 ; i++)
	{
		MSG_WriteCoord(&sv.signon, ent->v.origin[i]);
		MSG_WriteAngle(&sv.signon, ent->v.angles[i]);
	}

	// throw the entity away now
	ED_Free (ent);
#endif
}

//=============================================================================

/*
==============
PF_setspawnparms
==============
*/
void PF_setspawnparms (void)
{
	edict_t	*ent;
	int		i;
	client_t	*client;

	ent = G_EDICT(OFS_PARM0);
	i = NUM_FOR_EDICT(ent);
	if (i < 1 || i > MAX_CLIENTS)
		PR_RunError ("Entity is not a client");

	// copy spawn parms out of the client_t
	client = svs.clients + (i-1);

	for (i=0 ; i< NUM_SPAWN_PARMS ; i++)
		(&pr_global_struct->parm1)[i] = client->spawn_parms[i];
}

/*
==============
PF_changelevel
==============
*/
void PF_changelevel (void)
{
	char	*s;
	static	int	last_spawncount;

	// make sure we don't issue two changelevels
	if (svs.spawncount == last_spawncount)
		return;
	last_spawncount = svs.spawncount;

	s = G_STRING(OFS_PARM0);
	Cbuf_AddText (va("map %s\n",s));
}


/*
==============
PF_logfrag
 
logfrag (killer, killee)
==============
*/
void PF_logfrag (void)
{
	edict_t	*ent1, *ent2;
	int		e1, e2;
	char	*s;
	// -> scream
	time_t		t;
	struct tm	*tblock;
	// <-

	ent1 = G_EDICT(OFS_PARM0);
	ent2 = G_EDICT(OFS_PARM1);

	e1 = NUM_FOR_EDICT(ent1);
	e2 = NUM_FOR_EDICT(ent2);

	if (e1 < 1 || e1 > MAX_CLIENTS || e2 < 1 || e2 > MAX_CLIENTS)
		return;
	// -> scream
	t = time (NULL);
	tblock = localtime (&t);

	//bliP: date check ->
	if (!tblock)
		s = va("%s\n", "#bad date#");
	else
		if ((int)frag_log_type.value) // need for old-style frag log file
			s = va("\\frag\\%s\\%s\\%s\\%s\\%d-%d-%d %d:%d:%d\\\n",
			       svs.clients[e1-1].name, svs.clients[e2-1].name,
			       svs.clients[e1-1].team, svs.clients[e2-1].team,
			       tblock->tm_year + 1900, tblock->tm_mon + 1, tblock->tm_mday,
			       tblock->tm_hour, tblock->tm_min, tblock->tm_sec);
		else
			s = va("\\%s\\%s\\\n",svs.clients[e1-1].name, svs.clients[e2-1].name);
	// <-
	SZ_Print (&svs.log[svs.logsequence&1], s);
	SV_Write_Log(FRAG_LOG, 1, s);
	//	SV_Write_Log(MOD_FRAG_LOG, 1, "\n==== PF_logfrag ===={\n");
	//	SV_Write_Log(MOD_FRAG_LOG, 1, va("%d\n", time(NULL)));
	//	SV_Write_Log(MOD_FRAG_LOG, 1, s);
	//	SV_Write_Log(MOD_FRAG_LOG, 1, "}====================\n");
}

//bliP: map voting ->
/*==================
PF_findmap
finds maps in sv_gamedir either by id number or name
returns id for exist, 0 for not
float(string s) findmap
==================*/
void PF_findmap (void)
{
	dir_t	dir;
	file_t *list;
	char map[MAX_DEMO_NAME];
	char *s;
	int id;
	int i;

	strlcpy(map, G_STRING(OFS_PARM0), sizeof(map));
	for (i = 0, s = map; *s; s++)
	{
		if (*s < '0' || *s > '9')
		{
			i = 1;
			break;
		}
	}
	id = (i) ? 0 : Q_atoi(map);

	if (!strstr(map, ".bsp"))
		strlcat(map, ".bsp", sizeof(map));

	dir = Sys_listdir(va("%s/maps", Info_ValueForKey(svs.info, "*gamedir")),
	                  ".bsp$", SORT_BY_NAME);
	list = dir.files;

	i = 1;
	while (list->name[0])
	{
		if (((id > 0) && (i == id)) || !strcmp(list->name, map))
		{
			G_FLOAT(OFS_RETURN) = i;
			return;
		}
		i++;
		list++;
	}

	G_FLOAT(OFS_RETURN) = 0;
}

/*==================
PF_findmapname
returns map name from a map id
string(float id) findmapname
==================*/
void PF_findmapname (void)
{
	dir_t	dir;
	file_t *list;
	//char *s;
	int id;
	int i;

	id = G_FLOAT(OFS_PARM0);

	dir = Sys_listdir(va("%s/maps", Info_ValueForKey(svs.info, "*gamedir")),
	                  ".bsp$", SORT_BY_NAME);
	list = dir.files;

	i = 1;
	while (list->name[0])
	{
		if (i == id)
		{
			list->name[strlen(list->name) - 4] = 0; //strip .bsp
			//if ((s = strchr(list->name, '.')))
			//  *s = '\0';
			RETURN_STRING(list->name);
			return;
		}
		i++;
		list++;
	}
	G_FLOAT(OFS_RETURN) = 0;
}

/*==================
PF_listmaps
prints a range of map names from sv_gamedir (because of the likes of thundervote)
returns position if more maps, 0 if displayed them all
float(entity client, float level, float range, float start, float style, float footer) listmaps
==================*/
void PF_listmaps (void)
{
	int entnum, level, start, range, foot, style;
	client_t	*client;
	char line[256];
	char tmp[64];
	char num[16];
	dir_t	dir;
	file_t *list;
	//char *s;
	int id, pad;
	int ti, i, j;

	entnum = G_EDICTNUM(OFS_PARM0);
	level = G_FLOAT(OFS_PARM1);
	range = G_FLOAT(OFS_PARM2);
	start = G_FLOAT(OFS_PARM3);
	style = G_FLOAT(OFS_PARM4);
	foot = G_FLOAT(OFS_PARM5);

	if (entnum < 1 || entnum > MAX_CLIENTS)
	{
		Con_Printf ("tried to listmap to a non-client\n");
		G_FLOAT(OFS_RETURN) = 0;
		return;
	}

	client = &svs.clients[entnum-1];
	dir = Sys_listdir(va("%s/maps", Info_ValueForKey(svs.info, "*gamedir")),
	                  ".bsp$", SORT_BY_NAME);
	list = dir.files;
	snprintf(tmp, sizeof(tmp), "%d", dir.numfiles);
	pad = strlen(tmp);

	if (!list->name[0])
	{
		SV_ClientPrintf(client, level, "No maps.\n");
		G_FLOAT(OFS_RETURN) = 0;
		return;
	}

	//don't go off the end of the world
	if ((range < 0) || (range > dir.numfiles))
	{
		range = dir.numfiles;
	}
	start--;
	if ((start < 0) || (start > dir.numfiles-1))
	{
		start = 0;
	}
	ti = (range <= 10) ? range : 10;

	//header - progs can do this
	/*if (!start) {
	  SV_ClientPrintf(client, level, "Available maps:\n");
	}*/

	list = dir.files + start;
	line[0] = '\0';
	j = 1;
	for (i = 0, id = start + 1; list->name[0] && i < range && id < dir.numfiles + 1; id++)
	{
		list->name[strlen(list->name) - 4] = 0; //strip .bsp
		//if ((s = strchr(list->name, '.'))) //strip .bsp
		//	*s = '\0';
		switch (style)
		{
		case 1:
			if (i % ti == 0)
			{ //print header
				snprintf(tmp, sizeof(tmp), "%d-%d", id, id + ti - 1);
				num[0] = '\0';
				for (j = strlen(tmp); j < ((pad * 2) + 1); j++) //padding to align
					strlcat(num, " ", sizeof(num));
				SV_ClientPrintf(client, level, "%s%s %c ", num, tmp, 133);
				j = 1;
			}
			i++;
			//print id and name
			snprintf(tmp, sizeof(tmp), "%d:%s ", j++, list->name);
			if (i % 2 != 0) //red every second
				Q_redtext((unsigned char*)tmp);
			strlcat(line, tmp, sizeof(line));
			if (i % 10 == 0)
			{ //print entire line
				SV_ClientPrintf(client, level, "%s\n", line);
				line[0] = '\0';
			}
			break;
		case 2:
			snprintf(tmp, sizeof(tmp), "%d", id);
			num[0] = '\0';
			for (j = strlen(tmp); j < pad; j++) //padding to align
				strlcat(num, " ", sizeof(num));
			Q_redtext((unsigned char*)tmp);
			SV_ClientPrintf(client, level, "%s%s%c %s\n", num, tmp, 133, list->name);
			break;
		case 3:
			list->name[13] = 0;
			snprintf(tmp, sizeof(tmp), "%03d", id);
			Q_redtext((unsigned char*)tmp);
			snprintf(line, sizeof(line), "%s\x85%-13s", tmp, list->name);
			id++;
			list++;
			if (!list->name[0])
				continue;
			list->name[13] = 0;
			list->name[strlen(list->name) - 4] = 0;
			snprintf(tmp, sizeof(tmp), "%03d", id);
			Q_redtext((unsigned char*)tmp);
			SV_ClientPrintf(client, level, "%s %s\x85%-13s\n", line, tmp, list->name);
			line[0] = 0; //bliP: 24/9 bugfix
			break;
		default:
			snprintf(tmp, sizeof(tmp), "%d", id);
			Q_redtext((unsigned char*)tmp);
			SV_ClientPrintf(client, level, "%s%c%s%s", tmp, 133, list->name, (i == range) ? "\n" : " ");
			i++;
		}
		list++;
	}
	if (((style == 1) || (style == 3)) && line[0]) //still things to print
		SV_ClientPrintf(client, level, "%s\n", line);
	else if (style == 0)
		SV_ClientPrintf(client, level, "\n");

	if (id < dir.numfiles + 1)
	{ //more to come
		G_FLOAT(OFS_RETURN) = id;
		return;
	}

	if (foot)
	{ //footer
		strlcpy(tmp, "Total:", sizeof(tmp));
		Q_redtext((unsigned char*)tmp);
		SV_ClientPrintf (client, level,	"%s %d maps %.0fKB (%.2fMB)\n", tmp, dir.numfiles, (float)dir.size/1024, (float)dir.size/1024/1024);
	}

	G_FLOAT(OFS_RETURN) = 0;
}
//<-

/*
==============
PF_infokey

string(entity e, string key) infokey
==============
*/
void PF_infokey (void)
{
	edict_t	*e;
	int		e1;
	char	*value;
	char	*key;
	static	char ov[256];
	client_t *cl;

	e = G_EDICT(OFS_PARM0);
	e1 = NUM_FOR_EDICT(e);
	key = G_STRING(OFS_PARM1);
	cl = &svs.clients[e1-1];

	if (e1 == 0)
	{
		if (is_ktpro && !strncmp(key, "*version", 9))
			value = QW_VERSION;
		else if (is_ktpro && !strncmp(key, "*qwe_version", 13))
			value = QWE_VERSION;
		else if ((value = Info_ValueForKey (svs.info, key)) == NULL || !*value)
			value = Info_ValueForKey(localinfo, key);
	}
	else if (e1 <= MAX_CLIENTS)
	{
		value = ov;
		if (!strncmp(key, "ip", 3))
			strlcpy(ov, NET_BaseAdrToString (cl->netchan.remote_address), sizeof(ov));
		else if (!strncmp(key, "realip", 7))
			strlcpy(ov, NET_BaseAdrToString (cl->realip), sizeof(ov));
		else if (!strncmp(key, "download", 9))
			//snprintf(ov, sizeof(ov), "%d", cl->download != NULL ? (int)(100*cl->downloadcount/cl->downloadsize) : -1);
			snprintf(ov, sizeof(ov), "%d", cl->file_percent ? cl->file_percent : -1); //bliP: file percent
		else if (!strncmp(key, "ping", 5))
			snprintf(ov, sizeof(ov), "%d", SV_CalcPing (cl));
		else if (!strncmp(key, "*userid", 8))
			snprintf(ov, sizeof(ov), "%d", svs.clients[e1 - 1].userid);
		else if (!strncmp(key, "login", 6))
			value = cl->login;
		else
			value = Info_Get (&cl->_userinfo_ctx_, key);
	}
	else
		value = "";

	strlcpy(pr_string_temp, value, MAX_PR_STRING_SIZE);
	RETURN_STRING(pr_string_temp);
	PF_SetTempString();
}

/*
==============
PF_stof
 
float(string s) stof
==============
*/
void PF_stof (void)
{
	char	*s;

	s = G_STRING(OFS_PARM0);

	G_FLOAT(OFS_RETURN) = Q_atof(s);
}


/*
==============
PF_multicast
 
void(vector where, float set) multicast
==============
*/
void PF_multicast (void)
{
	float	*o;
	int		to;

	o = G_VECTOR(OFS_PARM0);
	to = G_FLOAT(OFS_PARM1);

	SV_Multicast (o, to, 0, 0);
}

//DP_QC_SINCOSSQRTPOW
//float sin(float x) = #60
void PF_sin (void)
{
	G_FLOAT(OFS_RETURN) = sin(G_FLOAT(OFS_PARM0));
}

//DP_QC_SINCOSSQRTPOW
//float cos(float x) = #61
void PF_cos (void)
{
	G_FLOAT(OFS_RETURN) = cos(G_FLOAT(OFS_PARM0));
}

//DP_QC_SINCOSSQRTPOW
//float sqrt(float x) = #62
void PF_sqrt (void)
{
	G_FLOAT(OFS_RETURN) = sqrt(G_FLOAT(OFS_PARM0));
}

//DP_QC_SINCOSSQRTPOW
//float pow(float x, float y) = #97;
static void PF_pow (void)
{
	G_FLOAT(OFS_RETURN) = pow(G_FLOAT(OFS_PARM0), G_FLOAT(OFS_PARM1));
}

//DP_QC_MINMAXBOUND
//float min(float a, float b, ...) = #94
void PF_min (void)
{
	// LordHavoc: 3+ argument enhancement suggested by FrikaC
	if (pr_argc == 2)
		G_FLOAT(OFS_RETURN) = min(G_FLOAT(OFS_PARM0), G_FLOAT(OFS_PARM1));
	else if (pr_argc >= 3)
	{
		int i;
		float f = G_FLOAT(OFS_PARM0);
		for (i = 1;i < pr_argc;i++)
			if (G_FLOAT((OFS_PARM0+i*3)) < f)
				f = G_FLOAT((OFS_PARM0+i*3));
		G_FLOAT(OFS_RETURN) = f;
	}
	else
		Sys_Error("min: must supply at least 2 floats\n");
}

//DP_QC_MINMAXBOUND
//float max(float a, float b, ...) = #95
void PF_max (void)
{
	// LordHavoc: 3+ argument enhancement suggested by FrikaC
	if (pr_argc == 2)
		G_FLOAT(OFS_RETURN) = max(G_FLOAT(OFS_PARM0), G_FLOAT(OFS_PARM1));
	else if (pr_argc >= 3)
	{
		int i;
		float f = G_FLOAT(OFS_PARM0);
		for (i = 1;i < pr_argc;i++)
			if (G_FLOAT((OFS_PARM0+i*3)) > f)
				f = G_FLOAT((OFS_PARM0+i*3));
		G_FLOAT(OFS_RETURN) = f;
	}
	else
		Sys_Error("max: must supply at least 2 floats\n");
}

//DP_QC_MINMAXBOUND
//float bound(float min, float value, float max) = #96
void PF_bound (void)
{
	G_FLOAT(OFS_RETURN) = bound(G_FLOAT(OFS_PARM0), G_FLOAT(OFS_PARM1), G_FLOAT(OFS_PARM2));
}

/*
=================
PF_tracebox

Like traceline but traces a box of the size specified
(NOTE: currently the hull size can only be one of the sizes used in the map
for bmodel collisions, entity collisions will pay attention to the exact size
specified however, this is a collision code limitation in quake itself,
and will be fixed eventually).

DP_QC_TRACEBOX

void(vector v1, vector mins, vector maxs, vector v2, float nomonsters, entity ignore) tracebox = #90;
=================
*/
static void PF_tracebox (void)
{
        float       *v1, *v2, *mins, *maxs;
        edict_t     *ent;
        int          nomonsters;
        trace_t      trace;

        v1 = G_VECTOR(OFS_PARM0);
        mins = G_VECTOR(OFS_PARM1);
        maxs = G_VECTOR(OFS_PARM2);
        v2 = G_VECTOR(OFS_PARM3);
        nomonsters = G_FLOAT(OFS_PARM4);
        ent = G_EDICT(OFS_PARM5);

        trace = SV_Trace (v1, mins, maxs, v2, nomonsters, ent);

        pr_global_struct->trace_allsolid = trace.allsolid;
        pr_global_struct->trace_startsolid = trace.startsolid;
        pr_global_struct->trace_fraction = trace.fraction;
        pr_global_struct->trace_inwater = trace.inwater;
        pr_global_struct->trace_inopen = trace.inopen;
        VectorCopy (trace.endpos, pr_global_struct->trace_endpos);
        VectorCopy (trace.plane.normal, pr_global_struct->trace_plane_normal);
        pr_global_struct->trace_plane_dist =  trace.plane.dist;
        if (trace.e.ent)
                pr_global_struct->trace_ent = EDICT_TO_PROG(trace.e.ent);
        else
                pr_global_struct->trace_ent = EDICT_TO_PROG(sv.edicts);
}

/*
=================
PF_randomvec

DP_QC_RANDOMVEC
vector randomvec() = #91
=================
*/
static void PF_randomvec (void)
{
	vec3_t temp;

	do {
		temp[0] = (rand() & 0x7fff) * (2.0 / 0x7fff) - 1.0;
		temp[1] = (rand() & 0x7fff) * (2.0 / 0x7fff) - 1.0;
		temp[2] = (rand() & 0x7fff) * (2.0 / 0x7fff) - 1.0;
	} while (DotProduct(temp, temp) >= 1);

	VectorCopy (temp, G_VECTOR(OFS_RETURN));
}

/*
=================
PF_cvar_string

QSG_CVARSTRING DP_QC_CVAR_STRING
string cvar_string(string varname) = #103;
=================
*/
static void PF_cvar_string (void)
{
	char	*str,*vart;
	//cvar_t	*var;

	str = G_STRING(OFS_PARM0);
	vart = Cvar_String(str);
	/*
	var = Cvar_FindVar(str);
	if (!var) {
		G_INT(OFS_RETURN) = 0;
		return;
	}
	strlcpy (pr_string_temp, var->string, sizeof(pr_string_temp));
	RETURN_STRING(pr_string_temp);
	*/
	RETURN_STRING(vart);
}


// ZQ_PAUSE
// void(float pause) setpause = #531;
void PF_setpause (void)
{
	qbool pause;

	pause = G_FLOAT(OFS_PARM0) ? true : false;
	if (pause != sv.paused)
		SV_TogglePause (NULL);
}


/*
==============
PF_checkextension

float checkextension(string extension) = #99;
==============
*/
static void PF_checkextension (void)
{
	static char *supported_extensions[] = {
		"DP_CON_SET",               // http://wiki.quakesrc.org/index.php/DP_CON_SET
		"DP_QC_CVAR_STRING",		// http://wiki.quakesrc.org/index.php/DP_QC_CVAR_STRING
		"DP_QC_MINMAXBOUND",        // http://wiki.quakesrc.org/index.php/DP_QC_MINMAXBOUND
		"DP_QC_RANDOMVEC",			// http://wiki.quakesrc.org/index.php/DP_QC_RANDOMVEC
		"DP_QC_SINCOSSQRTPOW",      // http://wiki.quakesrc.org/index.php/DP_QC_SINCOSSQRTPOW
		"DP_QC_TRACEBOX",			// http://wiki.quakesrc.org/index.php/DP_QC_TRACEBOX
		"FTE_CALLTIMEOFDAY",        // http://wiki.quakesrc.org/index.php/FTE_CALLTIMEOFDAY
		"QSG_CVARSTRING",			// http://wiki.quakesrc.org/index.php/QSG_CVARSTRING
		"ZQ_CLIENTCOMMAND",			// http://wiki.quakesrc.org/index.php/ZQ_CLIENTCOMMAND
		"ZQ_ITEMS2",                // http://wiki.quakesrc.org/index.php/ZQ_ITEMS2
		"ZQ_MOVETYPE_NOCLIP",       // http://wiki.quakesrc.org/index.php/ZQ_MOVETYPE_NOCLIP
		"ZQ_MOVETYPE_FLY",          // http://wiki.quakesrc.org/index.php/ZQ_MOVETYPE_FLY
		"ZQ_MOVETYPE_NONE",         // http://wiki.quakesrc.org/index.php/ZQ_MOVETYPE_NONE
		"ZQ_PAUSE",					// http://wiki.quakesrc.org/index.php/ZQ_PAUSE
		"ZQ_QC_STRINGS",			// http://wiki.quakesrc.org/index.php/ZQ_QC_STRINGS
		"ZQ_QC_TOKENIZE",           // http://wiki.quakesrc.org/index.php/ZQ_QC_TOKENIZE
#ifdef VWEP_TEST
		"ZQ_VWEP",
#endif
		NULL
	};
	char **pstr, *extension;
	extension = G_STRING(OFS_PARM0);

	for (pstr = supported_extensions; *pstr; pstr++) {
		if (!strcasecmp(*pstr, extension)) {
			G_FLOAT(OFS_RETURN) = 1.0;	// supported
			return;
		}
	}

	G_FLOAT(OFS_RETURN) = 0.0;	// not supported
}



void PF_Fixme (void)
{
	PR_RunError ("unimplemented bulitin");
}

// XavioR: Ofn stuff.
/*
==============
PF_strcasecmp

float(string st1, string st2) strcasecmp
==============
*/

void PF_strcasecmp (void)
{
	float retval;

	char *st1;
	char *st2;
	
	st1 = G_STRING(OFS_PARM0);
	st2 = G_STRING(OFS_PARM1);

	retval = (float)strncasecmp(st1,st2,99999);
	
	G_FLOAT(OFS_RETURN) = retval;
}

/*
==============
PF_validatefile

float(string st) validatefile
==============
*/

void PF_validatefile (void)
{
	float retval;
	FILE	*f;
	char *st;
	
	st = G_STRING(OFS_PARM0);
		
	COM_FOpenFile (st, &f);
	if (!f)
		retval = (float)0;
	else
	{
		retval = (float)1;
		fclose (f);
	}	

	G_FLOAT(OFS_RETURN) = retval;
}

/*
==============
PF_setbot

void(entity botent, float botid, string botname, float botclass) setbot
==============
*/
//xavior: misc
const char *TF_GetSkin ( int cl_num )
{
	switch (cl_num) {
		case 1:
			return "airscout";
			break;
		case 2:
			return "tf_snipe";
			break;
		case 3:
			return "tf_sold";
			break;
		case 4:
			return "tf_demo";
			break;
		case 5:
			return "tf_medic";
			break;
		case 6:
			return "tf_hwguy";
			break;
		case 7:
			return "tf_pyro";
			break;
		case 8:
			return "tf_spy";
			break;
		case 9:
			return "tf_eng";
			break;
		default:
			return "tf_civ";
			break;
	}
};

//regular: void(entity botent, float botid, string botname, float botclass) setbot = #98;
//extended: void(entity botent, float botid, string botname, float botclass, botid, botshirt, botpants, botteam) setbot = #98;
void botUpdateUserInfo ( client_t *cl, int clientno, int clientid, int clientshirt, int clientpants, char *clientname, int botteam, int botskin);
void PF_setbot (void)
{
	client_t	*cl;
	edict_t	*e;
	char	*m_a;
	int		c_num, pc_num;
	//extended:
	int fBotUserID, fBotShirt, fBotPants, fBotTeam, i;
	client_t	*client;			// real client

	e = G_EDICT(OFS_PARM0);				// the bot entity
	c_num = G_FLOAT(OFS_PARM1);			// bot's fake userid (fClientNo)
	m_a = G_STRING(OFS_PARM2);			// bot's name
	pc_num = G_FLOAT(OFS_PARM3);		// bot's skin
// optionals:
	fBotUserID = G_FLOAT(OFS_PARM4);
	fBotShirt = G_FLOAT(OFS_PARM5);
	fBotPants = G_FLOAT(OFS_PARM6);
	fBotTeam = G_FLOAT(OFS_PARM7);


	if ( c_num != 0 )
		sbi.bots[c_num].state = 666;	// 666, number of the bot :D

	sbi.bots[c_num].botent = e;
//	*sbi.bots[c_num].name = m_a;

	cl = &sbi.bots[c_num];
	//strncpy (cl->name, m_a, sizeof(cl->name)-1);
	//cl->name[sizeof(cl->name) - 1] = 0;
	//Info_SetValueForKey(cl->_userinfo_ctx_,"skin",cl->name,MAX_INFO_STRING);

	strncpy (cl->pskin, TF_GetSkin(pc_num), sizeof(cl->pskin)-1);
	cl->pskin[sizeof(cl->pskin) - 1] = 0;
	Info_Set (&cl->_userinfo_ctx_,"skin",cl->pskin);

	strncpy (cl->bname, m_a, sizeof(cl->bname)-1);
	cl->bname[sizeof(cl->bname) - 1] = 0;
	Info_Set (&cl->_userinfo_ctx_,"bname",cl->bname);

	if ( fBotUserID > 0 )
	{
//void botUpdateUserInfo ( int clientno, int clientid, int clientshirt, int clientpants, char *clientname, int botteam, int botskin)
		cl->buserid = fBotUserID;
		cl->bpc_num = pc_num;
		cl->bfullupdate = c_num;
		cl->bteam = fBotTeam;
		cl->connection_started = realtime;


	for (i=0, client = svs.clients ; i<MAX_CLIENTS ; i++, client++)
	{
		if (client->state != cs_spawned)
			continue;

		/*if (sv_client->sendinfo)
		{
			sv_client->sendinfo = false;
			SV_FullClientUpdate (sv_client, &sv.reliable_datagram);
		}*/
		botUpdateUserInfo ( client, c_num, fBotUserID, fBotShirt, fBotPants, m_a, cl->bteam, cl->bpc_num );

	}


		//SZ_Clear (&sv.reliable_datagram);
		//SZ_Clear (&sv.datagram);
	}
}

// frik_file support (xavior)
// XavioR: FIXME: BROKEN!!!!!!!!! 
//fte
cvar_t pr_tempstringcount = {"pr_tempstringcount", "16"};
cvar_t pr_tempstringsize = {"pr_tempstringsize", "4096"};

//typedef unsigned char 		qbyte;
extern void Q_strncpyz(char *d, const char *s, int n);
extern void Z_Free (void *ptr);
extern qbyte *COM_LoadMallocFile (char *path);



#define PR_StackTrace(pf)									(*pf->StackTrace)			(pf)
#define PR_AbortStack(pf)									(*pf->AbortStack)			(pf)

#define PR_globals(pf, num)									(*pf->globals)				(pf, num)

#define PR_CURRENT	-1

#define MAX_QC_FILES 8

#define FIRST_QC_FILE_INDEX 1000

typedef struct {
	char name[256];
	char *data;
	int bufferlen;
	int len;
	int ofs;
	int accessmode;
	progfuncs_t *prinst;
} pf_fopen_files_t;
pf_fopen_files_t pf_fopen_files[MAX_QC_FILES];

progfuncs_t *svprogfuncs;

//#define PR_SetString(p, s) ((s&&*s)?(s - p->stringtable):0)
//#define	RETURN_SSTRING(s) (((int *)pr_globals)[OFS_RETURN] = PR_SetString(prinst, s))	//static - exe will not change it.
#define	RETURN_SSTRING(s) (((int *)pr_globals)[OFS_RETURN] = PR_SetString(prinst, s))	//static - exe will not change it.
#define MAX_TEMPSTRS	((int)pr_tempstringcount.value)
#define MAXTEMPBUFFERLEN	((int)pr_tempstringsize.value)
char *PF_TempStr(progfuncs_t *prinst)
{
	if (prinst->tempstringnum == MAX_TEMPSTRS)
		prinst->tempstringnum = 0;
	return prinst->tempstringbase + (prinst->tempstringnum++)*MAXTEMPBUFFERLEN;
}


//returns a section of a string as a tempstring
void PF_substring (void)
{
	int i, start, length;
	char *s;
	char string[4096];

	s = G_STRING(OFS_PARM0);
	start = G_FLOAT(OFS_PARM1);
	length = G_FLOAT(OFS_PARM2);

	if (start < 0)
		start = strlen(s)-start;
	if (length < 0)
		length = strlen(s)-start+(length+1);
	if (start < 0)
	{
	//	length += start;
		start = 0;
	}

	if (start >= strlen(s) || length<=0 || !*s)
	{
		G_INT(OFS_RETURN) = PR_SetString("");
		//RETURN_TSTRING("");
		return;
	}

	if (length >= MAXTEMPBUFFERLEN)
		length = MAXTEMPBUFFERLEN-1;

	for (i = 0; i < start && *s; i++, s++)
		;

	for (i = 0; *s && i < length; i++, s++)
		string[i] = *s;
	string[i] = 0;

	G_INT(OFS_RETURN) = PR_SetString(string);
	//RETURN_TSTRING(string);
}

void PF_fopen (void)
{
	char *name = /*PR_GetStringOfs(prinst, OFS_PARM0);*/ G_STRING(OFS_PARM0);
	int fmode = (int) G_FLOAT(OFS_PARM1);
	int i;

	for (i = 0; i < MAX_QC_FILES; i++)
		if (!pf_fopen_files[i].data)
			break;

	if (i == MAX_QC_FILES)	//too many already open
	{
		G_FLOAT(OFS_RETURN) = -1;
		return;
	}

	if (name[1] == ':' ||	//dos filename absolute path specified - reject.
		strchr(name, '\\') || *name == '/' ||	//absolute path was given - reject
		strstr(name, ".."))	//someone tried to be cleaver.
	{
		G_FLOAT(OFS_RETURN) = -1;
		return;
	}

	Q_strncpyz(pf_fopen_files[i].name, va("data/%s", name), sizeof(pf_fopen_files[i].name));

	pf_fopen_files[i].accessmode = fmode;
	switch (fmode)
	{
	case 0:	//read
		pf_fopen_files[i].data = COM_LoadMallocFile(pf_fopen_files[i].name);
		if (!pf_fopen_files[i].data)
		{
			Q_strncpyz(pf_fopen_files[i].name, name, sizeof(pf_fopen_files[i].name));
			pf_fopen_files[i].data = COM_LoadMallocFile(pf_fopen_files[i].name);
		}

		if (pf_fopen_files[i].data)
		{
			G_FLOAT(OFS_RETURN) = i + FIRST_QC_FILE_INDEX;
			pf_fopen_files[i].prinst = svprogfuncs;
		}
		else
			G_FLOAT(OFS_RETURN) = -1;

		pf_fopen_files[i].bufferlen = pf_fopen_files[i].len = fs_filesize;
		pf_fopen_files[i].ofs = 0;
		break;
	case 1:	//append
		pf_fopen_files[i].data = COM_LoadMallocFile(pf_fopen_files[i].name);
		pf_fopen_files[i].ofs = pf_fopen_files[i].bufferlen = pf_fopen_files[i].len = fs_filesize;
		if (pf_fopen_files[i].data)
		{
			G_FLOAT(OFS_RETURN) = i + FIRST_QC_FILE_INDEX;
			pf_fopen_files[i].prinst = svprogfuncs;
			break;
		}
		//file didn't exist - fall through
	case 2:	//write
		pf_fopen_files[i].bufferlen = 8192;
		pf_fopen_files[i].data = /*B*/Z_Malloc(pf_fopen_files[i].bufferlen);
		pf_fopen_files[i].len = 0;
		pf_fopen_files[i].ofs = 0;
		G_FLOAT(OFS_RETURN) = i + FIRST_QC_FILE_INDEX;
		pf_fopen_files[i].prinst = svprogfuncs;
		break;
	default: //bad
		G_FLOAT(OFS_RETURN) = -1;
		break;
	}
}

void PF_fclose (void)
{
	int fnum = G_FLOAT(OFS_PARM0)-FIRST_QC_FILE_INDEX;

	if (fnum < 0 || fnum >= MAX_QC_FILES)
	{
		Con_Printf("PF_fclose: File out of range\n");
		return;	//out of range
	}

	if (!pf_fopen_files[fnum].data)
	{
		Con_Printf("PF_fclose: File is not open\n");
		return;	//not open
	}

	if (pf_fopen_files[fnum].prinst != svprogfuncs)
	{
		Con_Printf("PF_fclose: File is from wrong instance\n");
		return;	//this just isn't ours.
	}

	switch(pf_fopen_files[fnum].accessmode)
	{
	case 0:
		/*B*/Z_Free(pf_fopen_files[fnum].data);
		break;
	case 1:
	case 2:
		COM_WriteFile(pf_fopen_files[fnum].name, pf_fopen_files[fnum].data, pf_fopen_files[fnum].len);
		/*B*/Z_Free(pf_fopen_files[fnum].data);
		break;
	}
	pf_fopen_files[fnum].data = NULL;
	pf_fopen_files[fnum].prinst = NULL;
}

void /*VARGS */PR_BIError(progfuncs_t *progfuncs, char *format, ...)
{
	va_list		argptr;
	static char		string[2048];

	va_start (argptr, format);
	vsnprintf (string,sizeof(string)-1, format,argptr);
	va_end (argptr);

	if (developer.value)
	{
		globalvars_t2 *pr_globals = PR_globals(progfuncs, PR_CURRENT);
		Con_Printf("%s\n", string);
		*progfuncs->pr_trace = 1;
		G_INT(OFS_RETURN)=0;	//just in case it was a float and should be an ent...
		G_INT(OFS_RETURN+1)=0;
		G_INT(OFS_RETURN+2)=0;
	}
	else
	{
		PR_StackTrace(progfuncs);
		PR_AbortStack(progfuncs);
		progfuncs->parms->Abort ("%s", string);
	}
}

#define MAX_TEMPSTRS	((int)pr_tempstringcount.value)
#define MAXTEMPBUFFERLEN	((int)pr_tempstringsize.value)
string_t PR_TempString(progfuncs_t *prinst, char *str)
{
	char *tmp;
	if (!prinst->tempstringbase)
		return prinst->TempString(prinst, str);

	if (!str || !*str)
		return 0;

	if (prinst->tempstringnum == MAX_TEMPSTRS)
		prinst->tempstringnum = 0;
	tmp = prinst->tempstringbase + (prinst->tempstringnum++)*MAXTEMPBUFFERLEN;

	Q_strncpyz(tmp, str, MAXTEMPBUFFERLEN);
	return tmp - prinst->stringtable;
}
#define	RETURN_TSTRING(s) (((int *)pr_globals)[OFS_RETURN] = PR_TempString(prinst, s))	//temp (static but cycle buffers)

void PF_fgets (void)
{
	char c, *s, *o, *max/*, *tmp*/;
	int fnum = G_FLOAT(OFS_PARM0) - FIRST_QC_FILE_INDEX;
	char pr_string_temp[4096];

	if (!svprogfuncs)
	{
//		memset(svprogfuncs, 0, sizeof(*svprogfuncs));
		//svprogfuncs = InitProgs(&svprogparms);
	}

	*pr_string_temp = '\0';
	G_INT(OFS_RETURN) = 0;	//EOF
	if (fnum < 0 || fnum >= MAX_QC_FILES)
	{
		PR_BIError(svprogfuncs, "PF_fgets: File out of range\n");
		return;	//out of range
	}

	if (!pf_fopen_files[fnum].data)
	{
		PR_BIError(svprogfuncs, "PF_fgets: File is not open\n");
		return;	//not open
	}

	if (pf_fopen_files[fnum].prinst != svprogfuncs)
	{
		PR_BIError(svprogfuncs, "PF_fgets: File is from wrong instance\n");
		return;	//this just isn't ours.
	}

	//read up to the next \n, ignoring any \rs.
	o = pr_string_temp;
	max = o + MAXTEMPBUFFERLEN-1;
	s = pf_fopen_files[fnum].data+pf_fopen_files[fnum].ofs;
	while(*s)
	{
		c = *s++;
		if (c == '\n')
			break;
		if (c == '\r')
			continue;

		if (o == max)
			break;
		*o++ = c;
	}
	*o = '\0';

	pf_fopen_files[fnum].ofs = s - pf_fopen_files[fnum].data;

	if (!pr_string_temp[0] && !*s)
		G_INT(OFS_RETURN) = 0;	//EOF
	else
	{

		//RETURN_TSTRING(pr_string_temp);
	//	(((int *)pr_globals)[OFS_RETURN] = PR_TempString(prinst, s));	//temp (static but cycle buffers)
		G_INT(OFS_RETURN) = pr_string_temp - pr_strings;
		/*if (!svprogfuncs->tempstringbase) {
			((int *)pr_globals)[OFS_RETURN] = svprogfuncs->TempString(svprogfuncs, s);
			return;
		}

		//if (!str || !*str)
		//	return 0;

		if (svprogfuncs->tempstringnum == MAX_TEMPSTRS)
			svprogfuncs->tempstringnum = 0;
		tmp = svprogfuncs->tempstringbase + (svprogfuncs->tempstringnum++)*MAXTEMPBUFFERLEN;

		Q_strncpyz(tmp, s, MAXTEMPBUFFERLEN);
		((int *)pr_globals)[OFS_RETURN] = tmp - svprogfuncs->stringtable;*/
	}
}

char *Translate(char *message)
{
	return message;
}

char *PF_VarString2 (progfuncs_t *prinst, int	first)
{
	int		i;
	static char buffer[2][4096];
	static int bufnum;
	char *s, *out;

	out = buffer[(bufnum++)&1];

	out[0] = 0;
	for (i=first ; i</**prinst->callargc*/pr_argc ; i++)
	{
//		if (G_INT(OFS_PARM0+i*3) < 0 || G_INT(OFS_PARM0+i*3) >= 1024*1024);
//			break;

		s = PR_GetStringOfs(prinst, OFS_PARM0+i*3);
		if (s)
		{
			s = Translate(s);
			if (strlen(out)+strlen(s)+1 >= sizeof(buffer[0]))
				SV_Error("VarString (builtin call ending with strings) exceeded maximum string length of %i chars", sizeof(buffer[0]));

			strcat (out, s);
		}
	}
	return out;
}

void *Z_TagMalloc (int size, int tag);
void *BZF_Malloc(int size)	//BZ_Malloc but allowed to fail - like straight malloc.
{
	//return Z_BaseTagMalloc(size, 1, false);		//xavior: more hax
	return Z_TagMalloc(size, 1);
}

void PF_fputs (void)
{
	int fnum = G_FLOAT(OFS_PARM0) - FIRST_QC_FILE_INDEX;
	//char *msg = PF_VarString2(prinst, 1, pr_globalsa);
	char *msg = PF_VarString(1);
	int len = strlen(msg);

	if (fnum < 0 || fnum >= MAX_QC_FILES)
	{
		Con_Printf("PF_fgets: File out of range\n");
		return;	//out of range
	}

	if (!pf_fopen_files[fnum].data)
	{
		Con_Printf("PF_fgets: File is not open\n");
		return;	//not open
	}

	if (pf_fopen_files[fnum].prinst != svprogfuncs)
	{
		Con_Printf("PF_fgets: File is from wrong instance\n");
		return;	//this just isn't ours.
	}

	if (pf_fopen_files[fnum].bufferlen < pf_fopen_files[fnum].ofs + len)
	{
		char *newbuf;
		pf_fopen_files[fnum].bufferlen = pf_fopen_files[fnum].bufferlen*2 + len;
		newbuf = BZF_Malloc(pf_fopen_files[fnum].bufferlen);
		memcpy(newbuf, pf_fopen_files[fnum].data, pf_fopen_files[fnum].len);
		/*B*/Z_Free(pf_fopen_files[fnum].data);
		pf_fopen_files[fnum].data = newbuf;
	}

	memcpy(pf_fopen_files[fnum].data + pf_fopen_files[fnum].ofs, msg, len);
	if (pf_fopen_files[fnum].len < pf_fopen_files[fnum].ofs + len)
		pf_fopen_files[fnum].len = pf_fopen_files[fnum].ofs + len;
	pf_fopen_files[fnum].ofs+=len;
}

void PF_fcloseall (void)
{
	int i;

	for (i = 0; i < MAX_QC_FILES; i++)
	{
		if (pf_fopen_files[i].prinst != svprogfuncs)
			continue;
		G_FLOAT_FTE(OFS_PARM0) = i+FIRST_QC_FILE_INDEX;
		PF_fclose(/*svprogfuncs, pr_globals*/);
	}
}

// End FTE frik_file hax
// Tomaz Quake Hax0r!
void PF_stov (void)
{
	char *v;
	int i;
	vec3_t d;
	
	v = G_STRING_TOMAZ(OFS_PARM0);

	for (i=0; i<3; i++)
	{
		while(v && (v[0] == ' ' || v[0] == '\'')) //skip unneeded data
			v++;
		d[i] = atof(v);
		while (v && v[0] != ' ') // skip to next space
			v++;
	}
	VectorCopy (d, G_VECTOR(OFS_RETURN));
}

// Tomaz - QuakeC File System Begin
// these ints are in sv_main.c:
int Sys_FileOpenRead (char *path, int *hndl);
int Sys_FileOpenWrite (char *path);
int Sys_FileWrite (int handle, void *data, int count);
int Sys_FileOpenWrite (char *path);
int Sys_FileRead (int handle, void *dest, int count);
void Sys_FileClose (int handle);
void PF_TQ_open (void)
{
	char *p = G_STRING(OFS_PARM0);
	char *ftemp;
	int fmode = G_FLOAT(OFS_PARM1);
	int h = 0, fsize = 0;

	char *name = /*PR_GetStringOfs(prinst, OFS_PARM0);*/ G_STRING(OFS_PARM0);
	//int fmode = (int) G_FLOAT(OFS_PARM1);
//	int i;

/*	for (i = 0; i < MAX_QC_FILES; i++)
		if (!pf_fopen_files[i].data)
			break;

	if (i == MAX_QC_FILES)	//too many already open
	{
		G_FLOAT(OFS_RETURN) = -1;
		return;
	}

	if (name[1] == ':' ||	//dos filename absolute path specified - reject.
		strchr(name, '\\') || *name == '/' ||	//absolute path was given - reject
		strstr(name, ".."))	//someone tried to be cleaver.
	{
		G_FLOAT(OFS_RETURN) = -1;
		return;
	}
*/
/*	FILE *f;
	char namee[MAX_OSPATH];

	snprintf (namee, MAX_OSPATH, "%s/%s", fs_gamedir, name);
	f = fopen (namee, "r");	// "rb"
	if (!f) {
		f = fopen (namee, "wb");
		if (!f) {
			G_FLOAT(OFS_RETURN) = 0;
			return;
		}
	}
	fclose(f);*/


	switch (fmode)
	{  
		case 0: // read
			Sys_FileOpenRead (va("%s/%s",fs_gamedir, p), &h);
			G_FLOAT(OFS_RETURN) = (float) h;
			return;
		case 1: // append -- this is nasty
			// copy whole file into the zone
			fsize = Sys_FileOpenRead(va("%s/%s",fs_gamedir, p), &h);
			if (h == -1)
			{
				h = Sys_FileOpenWrite(va("%s/%s",fs_gamedir, p));
				G_FLOAT(OFS_RETURN) = (float) h;
				return;
			}
			ftemp = Z_Malloc(fsize + 1);
			Sys_FileRead(h, ftemp, fsize);
			Sys_FileClose(h);
			// spit it back out
			h = Sys_FileOpenWrite(va("%s/%s",fs_gamedir, p));
			Sys_FileWrite(h, ftemp, fsize);
			Z_Free(ftemp); // free it from memory
			G_FLOAT(OFS_RETURN) = (float) h;  // return still open handle
			return;
		default: // write
			h = Sys_FileOpenWrite (va("%s/%s", fs_gamedir, p));
			G_FLOAT(OFS_RETURN) = (float) h; 
			return;
	}

}

void PF_TQ_close (void)
{
	int h = (int)G_FLOAT(OFS_PARM0);
	Sys_FileClose(h);
}

void PF_TQ_read (void)
{
	// reads one line (to a \n) into a string
	int h = (int)G_FLOAT(OFS_PARM0);
	int test;
	char *p;

	memset(pr_string_temp, 0, 127);
	p = pr_string_temp;
	Sys_FileRead(h, p, 1);
	while (p && p[0] != '\n')
	{
		*p++;
		test = Sys_FileRead(h, p, 1);
		if (p[0] == 13) // carriage return
			Sys_FileRead(h, p, 1); // skip
		if (!test)
			break;
	};
	p[0] = 0;
	if (strlen(pr_string_temp) == 0)
		G_INT(OFS_RETURN) = OFS_NULL;
	else
		G_INT(OFS_RETURN) = pr_string_temp - pr_strings;
}

void PF_TQ_write (void)
{
	// writes to file, like bprint
	float handle = G_FLOAT(OFS_PARM0);
	char *str = PF_VarString(1);
	Sys_FileWrite (handle, str, strlen(str)); 
}

// TomazQuake STOF
void PF_TQ_stof (void)
{
	char	*s;

	s = G_STRING_TOMAZ(OFS_PARM0);
	G_FLOAT(OFS_RETURN) = atof(s);
}

void PF_checkfilename (void)
{
	FILE *f;
	char namee[MAX_OSPATH];
	char *name = /*PR_GetStringOfs(prinst, OFS_PARM0);*/ G_STRING(OFS_PARM0);

	snprintf (namee, MAX_OSPATH, "%s/%s", fs_gamedir, name);
	f = fopen (namee, "r");	// "rb"
	if (!f) {
		//fclose(f);
		f = fopen (namee, "wb");
		if (!f) {
			G_FLOAT(OFS_RETURN) = 0;
			return;
		}
		//return;
	}
		//fprintf (f, "%s", text);
		//fflush (f);
		fclose(f);

	G_FLOAT(OFS_RETURN) = 1;
}

// Checks the general precache
void PF_checkmodelprecache (void)
{
	char	*s;
	int		i;

	s = G_STRING(OFS_PARM0);
	G_INT(OFS_RETURN) = G_INT(OFS_PARM0);
	PR_CheckEmptyString (s);

	for (i=0 ; i<MAX_MODELS && sv.model_precache[i] ; i++)
		if (!strcmp(sv.model_precache[i], s)) {
			G_FLOAT(OFS_RETURN) = 1;
			break;
			return;
		}
	if (i==MAX_MODELS || !sv.model_precache[i]) { 
		G_FLOAT(OFS_RETURN) = 0;
		return;
	}

/*	for (i=0 ; i<MAX_MODELS ; i++)
	{
		if (!strcmp(sv.model_precache[i], s)) {
			G_FLOAT(OFS_RETURN) = 1;
			break;
			return;
		}
		if (!sv.model_precache[i])
		{
			G_FLOAT(OFS_RETURN) = 1;
			break;
			return;
		}
	}
	G_FLOAT(OFS_RETURN) = 0;
	*/
}

// Checks the general precache
void PF_checkstrzone (void)
{
	char *s;
	int i;

	s = G_STRING(OFS_PARM0);

	for (i = 0; i < MAX_PRSTR; i++)
	{
		if (!pr_newstrtbl[i] || pr_newstrtbl[i] == pr_strings)
			break;
	}

	G_FLOAT(OFS_RETURN) = i;
}

static char *strtoupper(char *s)
{
	char *p;

	p = s;
	while(*p)
	{
		*p = toupper(*p);
		p++;
	}

	return s;
}

//DP_QC_STRING_CASE_FUNCTIONS
#define	RETURN_TTSTRING(s) (((int *)pr_globals)[OFS_RETURN] = PR_TempString(s))	//temp (static but cycle buffers)

void PF_strtoupper (void)
{
	char *in = G_STRING(OFS_PARM0);//PR_GetStringOfs(prinst, OFS_PARM0);
	char result[8192];

	Q_strncpyz(result, in, sizeof(result));
	strtoupper(result);

	//G_INT(OFS_RETURN) = PR_SetString(result);
	RETURN_STRING(result);
}

static void PF_SendPacket(void/*progfuncs_t *prinst*/)
{
	char send[2048], *in, *out;
	int i, l;
	netadr_t adr;
	char *address = G_STRING(OFS_PARM0);

	char *contents = PF_VarString(1);

	NET_StringToAdr(address, &adr);

		/*
	if (!NET_StringToAdr (G_STRING(OFS_PARM1), &adr)) {
		Com_Printf ("Bad address\n");
		return;
	}
	*/

	if (adr.port == 0)
		adr.port =  BigShort(PORT_SERVER);

	in = PF_VarString(1);//G_STRING(OFS_PARM1);
	out = send + 4;
	send[0] = send[1] = send[2] = send[3] = 0xff;

	l = strlen (in);
	for (i = 0; i < l; i++) {
		if (in[i] == '\\' && in[i+1] == 'n') {
			*out++ = '\n';
			i++;
		} else {
			*out++ = in[i];
		}
	}
	*out = 0;

		
	//NET_SendPacket(/*NS_SERVER, */strlen(contents), contents, adr);

	NET_SendPacket (/*NS_CLIENT, */strlen(send), send, adr);

#ifdef blah
	char *address = G_STRING(OFS_PARM0);
	char *contents = PF_VarString(1);

	//char *address = PR_GetStringOfs(prinst, OFS_PARM0);
	//char *contents = PF_VarString2(prinst, 1/*, pr_globals*/);

	NET_StringToAdr(address, &to);
	NET_SendPacket(/*NS_SERVER, */strlen(contents), contents, to);
#endif
}

static builtin_t std_builtins[] =
    {
        PF_Fixme,		//#0
        PF_makevectors,	// void(entity e)	makevectors 		= #1;
        PF_setorigin,	// void(entity e, vector o) setorigin	= #2;
        PF_setmodel,	// void(entity e, string m) setmodel	= #3;
        PF_setsize,	// void(entity e, vector min, vector max) setsize = #4;
        PF_Fixme,	// void(entity e, vector min, vector max) setabssize = #5;
        PF_break,	// void() break						= #6;
        PF_random,	// float() random						= #7;
        PF_sound,	// void(entity e, float chan, string samp) sound = #8;
        PF_normalize,	// vector(vector v) normalize			= #9;
        PF_error,	// void(string e) error				= #10;
        PF_objerror,	// void(string e) objerror				= #11;
        PF_vlen,	// float(vector v) vlen				= #12;
        PF_vectoyaw,	// float(vector v) vectoyaw		= #13;
        PF_Spawn,	// entity() spawn						= #14;
        PF_Remove,	// void(entity e) remove				= #15;
        PF_traceline,	// float(vector v1, vector v2, float tryents) traceline = #16;
        PF_checkclient,	// entity() clientlist					= #17;
        PF_Find,	// entity(entity start, .string fld, string match) find = #18;
        PF_precache_sound,	// void(string s) precache_sound		= #19;
        PF_precache_model,	// void(string s) precache_model		= #20;
        PF_stuffcmd,	// void(entity client, string s)stuffcmd = #21;
        PF_findradius,	// entity(vector org, float rad) findradius = #22;
        PF_bprint,	// void(string s) bprint				= #23;
        PF_sprint,	// void(entity client, string s) sprint = #24;
        PF_dprint,	// void(string s) dprint				= #25;
        PF_ftos,	// void(string s) ftos				= #26;
        PF_vtos,	// void(string s) vtos				= #27;
        PF_coredump,
        PF_traceon,
        PF_traceoff,		//#30
        PF_eprint,	// void(entity e) debug print an entire entity
        PF_walkmove, // float(float yaw, float dist) walkmove
        PF_Fixme, // float(float yaw, float dist) walkmove
        PF_droptofloor,
        PF_lightstyle,
        PF_rint,
        PF_floor,
        PF_ceil,
        PF_Fixme,
        PF_checkbottom,		//#40
        PF_pointcontents,
        PF_Fixme,
        PF_fabs,
        PF_aim,
        PF_cvar,
        PF_localcmd,
        PF_nextent,
        PF_Fixme,
        PF_changeyaw,
        PF_Fixme,		//#50
        PF_vectoangles,

        PF_WriteByte,
        PF_WriteChar,
        PF_WriteShort,
        PF_WriteLong,
        PF_WriteCoord,
        PF_WriteAngle,
        PF_WriteString,
        PF_WriteEntity,		//#59

        //bliP: added pr as requested ->
        PF_sin, //float(float f) sin = #60;
        PF_cos, //float(float f) cos = #61;
        PF_sqrt, //float(float f) sqrt = #62;
        PF_min, //float(float val1, float val2) min = #63;
        PF_max, //float(float val1, float val2) max = #64;
        //<-

        PF_Fixme,
        PF_Fixme,

        SV_MoveToGoal,
        PF_precache_file,
        PF_makestatic,

        PF_changelevel,		//#70
        PF_Fixme,

        PF_cvar_set,
        PF_centerprint,

        PF_ambientsound,

        PF_precache_model,
        PF_precache_sound,		// precache_sound2 is different only for qcc
        PF_precache_file,

        PF_setspawnparms,

        PF_logfrag,

        PF_infokey,		//#80
        PF_stof,
        PF_multicast,
// MVDSV extensions:
        PF_executecmd,		//#83
        PF_tokenize,
        PF_argc,
        PF_argv,
        PF_teamfield,
        PF_substring,
        PF_strcat,
        PF_strlen,		//#90
        PF_str2byte,
        PF_str2short,
        PF_strzone,
        PF_strunzone,
        PF_conprint,
        PF_readcmd,
        PF_strcpy,
        PF_strstr,
        PF_strncpy,
        PF_log,			//#100
        PF_redirectcmd,
        PF_calltimeofday,
        PF_forcedemoframe,	//#103
        //bliP: find map ->
        PF_findmap,		//#104
        PF_listmaps,		//#105
        PF_findmapname,		//#106
        //<-
#ifdef VWEP_TEST // FIXME: random builtin number
		PF_precache_vwep_model,	// #107 but should be #0x5a09
#endif
		PF_strstrofs, // #221
    };

#define num_mvdsv_builtins (sizeof(std_builtins)/sizeof(std_builtins[0]))
#define num_id_builtins 83

static struct { int num; builtin_t func; } ext_builtins[] =
{
{63, PF_Fixme},		// mvdsv min() -- use QSG min() #94 instead
{64, PF_Fixme},		// mvdsv max() -- use QSG max() #95 instead

{84, PF_tokenize},		// float(string s) tokenize
{85, PF_argc},			// float() argc
{86, PF_argv},			// string(float n) argv
{87, PF_strcasecmp},

{90, PF_tracebox},		// void (vector v1, vector mins, vector maxs, vector v2, float nomonsters, entity ignore) tracebox
//{91, PF_randomvec},		// vector() randomvec
{91, PF_validatefile},		// XavioR: validate map func
////
{94, PF_min},			// float(float a, float b, ...) min
{95, PF_max},			// float(float a, float b, ...) max
{96, PF_bound},			// float(float min, float value, float max) bound
{97, PF_pow},			// float(float x, float y) pow
{98, PF_setbot},		// XavioR: Evil bot extension
////
{99, PF_checkextension},// float(string name) checkextension
////
{103, PF_cvar_string},	// string(string varname) cvar_string
////
// FTE frik_file (implementation by XavioR)
{110, PF_fopen},// #110 float(string filename, float mode) fopen (FRIK_FILE)
{111, PF_fclose},// #111 void(float fhandle) fclose (FRIK_FILE)
{112, PF_fgets},// #112 string(float fhandle) fgets (FRIK_FILE)
{113, PF_fputs},// #113 void(float fhandle, string s) fputs (FRIK_FILE)
////
{114, PF_strlen},		// float(string s) strlen
{115, PF_strcat},		// string(string s1, string s2, ...) strcat
{116, PF_substring},		// string(string s, float start, float count) substr

{118, PF_strzone},		// string(string s) strzone
{119, PF_strunzone},	// void(string s) strunzone
// xavior adds:
{130, PF_stov},			// vector(string s) stov
{131, PF_TQ_open},			// float(string fname, float opentype) open
{132, PF_TQ_close},			// void(float fs) close
{133, PF_TQ_read},			// string(float f) read
{134, PF_TQ_write},			// string (...) write
{135, PF_TQ_stof},			//
{136, PF_checkfilename},	// checks to make sure the client has a valid name for filewrite
{137, PF_checkmodelprecache},	// returns if a model has been precached
{138, PF_checkstrzone},

{221, PF_strstrofs},		// nvm u, PF_strofs		

{231, PF_calltimeofday},// void() calltimeofday
{242, PF_SendPacket},// void(string dest, string content)
{448, PF_cvar_string},	// string(string varname) cvar_string
{481, PF_strtoupper},	// string(string s) strtoupper
{531,PF_setpause},		//void(float pause) setpause
{532,PF_precache_vwep_model},	// float(string model) precache_vwep_model = #532;
};

#define num_ext_builtins (sizeof(ext_builtins)/sizeof(ext_builtins[0]))

builtin_t *pr_builtins;
int pr_numbuiltins;

void PR_InitBuiltins (void)
{
	int i;
	enum { UNINITIALIZED, KTPRO, QSG };
	static int builtin_mode = UNINITIALIZED;
	int newmode;

	newmode = is_ktpro ? KTPRO : QSG;
	if (newmode == builtin_mode)
		return;

	if (builtin_mode == QSG)
		Q_free (pr_builtins);

	if (newmode == KTPRO) {
		builtin_mode = KTPRO;
		pr_builtins = std_builtins;
		pr_numbuiltins = num_mvdsv_builtins;
		return;
	}

	builtin_mode = QSG;

	// find highest builtin number to see how much space we need
	pr_numbuiltins = num_id_builtins;
	for (i = 0; i < num_ext_builtins; i++)
		if (ext_builtins[i].num + 1 > pr_numbuiltins)
			pr_numbuiltins = ext_builtins[i].num + 1;

	pr_builtins = Q_malloc(pr_numbuiltins * sizeof(builtin_t));
	memcpy (pr_builtins, std_builtins, num_id_builtins * sizeof(builtin_t));
	for (i = num_id_builtins; i < pr_numbuiltins; i++)
		pr_builtins[i] = PF_Fixme;
	for (i = 0; i < num_ext_builtins; i++) {
		assert (ext_builtins[i].num >= 0);
		pr_builtins[ext_builtins[i].num] = ext_builtins[i].func;
	}
}

#define	G_FLOATa(o) (((float *)pr_globals)[o])
func_t ParseConnectionlessPacket;
qbool PR_GameCodePacket(char *s)
{
	char adr[64];

	if (!ParseConnectionlessPacket)
		return false;

	G_INT(OFS_PARM0) = PR_SetString(NET_AdrToString (/*adr, sizeof(adr), */net_from));
	G_INT(OFS_PARM1) = PR_SetString( s);
	PR_ExecuteProgram (ParseConnectionlessPacket);

	return true;

#ifdef sanity
	int i;
	client_t *cl;
	char adr[64];

	if (!svprogfuncs)
		return false;

	G_INT(OFS_PARM0) = PR_TempString(svprogfuncs, NET_AdrToString (/*adr, sizeof(adr), */net_from));

	G_INT(OFS_PARM1) = PR_TempString(svprogfuncs, s);
	PR_ExecuteProgram (ParseConnectionlessPacket);
	return G_FLOATa(OFS_RETURN);
#endif
}