/*
Copyright (C) 2004 Frederick Lascelles, Jr.

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
#include "qwsvdef.h"
#include "rcon.h"
#include "timer.h" // for when account is removed

void SV_RconWriteAccounts_f (void);
extern cvar_t rcon_password;
extern cvar_t lock_cmdLevels;
extern cvar_t lock_cvarLevels;
redirect_t		sv_redirected;

cvar_t	if_filterban = {"if_filterban", "1"};
cvar_t	if_filtermute = {"if_filtermute", "1"};
// see rcon.h

int eRcon_Validate (char *pwd)
{
	int		i, fromip[4];
	char	*s;

	// if it is empty try to grab it from somewhere else
	if (!pwd[0])
		s = Cmd_Argv (1);

	if (pRconRoot != NULL) 
	{ 
		// AM101 start rcon account check
		pRconCurPos = pRconRoot;
		while (pRconCurPos != NULL) 
		{
			if (!strcmp(pRconCurPos->password, pwd)) 
			{ 
				// passwords match, begin IP verification
				NET_AdrToArray (fromip);
				for (i=0 ; i <= 4 ; i++) 
				{
					if (NET_CompareArrays(fromip, pRconCurPos->ips[i])) 
					{
						return pRconCurPos->accesslevel;
					}
				}
				return 0;
			}
			pRconCurPos = pRconCurPos->next;
		} 
	} // end rcon account check

	if (!strlen(rcon_password.string))
		return 0;

	if (strcmp(pwd, rcon_password.string))
		return 0;

	return MAIN_RCON;
}

/*
===============
SV_set_cmdlevel_f

Originally by Zxeses
===============
*/
void SV_set_cmdlevel_f (void)
{
	int		arg2;
	
	// plexi: only allow level adjustments from local console 
	if ((lock_cmdLevels.value) && (sv_redirected != RD_NONE))
	{
		Com_Printf ("Changing command levels has been disabled by the admin\n");
		return;
	}

	arg2 = atoi (Cmd_Argv (2));

 	if ((Cmd_Argc() < 3) || (arg2 > 6))
	{
		Com_Printf ("usage: set_cmdLevel <command> <level 1-6>\n");
		return;
	}

	if (Cmd_ChangeCmdRconLvl(Cmd_Argv (1), arg2))
		Com_Printf ("CmdAdj  : \"%s\" is now level: \"%d\"\n",
		Cmd_Argv (1), arg2);

	return;
}

/*
===============
SV_set_cvarlevel_f

Originally by Zxeses
===============
*/
void SV_set_cvarlevel_f (void)
{
 	int				arg2;
	cvar_t			*v;

	if ((lock_cvarLevels.value) && (sv_redirected != RD_NONE)) 
	{
		Com_Printf ("CvarAdj : changing command levels has been disabled by the admin\n");
		return;
	}

	arg2 = atoi (Cmd_Argv (2));

 	if ((Cmd_Argc () < 3) || (arg2 > 6))
	{
 		Com_Printf ("usage: set_cvarLevel <cvar> <level 1-6>\n"); //plexi: changed from 98 levels to 6, added lock check, moved old procedure into here 01/27/02 06:10
 		return;
	}

	if (Cmd_Exists (Cmd_Argv(1))) 
	{
 		Com_Printf ("CvarAdj : error: %s is a command\n", Cmd_Argv(1));
 		return;
	}

	v = Cvar_Find (Cmd_Argv(1), false);
	if (!v) 
	{
 		Com_Printf ("CvarAdj : error: cvar \"%s\" does not exist!\n", Cmd_Argv(1)); //plexi: changed from 98 levels to 5 01/29/02 16:10
 		return;
	}

	v->rconlevel = arg2;
 	Com_Printf ("CvarAdj : \"%s\" is now level: \"%d\"\n", 
		Cmd_Argv (1), arg2);
	
	return;
}

/*
================
SV_RconDelNode 

AM101's internal function to rcon_del
================
*/
void SV_RconDelNode (char *name)
{
	rcon_struct		*pTemp = pRconRoot; // we'll need this later, wink, wink

	if (!strcmp(pRconCurPos->name,name))
	{	

		// first make sure any timers that were created by this account die 
		Timer_AccountRemoved (pRconCurPos);

		// double check :)
		free (pRconCurPos->name);
		free (pRconCurPos->password);
		if (pRconRoot == pRconCurPos) 
			pRconRoot = pRconCurPos->next;
		else
		{
			while (pTemp->next != pRconCurPos)
				pTemp = pTemp->next;
			pTemp->next = pRconCurPos->next; // break the link, attach to next link
		}
		free (pRconCurPos);
		pRconCurPos = NULL; // don't screw up
	} 
	else
		Com_Printf ("WEIRD ERROR: deleting account %s\n", name); //plexi: added line break

}

