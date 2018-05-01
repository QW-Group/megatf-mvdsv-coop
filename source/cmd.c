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

    $Id: cmd.c,v 1.22 2007/03/17 06:05:44 qqshka Exp $
*/
// cmd.c -- Quake script command processing module

#include "qwsvdef.h"
#include "rcon.h"
#include "log.h"

cbuf_t cbuf_main;
cbuf_t *cbuf_current = NULL;

void Com_Log (logType_t logType, int logOptions, char *text, ...);

//=============================================================================

/*
============
Cmd_Wait_f

Causes execution of the remainder of the command buffer to be delayed until
next frame.  This allows commands like:
bind g "impulse 5 ; +attack ; wait ; -attack ; impulse 2"
============
*/
void Cmd_Wait_f (void)
{
	if (cbuf_current)
		cbuf_current->wait = true;
}

/*
=============================================================================
 
						COMMAND BUFFER
 
=============================================================================
*/


void Cbuf_AddText (char *text) { Cbuf_AddTextEx (&cbuf_main, text); }
void Cbuf_InsertText (char *text) { Cbuf_InsertTextEx (&cbuf_main, text); }
void Cbuf_Execute () { Cbuf_ExecuteEx (&cbuf_main); }

/*
============
Cbuf_Init
============
*/
void Cbuf_Init (void)
{
	cbuf_main.text_start = cbuf_main.text_end = MAXCMDBUF / 2;
	cbuf_main.wait = false;
}

/*
============
Cbuf_AddText

Adds command text at the end of the buffer
============
*/
void Cbuf_AddTextEx (cbuf_t *cbuf, char *text)
{
	int		len;
	int		new_start;
	int		new_bufsize;

	len = strlen (text);

	if (cbuf->text_end + len <= MAXCMDBUF)
	{
		memcpy (cbuf->text_buf + cbuf->text_end, text, len);
		cbuf->text_end += len;
		return;
	}

	new_bufsize = cbuf->text_end-cbuf->text_start+len;
	if (new_bufsize > MAXCMDBUF)
	{
		Con_Printf ("Cbuf_AddText: overflow\n");
		return;
	}

	// Calculate optimal position of text in buffer
	new_start = (MAXCMDBUF - new_bufsize) / 2;

	memcpy (cbuf->text_buf + new_start, cbuf->text_buf + cbuf->text_start, cbuf->text_end-cbuf->text_start);
	memcpy (cbuf->text_buf + new_start + cbuf->text_end-cbuf->text_start, text, len);
	cbuf->text_start = new_start;
	cbuf->text_end = cbuf->text_start + new_bufsize;
}


/*
============
Cbuf_InsertText

Adds command text immediately after the current command
Adds a \n to the text
============
*/
void Cbuf_InsertTextEx (cbuf_t *cbuf, char *text)
{
	int		len;
	int		new_start;
	int		new_bufsize;

	len = strlen(text);

	if (len < cbuf->text_start)
	{
		memcpy (cbuf->text_buf + (cbuf->text_start - len - 1), text, len);
		cbuf->text_buf[cbuf->text_start-1] = '\n';
		cbuf->text_start -= len + 1;
		return;
	}

	new_bufsize = cbuf->text_end - cbuf->text_start + len + 1;
	if (new_bufsize > MAXCMDBUF)
	{
		Con_Printf ("Cbuf_InsertText: overflow\n");
		return;
	}

	// Calculate optimal position of text in buffer
	new_start = (MAXCMDBUF - new_bufsize) / 2;

	memmove (cbuf->text_buf + (new_start + len + 1), cbuf->text_buf + cbuf->text_start, cbuf->text_end-cbuf->text_start);
	memcpy (cbuf->text_buf + new_start, text, len);
	cbuf->text_buf[new_start + len] = '\n';
	cbuf->text_start = new_start;
	cbuf->text_end = cbuf->text_start + new_bufsize;
}

/*
============
Cbuf_Execute
============
*/
void Cbuf_ExecuteEx (cbuf_t *cbuf)
{
	int i;
	char *text;
	char line[1024];
	int quotes;
	int cursize;
	int semicolon = 0;

	cbuf_current = cbuf;

	while (cbuf->text_end > cbuf->text_start)
	{
		// find a \n or ; line break
		text = (char *)cbuf->text_buf + cbuf->text_start;

		cursize = cbuf->text_end - cbuf->text_start;
		quotes = 0;
		for (i = 0; i < cursize; i++)
		{
			if (text[i] == '"')
				quotes++;
/* EXPERIMENTAL: Forbid ';' as commands separator, because ktpro didn't quote arguments
   from admin users. Example: cmd fkick "N;quit" => kick N;quit => server will exit.*/
			if (!(quotes & 1) && text[i] == ';')	// don't break if inside a quoted string
			{
				switch (semicolon)
				{
					case 0:
					case 3: semicolon = 1; break;
					case 1: semicolon = 2; break;
					default:;
				}
				break;
			}

			if (text[i] == '\n')
			{
				switch (semicolon)
				{
					case 1:
					case 2: semicolon = 3; break;
					case 3: semicolon = 0; break;
					default:;
				}
				break;
			}
		}

		// don't execute lines without ending \n; this fixes problems with
		// partially stuffed aliases not being executed properly

		memcpy(line, text, i);
		line[i] = 0;
		if (i > 0 && line[i - 1] == '\r')
			line[i - 1] = 0;	// remove DOS ending CR

		// delete the text from the command buffer and move remaining commands down
		// this is necessary because commands (exec, alias) can insert data at the
		// beginning of the text buffer

		if (i == cursize)
		{
			cbuf->text_start = cbuf->text_end = MAXCMDBUF / 2;
		}
		else
		{
			i++;
			cbuf->text_start += i;
		}

		// security bugfix in ktpro
		if (is_ktpro && semicolon > 1)
			Sys_Printf("ATTENTION: possibly tried to use ktpro's security hole, "
						"server don't run command after ';'!\nCommand: %s\n", line);
		else
			// execute the command line
			Cmd_ExecuteString (line);

		if (cbuf->wait)
		{	// skip out while text still remains in buffer, leaving it
			// for next frame
			cbuf->wait = false;
			break;
		}
	}

	cbuf_current = NULL;
}


