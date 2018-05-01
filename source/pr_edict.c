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

	$Id: pr_edict.c,v 1.23 2007/01/08 18:44:20 disconn3ct Exp $
*/
// sv_edict.c -- entity dictionary

#include "qwsvdef.h"

dprograms_t		*progs;
dfunction_t		*pr_functions;
char			*pr_strings;
ddef_t			*pr_fielddefs;
ddef_t			*pr_globaldefs;
dstatement_t	*pr_statements;
globalvars_t	*pr_global_struct;
float			*pr_globals;			// same as pr_global_struct
int				pr_edict_size;	// in bytes
int				pr_teamfield = 0;	// field for team storage

int		type_size[8] = {1,sizeof(void *)/4,1,3,1,1,sizeof(void *)/4,sizeof(void *)/4};

ddef_t *ED_FieldAtOfs (int ofs);
qbool ED_ParseEpair (void *base, ddef_t *key, char *s);

#define	MAX_FIELD_LEN	64
#define GEFV_CACHESIZE	2

typedef struct
{
	ddef_t	*pcache;
	char	field[MAX_FIELD_LEN];
}
gefv_cache;

static gefv_cache	gefvCache[GEFV_CACHESIZE] = {{NULL, ""}, {NULL, ""}};

func_t SpectatorConnect, SpectatorThink, SpectatorDisconnect;
func_t ClientCommand; // OfN
func_t MTF_SpawnBots, MTF_RemoveBots;	// XavioR: MTF Bots
func_t GE_ClientCommand, GE_PausedTic, GE_ShouldPause;
func_t ParseConnectionlessPacket;

func_t mod_ConsoleCmd, mod_UserCmd;
func_t UserInfo_Changed, localinfoChanged;
func_t ChatMessage;

#ifdef VWEP_TEST
int		fofs_vw_index, fofs_vw_frame;
#endif

cvar_t	sv_progsname = {"sv_progsname", "qwprogs"};
/*
=================
ED_ClearEdict
 
Sets everything to NULL
=================
*/
void ED_ClearEdict (edict_t *e)
{
	memset (&e->v, 0, progs->entityfields * 4);
	e->free = false;
}

/*
=================
ED_Alloc
 
Either finds a free edict, or allocates a new one.
Try to avoid reusing an entity that was recently freed, because it
can cause the client to think the entity morphed into something else
instead of being removed and recreated, which can cause interpolated
angles and bad trails.
=================
*/
edict_t *ED_Alloc (void)
{
	int			i;
	edict_t		*e;

	for ( i=MAX_CLIENTS+1 ; i<sv.num_edicts ; i++)
	{
		e = EDICT_NUM(i);
		// the first couple seconds of server time can involve a lot of
		// freeing and allocating, so relax the replacement policy
		if (e->free && ( e->freetime < 2 || sv.time - e->freetime > 0.5 ) )
		{
			ED_ClearEdict (e);
			return e;
		}
	}

	if (i >= 512)
	{
		Con_Printf ("WARNING: Edicts over 512, expect crazyness\n");
	}
	if (i == MAX_EDICTS)
	{
		Con_Printf ("WARNING: ED_Alloc: no free edicts\n");
		// step on whatever is the last edict
		e = EDICT_NUM(--i);
		SV_UnlinkEdict(e);
	}
	else
		sv.num_edicts++;
	e = EDICT_NUM(i);
	ED_ClearEdict (e);

	return e;
}

/*
=================
ED_Free
 
Marks the edict as free
FIXME: walk all entities and NULL out references to this entity
=================
*/
void ED_Free (edict_t *ed)
{
	SV_UnlinkEdict (ed);		// unlink from world bsp

	ed->free = true;
	ed->v.model = 0;
	ed->v.takedamage = 0;
	ed->v.modelindex = 0;
	ed->v.colormap = 0;
	ed->v.skin = 0;
	ed->v.frame = 0;
	ed->v.health = 0;
	ed->v.classname = 0;
	VectorClear (ed->v.origin);
	VectorClear (ed->v.angles);
	ed->v.nextthink = -1;
	ed->v.solid = 0;

	ed->freetime = sv.time;
}

//===========================================================================

/*
============
ED_GlobalAtOfs
============
*/
ddef_t *ED_GlobalAtOfs (int ofs)
{
	ddef_t		*def;
	int			i;

	for (i=0 ; i<progs->numglobaldefs ; i++)
	{
		def = &pr_globaldefs[i];
		if (def->ofs == ofs)
			return def;
	}
	return NULL;
}

/*
============
ED_FieldAtOfs
============
*/
ddef_t *ED_FieldAtOfs (int ofs)
{
	ddef_t		*def;
	int			i;

	for (i=0 ; i<progs->numfielddefs ; i++)
	{
		def = &pr_fielddefs[i];
		if (def->ofs == ofs)
			return def;
	}
	return NULL;
}

