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

	$Id: cvar.c,v 1.15 2007/01/14 20:02:33 tonik Exp $
*/
// cvar.c -- dynamic variable tracking

#include "qwsvdef.h"
#include "rcon.h"

//cvar_t			*cvar_vars;

static cvar_t	*cvar_hash[32];
static cvar_t	*cvar_vars;
static char	*cvar_null_string = "";

//eqds
extern cvar_t	maxfps;

/*
============
Cvar_Find
============
*/
extern rcon_struct *pRconRoot, *pRconCurPos;

cvar_t *Cvar_Find (char *var_name, qbool restricted)
{
	cvar_t	*var;
	int		key;

	key = Com_HashKey (var_name);
	
	for (var=cvar_hash[key] ; var ; var=var->hash_next)
		if (!Q_strcasecmp (var_name, var->name))
		{
			if (restricted && 
				pRconCurPos != NULL && 
				pRconCurPos->accesslevel < 6) 
			{
				if (var->rconlevel <= pRconCurPos->accesslevel)
					return var;
				else
					return NULL;
			}
			else
				return var;
		}

	return NULL;
}


/*
==========
Key
==========
Returns hash key for a string
*/
static int Key (char *name)
{
	int	v;
	int c;

	v = 0;
	while ( (c = *name++) != 0 )
		//		v += *name;
		v += c &~ 32;	// very lame, but works (case insensitivity)

	return v % 32;
}

/*
============
Cvar_FindVar
============
*/
cvar_t *Cvar_FindVar (const char *var_name)
{
	cvar_t *var;
	int key;

	key = Com_HashKey (var_name);

	for (var=cvar_hash[key] ; var ; var=var->hash_next)
		if (!strcasecmp (var_name, var->name))
			return var;

	return NULL;
}

/*
============
Cvar_Value
============
*/
float Cvar_Value (char *var_name)
{
	cvar_t	*var;

	var = Cvar_FindVar (var_name);
	if (!var)
		return 0;
	return Q_atof (var->string);
}


/*
============
Cvar_String
============
*/
char *Cvar_String (char *var_name)
{
	cvar_t *var;

	var = Cvar_FindVar (var_name);
	if (!var)
		return cvar_null_string;
	return var->string;
}

/*
============
Cvar_CompleteVariable
============
*/
char *Cvar_CompleteVariable (char *partial)
{
	cvar_t		*cvar;
	int			len;
	
	len = Q_strlen(partial);
	
	if (!len)
		return NULL;
		
	// check exact match
	for (cvar=cvar_vars ; cvar ; cvar=cvar->next)
		if (!strcmp (partial,cvar->name))
			return cvar->name;

	// check partial match
	for (cvar=cvar_vars ; cvar ; cvar=cvar->next)
		if (!Q_strncmp (partial,cvar->name, len))
			return cvar->name;

	return NULL;
}


void SV_SendServerInfoChange(char *key, char *value);


/*
============
Cvar_Set
============
*/
void Cvar_Set (cvar_t *var, char *value)
{
	static qbool changing = false;

	if (!var)
		return;

	if (var->flags & CVAR_ROM)
		return;

	if (var->OnChange && !changing)
	{
		changing = true;
		if (var->OnChange(var, value))
		{
			changing = false;
			return;
		}
		changing = false;
	}

	Q_free (var->string);	// free the old value string

	var->string = (char *) Q_malloc (strlen(value)+1);
	strlcpy (var->string, value, strlen(value) + 1);
	var->value = Q_atof (var->string);

	if (var->flags & CVAR_SERVERINFO)
	{
		if (strcmp(var->string, Info_ValueForKey (svs.info, var->name)))
		{
			// Con_Printf("var->name = %s, value = %s, var->string = %s\n", var->name, value, var->string);
			Info_SetValueForKey (svs.info, var->name, var->string, MAX_SERVERINFO_STRING);
			SV_SendServerInfoChange(var->name, var->string);
		}
	}
}

/*
============
Cvar_SetROM
============
*/
void Cvar_SetROM (cvar_t *var, char *value)
{
	int saved_flags;

	if (!var)
		return;

	saved_flags = var->flags;
	var->flags &= ~CVAR_ROM;
	Cvar_Set (var, value);
	var->flags = saved_flags;
}