/*
==============================================================================
 
						SCRIPT COMMANDS
 
==============================================================================
*/

/*
===============
Cmd_StuffCmds_f

Adds command line parameters as script statements
Commands lead with a +, and continue until a - or another +
quake +prog jctest.qp +cmd amlev1
quake -nosound +cmd amlev1
===============
*/
void Cmd_StuffCmds_f (void)
{
	int i, j;
	int s;
	char *text, *build, c;

	// build the combined string to parse from
	s = 0;
	for (i = 1; i < com_argc; i++)
		s += strlen (com_argv[i]) + 1;

	if (!s)
		return;

	text = (char *) Q_malloc (s+1);
	text[0] = 0;
	for (i = 1; i < com_argc; i++)
	{
		strlcat (text, com_argv[i], s + 1);
		if (i != com_argc-1)
			strlcat (text, " ", s + 1);
	}

	// pull out the commands
	build = (char *) Q_malloc (s+1);
	build[0] = 0;

	for (i=0 ; i<s-1 ; i++)
	{
		if (text[i] == '+')
		{
			i++;

			for (j=i ; (text[j] != '+') && (text[j] != '-') && (text[j] != 0) ; j++)
				;

			c = text[j];
			text[j] = 0;

			strlcat (build, text + i, s + 1);
			strlcat (build, "\n", s + 1);
			text[j] = c;
			i = j-1;
		}
	}

	if (build[0])
		Cbuf_InsertText (build);

	Q_free (text);
	Q_free (build);
}


/*
===============
Cmd_Exec_f
===============
*/
extern qbyte *COM_LoadMallocFile (char *path);
extern void Q_strncpyz(char *d, const char *s, int n);
// ^ FTE stuff so you dont have to type in ".cfg"
void Cmd_Exec_f (void)
{
	char	*f;
	int		mark;
	char	name[256];

	if (Cmd_Argc () != 2)
	{
		Con_Printf ("exec <filename> : execute a script file\n");
		return;
	}

	// FIXME: is this safe freeing the hunk here???
	mark = Hunk_LowMark ();
	Q_strncpyz(name, Cmd_Argv(1), sizeof(name));
	if ((f = (char *)COM_LoadMallocFile(name)))
		;
	else if ((f = (char *)COM_LoadMallocFile(va("%s.cfg", name))))
		;
	else
	{
		Con_Printf ("couldn't exec %s\n",Cmd_Argv(1));
		return;
	}
	if (!Cvar_Command ())
		Con_Printf ("execing %s\n",Cmd_Argv(1));

	Cbuf_InsertText (f);
	Hunk_FreeToLowMark (mark);
}


/*
===============
Cmd_Echo_f

Just prints the rest of the line to the console
===============
*/
void Cmd_Echo_f (void)
{
	int		i;

	for (i=1 ; i<Cmd_Argc() ; i++)
		Con_Printf ("%s ",Cmd_Argv(i));
	Con_Printf ("\n");
}

/*
=============================================================================
 
								HASH
 
=============================================================================
*/

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
		v += c &~ 32;	// make it case insensitive

	return v % 32;
}

/*
=============================================================================
 
								ALIASES
 
=============================================================================
*/

static cmd_alias_t	*cmd_alias_hash[32];
static cmd_alias_t	*cmd_alias;

/*
===============
Cmd_Alias_f
 
Creates a new command that executes a command string (possibly ; seperated)
===============
*/

char *CopyString (char *in)
{
	char *out;

	out = (char *) Q_malloc (strlen(in)+1);
	strlcpy (out, in, strlen(in) + 1);
	return out;
}

void Cmd_Alias_f (void)
{
	cmd_alias_t	*a;
	char		cmd[1024];
	int			i, c;
	int			key;
	char		*s;
	//	cvar_t		*var;

	c = Cmd_Argc();
	if (c == 1)
	{
		Con_Printf ("Current alias commands:\n");
		for (a = cmd_alias ; a ; a=a->next)
			Con_Printf ("%s : %s\n\n", a->name, a->value);
		return;
	}

	s = Cmd_Argv(1);
	if (strlen(s) >= MAX_ALIAS_NAME)
	{
		Con_Printf ("Alias name is too long\n");
		return;
	}

#if 0
	if ( (var = Cvar_FindVar(s)) != NULL )
	{
		if (var->flags & CVAR_USER_CREATED)
			Cvar_Delete (var->name);
		else
		{
			//			Con_Printf ("%s is a variable\n");
			return;
		}
	}
#endif

	key = Key(s);

	// if the alias already exists, reuse it
	for (a = cmd_alias_hash[key] ; a ; a=a->hash_next)
	{
		if (!strcasecmp(a->name, s))
		{
			Q_free (a->value);
			break;
		}
	}

	if (!a)
	{
		a = (cmd_alias_t*) Q_malloc (sizeof(cmd_alias_t));
		a->next = cmd_alias;
		cmd_alias = a;
		a->hash_next = cmd_alias_hash[key];
		cmd_alias_hash[key] = a;
	}
	strlcpy (a->name, s, MAX_ALIAS_NAME);

	// copy the rest of the command line
	cmd[0] = 0;		// start out with a null string
	for (i=2 ; i<c ; i++)
	{
		if (i > 2)
			strlcat (cmd, " ", sizeof(cmd));
		strlcat (cmd, Cmd_Argv(i), sizeof(cmd));
	}

	a->value = CopyString (cmd);
}