/*
============
ED_FindField
============
*/
ddef_t *ED_FindField (char *name)
{
	ddef_t		*def;
	int			i;

	for (i=0 ; i<progs->numfielddefs ; i++)
	{
		def = &pr_fielddefs[i];
		if (!strcmp(PR_GetString(def->s_name),name) )
			return def;
	}
	return NULL;
}


/*
============
ED_FindGlobal
============
*/
ddef_t *ED_FindGlobal (char *name)
{
	ddef_t		*def;
	int			i;

	for (i=0 ; i<progs->numglobaldefs ; i++)
	{
		def = &pr_globaldefs[i];
		if (!strcmp(PR_GetString(def->s_name),name) )
			return def;
	}
	return NULL;
}

/*
============
ED_FindFieldOffset
============
*/
int ED_FindFieldOffset (char *field)
{
	ddef_t *d;
	d = ED_FindField(field);
	if (!d)
		return 0;
	return d->ofs*4;
}

/*
============
ED_FindFunction
============
*/
dfunction_t *ED_FindFunction (char *name)
{
	register dfunction_t		*func;
	register int				i;

	for (i=0 ; i<progs->numfunctions ; i++)
	{
		func = &pr_functions[i];
		if (!strcmp(PR_GetString(func->s_name), name))
			return func;
	}
	return NULL;
}

func_t ED_FindFunctionOffset (char *name)
{
	dfunction_t *func;

	func = ED_FindFunction (name);
	return func ? (func_t)(func - pr_functions) : 0;
}

eval_t *GetEdictFieldValue(edict_t *ed, char *field)
{
	ddef_t			*def = NULL;
	int				i;
	static int		rep = 0;

	for (i=0 ; i<GEFV_CACHESIZE ; i++)
	{
		if (!strcmp(field, gefvCache[i].field))
		{
			def = gefvCache[i].pcache;
			goto Done;
		}
	}

	def = ED_FindField (field);

	if (strlen(field) < MAX_FIELD_LEN)
	{
		gefvCache[rep].pcache = def;
		strlcpy (gefvCache[rep].field, field, MAX_FIELD_LEN);
		rep ^= 1;
	}

Done:
	if (!def)
		return NULL;

	return (eval_t *)((char *)&ed->v + def->ofs*4);
}

/*
============
PR_ValueString
 
Returns a string describing *data in a type specific manner
=============
*/
char *PR_ValueString (etype_t type, eval_t *val)
{
	static char	line[256];
	ddef_t		*def;
	dfunction_t	*f;

	type &= ~DEF_SAVEGLOBAL;

	switch (type)
	{
	case ev_string:
		snprintf (line, sizeof(line), "%s", PR_GetString(val->string));
		break;
	case ev_entity:
		snprintf (line, sizeof(line), "entity %i", NUM_FOR_EDICT(PROG_TO_EDICT(val->edict)) );
		break;
	case ev_function:
		f = pr_functions + val->function;
		snprintf (line, sizeof(line), "%s()", PR_GetString(f->s_name));
		break;
	case ev_field:
		def = ED_FieldAtOfs ( val->_int );
		snprintf (line, sizeof(line), ".%s", PR_GetString(def->s_name));
		break;
	case ev_void:
		snprintf (line, sizeof(line), "void");
		break;
	case ev_float:
		snprintf (line, sizeof(line), "%5.1f", val->_float);
		break;
	case ev_vector:
		snprintf (line, sizeof(line), "'%5.1f %5.1f %5.1f'", val->vector[0], val->vector[1], val->vector[2]);
		break;
	case ev_pointer:
		snprintf (line, sizeof(line), "pointer");
		break;
	default:
		snprintf (line, sizeof(line), "bad type %i", type);
		break;
	}

	return line;
}

/*
============
PR_UglyValueString
 
Returns a string describing *data in a type specific manner
Easier to parse than PR_ValueString
=============
*/
char *PR_UglyValueString (etype_t type, eval_t *val)
{
	static char	line[256];
	ddef_t		*def;
	dfunction_t	*f;

	type &= ~DEF_SAVEGLOBAL;

	switch (type)
	{
	case ev_string:
		snprintf (line, sizeof(line), "%s", PR_GetString(val->string));
		break;
	case ev_entity:
		snprintf (line, sizeof(line), "%i", NUM_FOR_EDICT(PROG_TO_EDICT(val->edict)));
		break;
	case ev_function:
		f = pr_functions + val->function;
		snprintf (line, sizeof(line), "%s", PR_GetString(f->s_name));
		break;
	case ev_field:
		def = ED_FieldAtOfs ( val->_int );
		snprintf (line, sizeof(line), "%s", PR_GetString(def->s_name));
		break;
	case ev_void:
		snprintf (line, sizeof(line), "void");
		break;
	case ev_float:
		snprintf (line, sizeof(line), "%f", val->_float);
		break;
	case ev_vector:
		snprintf (line, sizeof(line), "%f %f %f", val->vector[0], val->vector[1], val->vector[2]);
		break;
	default:
		snprintf (line, sizeof(line), "bad type %i", type);
		break;
	}

	return line;
}