/*
============
Cvar_SetByName
============
*/
void Cvar_SetByName (char *var_name, char *value)
{
	cvar_t	*var;

	var = Cvar_FindVar (var_name);
	if (!var)
	{	// there is an error in C code if this happens
		Con_DPrintf ("Cvar_Set: variable %s not found\n", var_name);
		return;
	}

	Cvar_Set (var, value);
}

/*
============
Cvar_SetValue
============
*/
void Cvar_SetValue (cvar_t *var, float value)
{
	char	val[32];
	int	i;

	snprintf (val, sizeof(val), "%f", value);
	for (i=strlen(val)-1 ; i>0 && val[i]=='0' ; i--)
		val[i] = 0;
	if (val[i] == '.')
		val[i] = 0;
	Cvar_Set (var, val);
}

/*
============
Cvar_SetValueByName
============
*/
void Cvar_SetValueByName (char *var_name, float value)
{
	char	val[32];

	snprintf (val, sizeof(val), "%.8g",value);
	Cvar_SetByName (var_name, val);
}


/*
============
Cvar_Register
 
Adds a freestanding variable to the variable list.
============
*/
void Cvar_Register (cvar_t *variable)
{
	char	value[512];
	int		key;

	// first check to see if it has already been defined
	if (Cvar_FindVar (variable->name))
	{
		Con_Printf ("Can't register variable %s, already defined\n", variable->name);
		return;
	}

	// check for overlap with a command
	if (Cmd_Exists (variable->name))
	{
		Con_Printf ("Cvar_Register: %s is a command\n", variable->name);
		return;
	}

	// link the variable in
	key = Key (variable->name);
	variable->hash_next = cvar_hash[key];
	cvar_hash[key] = variable;
	variable->next = cvar_vars;
	cvar_vars = variable;

	// copy the value off, because future sets will Q_free it
	strlcpy (value, variable->string, sizeof(value));
	variable->string = (char *) Q_malloc (1);

	// set it through the function to be consistent
	Cvar_SetROM (variable, value);
}


/*
============
Cvar_Command
 
Handles variable inspection and changing from the console
============
*/
qbool Cvar_Command (void)
{
	int		i, c;
	cvar_t		*v;
	char		string[1024];

	// check variables
	v = Cvar_FindVar (Cmd_Argv(0));
	if (!v)
		return false;

	// perform a variable print or set
	c = Cmd_Argc();
	if (c == 1)
	{
		Con_Printf ("\"%s\" is \"%s\"\n", v->name, v->string);
		return true;
	}

	string[0] = 0;
	for (i=1 ; i < c ; i++)
	{
		if (i > 1)
			strlcat (string, " ", sizeof(string));
		strlcat (string, Cmd_Argv(i), sizeof(string));
	}

	Cvar_Set (v, string);
	return true;
}

/*
=============
Cvar_Toggle_f
=============
*/
void Cvar_Toggle_f (void)
{
	cvar_t *var;

	if (Cmd_Argc() != 2)
	{
		Con_Printf ("toggle <cvar> : toggle a cvar on/off\n");
		return;
	}

	var = Cvar_FindVar (Cmd_Argv(1));
	if (!var)
	{
		Con_Printf ("Unknown variable \"%s\"\n", Cmd_Argv(1));
		return;
	}

	Cvar_Set (var, var->value ? "0" : "1");
}

/*
===============
Cvar_CvarList_f
===============
List all cvars
TODO: allow cvar name mask as a parameter, e.g. cvarlist cl_*
*/
void Cvar_CvarList_f (void)
{
	cvar_t	*var;
	int i;

	for (var=cvar_vars, i=0 ; var ; var=var->next, i++)
		Con_Printf("%c%c%c %s\n",
		           var->flags & CVAR_ARCHIVE ? '*' : ' ',
		           var->flags & CVAR_USERINFO ? 'u' : ' ',
		           var->flags & CVAR_SERVERINFO ? 's' : ' ',
		           var->name);

	Con_Printf ("------------\n%d variables\n", i);
}