qbool Cmd_DeleteAlias (char *name)
{
	cmd_alias_t	*a, *prev;
	int			key;

	key = Key (name);

	prev = NULL;
	for (a = cmd_alias_hash[key] ; a ; a = a->hash_next)
	{
		if (!strcasecmp(a->name, name))
		{
			// unlink from hash
			if (prev)
				prev->hash_next = a->hash_next;
			else
				cmd_alias_hash[key] = a->hash_next;
			break;
		}
		prev = a;
	}

	if (!a)
		return false;	// not found

	prev = NULL;
	for (a = cmd_alias ; a ; a = a->next)
	{
		if (!strcasecmp(a->name, name))
		{
			// unlink from alias list
			if (prev)
				prev->next = a->next;
			else
				cmd_alias = a->next;

			// free
			Q_free (a->value);
			Q_free (a);
			return true;
		}
		prev = a;
	}

	Sys_Error ("Cmd_DeleteAlias: alias list broken");
	return false;	// shut up compiler
}

void Cmd_UnAlias_f (void)
{
	char		*s;

	if (Cmd_Argc() != 2)
	{
		Con_Printf ("unalias <alias>: erase an existing alias\n");
		return;
	}

	s = Cmd_Argv(1);
	if (strlen(s) >= MAX_ALIAS_NAME)
	{
		Con_Printf ("Alias name is too long\n");
		return;
	}

	if (!Cmd_DeleteAlias(s))
		Con_Printf ("Unknown alias \"%s\"\n", s);
}

// remove all aliases
void Cmd_UnAliasAll_f (void)
{
	cmd_alias_t	*a, *next;
	int i;

	for (a=cmd_alias ; a ; a=next)
	{
		next = a->next;
		Q_free (a->value);
		Q_free (a);
	}
	cmd_alias = NULL;

	// clear hash
	for (i=0 ; i<32 ; i++)
	{
		cmd_alias_hash[i] = NULL;
	}
}


/*
=============================================================================
 
					COMMAND EXECUTION
 
=============================================================================
*/

static	int			cmd_argc;
static	char		*cmd_argv[MAX_ARGS];
static	char		*cmd_null_string = "";
static	char		*cmd_args = NULL;

static cmd_function_t	*cmd_hash_array[32];
static cmd_function_t	*cmd_functions;		// possible commands to execute

/*
============
Cmd_Argc
============
*/
int Cmd_Argc (void)
{
	return cmd_argc;
}

/*
============
Cmd_Argv
============
*/
char *Cmd_Argv (int arg)
{
	if ( arg >= cmd_argc )
		return cmd_null_string;
	return cmd_argv[arg];
}

/*
============
Cmd_Args
 
Returns a single string containing argv(1) to argv(argc()-1)
============
*/
char *Cmd_Args (void)
{
	if (!cmd_args)
		return "";
	return cmd_args;
}


/*
============
Cmd_TokenizeString
 
Parses the given string into command line tokens.
============
*/
void Cmd_TokenizeString (char *text)
{
	size_t idx;
	static char argv_buf[MAX_MSGLEN + MAX_ARGS];

	idx = 0;

	cmd_argc = 0;
	cmd_args = NULL;

	while (1)
	{
		// skip whitespace
		while (*text == ' ' || *text == '\t' || *text == '\r')
		{
			text++;
		}

		if (*text == '\n')
		{	// a newline seperates commands in the buffer
			text++;
			break;
		}

		if (!*text)
			return;

		if (cmd_argc == 1)
			cmd_args = text;

		text = COM_Parse (text);
		if (!text)
			return;
		if (cmd_argc < MAX_ARGS && sizeof(argv_buf) - 1 > idx)
		{
			cmd_argv[cmd_argc] = argv_buf + idx;
			strlcpy (cmd_argv[cmd_argc], com_token, sizeof(argv_buf) - idx);
			idx += strlen(com_token) + 1;
			cmd_argc++;
		}
	}

}

/*
============
Cmd_TokenizeString_FTE

Parses the given string into command line tokens.
============
*/