/*
============
PR_GlobalString
 
Returns a string with a description and the contents of a global,
padded to 20 field width
============
*/
char *PR_GlobalString (int ofs)
{
	char	*s;
	int		i;
	ddef_t	*def;
	void	*val;
	static char	line[128];

	val = (void *)&pr_globals[ofs];
	def = ED_GlobalAtOfs(ofs);
	if (!def)
	{
		snprintf (line, sizeof(line), "%i(?""?""?)", ofs); // separate the ?'s to shut up gcc
	}
	else
	{
		s = PR_ValueString (def->type, val);
		snprintf (line, sizeof(line), "%i(%s)%s", ofs, PR_GetString(def->s_name), s);
	}

	i = strlen(line);
	for ( ; i<20 ; i++)
		strlcat (line, " ", sizeof(line));
	strlcat (line, " ", sizeof(line));

	return line;
}

char *PR_GlobalStringNoContents (int ofs)
{
	int		i;
	ddef_t	*def;
	static char	line[128];

	def = ED_GlobalAtOfs(ofs);
	if (!def)
		snprintf (line, sizeof(line), "%i(?""?""?)", ofs); // separate the ?'s to shut up gcc
	else
		snprintf (line, sizeof(line), "%i(%s)", ofs, PR_GetString(def->s_name));

	i = strlen(line);
	for ( ; i<20 ; i++)
		strlcat (line, " ", sizeof(line));
	strlcat (line, " ", sizeof(line));

	return line;
}


/*
=============
ED_Print
 
For debugging
=============
*/
void ED_Print (edict_t *ed)
{
	int		l;
	ddef_t	*d;
	int		*v;
	int		i, j;
	char	*name;
	int		type;

	if (ed->free)
	{
		Con_Printf ("FREE\n");
		return;
	}

	for (i=1 ; i<progs->numfielddefs ; i++)
	{
		d = &pr_fielddefs[i];
		name = PR_GetString(d->s_name);
		if (name[strlen(name)-2] == '_')
			continue;	// skip _x, _y, _z vars

		v = (int *)((char *)&ed->v + d->ofs*4);

		// if the value is still all 0, skip the field
		type = d->type & ~DEF_SAVEGLOBAL;

		for (j=0 ; j<type_size[type] ; j++)
			if (v[j])
				break;
		if (j == type_size[type])
			continue;

		Con_Printf ("%s",name);
		l = strlen (name);
		while (l++ < 15)
			Con_Printf (" ");

		Con_Printf ("%s\n", PR_ValueString(d->type, (eval_t *)v));
	}
}

/*
=============
ED_Write
 
For savegames
=============
*/
void ED_Write (FILE *f, edict_t *ed)
{
	ddef_t	*d;
	int		*v;
	int		i, j;
	char	*name;
	int		type;

	fprintf (f, "{\n");

	if (ed->free)
	{
		fprintf (f, "}\n");
		return;
	}

	for (i=1 ; i<progs->numfielddefs ; i++)
	{
		d = &pr_fielddefs[i];
		name = PR_GetString(d->s_name);
		if (name[strlen(name)-2] == '_')
			continue;	// skip _x, _y, _z vars

		v = (int *)((char *)&ed->v + d->ofs*4);

		// if the value is still all 0, skip the field
		type = d->type & ~DEF_SAVEGLOBAL;
		for (j=0 ; j<type_size[type] ; j++)
			if (v[j])
				break;
		if (j == type_size[type])
			continue;

		fprintf (f,"\"%s\" ",name);
		fprintf (f,"\"%s\"\n", PR_UglyValueString(d->type, (eval_t *)v));
	}

	fprintf (f, "}\n");
}

void ED_PrintNum (int ent)
{
	ED_Print (EDICT_NUM(ent));
}

/*
=============
ED_PrintEdicts
 
For debugging, prints all the entities in the current server
=============
*/
void ED_PrintEdicts (void)
{
	int		i;

	Con_Printf ("%i entities\n", sv.num_edicts);
	for (i=0 ; i<sv.num_edicts ; i++)
	{
		Con_Printf ("\nEDICT %i:\n",i);
		ED_PrintNum (i);
	}
}

/*
=============
ED_PrintEdict_f
 
For debugging, prints a single edicy
=============
*/
void ED_PrintEdict_f (void)
{
	int		i;

	i = Q_atoi (Cmd_Argv(1));
	Con_Printf ("\n EDICT %i:\n",i);
	ED_PrintNum (i);
}

/*
=============
ED_Count
 
For debugging
=============
*/
void ED_Count (void)
{
	int		i;
	edict_t	*ent;
	int		active, models, solid, step;

	active = models = solid = step = 0;
	for (i=0 ; i<sv.num_edicts ; i++)
	{
		ent = EDICT_NUM(i);
		if (ent->free)
			continue;
		active++;
		if (ent->v.solid)
			solid++;
		if (ent->v.model)
			models++;
		if (ent->v.movetype == MOVETYPE_STEP)
			step++;
	}

	Con_Printf ("num_edicts:%3i\n", sv.num_edicts);
	Con_Printf ("active    :%3i\n", active);
	Con_Printf ("view      :%3i\n", models);
	Con_Printf ("touch     :%3i\n", solid);
	Con_Printf ("step      :%3i\n", step);

}