/*
===========
Cvar_Create
===========
*/
cvar_t *Cvar_Create (char *name, char *string, int cvarflags)
{
	cvar_t		*v;
	int			key;

	v = Cvar_FindVar(name);
	if (v)
		return v;
	v = (cvar_t *) Q_malloc (sizeof(cvar_t));
	// Cvar doesn't exist, so we create it
	v->next = cvar_vars;
	cvar_vars = v;

	key = Key (name);
	v->hash_next = cvar_hash[key];
	cvar_hash[key] = v;

	v->name = (char *) Q_malloc (strlen(name)+1);
	strlcpy (v->name, name, strlen(name) + 1);
	v->string = (char *) Q_malloc (strlen(string)+1);
	strlcpy (v->string, string, strlen(string) + 1);
	v->flags = cvarflags;
	v->value = Q_atof (v->string);
	v->OnChange = NULL;

	return v;
}

/*
===========
Cvar_Delete
===========
returns true if the cvar was found (and deleted)
*/
qbool Cvar_Delete (char *name)
{
	cvar_t	*var, *prev;
	int		key;

	key = Key (name);

	prev = NULL;
	for (var = cvar_hash[key] ; var ; var=var->hash_next)
	{
		if (!strcasecmp(var->name, name))
		{
			// unlink from hash
			if (prev)
				prev->hash_next = var->next;
			else
				cvar_hash[key] = var->next;
			break;
		}
		prev = var;
	}

	if (!var)
		return false;

	prev = NULL;
	for (var = cvar_vars ; var ; var=var->next)
	{
		if (!strcasecmp(var->name, name))
		{
			// unlink from cvar list
			if (prev)
				prev->next = var->next;
			else
				cvar_vars = var->next;

			// free
			Q_free (var->string);
			Q_free (var->name);
			Q_free (var);
			return true;
		}
		prev = var;
	}

	Sys_Error ("Cvar list broken");
	return false;	// shut up compiler
}

//DP_CON_SET
void Cvar_Set_f (void)
{
	cvar_t *var;
	char *var_name;

	if (Cmd_Argc() != 3)
	{
		Con_Printf ("usage: set <cvar> <value>\n");
		return;
	}

	var_name = Cmd_Argv (1);
	var = Cvar_FindVar (var_name);

	if (var)
	{
		Cvar_Set (var, Cmd_Argv(2));
	}
	else
	{
		if (Cmd_Exists(var_name))
		{
			Con_Printf ("\"%s\" is a command\n", var_name);
			return;
		}

#if 0
		// delete alias with the same name if it exists
		Cmd_DeleteAlias (var_name);
#endif

		var = Cvar_Create (var_name, Cmd_Argv(2), CVAR_USER_CREATED);
	}
}

void Cvar_Inc_f (void)
{
	int		c;
	cvar_t	*var;
	float	delta;

	c = Cmd_Argc();
	if (c != 2 && c != 3)
	{
		Con_Printf ("inc <cvar> [value]\n");
		return;
	}

	var = Cvar_FindVar (Cmd_Argv(1));
	if (!var)
	{
		Con_Printf ("Unknown variable \"%s\"\n", Cmd_Argv(1));
		return;
	}

	if (c == 3)
		delta = Q_atof (Cmd_Argv(2));
	else
		delta = 1;

	Cvar_SetValue (var, var->value + delta);
}

char *RandomMap (char *mfilter);
char *RandomBotMap (char *mfilter);
void Cvar_Ranmap_f (void)
{
	int		c, maptype;
	char	*mname;		// map name

	c = Cmd_Argc();
	if (c < 2)
	{
		maptype = 1;
	}
	else
	{
		maptype = atoi(Cmd_Argv(1));
	}

	if ( maptype == 1)
		mname = RandomMap("");
	else if ( maptype == 2 )
		mname = RandomBotMap("");
	else
		return;

	Cbuf_AddText (va("map %s\n",mname));
}

extern func_t MTF_SpawnBots, MTF_RemoveBots;