//same as COM_Parse, but parses two quotes next to each other as a single quote as part of the string
char *COM_StringParse (char *data, qbool expandmacros, qbool qctokenize)
{
	int		c;
	int		len;
	char *s;

	len = 0;
	com_token[0] = 0;

	if (!data)
		return NULL;

// skip whitespace
skipwhite:
	while ( (c = *data), (unsigned)c <= ' ' && c != '\n')
	{
		if (c == 0)
			return NULL;			// end of file;
		data++;
	}

// skip // comments
	if (c=='/')
	{
		if (data[1] == '/')
		{
			while (*data && *data != '\n')
				data++;
			goto skipwhite;
		}
	}

//skip / * comments
	if (c == '/' && data[1] == '*' && !qctokenize)
	{
		data+=2;
		while(*data)
		{
			if (*data == '*' && data[1] == '/')
			{
				data+=2;
				goto skipwhite;
			}
			data++;
		}
		goto skipwhite;
	}


// handle quoted strings specially
	if (c == '\"')
	{
		data++;
		while (1)
		{
			if (len >= sizeof(com_token)-1)
			{
				com_token[len] = '\0';
				return data;
			}


			c = *data++;
			if (c=='\"')
			{
				c = *(data);
				if (c!='\"')
				{
					com_token[len] = 0;
					return data;
				}
				while (c=='\"')
				{
					com_token[len] = c;
					len++;
					data++;
					c = *(data+1);
				}
			}
			if (!c)
			{
				com_token[len] = 0;
				return data;
			}
			com_token[len] = c;
			len++;
		}
	}

	// handle quoted strings specially
	if (c == '\'' && qctokenize)
	{
		data++;
		while (1)
		{
			if (len >= sizeof(com_token)-1)
			{
				com_token[len] = '\0';
				return data;
			}


			c = *data++;
			if (c=='\'')
			{
				c = *(data);
				if (c!='\'')
				{
					com_token[len] = 0;
					return data;
				}
				while (c=='\'')
				{
					com_token[len] = c;
					len++;
					data++;
					c = *(data+1);
				}
			}
			if (!c)
			{
				com_token[len] = 0;
				return data;
			}
			com_token[len] = c;
			len++;
		}
	}

	if (qctokenize && (c == '\n' || c == '{' || c == '}' || c == ')' || c == '(' || c == ']' || c == '[' || c == '\'' || c == ':' || c == ',' || c == ';'))
	{
		// single character
		com_token[len++] = c;
		com_token[len] = 0;
		return data+1;
	}

// parse a regular word
	do
	{
		if (len >= sizeof(com_token)-1)
		{
			com_token[len] = '\0';
			return data;
		}

		com_token[len] = c;
		data++;
		len++;
		c = *data;
	} while ((unsigned)c>32 && !(qctokenize && (c == '\n' || c == '{' || c == '}' || c == ')' || c == '(' || c == ']' || c == '[' || c == '\'' || c == ':' || c == ',' || c == ';')));

	com_token[len] = 0;

	if (!expandmacros)
		return data;

	//now we check for macros.
	for (s = com_token, c= 0; c < len; c++, s++)	//this isn't a quoted token by the way.
	{
		if (*s == '$')
		{
			cvar_t *macro;
			char name[64];
			int i;

			for (i = 1; i < sizeof(name); i++)
			{
				if (((unsigned char*)s)[i] <= ' ' || s[i] == '$')
					break;
			}

			Q_strncpyz(name, s+1, i);
			i-=1;

			macro = Cvar_FindVar(name);
			if (macro)	//got one...
			{
				if (len+strlen(macro->string)-(i+1) >= sizeof(com_token)-1)	//give up.
				{
					com_token[len] = '\0';
					return data;
				}
				memmove(s+strlen(macro->string), s+i+1, len-c-i);
				memcpy(s, macro->string, strlen(macro->string));
				s+=strlen(macro->string);
				len+=strlen(macro->string)-(i+1);
			}
		}
	}

	return data;
}

void Cmd_TokenizeString_FTE (char *text, qbool expandmacros, qbool qctokenize)
{
	int		i;

// clear the args from the last string
	for (i=0 ; i<cmd_argc ; i++)
		Z_Free (cmd_argv[i]);

	cmd_argc = 0;
	cmd_args = NULL;

	while (1)
	{
// skip whitespace up to a /n
		while (*text && (unsigned)*text <= ' ' && *text != '\n')
		{
			text++;
		}

		if (*text == '\n')
		{	// a newline seperates commands in the buffer
			text++;
			break;
		}

		if (!*text)
			return;

		if (cmd_argc == 1)
			 cmd_args = text;

		text = COM_StringParse (text, expandmacros, qctokenize);
		if (!text)
			return;

		if (cmd_argc < MAX_ARGS)
		{
			cmd_argv[cmd_argc] = (char*)Z_Malloc (Q_strlen(com_token)+1);
			Q_strcpy (cmd_argv[cmd_argc], com_token);
			cmd_argc++;
		}
	}

}

/*
============
Cmd_AddCommand
============
*/
int	cmd_deflvl = RCON_ACCT_DEF; // variable for Cmd_AddLevel
void Cmd_AddCommand (char *cmd_name, xcommand_t function)
{
	cmd_function_t	*cmd;
	int	key;

	if (host_initialized)	// because hunk allocation would get stomped
		Sys_Error ("Cmd_AddCommand after host_initialized");

	// fail if the command is a variable name
	if (Cvar_FindVar(cmd_name))
	{
		Con_Printf ("Cmd_AddCommand: %s already defined as a var\n", cmd_name);
		return;
	}

	key = Key (cmd_name);

	// fail if the command already exists
	for (cmd=cmd_hash_array[key] ; cmd ; cmd=cmd->hash_next)
	{
		if (!strcasecmp (cmd_name, cmd->name))
		{
			Con_Printf ("Cmd_AddCommand: %s already defined\n", cmd_name);
			return;
		}
	}

	cmd = (cmd_function_t *) Hunk_Alloc (sizeof(cmd_function_t));
	cmd->name = cmd_name;
	cmd->function = function;
	cmd->rconlevel = cmd_deflvl;
	cmd->deflevel = cmd->rconlevel;
	cmd->desc = NULL; // might have one when help is loaded
	cmd->next = cmd_functions;
	cmd_functions = cmd;
	cmd->hash_next = cmd_hash_array[key];
	cmd_hash_array[key] = cmd;
}