/*
==============================================================================
 
					ARCHIVING GLOBALS
 
FIXME: need to tag constants, doesn't really work
==============================================================================
*/

/*
=============
ED_WriteGlobals
=============
*/
void ED_WriteGlobals (FILE *f)
{
	ddef_t		*def;
	int			i;
	char		*name;
	int			type;

	fprintf (f,"{\n");
	for (i=0 ; i<progs->numglobaldefs ; i++)
	{
		def = &pr_globaldefs[i];
		type = def->type;
		if ( !(def->type & DEF_SAVEGLOBAL) )
			continue;
		type &= ~DEF_SAVEGLOBAL;

		if (type != ev_string
		        && type != ev_float
		        && type != ev_entity)
			continue;

		name = PR_GetString(def->s_name);
		fprintf (f,"\"%s\" ", name);
		fprintf (f,"\"%s\"\n", PR_UglyValueString(type, (eval_t *)&pr_globals[def->ofs]));
	}
	fprintf (f,"}\n");
}

/*
=============
ED_ParseGlobals
=============
*/
void ED_ParseGlobals (char *data)
{
	char	keyname[64];
	ddef_t	*key;

	while (1)
	{
		// parse key
		data = COM_Parse (data);
		if (com_token[0] == '}')
			break;
		if (!data)
			SV_Error ("ED_ParseEntity: EOF without closing brace");

		strlcpy (keyname, com_token, sizeof(keyname));

		// parse value
		data = COM_Parse (data);
		if (!data)
			SV_Error ("ED_ParseEntity: EOF without closing brace");

		if (com_token[0] == '}')
			SV_Error ("ED_ParseEntity: closing brace without data");

		key = ED_FindGlobal (keyname);
		if (!key)
		{
			Con_Printf ("%s is not a global\n", keyname);
			continue;
		}

		if (!ED_ParseEpair ((void *)pr_globals, key, com_token))
			SV_Error ("ED_ParseGlobals: parse error");
	}
}

//============================================================================


/*
=============
ED_NewString
=============
*/
char *ED_NewString (char *string)
{
	char	*new, *new_p;
	int		i,l;

	l = strlen(string) + 1;
	new = (char *) Hunk_Alloc (l);
	new_p = new;

	for (i=0 ; i< l ; i++)
	{
		if (string[i] == '\\' && i < l-1)
		{
			i++;
			if (string[i] == 'n')
				*new_p++ = '\n';
			else
				*new_p++ = '\\';
		}
		else
			*new_p++ = string[i];
	}

	return new;
}


/*
=============
ED_ParseEval
 
Can parse either fields or globals
returns false if error
=============
*/
qbool ED_ParseEpair (void *base, ddef_t *key, char *s)
{
	int		i;
	char	string[128];
	ddef_t	*def;
	char	*v, *w;
	void	*d;
	dfunction_t	*func;

	d = (void *)((int *)base + key->ofs);

	switch (key->type & ~DEF_SAVEGLOBAL)
	{
	case ev_string:
		*(string_t *)d = PR_SetString(ED_NewString (s));
		break;

	case ev_float:
		*(float *)d = Q_atof (s);
		break;

	case ev_vector:
		strlcpy (string, s, sizeof(string));
		v = string;
		w = string;
		for (i=0 ; i<3 ; i++)
		{
			while (*v && *v != ' ')
				v++;
			*v = 0;
			((float *)d)[i] = Q_atof (w);
			w = v = v+1;
		}
		break;

	case ev_entity:
		*(int *)d = EDICT_TO_PROG(EDICT_NUM(Q_atoi (s)));
		break;

	case ev_field:
		def = ED_FindField (s);
		if (!def)
		{
			Con_Printf ("Can't find field %s\n", s);
			return false;
		}
		*(int *)d = G_INT(def->ofs);
		break;

	case ev_function:
		func = ED_FindFunction (s);
		if (!func)
		{
			Con_Printf ("Can't find function %s\n", s);
			return false;
		}
		*(func_t *)d = func - pr_functions;
		break;

	default:
		break;
	}
	return true;
}

