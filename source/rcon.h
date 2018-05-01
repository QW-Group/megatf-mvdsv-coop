/*
Copyright (C) 2003 Frederick Lascelles, Jr.

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

*/

#ifndef TIMER_H
#define TIMER_H

#define MAIN_RCON		99

#define RCON_ACCT_MIN	1
#define RCON_ACCT_MAX	5
#define	RCON_ACCT_DEF	5

#define RCON_MAXNAMELEN		20
#define RCON_MAXPASSLEN		35

typedef struct rcon_struct_s
{
	char		*name;
	char		*password;
	int			accesslevel;
	int			ips[RCON_ACCT_MAX][4];
	struct rcon_struct_s *next;
} rcon_struct;

rcon_struct		*pRconRoot;
rcon_struct		*pRconCurPos;


extern rcon_struct	*pRconRoot;
extern rcon_struct	*pRconCurPos;


extern int eRcon_Validate (char *pwd);
void Rcon_Init (void);

// BELOW IS STUFF ARE RANDOM EQUAKE-SPECIFIC DEFS


typedef struct qfilter_s
{
	unsigned			mask;
	unsigned			compare;
	long				expireTime;
	char				*reason;	// reason (if any) of the filter
	int					flags;

	struct qfilter_s	*next;
} qfilter_t;

#ifndef _WIN32
        #define stricmp strcasecmp
		#define _stricmp strcasecmp
#endif

// filter stuff
int					filtercount;
// filter list's max reason length 
// (kickban reasons can get lengthy: 32 name length + 80 typed reason)
#define IF_MAXREASONLEN		112 

// maximum amount of addresses to the filter list
#define MAX_IP_FILTERS		1024

#endif

void Com_DPrintf (char *fmt, ...);
void Z_Free (void *ptr);
void NET_AdrToArray (int *ip);
int NET_CompareArrays (int *one, int *two);
qbool Cmd_ChangeCmdRconLvl (char *cmd_name, int rconlevel);
qbool SV_RconFindAcctName (char *name);
qbool NET_StringToArray (char *address, int *ip);
char *Com_PrintLine (int length);
void *Q_Malloc (size_t size);

qbool OnChange_logcommands_var (cvar_t *var, char *string);
qbool OnChange_logfrags_var (cvar_t *var, char *string);

// filter list flags (what type of filter)
#define	IF_BAN				1
#define IF_MUTE				2

void Sys_TimeOfDayExt (qdate_t *date, char *format);
void Q_snprintf (char *dest, size_t size, char *fmt, ...);
char*	Q_stristr (char *strc1, char *strc2);
int NET_AdrToPort (netadr_t a);
void Sys_TimeOfDay (qdate_t *date);
qbool Rcon_CmdValidate (char *cmd_name, int inc_level);
int Rcon_CvarValidate (char *var_name, int inc_level);

//filters
//void IF_FilterList (int type);
extern qfilter_t *IF_AddFilter (int type, int uid, char *ip, char *reason);
extern cvar_t	if_cfgdir;