/*
============
Cmd_AddLvl

HACK HACK HACK (plexi: clean this up sometime)
============
*/
void Cmd_AddLvl (char *cmd_name, xcommand_t function, int level)
{
	int		def;

	def = cmd_deflvl;
	cmd_deflvl = level;
	Cmd_AddCommand (cmd_name, function);
	cmd_deflvl = def;
}

/*
============
Cmd_Exists
============
*/
qbool Cmd_Exists (char *cmd_name)
{
	int	key;
	cmd_function_t	*cmd;

	key = Key (cmd_name);
	for (cmd=cmd_hash_array[key] ; cmd ; cmd=cmd->hash_next)
	{
		if (!strcasecmp (cmd_name, cmd->name))
			return true;
	}

	return false;
}

/*
============
Cmd_CompleteCommand
============
*/
char *Cmd_CompleteCommand (char *partial)
{
	cmd_function_t	*cmd;
	int				len;
	cmd_alias_t		*a;
	
	len = Q_strlen(partial);
	
	if (!len)
		return NULL;
		
// check for exact match
	for (cmd=cmd_functions ; cmd ; cmd=cmd->next)
		if (!strcmp (partial,cmd->name))
			return cmd->name;
	for (a=cmd_alias ; a ; a=a->next)
		if (!strcmp (partial, a->name))
			return a->name;

// check for partial match
	for (cmd=cmd_functions ; cmd ; cmd=cmd->next)
		if (!strncmp (partial,cmd->name, len))
			return cmd->name;
	for (a=cmd_alias ; a ; a=a->next)
		if (!strncmp (partial, a->name, len))
			return a->name;

	return NULL;
}

void Cmd_CmdList_f (void)
{
	cmd_function_t	*cmd;
	int	i;

	for (cmd=cmd_functions, i=0 ; cmd ; cmd=cmd->next, i++)
		Con_Printf("%s\n", cmd->name);

	Con_Printf ("------------\n%d commands\n", i);
}


/*
================
Cmd_ExpandString

Expands all $cvar expressions to cvar values
Note: dest must point to a 1024 byte buffer
================
*/
char *TP_MacroString (char *s);
void Cmd_ExpandString (char *data, char *dest)
{
	unsigned int	c;
	char	buf[255];
	int	i, len;
	cvar_t	*var, *bestvar;
	int	quotes = 0;
	char	*str;
	int	name_length = 0;

	len = 0;

	while ( (c = *data) != 0)
	{
		if (c == '"')
			quotes++;

		if (c == '$' && !(quotes&1))
		{
			data++;

			// Copy the text after '$' to a temp buffer
			i = 0;
			buf[0] = 0;
			bestvar = NULL;
			while ((c = *data) > 32)
			{
				if (c == '$')
					break;
				data++;
				buf[i++] = c;
				buf[i] = 0;
				if ( (var = Cvar_FindVar(buf)) != NULL )
					bestvar = var;

				if (i >= (int)sizeof(buf)-1)
					break; // there no more space in buf
			}

			if (bestvar)
			{
				str = bestvar->string;
				name_length = strlen(bestvar->name);
			}
			else
				str = NULL;

			if (str)
			{
				// check buffer size
				if (len + strlen(str) >= 1024-1)
					break;

				strlcpy(dest + len, str, 1024 - len);
				len += strlen(str);
				i = name_length;
				while (buf[i])
					dest[len++] = buf[i++];
			}
			else
			{
				// no matching cvar or macro
				dest[len++] = '$';
				if (len + strlen(buf) >= 1024-1)
					break;
				strlcpy (dest + len, buf, 1024 - len);
				len += strlen(buf);
			}
		}
		else
		{
			dest[len] = c;
			data++;
			len++;
			if (len >= 1024-1)
				break;
		}
	};

	dest[len] = 0;
}


/*
============
Cmd_ExecuteString
 
A complete command line has been parsed, so try to execute it
FIXME: lookupnoadd the token to speed search?
============
*/
extern qbool PR_ConsoleCmd(void);

void Cmd_ExecuteString (char *text)
{
	cmd_function_t	*cmd;
	cmd_alias_t	*a;
	int		key;
	static char	buf[1024];

	Cmd_ExpandString (text, buf);
	Cmd_TokenizeString (buf);

	// execute the command line
	if (!Cmd_Argc())
		return;		// no tokens

	key = Key (cmd_argv[0]);

	// check functions
	for (cmd=cmd_hash_array[key] ; cmd ; cmd=cmd->hash_next)
	{
		if (!strcasecmp (cmd_argv[0], cmd->name))
		{
			if (cmd->function) {
				if (pRconCurPos != NULL)
				{
					if (!Rcon_CmdValidate(cmd->name, pRconCurPos->accesslevel))
					{
						Com_Log (RCONLOG, LO_PRINT|LO_FORMAT|LO_TIMESTAMP, "level %i restricted command from rcon account \"%s\":\n%s\n",
							cmd->rconlevel, pRconCurPos->name, cmd->name);
						return;
					}
				}
				cmd->function ();
			}
			return;
		}
	}

	// check cvars
	if (Cvar_Command())
		return;

	// check alias
	for (a=cmd_alias_hash[key] ; a ; a=a->hash_next)
	{
		if (!strcasecmp (cmd_argv[0], a->name))
		{
			Cbuf_InsertText ("\n");
			Cbuf_InsertText (a->value);
			return;
		}
	}

	if (PR_ConsoleCmd())
		return;

	Con_Printf ("Unknown command \"%s\"\n", Cmd_Argv(0));
}