/*
====================
ED_ParseEdict
 
Parses an edict out of the given string, returning the new position
ed should be a properly initialized empty edict.
Used for initial level load and for savegames.
====================
*/
char *ED_ParseEdict (char *data, edict_t *ent)
{
	ddef_t		*key;
	qbool		anglehack;
	qbool		init;
	char		keyname[256], cvname[1024];
strlcpy(cvname, "", sizeof(cvname));

	init = false;

	// clear it
	if (ent != sv.edicts)	// hack
		memset (&ent->v, 0, progs->entityfields * 4);

	// go through all the dictionary pairs
	while (1)
	{
		// parse key
		data = COM_Parse (data);
		if (com_token[0] == '}')
			break;
		if (!data)
			SV_Error ("ED_ParseEntity: EOF without closing brace");

		// anglehack is to allow QuakeEd to write single scalar angles
		// and allow them to be turned into vectors. (FIXME...)
		if (!strcmp(com_token, "angle"))
		{
			strlcpy (com_token, "angles", MAX_COM_TOKEN);
			anglehack = true;
		}
		else
			anglehack = false;

		// FIXME: change light to _light to get rid of this hack
		if (!strcmp(com_token, "light"))
			strlcpy (com_token, "light_lev", MAX_COM_TOKEN);	// hack for single light def

		strlcpy (keyname, com_token, sizeof(keyname));

		// parse value
		data = COM_Parse (data);
		if (!data)
			SV_Error ("ED_ParseEntity: EOF without closing brace");

		if (com_token[0] == '}')
			SV_Error ("ED_ParseEntity: closing brace without data");

		init = true;

		// keynames with a leading underscore are used for utility comments,
		// and are immediately discarded by quake
		if (keyname[0] == '_')
			continue;

		key = ED_FindField (keyname);
		if (!key)
		{
			char	temptwo[32];
			strlcpy (temptwo, com_token, sizeof(temptwo));
			strcat(cvname, keyname);
			strcat(cvname, "\t");
			strcat(cvname, temptwo);
			strcat(cvname, "\t");
			
			/*
			strlcpy(cvname, "\"testING ");
			Cvar_Set( "a2temp", keyname );
			
			Cbuf_AddText( "set a2temp " );
			Cbuf_AddText( Cvar_String( "a2temp" ) );
			Cbuf_AddText( keyname );
			Cbuf_AddText( "\n");
		*/

			Con_Printf ("%s is not a field\n", keyname);
			continue;
		}

		if (anglehack)
		{
			char	temp[32];
			strlcpy (temp, com_token, sizeof(temp));
			snprintf (com_token, MAX_COM_TOKEN, "0 %s 0", temp);
		}

		if (!ED_ParseEpair ((void *)&ent->v, key, com_token))
			SV_Error ("ED_ParseEdict: parse error");
	}

	if (!init)
		ent->free = true;


	if (!strcmp(PR_GetString(ent->v.classname), "multi_manager")) {
			ent->v.netname = PR_SetTmpString(cvname);
			/*Cbuf_AddText( "set " );
			Cbuf_AddText( PR_GetString(ent->v.classname) );
			Cbuf_AddText( " ");
			Cbuf_AddText( cvname );
			Cbuf_AddText( "\n");*/
	}

	return data;
}