#define BOT_ADD			1
#define BOT_REMOVE		2
#define BOT_LIST		3
#define BOT_HELP		4
void Cvar_MTFBots_f ( void )
{
	int c, i, cmdtype_no, team_no, class_no, bot_id;
	char *cmdtype;
	client_t *cl;

	cmdtype_no = 0;
	bot_id = 0;
	cmdtype = Cmd_Argv(1);

	c = Cmd_Argc();
	if ( c < 2 )
	{
		Con_Printf( "Usage: Bots [add] | [remove] | [list] | [help]\n" );
		return;
	}

	if (!strcmp(cmdtype,"add"))
	{
		cmdtype_no = BOT_ADD;
		if ( c < 3)
		{
			Con_Printf( "Usage: bots add [class#] [team#]\n" );
			return;
		}
		class_no = atoi(Cmd_Argv(2));
		team_no = atoi(Cmd_Argv(3));
		if ( c < 4)
		{
			Con_Printf( "No team specified, assuming 1..\n" );
			Con_Printf( "Usage: bots add [class#] [team#]\n" );
			team_no = 1;
		}
		G_FLOAT(OFS_PARM0) = class_no;
		G_FLOAT(OFS_PARM1) = team_no;
		PR_ExecuteProgram (MTF_SpawnBots);
		return;
	}
	else if (!strcmp(cmdtype,"remove"))
	{
		cmdtype_no = BOT_REMOVE;
		if ( c < 3)
		{
			Con_Printf( "Usage: bots remove [all]|[team]|[one] [team#]|[id#]\n" );
			return;
		}
		G_FLOAT(OFS_PARM0) = 0;
		G_FLOAT(OFS_PARM1) = 0;
		if (!strcmp(Cmd_Argv(2),"all"))
		{
/*			for (e++ ; e < sv.num_edicts ; e++)
			{
				ed = EDICT_NUM(e);
				if (ed->free)
					continue;
				if (!strcmp(ed->v.classname,"bot")
				{
				}
			}*/
			G_FLOAT(OFS_PARM0) = 1;
		}
		else if (!strcmp(Cmd_Argv(2),"team"))
		{
			G_FLOAT(OFS_PARM0) = 2;
			G_FLOAT(OFS_PARM1) = atoi(Cmd_Argv(3));
		}
		else if (!strcmp(Cmd_Argv(2),"one"))
		{
			G_FLOAT(OFS_PARM0) = 3;
			G_FLOAT(OFS_PARM1) = atoi(Cmd_Argv(3));
		}
		else
		{
			Con_Printf("Invalid \"remove\" argument.\n");
			Con_Printf( "Usage: bots remove [all]|[team]|[one] [team#]|[id#]\n" );
			return;
		}
		PR_ExecuteProgram (MTF_RemoveBots);
	}
	else if (!strcmp(cmdtype,"list"))
	{
		cmdtype_no = BOT_LIST;
		Con_Printf("---Bots in server:---\n");
		Con_Printf("id#   name\n");
		Con_Printf("---   ----\n");
		for (i=0 ; i<MAX_BOTS ; i++)
		{
			cl = &sbi.bots[i];
			if ( cl->state == 666 )
			{
				if ( cl->botent && cl->botent->v.team != 0/* && cl->bfullupdate > 0*/) {
					Con_Printf("%i     %s\n", cl->buserid, cl->bname);
/*					bname = cl->bname;
					top = cl->botent->v.team - 1;
					bottom = cl->botent->v.team - 1;
					top = (top < 0) ? 0 : ((top > 13) ? 13 : top);
					bottom = (bottom < 0) ? 0 : ((bottom > 13) ? 13 : bottom);
					botUpdateUserInfo( sv_client, cl->bfullupdate, cl->buserid, top, bottom, bname, cl->bteam cl->botent->v.team, cl->bpc_num );
					*/
				}
			}
		}
		Con_Printf("---End of bot list.---\n");
	}
	else if (!strcmp(cmdtype,"help"))
	{
		Con_Printf("---\"bots\" cvar help:---\n");
		Con_Printf("-- Note: This feature requires your mod to have\n");
		Con_Printf("-- XavioR's (M)TF bot support functions\n");
		Con_Printf("-\n- \"bots add\": Adds a bot to a specific class & team\n");
		Con_Printf("-  Usage: bots add [class#] [team#]\n" );
		Con_Printf("-\n- \"bots remove\": Removes either a single bot, a team, or all bots\n");
		Con_Printf("-  Usage: bots remove [all]|[team]|[one] [team#]|[id#]\n" );
		Con_Printf("-\n- \"bots list\": Lists all bots on server bot bot id# and name\n");
		Con_Printf("-\n- \"bots help\": Displays this help segment\n");
		cmdtype_no = BOT_HELP;
	}

	if ( cmdtype_no < 1 )
	{
		Con_Printf("Invalid usage of \"bots\" cvar.\n");
		Con_Printf( "Usage: Bots [add] | [remove] | [list] | [help]\n" );
		return;
	}

/*	if ( cmdtype_no == BOT_ADD )
		PR_ExecuteProgram (MTF_SpawnBots);
	else
		PR_ExecuteProgram (MTF_RemoveBots);*/
}