static qbool is_numeric (char *c)
{
	return (*c >= '0' && *c <= '9') ||
	       ((*c == '-' || *c == '+') && (c[1] == '.' || (c[1]>='0' && c[1]<='9'))) ||
	       (*c == '.' && (c[1]>='0' && c[1]<='9'));
}
/*
================
Cmd_If_f
================
*/
void Cmd_If_f (void)
{
	int		i, c;
	char	*op;
	qbool	result;
	char	buf[256];

	c = Cmd_Argc ();
	if (c < 5)
	{
		Con_Printf ("usage: if <expr1> <op> <expr2> <command> [else <command>]\n");
		return;
	}

	op = Cmd_Argv (2);
	if (!strcmp(op, "==") || !strcmp(op, "=") || !strcmp(op, "!=")
	        || !strcmp(op, "<>"))
	{
		if (is_numeric(Cmd_Argv(1)) && is_numeric(Cmd_Argv(3)))
			result = Q_atof(Cmd_Argv(1)) == Q_atof(Cmd_Argv(3));
		else
			result = !strcmp(Cmd_Argv(1), Cmd_Argv(3));

		if (op[0] != '=')
			result = !result;
	}
	else if (!strcmp(op, ">"))
		result = Q_atof(Cmd_Argv(1)) > Q_atof(Cmd_Argv(3));
	else if (!strcmp(op, "<"))
		result = Q_atof(Cmd_Argv(1)) < Q_atof(Cmd_Argv(3));
	else if (!strcmp(op, ">="))
		result = Q_atof(Cmd_Argv(1)) >= Q_atof(Cmd_Argv(3));
	else if (!strcmp(op, "<="))
		result = Q_atof(Cmd_Argv(1)) <= Q_atof(Cmd_Argv(3));
	else if (!strcmp(op, "isin"))
		result = strstr(Cmd_Argv(3), Cmd_Argv(1)) != NULL;
	else if (!strcmp(op, "!isin"))
		result = strstr(Cmd_Argv(3), Cmd_Argv(1)) == NULL;
	else
	{
		Con_Printf ("unknown operator: %s\n", op);
		Con_Printf ("valid operators are ==, =, !=, <>, >, <, >=, <=, isin, !isin\n");
		return;
	}

	buf[0] = '\0';
	if (result)
	{
		for (i=4; i < c ; i++)
		{
			if ((i == 4) && !strcasecmp(Cmd_Argv(i), "then"))
				continue;
			if (!strcasecmp(Cmd_Argv(i), "else"))
				break;
			if (buf[0])
				strlcat (buf, " ", sizeof(buf));
			strlcat (buf, Cmd_Argv(i), sizeof(buf));
		}
	}
	else
	{
		for (i=4; i < c ; i++)
		{
			if (!strcasecmp(Cmd_Argv(i), "else"))
				break;
		}

		if (i == c)
			return;

		for (i++ ; i < c ; i++)
		{
			if (buf[0])
				strlcat (buf, " ", sizeof(buf));
			strlcat (buf, Cmd_Argv(i), sizeof(buf));
		}
	}

	Cbuf_InsertText (buf);
}

/*
============
Cmd_Init
============
*/
void Cmd_Init (void)
{
	//
	// register our commands
	//
	Cmd_AddCommand ("exec",Cmd_Exec_f);
	Cmd_AddCommand ("echo",Cmd_Echo_f);
	Cmd_AddCommand ("alias",Cmd_Alias_f);
	Cmd_AddCommand ("wait", Cmd_Wait_f);
	Cmd_AddCommand ("cmdlist", Cmd_CmdList_f);
	Cmd_AddCommand ("unaliasall", Cmd_UnAliasAll_f);
	Cmd_AddCommand ("unalias", Cmd_UnAlias_f);
	Cmd_AddCommand ("if", Cmd_If_f);
}

/*
============
Cmd_ChangeCmdRconLvl
============
*/
qbool Cmd_ChangeCmdRconLvl (char *cmd_name, int rconlevel)
{
	int	key;
	cmd_function_t	*cmd;

	key = Com_HashKey (cmd_name);
	for (cmd=cmd_hash_array[key] ; cmd ; cmd=cmd->hash_next) 
	{
		if (!Q_strcasecmp (cmd_name, cmd->name)) 
		{
			cmd->rconlevel = rconlevel;
			return true;
		}
	}
	Com_Printf ("CmdAdj  : error: command %s does not exist!\n", cmd_name);
	return false;
}

/*
============
Rcon_CmdValidate
============
*/
qbool Rcon_CmdValidate (char *cmd_name, int inc_level) 
{
	cmd_function_t	*cmd;

	for (cmd=cmd_functions ; cmd ; cmd=cmd->next) 
	{
		if (!Q_strcasecmp (cmd_name,cmd->name)) 
		{
			if (cmd->rconlevel > inc_level)
				return false;
			else
				return true;
		}
	}
	return false;
}

/*
============
Rcon_CvarValidate
============
*/
int Rcon_CvarValidate (char *var_name, int inc_level) 
{
	cvar_t			*var;
 	
	var = Cvar_Find (var_name, false);
	if (!var)
 		return 3;

	if (var->rconlevel > inc_level)
		return 2;
	if ((var->rconlevel < RCON_ACCT_MIN) && 
		(inc_level < RCON_ACCT_MAX))
		return 2;

	return 1;

}