/*
================
ED_LoadFromFile
 
The entities are directly placed in the array, rather than allocated with
ED_Alloc, because otherwise an error loading the map would have entity
number references out of order.
 
Creates a server's entity / program execution context by
parsing textual entity definitions out of an ent file.
 
Used for both fresh maps and savegame loads.  A fresh map would also need
to call ED_CallSpawnFunctions () to let the objects initialize themselves.
================
*/
#ifdef fte_blah
void ED_LoadFromFile (char *data)
{
	edict_t		*ent;
	int			inhibit;
	dfunction_t	*func;

	char *datastart;
	eval_t *selfvar;
	eval_t *fulldata;	//this is part of FTE_FULLSPAWNDATA
	int num;
	ddef_t	*def;
	def = ED_GlobalAtOfs(ofs);

	ent = NULL;
	inhibit = 0;
	pr_global_struct->time = sv.time;


		num = ED_FindGlobalOfs("__fullspawndata");

		if (num)
			fulldata = (eval_t *)((int *)pr_globals + num);
		else
			fulldata = NULL;

	// parse ents
	while (1)
	{
		datastart = data;
		// parse the opening brace
		data = COM_Parse (data);
		if (!data)
			break;
		if (com_token[0] != '{')
			SV_Error ("ED_LoadFromFile: found %s when expecting {",com_token);

		if (!ent)
			ent = EDICT_NUM(0);
		else
			ent = ED_Alloc ();
		data = ED_ParseEdict (data, ent);

		// remove things from different skill levels or deathmatch
		if ((int)deathmatch.value)
		{
			if (((int)ent->v.spawnflags & SPAWNFLAG_NOT_DEATHMATCH))
			{
				ED_Free (ent);
				inhibit++;
				continue;
			}
		}
		else if ((current_skill == 0 && ((int)ent->v.spawnflags & SPAWNFLAG_NOT_EASY))
		         || (current_skill == 1 && ((int)ent->v.spawnflags & SPAWNFLAG_NOT_MEDIUM))
		         || (current_skill >= 2 && ((int)ent->v.spawnflags & SPAWNFLAG_NOT_HARD)) )
		{
			ED_Free (ent);
			inhibit++;
			continue;
		}

		//
		// immediately call spawn function
		//
		if (!ent->v.classname)
		{
			Con_Printf ("No classname for:\n");
			ED_Print (ent);
			ED_Free (ent);
			continue;
		}

		// look for the spawn function
		func = ED_FindFunction ( PR_GetString(ent->v.classname) );

		if (!func)
		{
			Con_Printf ("No spawn function for:\n");
			ED_Print (ent);
			ED_Free (ent);
			continue;
		}

					

					//added by request of Mercury.
					if (fulldata)	//this is a vital part of HL map support!!!
					{	//essentually, it passes the ent's spawn info to the ent.
						char *nl;	//otherwise it sees only the named fields of
						char *spawndata;//a standard quake ent.
						spawndata = Hunk_Alloc(data - datastart +1);
						strncpy(spawndata, datastart, data - datastart);
						spawndata[data - datastart] = '\0';
						for (nl = spawndata; *nl; nl++)
							if (*nl == '\n')
								*nl = '\t';
						fulldata->string = spawndata - PR_GetString(def->s_name);//progfuncs->stringtable;
					}

		pr_global_struct->self = EDICT_TO_PROG(ent);
		PR_ExecuteProgram (func - pr_functions);
		SV_FlushSignon();
	}

	Con_DPrintf ("%i entities inhibited\n", inhibit);
}
#else
void ED_LoadFromFile (char *data)
{
	edict_t		*ent;
	int			inhibit;
	dfunction_t	*func;

	ent = NULL;
	inhibit = 0;
	pr_global_struct->time = sv.time;

	// parse ents
	while (1)
	{
		// parse the opening brace
		data = COM_Parse (data);
		if (!data)
			break;
		if (com_token[0] != '{')
			SV_Error ("ED_LoadFromFile: found %s when expecting {",com_token);

		if (!ent)
			ent = EDICT_NUM(0);
		else
			ent = ED_Alloc ();
		data = ED_ParseEdict (data, ent);

		// remove things from different skill levels or deathmatch
		if ((int)deathmatch.value)
		{
			if (((int)ent->v.spawnflags & SPAWNFLAG_NOT_DEATHMATCH))
			{
				ED_Free (ent);
				inhibit++;
				continue;
			}
		}
		else if ((current_skill == 0 && ((int)ent->v.spawnflags & SPAWNFLAG_NOT_EASY))
		         || (current_skill == 1 && ((int)ent->v.spawnflags & SPAWNFLAG_NOT_MEDIUM))
		         || (current_skill >= 2 && ((int)ent->v.spawnflags & SPAWNFLAG_NOT_HARD)) )
		{
			ED_Free (ent);
			inhibit++;
			continue;
		}

		//
		// immediately call spawn function
		//
		if (!ent->v.classname)
		{
			Con_Printf ("No classname for:\n");
			ED_Print (ent);
			ED_Free (ent);
			continue;
		}

		// look for the spawn function
		func = ED_FindFunction ( PR_GetString(ent->v.classname) );

		if (!func)
		{
			Con_Printf ("No spawn function for:\n");
			ED_Print (ent);
			ED_Free (ent);
			continue;
		}

		pr_global_struct->self = EDICT_TO_PROG(ent);
		PR_ExecuteProgram (func - pr_functions);
		SV_FlushSignon();
	}

	Con_DPrintf ("%i entities inhibited\n", inhibit);
}
#endif
extern redirect_t	sv_redirected;
qbool PR_ConsoleCmd(void)
{
	if (mod_ConsoleCmd)
	{
		if (sv_redirected != RD_MOD)
		{
			pr_global_struct->time = sv.time;
			pr_global_struct->self = 0;
		}
		PR_ExecuteProgram (mod_ConsoleCmd);
		return (int) G_FLOAT(OFS_RETURN);
	}

	return false;
}

qbool PR_UserCmd(void)
{
	/*if (!strcmp(Cmd_Argv(0), "admin") || !strcmp(Cmd_Argv(0), "judge"))
	{
		Con_Printf ("user command %s is banned\n", Cmd_Argv(0));
		return true;
	}
	*/
	/*int i;
	if (!strcmp(Cmd_Argv(0), "mmode") || !strcmp(Cmd_Argv(0), "cmd"))
	{
		for (i = 0; i < Cmd_Argc(); i++)
			Con_Printf ("PR_UserCmd: [%d] %s | %d\n", i, Cmd_Argv(i), mod_UserCmd);
		//return true;
	}*/

	// ZQ_CLIENTCOMMAND extension
	if (!is_ktpro && GE_ClientCommand) {
		static char cmd_copy[128], args_copy[1024] /* Ouch! */;
		pr_global_struct->time = sv.time;
		pr_global_struct->self = EDICT_TO_PROG(sv_player);
		strlcpy (cmd_copy, Cmd_Argv(0), sizeof(cmd_copy));
		strlcpy (args_copy, Cmd_Args(), sizeof(args_copy));
		((int *)pr_globals)[OFS_PARM0] = PR_SetString (cmd_copy);
		((int *)pr_globals)[OFS_PARM1] = PR_SetString (args_copy);
		PR_ExecuteProgram (GE_ClientCommand);
		return G_FLOAT(OFS_RETURN) ? true : false;
	}

	if (mod_UserCmd)
	{
		pr_global_struct->time = sv.time;
		pr_global_struct->self = EDICT_TO_PROG(sv_player);

		PR_ExecuteProgram (mod_UserCmd);
		return G_FLOAT(OFS_RETURN) ? true : false;
	}

	return false;
}