/*
================
SV_RconDelIp

AM101's deletion of an IP for access to an account.
================
*/
void SV_RconDelIp_f (void)
{
	int			iptodel[4], a=0; // ip to delete (Cmd_Argv(2))

	if ((Cmd_Argc() != 3) || (strlen(Cmd_Argv(1)) > RCON_MAXNAMELEN))
	{
		Com_Printf ("usage: rcon_delip <name> <ip mask>\n");
		return;
	}
	
	if (!SV_RconFindAcctName(Cmd_Argv(1)))
	{
		Com_Printf ("user not found.\n");
		return;
	}
	
	// Parse IP

	if (!NET_StringToArray(Cmd_Argv(2),(int*)&iptodel))
	{	
		// not needed, but why not
		Com_Printf ("bad address: %i.%i.%i.%i\n", iptodel[0],
			iptodel[1],iptodel[2],iptodel[3]);
		return;
	}
	// add the ip to the list
	for (a = 0; a <= 4 ; a++) 
	{
		if (NET_CompareArrays(iptodel,pRconCurPos->ips[a]))
		{
			Com_Printf("%i.%i.%i.%i deleted.\n", pRconCurPos->ips[a][0],pRconCurPos->ips[a][1],pRconCurPos->ips[a][2],pRconCurPos->ips[a][3]);
			// print before you actually delete it :)
			pRconCurPos->ips[a][0] = pRconCurPos->ips[a][1] = pRconCurPos->ips[a][2] = pRconCurPos->ips[a][3] = 0;
		}
	}
	pRconCurPos = NULL; // safe
}

/*
================
SV_RconFindUser 

Originally from AM101
Transverses the rcon list displaying an account
================
*/
void SV_RconFindUser_f (void)
{
	int			a;

	if (Cmd_Argc() != 2 || strlen(Cmd_Argv(1)) > RCON_MAXNAMELEN)
	{
		Com_Printf ("usage: rcon_user <name>\n");
		return;
	}

	if (SV_RconFindAcctName(Cmd_Argv(1)))
	{
		Com_Printf ("name: %s \t access: %i\n", pRconCurPos->name, pRconCurPos->accesslevel);
		Com_Printf ("IPs: \n");
		for (a=0;a < 5; a++)
			Com_Printf ("\t%3i.%3i.%3i.%3i\n", pRconCurPos->ips[a][0], pRconCurPos->ips[a][1], pRconCurPos->ips[a][2], pRconCurPos->ips[a][3]);

		return;
	} 
	else 
	{
		Com_Printf ("user not found.\n");
		pRconCurPos = NULL; // safe
	}
}

/*
================
SV_RconList

Originally from AM101
Transverses the rcon list displaying all accounts
================
*/
void SV_RconList_f (void)
{
	if (pRconRoot == NULL) 
	{
		Con_Printf ("No accounts.\n");
		return;
	} 
	else
		pRconCurPos = pRconRoot;

	Con_Printf ("acct name           lvl\n");
	Con_Printf ("%s ---\n", Com_PrintLine (20)); // name length
	
	while (pRconCurPos != NULL)
	{
		Con_Printf ("%-20s %i\n", pRconCurPos->name, pRconCurPos->accesslevel);
		pRconCurPos = pRconCurPos->next;
	}

	pRconCurPos = NULL;
}

/*
================
SV_RconFindAcctName

Originally from AM101
Internal function to rcon_del and rcon_user
================
*/
qbool SV_RconFindAcctName (char *name)
{
	if (pRconRoot == NULL)
		return false;

	pRconCurPos = pRconRoot;
	while (pRconCurPos != NULL) 
	{
		if (!strcmp(pRconCurPos->name,name))
			return true;
		pRconCurPos = pRconCurPos->next;
	}

	return false;
}

/*
================
SV_RconAddIp

Originally from AM101
Adds an IP for access to an account
================
*/
void SV_RconAddIp_f (void)
{
	int			iptoadd[4], i, a; // ip to add (Cmd_Argv(2))

	if ((Cmd_Argc() != 3) || (strlen(Cmd_Argv(1)) > RCON_MAXNAMELEN))
	{
		Con_Printf ("usage: rcon_addip <name> <ip mask>\n");
		return;
	}
	
	if (!SV_RconFindAcctName(Cmd_Argv(1))) 
	{
		Con_Printf ("RconAcct: user not found.\n");
		return;
	}
	for (a = 0 ; a < RCON_ACCT_MAX ; a++) 
	{
		if (pRconCurPos->ips[a][0] == 0)
			break; // open spot
	}
	
	if (a == RCON_ACCT_MAX)
	{
		Con_Printf ("RconAcct: no spaces open -- delete an IP first.\n");
		return;
	}
	
	// Parse IP
	if (!NET_StringToArray(Cmd_Argv(2),(int*)&iptoadd)) 
	{
		Con_Printf ("RconAcct: bad address: %i.%i.%i.%i\n",
			iptoadd[0], iptoadd[1],iptoadd[2],iptoadd[3]);
		return;
	}

	// add the ip to the list
	for (i = 0; i <= 3 ; i++)
		pRconCurPos->ips[a][i] = iptoadd[i];

	Con_Printf ("RconAcct: added %i.%i.%i.%i for account %s\n", iptoadd[0], iptoadd[1],iptoadd[2],iptoadd[3], pRconCurPos->name);
	
	pRconCurPos = NULL; // safe
}