char	com_gamedir[MAX_OSPATH];
/*
=================
SV_RconWriteAccounts_f
AM101 real command: rcon_write
Writes all accounts
=================
*/
cvar_t			*cvar_vars;
void Com_Printf (char *fmt, ...);
void SV_RconWriteAccounts_f (void)
{
	FILE			*f;
	char			name[MAX_OSPATH], time[80];
	int				i, a=0, m, c;
	cmd_function_t	*wcmd;
	cvar_t			*cvar;
	qdate_t			date;

	Sys_TimeOfDayExt (&date, "%x %I:%M:%S %p");
	strncpy (time, date.cTimeStr, sizeof(time));

	Q_snprintf (name, sizeof(name), "%s/rcon.cfg", fs_gamedir); //plexi: changed to rcon.cfg
	Com_DPrintf ("Writing %s...\n", name);

	f = fopen (name, "wt");
	if (!f) 
	{
		Com_Printf ("Couldn't open %s\n", name);
		return;
	}

	// print basic information
	fprintf (f, "// Generated by %s Server %s\n// on %s\n\n// Adjusted command and cvar levels:\n",
		PROGRAM, SV_VERSION, time);

	for (m=0, wcmd=cmd_functions ; wcmd ; wcmd=wcmd->next) 
	{
		if (wcmd->rconlevel == wcmd->deflevel)
			continue; // it's level hasn't been modified

		fprintf (f, "set_cmdLevel %s %i\n", wcmd->name, 
			wcmd->rconlevel);
		m++;
	}
	for (c=0, cvar=cvar_vars ; cvar ; cvar=cvar->next) 
	{
		if (cvar->flags & CVAR_USER_CREATED)
			continue; // dont save user-created cvars

		if (cvar->rconlevel == cvar->deflevel)
			continue; // it's level hasn't been modified

		fprintf (f, "set_cvarLevel %s %i\n", cvar->name,
			cvar->rconlevel);
		c++;
	}

	fprintf (f, "\n//Rcon Accounts:\n");

	pRconCurPos = pRconRoot;
	while (pRconCurPos != NULL) 
	{
		// name, password, level
		fprintf (f, "rcon_add %s %s %i\n", pRconCurPos->name,
			pRconCurPos->password, pRconCurPos->accesslevel);
		for (a=0, i=0 ; i <=4 ; i++, a++) 
		{
			if (pRconCurPos->ips[i][0] == 0)
				continue;
			else
				fprintf (f, "rcon_addip %s %i.%i.%i.%i\n", 
				pRconCurPos->name, pRconCurPos->ips[i][0], 
				pRconCurPos->ips[i][1], pRconCurPos->ips[i][2], 
				pRconCurPos->ips[i][3]);
		}
		pRconCurPos = pRconCurPos->next;
	}
	fprintf (f, "\n//wrote %i command levels, %i cvar levels and %i accounts\n", m, c, a);
	fclose (f);

	Con_Printf("Rcon_Write: %i cmd levels; %i cvar levels, %i accounts written.\n",m, c, a);

	pRconCurPos = NULL;

}

/*
==================
CL_MapList_f

Lists maps in the maps\ directory in current gamedir to the client.
Called by player saying "maplist" or, if using TF United, "cmd maplist" (alias "maplist")
==================
*/

void CL_MapList_f (char *mfilter)
{
	dir_t		dir;
	file_t		*list;
	int			i, c;
	int			ps = 0, pi = 0;
	char		*key, *end, *pmap;

	dir = Sys_listdir(va("%s/maps", fs_gamedir), ".bsp", SORT_BY_NAME);
	key = mfilter;

	list = dir.files;
	if (!list->name[0]) 
	{
		Con_Printf( "no maps.\n");
		//Com_Printf("no maps.\n"); // if all maps are in pak file(s)
		return;
	}
/*	if (Cmd_Argc() == 2) {
		Com_Printf ("first.\n");
		key = Cmd_Argv(1);
	}
	else {
		key = "";
		Com_Printf ("last.\n");
	}*/
	if (strcmp(key, ""))
		//Com_Printf("Contents of %s/maps/*%s*.bsp:\n", fs_gamedir, key);
		Con_Printf( "Contents of %s/maps/*%s*.bsp:\n", fs_gamedir, key);
	else
		//Com_Printf("Contents of %s/maps/*.bsp:\n", fs_gamedir);
		Con_Printf( "Contents of %s/maps/*.bsp:\n", fs_gamedir);

	//Com_Printf ("\n");
	Con_Printf( "\n");
	for (i = 0, c = 0; list->name[0]; i++, list++)
	{
		if (Q_stristr(list->name, key))
		{ 
			// use case-insensitive search
			
			pmap = list->name;
			end = strstr (pmap, ".bsp");

			if (c < 1)
			{ 
				//split up into two columns
				//Com_Printf ("%-16.*s ", end - pmap, pmap);
				Con_Printf( "%-16.*s ", end - pmap, pmap);
				c++;
			}
			else 
			{
				//Com_Printf ("%-1.*s\n", end - pmap, pmap);
				Con_Printf( "%-1.*s\n", end - pmap, pmap);
				c = 0;
			}
			if (strcmp(key, ""))
			{
				ps += list->size;
				pi++;
			}
		}
	}
	//Com_Printf ("\n");
	//Com_Printf ("\n"); //ugh @ timestamps
	Con_Printf( "\n");
	Con_Printf( "\n");
	if (strcmp(key, ""))
		//Com_Printf ("Wildcard total:  %d maps, %.1fMB\n", pi, (float)ps/(1024*1024));
		Con_Printf( "Wildcard total:  %d maps, %.1fMB\n", pi, (float)ps/(1024*1024));
	//Com_Printf ("Directory total: %d maps, %.1fMB\n", i, (float)dir.size/(1024*1024));
	Con_Printf( "Directory total: %d maps, %.1fMB\n", i, (float)dir.size/(1024*1024));

}