/*
===============
PR_LoadProgs
===============
*/
void PF_clear_strtbl(void);

qbool is_ktpro;
static void CheckKTPro (void)
{
	extern cvar_t sv_ktpro_mode;
	int i, len;
	char *s;

	if (!strcasecmp(sv_ktpro_mode.string, "auto"))
	{
		// attempt automatic detection
		is_ktpro = false;
		for (i = 0; i < progs->numstrings; i++)
		{
			if ((s = PR_GetString(i)))
				if (*s)
				{
					if ((len = strlen(s)) >= 23)
						if (strstr(s, "http://ktpro.does.it/ for") ||
							strstr(s, "http://qwex.n3.net/ for"))
						{
							is_ktpro = true;
							Con_DPrintf ("Treat mod as ktpro.\n");
							break;
						}
					i += len;
				}
		}
	}
	else
		is_ktpro = sv_ktpro_mode.value ? true : false;
}

void PR_LoadProgs (void)
{
	int	i;
	char	num[32];
	char	name[MAX_OSPATH];
	dfunction_t *f;		// cpqwsv

	// flush the non-C variable lookup cache
	for (i = 0; i < GEFV_CACHESIZE; i++)
		gefvCache[i].field[0] = 0;

	// clear pr_newstrtbl
	PF_clear_strtbl();

	snprintf(name, sizeof(name), "%s.dat", sv_progsname.string);
	progs = (dprograms_t *)COM_LoadHunkFile (name);
	if (!progs)
		progs = (dprograms_t *)COM_LoadHunkFile ("qwprogs.dat");
	if (!progs)
		progs = (dprograms_t *)COM_LoadHunkFile ("progs.dat");
	if (!progs)
		SV_Error ("PR_LoadProgs: couldn't load progs.dat");
	Con_DPrintf ("Programs occupy %iK.\n", fs_filesize/1024);

	// add prog crc to the serverinfo
	snprintf (num, sizeof(num), "%i", CRC_Block ((byte *)progs, fs_filesize));
#ifdef USE_PR2
	Info_SetValueForStarKey( localinfo, "*qvm", "DAT", MAX_LOCALINFO_STRING );
	//	Info_SetValueForStarKey (svs.info, "*qvm", "DAT", MAX_SERVERINFO_STRING);
#endif
	Info_SetValueForStarKey (svs.info, "*progs", num, MAX_SERVERINFO_STRING);

	// byte swap the header
	for (i = 0; i < (int) sizeof(*progs) / 4 ; i++)
		((int *)progs)[i] = LittleLong ( ((int *)progs)[i] );

	if (progs->version != PROG_VERSION)
		SV_Error ("progs.dat has wrong version number (%i should be %i)", progs->version, PROG_VERSION);
	if (progs->crc != PROGHEADER_CRC)
		SV_Error ("You must have the progs.dat from QuakeWorld installed");

	pr_functions = (dfunction_t *)((byte *)progs + progs->ofs_functions);
	pr_strings = (char *)progs + progs->ofs_strings;
	pr_globaldefs = (ddef_t *)((byte *)progs + progs->ofs_globaldefs);
	pr_fielddefs = (ddef_t *)((byte *)progs + progs->ofs_fielddefs);
	pr_statements = (dstatement_t *)((byte *)progs + progs->ofs_statements);

	num_prstr = 0;

	pr_global_struct = (globalvars_t *)((byte *)progs + progs->ofs_globals);
	pr_globals = (float *)pr_global_struct;

	pr_edict_size = progs->entityfields * 4 + sizeof (edict_t) - sizeof(entvars_t);

	// byte swap the lumps
	for (i = 0; i < progs->numstatements; i++)
	{
		pr_statements[i].op = LittleShort(pr_statements[i].op);
		pr_statements[i].a = LittleShort(pr_statements[i].a);
		pr_statements[i].b = LittleShort(pr_statements[i].b);
		pr_statements[i].c = LittleShort(pr_statements[i].c);
	}

	for (i = 0; i < progs->numfunctions; i++)
	{
		pr_functions[i].first_statement = LittleLong (pr_functions[i].first_statement);
		pr_functions[i].parm_start = LittleLong (pr_functions[i].parm_start);
		pr_functions[i].s_name = LittleLong (pr_functions[i].s_name);
		pr_functions[i].s_file = LittleLong (pr_functions[i].s_file);
		pr_functions[i].numparms = LittleLong (pr_functions[i].numparms);
		pr_functions[i].locals = LittleLong (pr_functions[i].locals);
	}

	for (i = 0; i < progs->numglobaldefs; i++)
	{
		pr_globaldefs[i].type = LittleShort (pr_globaldefs[i].type);
		pr_globaldefs[i].ofs = LittleShort (pr_globaldefs[i].ofs);
		pr_globaldefs[i].s_name = LittleLong (pr_globaldefs[i].s_name);
	}

	for (i = 0; i < progs->numfielddefs; i++)
	{
		pr_fielddefs[i].type = LittleShort (pr_fielddefs[i].type);
		if (pr_fielddefs[i].type & DEF_SAVEGLOBAL)
			SV_Error ("PR_LoadProgs: pr_fielddefs[i].type & DEF_SAVEGLOBAL");
		pr_fielddefs[i].ofs = LittleShort (pr_fielddefs[i].ofs);
		pr_fielddefs[i].s_name = LittleLong (pr_fielddefs[i].s_name);
	}

	for (i = 0; i < progs->numglobals; i++)
		((int *)pr_globals)[i] = LittleLong (((int *)pr_globals)[i]);

	// Zoid, find the spectator functions
	localinfoChanged = mod_UserCmd = mod_ConsoleCmd = UserInfo_Changed = ChatMessage = SpectatorConnect = SpectatorThink = SpectatorDisconnect = 0;
	// Zoid, find the spectator functions               //OfN, and clientcommand
	SpectatorConnect = SpectatorThink = SpectatorDisconnect = ClientCommand = 0;

	SpectatorConnect = ED_FindFunctionOffset ("SpectatorConnect");
	SpectatorThink = ED_FindFunctionOffset ("SpectatorThink");
	SpectatorDisconnect = ED_FindFunctionOffset ("SpectatorDisconnect");
	ChatMessage = ED_FindFunctionOffset ("ChatMessage");
	UserInfo_Changed = ED_FindFunctionOffset ("UserInfo_Changed");
	mod_ConsoleCmd = ED_FindFunctionOffset ("ConsoleCmd");
	mod_UserCmd = ED_FindFunctionOffset ("UserCmd");
	localinfoChanged = ED_FindFunctionOffset ("localinfoChanged");
	GE_ClientCommand = ED_FindFunctionOffset ("GE_ClientCommand");

	//ParseConnectionlessPacket = ED_FindFunctionOffset ("SV_ParseConnectionlessPacket");

	GE_PausedTic = ED_FindFunctionOffset ("GE_PausedTic");
	GE_ShouldPause = ED_FindFunctionOffset ("GE_ShouldPause");
	if ((f = ED_FindFunction ("SV_ParseClientCommand")) != NULL) //OfN			// xavior: legacy: 10/29/12 ClientCommand
		ClientCommand = (func_t)(f - pr_functions);
	if ((f = ED_FindFunction ("MTF_SpawnBots")) != NULL) //XavioR: only for mtf
		MTF_SpawnBots = (func_t)(f - pr_functions);
	if ((f = ED_FindFunction ("MTF_RemoveBots")) != NULL) //XavioR: only for mtf
		MTF_RemoveBots = (func_t)(f - pr_functions);
	if ((f = ED_FindFunction ("SV_ParseConnectionlessPacket")) != NULL) //XavioR: only for mtf
		ParseConnectionlessPacket = (func_t)(f - pr_functions);
#ifdef VWEP_TEST
/*	if ((f = ED_FindFunction ("vw_index")) != NULL)
		fofs_vw_index = (func_t)(f - pr_functions);
	if ((f = ED_FindFunction ("vw_frame")) != NULL)
		fofs_vw_frame = (func_t)(f - pr_functions);*/
#endif

	CheckKTPro ();
}


/*
===============
PR_Init
===============
*/
//void PR_CleanLogText_Init();
void PR_Init (void)
{
	Cvar_Register(&sv_progsname);

	Cmd_AddCommand ("edict", ED_PrintEdict_f);
	Cmd_AddCommand ("edicts", ED_PrintEdicts);
	Cmd_AddCommand ("edictcount", ED_Count);
	Cmd_AddCommand ("profile", PR_Profile_f);

	memset(pr_newstrtbl, 0, sizeof(pr_newstrtbl));
	//	PR_CleanLogText_Init();
}


edict_t *EDICT_NUM(int n)
{
	if (n < 0 || n >= MAX_EDICTS)
		SV_Error ("EDICT_NUM: bad number %i", n);
	return (edict_t *)((byte *)sv.edicts+ (n)*pr_edict_size);
}

int NUM_FOR_EDICT(edict_t *e)
{
	int		b;

	b = (byte *)e - (byte *)sv.edicts;
	b /= pr_edict_size;

	if (b < 0 || b >= sv.num_edicts)
		SV_Error ("NUM_FOR_EDICT: bad pointer");

	return b;
}