/*
================
SV_Rcon_InsertNode

Originally from AM101
internal function to rcon_add
================
*/
void SV_Rcon_InsertNode (rcon_struct *pNew)
{
	if (pRconRoot == NULL)
	{
		if (!Q_strcasecmp(pNew->password,rcon_password.string))
		{
			Com_Printf ("Error: name or password is not unique.\n");
			return;
		}
		pRconRoot = pNew;
		Com_Printf ("RconAcct: added %s to rcon list\n", pNew->name);
		return;
	} 
	else
		pRconCurPos = pRconRoot;

	while (pRconCurPos->next != NULL)
	{
		if ((!strcmp(pRconCurPos->name,pNew->name)) ||
			(!Q_strcasecmp(pNew->password,rcon_password.string)) ||
			(!strcmp(pRconCurPos->password,pNew->password))) 
		{
			Com_Printf ("Error: name or password is not unique.\n");
			return;
		}
		pRconCurPos = pRconCurPos->next;
	}

	if ((!strcmp(pRconCurPos->name,pNew->name)) ||
		(!strcmp(pRconCurPos->password,rcon_password.string)) ||
		(!strcmp(pRconCurPos->password,pNew->password))) 
	{
		Com_Printf ("Error: name or password is not unique.\n");
		return;
	}

	pRconCurPos->next = pNew;
	Com_Printf("RconAcct: added %s to rcon list\n", pNew->name);
}

/*
================
SV_RconDelAcct

Originally from AM101
Deletes an account by name
================
*/
void SV_RconDelAcct_f (void)
{
	if ((Cmd_Argc() != 2) || (strlen(Cmd_Argv(1)) > RCON_MAXNAMELEN))
	{
		Com_Printf ("usage: rcon_del <name>\n");
		return;
	}

	if (SV_RconFindAcctName(Cmd_Argv(1)))
		SV_RconDelNode (Cmd_Argv(1));
	else 
	{
		Com_Printf ("RconAcct: error - user %s not in rcon list\n", 
			Cmd_Argv(1));
		return;
	}
	Com_Printf ("RconAcct: deleted %s\n", Cmd_Argv(1));
}

/*
================
SV_RconAddAcct

Originally from AM101
Starting point for many functions
================
*/
void SV_RconAddAcct_f (void)
{ 
	int a, b;
	rcon_struct *pNew = NULL;

	if ((Cmd_Argc() != 4) || (strlen(Cmd_Argv(1)) > RCON_MAXNAMELEN) || 
		(strlen(Cmd_Argv(2)) > RCON_MAXPASSLEN) || (strlen(Cmd_Argv(3)) > 1))
	{
		Com_Printf ("usage:  rcon_add <name> <password> <level>\n");
		return;
	}
	
	if ((atoi(Cmd_Argv(3)) < 1) || (atoi(Cmd_Argv(3)) > 5))
	{
		Com_Printf("RconAcct: error - level must be 1-5\n");
		return;
	}

	if (!Q_strcasecmp(Cmd_Argv(3),rcon_password.string)) 
	{
		Com_Printf ("RconAcct: error - password is not unique\n");
		return;
	}

	pNew = (rcon_struct *)Q_Malloc(sizeof(rcon_struct));
	if (pNew == NULL)
	{
		Com_Printf ("RconAcct: error - not enough memory to create account!\n");
		return;
	}
	pNew->next = NULL; // now the struct is made, copy values

	pNew->name = (char *)Q_Malloc(strlen(Cmd_Argv(1))+1);
	pNew->password = (char *)Q_Malloc(strlen(Cmd_Argv(2))+1);

	strcpy(pNew->name,Cmd_Argv(1));
	strcpy(pNew->password,Cmd_Argv(2)); 
	pNew->accesslevel = atoi(Cmd_Argv(3));

	for (a = 0 ; a <= 4 ; a++)
		for (b = 0; b<= 3 ; b++)
			pNew->ips[a][b] = 0;

	SV_Rcon_InsertNode(pNew);

	pRconCurPos = NULL; // safe
}

void Rcon_Init (void)
{
	Cmd_AddCommand ("rcon_add", SV_RconAddAcct_f);
	Cmd_AddCommand ("rcon_addip", SV_RconAddIp_f);
	Cmd_AddCommand ("rcon_del", SV_RconDelAcct_f);
	Cmd_AddCommand ("rcon_delip", SV_RconDelIp_f);
	Cmd_AddCommand ("rcon_list", SV_RconList_f);
	Cmd_AddCommand ("rcon_user", SV_RconFindUser_f);
	Cmd_AddCommand ("rcon_write", SV_RconWriteAccounts_f);

	// plexi: rcon levels made by zxeses (modified)
	Cmd_AddCommand ("set_cmdLevel", SV_set_cmdlevel_f);
	Cmd_AddCommand ("set_cvarLevel", SV_set_cvarlevel_f);

	//filters
	Cvar_Register (&if_filterban);
	Cvar_Register (&if_filtermute);
	//Cvar_Register (&if_cfgdir);
	//Cvar_Register (&if_autosave);
}