void CL_SkinList_f (char *mfilter)
{
	dir_t		dir;
	file_t		*list;
	int			i, c;
	int			ps = 0, pi = 0;
	char		*key, *end, *pmap;

	dir = Sys_listdir(va("%s/skins", fs_gamedir), ".pcx", SORT_BY_NAME);
	key = mfilter;

	list = dir.files;
	if (!list->name[0]) 
	{
		Con_Printf( "no maps.\n");
		//Com_Printf("no maps.\n"); // if all maps are in pak file(s)
		return;
	}
/*	if (Cmd_Argc() == 2) {
		Com_Printf ("first.\n");
		key = Cmd_Argv(1);
	}
	else {
		key = "";
		Com_Printf ("last.\n");
	}*/
	if (strcmp(key, ""))
		//Com_Printf("Contents of %s/maps/*%s*.pcx:\n", fs_gamedir, key);
		Con_Printf( "Contents of %s/skins/*%s*.pcx:\n", fs_gamedir, key);
	else
		//Com_Printf("Contents of %s/maps/*.pcx:\n", fs_gamedir);
		Con_Printf( "Contents of %s/skins/*.pcx:\n", fs_gamedir);

	//Com_Printf ("\n");
	Con_Printf( "\n");
	for (i = 0, c = 0; list->name[0]; i++, list++)
	{
		if (Q_stristr(list->name, key))
		{ 
			// use case-insensitive search
			
			pmap = list->name;
			end = strstr (pmap, ".pcx");

			if (c < 1)
			{ 
				//split up into two columns
				//Com_Printf ("%-16.*s ", end - pmap, pmap);
				Con_Printf( "%-16.*s ", end - pmap, pmap);
				c++;
			}
			else 
			{
				//Com_Printf ("%-1.*s\n", end - pmap, pmap);
				Con_Printf( "%-1.*s\n", end - pmap, pmap);
				c = 0;
			}
			if (strcmp(key, ""))
			{
				ps += list->size;
				pi++;
			}
		}
	}
	//Com_Printf ("\n");
	//Com_Printf ("\n"); //ugh @ timestamps
	Con_Printf( "\n");
	Con_Printf( "\n");
	if (strcmp(key, ""))
		//Com_Printf ("Wildcard total:  %d maps, %.1fMB\n", pi, (float)ps/(1024*1024));
		Con_Printf( "Wildcard total:  %d skinss, %.1fMB\n", pi, (float)ps/(1024*1024));
	//Com_Printf ("Directory total: %d maps, %.1fMB\n", i, (float)dir.size/(1024*1024));
	Con_Printf( "Directory total: %d skins, %.1fMB\n", i, (float)dir.size/(1024*1024));

}

/*
==================
RandomMap

Returns a random map from the maps\ directory
==================
*/
char *RandomMap (char *mfilter)
{
	dir_t		dir;
	file_t		*list;
	int			i, q;
	int			ps = 0, pi = 0;
	char		*key, *end, *pmap;
	char		*mapname[1024];

	dir = Sys_listdir(va("%s/maps", fs_gamedir), ".bsp", SORT_BY_NAME);
	key = mfilter;

	list = dir.files;
	//strncat(mapname[0],"none",sizeof(mapname[0]));
	mapname[0] = "none";
	if (!list->name[0]) 
	{
		return mapname[0];
	}

	for (i = 0; list->name[0]; i++, list++)
	{
		if (Q_stristr(list->name, key))
		{ 
			// use case-insensitive search
			list->name[strlen(list->name) - 4] = 0;	// remove ".bsp"
			pmap = list->name;
			end = strstr (pmap, ".bsp");
			mapname[i] = pmap;
			//strncat(mapname[i],pmap,sizeof(mapname[i]));

			if (strcmp(key, ""))
			{
				ps += list->size;
				pi++;
			}
		}
	}

	q = rand() % i;
	return mapname[q];
}

/*
==================
RandomBotMap

Returns a random map from the gamedir (not in /maps) based on .wpt files
==================
*/
char *RandomBotMap (char *mfilter)
{
	dir_t		dir;
	file_t		*list;
	int			i, q;
	int			ps = 0, pi = 0;
	char		*key, *end, *pmap;
	char		*mapname[1024];

	dir = Sys_listdir(va("%s", fs_gamedir), ".wpt", SORT_BY_NAME);
	key = mfilter;

	list = dir.files;
	//strncat(mapname[0],"none",sizeof(mapname[0]));
	mapname[0] = "none";
	if (!list->name[0]) 
	{
		return mapname[0];
	}

	for (i = 0; list->name[0]; i++, list++)
	{
		if (Q_stristr(list->name, key))
		{ 
			// use case-insensitive search
			list->name[strlen(list->name) - 4] = 0;	// remove ".wpt"
			pmap = list->name;
			end = strstr (pmap, ".wpt");
			mapname[i] = pmap;
			//strncat(mapname[i],pmap,sizeof(mapname[i]));

			if (strcmp(key, ""))
			{
				ps += list->size;
				pi++;
			}
		}
	}

	q = rand() % i;
	return mapname[q];
}