void Cvar_Addbots_f ( void )
{
	int c, team_no, class_no;

	class_no = atoi(Cmd_Argv(1));
	team_no = atoi(Cmd_Argv(2));
	c = Cmd_Argc();
	if ( c < 2 )
	{
		Con_Printf( "Usage: Addbots [class#] [team#]\n" );
		return;
	}
	if ( c < 3)
	{
		Con_Printf( "No team specified, assuming 1..\n" );
		Con_Printf( "Usage: Addbots [class#] [team#]\n" );
		team_no = 1;
	}
	// Assign params
	//G_FLOAT(OFS_PARM0) = 2; // Number of args
	G_FLOAT(OFS_PARM0) = class_no;
	G_FLOAT(OFS_PARM1) = team_no;

	//for (j = 0; j < tmp; j++) // assign string params
//	for (j = 0; j < 7; j++) // assign string params
//	{
//		if (j < tmp)			
//			((int *)pr_globals)[OFS_PARM1+j*3] = PR_SetString(Cmd_Argv(j));
//		else
//			((int *)pr_globals)[OFS_PARM1+j*3] = 0;
//	}

//	((int *)pr_globals)[OFS_PARM1+0] = class_no;
//	((int *)pr_globals)[OFS_PARM1+1] = team_no;

	PR_ExecuteProgram (MTF_SpawnBots);
}

//#define CVAR_DEBUG
#ifdef CVAR_DEBUG
static void Cvar_Hash_Print_f (void)
{
	int		i, count;
	cvar_t	*cvar;

	Con_Printf ("Cvar hash:\n");
	for (i = 0; i<32; i++)
	{
		count = 0;
		for (cvar = cvar_hash[i]; cvar; cvar=cvar->hash_next, count++);
		Con_Printf ("%i: %i\n", i, count);
	}

}
#endif

void SV_KickBan_f (void);
void Cvar_Init (void)
{
	//eqds
	Cmd_AddCommand ("kickban", SV_KickBan_f);
	Cmd_AddCommand ("cvarlist", Cvar_CvarList_f);
	Cmd_AddCommand ("toggle", Cvar_Toggle_f);
	Cmd_AddCommand ("set", Cvar_Set_f); //DP_CON_SET
	Cmd_AddCommand ("inc", Cvar_Inc_f);
	// XavioR hax:
	Cmd_AddCommand ("ranmap", Cvar_Ranmap_f);
	Cmd_AddCommand ("addbots", Cvar_Addbots_f);
	Cmd_AddCommand ("bots", Cvar_MTFBots_f);

#ifdef CVAR_DEBUG
	Cmd_AddCommand ("cvar_hash_print", Cvar_Hash_Print_f);
#endif
}

// more eqds
qbool OnChange_maxfps_var (cvar_t *cvar, char *value)
{
	if (!value[0])
		return true;

	if (Q_atoi(value) == 0)
	{
		Cvar_Set (&maxfps, "");
		return true;
	}

	if (Q_atoi(value) < 30 && strcmp(value,"")) 
	{
		Com_Printf ("error: invalid maxfps value -- use 0 to unset\n");
		return true;
	}

	return !cvar; // shut up compiler
}