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

	$Id: sv_user.c,v 1.92 2007/04/09 15:06:41 vvd0 Exp $
*/
// sv_user.c -- server code for moving users

#include "qwsvdef.h"

#ifdef VOICECHAT
cvar_t sv_voip = {"sv_voip", "1"};//, "Enable reception of voice packets.");
cvar_t sv_voip_record = {"sv_voip_record", "0"};// "Record voicechat into mvds. Requires player support.");
cvar_t sv_voip_echo = {"sv_voip_echo", "0"};//, "Echo voice packets back to their sender, a debug/test setting.");
#endif

// eqds
extern cvar_t	sv_MegaTFHacks;
edict_t	*sv_player;
extern qbool IF_FilterPacket (netadr_t addr, int type);
#define IF_MUTE				2		// fixme: #define'd twice :|

usercmd_t	cmd;

cvar_t	sv_spectalk = {"sv_spectalk", "1"};
cvar_t	sv_stealthtalk =	{"sv_stealthtalk", "1"};
cvar_t	sv_sayteam_to_spec = {"sv_sayteam_to_spec", "1"};
cvar_t	sv_mapcheck = {"sv_mapcheck", "1"};
cvar_t	sv_minping = {"sv_minping", "0"};
cvar_t	sv_enable_cmd_minping = {"sv_enable_cmd_minping", "0"};

cvar_t	sv_use_internal_cmd_dl = {"sv_use_internal_cmd_dl", "1"};

cvar_t	sv_kickuserinfospamtime = {"sv_kickuserinfospamtime", "3"};
cvar_t	sv_kickuserinfospamcount = {"sv_kickuserinfospamcount", "150"};	// orig: 30

cvar_t	sv_maxuploadsize = {"sv_maxuploadsize", "1048576"};

#ifdef FTE_PEXT_CHUNKEDDOWNLOADS
cvar_t  sv_downloadchunksperframe = {"sv_downloadchunksperframe", "2"};
#endif

// check for bots or not (let the mods that really use this set it to "1" in QC)
cvar_t	sv_botscore = {"sv_botscore", "0"};

extern	vec3_t	player_mins;

extern int	fp_messages, fp_persecond, fp_secondsdead;
extern char	fp_msg[];
extern cvar_t	pausable;
extern cvar_t	pm_bunnyspeedcap;
extern cvar_t	pm_ktjump;
extern cvar_t	pm_slidefix;
extern cvar_t	pm_airstep;
extern cvar_t	pm_pground;
extern double	sv_frametime;

//bliP: init ->
extern cvar_t	sv_unfake; //bliP: 24/9 kickfake to unfake
extern cvar_t	sv_kicktop;
extern cvar_t	sv_speedcheck; //bliP: 24/9
//<-

static qbool IsLocalIP(netadr_t a)
{
	return a.ip.ip[0] == 10 || (a.ip.ip[0] == 172 && (a.ip.ip[1] & 0xF0) == 16)
	       || (a.ip.ip[0] == 192 && a.ip.ip[1] == 168) || a.ip.ip[0] >= 224;
}
static qbool IsInetIP(netadr_t a)
{
	return a.ip.ip[0] != 127 && !IsLocalIP(a);
}
/*
============================================================

USER STRINGCMD EXECUTION

sv_client and sv_player will be valid.
============================================================
*/

/*
================
Cmd_New_f

Sends the first message from the server to a connected client.
This will be sent on the initial connection and upon each server load.
================
*/
int SV_VIPbyIP (netadr_t adr);
static void Cmd_New_f (void)
{
	char		*gamedir;
	int			playernum;
	extern cvar_t sv_login;
	extern cvar_t sv_serverip;
	extern cvar_t sv_getrealip;

	if (sv_client->state == cs_spawned)
		return;

	if (!sv_client->connection_started || sv_client->state == cs_connected)
		sv_client->connection_started = realtime;

	sv_client->spawncount = svs.spawncount;
	// do not proceed if realip is unknown
    if (sv_client->state == cs_preconnected && !sv_client->realip.ip.ip[0] && (int)sv_getrealip.value)
	{
		char *server_ip = sv_serverip.string[0] ? sv_serverip.string : NET_AdrToString(net_local_adr);

		if (!((IsLocalIP(net_local_adr) && IsLocalIP(sv_client->netchan.remote_address))  ||
		        (IsInetIP (net_local_adr) && IsInetIP (sv_client->netchan.remote_address))) &&
		        sv_client->netchan.remote_address.ip.ip[0] != 127 && !sv_serverip.string[0])
		{
			Sys_Printf ("WARNING: Incorrect server ip address: %s\n"
			            "Set hostname in your operation system or set correctly sv_serverip cvar.\n",
			            server_ip);
			*(int *)&sv_client->realip = *(int *)&sv_client->netchan.remote_address;
			sv_client->state = cs_connected;
		}
		else
		{
			if (sv_client->realip_count++ < 10)
			{
				sv_client->state = cs_preconnected;
				MSG_WriteByte (&sv_client->netchan.message, svc_stufftext);
				MSG_WriteString (&sv_client->netchan.message,
								 va("packet %s \"ip %d %d\"\ncmd new\n", server_ip,
									sv_client - svs.clients, sv_client->realip_num));
			}
			if (realtime - sv_client->connection_started > 3 || sv_client->realip_count > 10)
			{
				if ((int)sv_getrealip.value == 2)
				{
					Netchan_OutOfBandPrint (net_from,
						"%c\nFailed to validate client's IP.\n\n", A2C_PRINT);
					sv_client->rip_vip = 2;
				}
				sv_client->state = cs_connected;
			}
			else
				return;
		}
	}

	// rip_vip means that client can be connected if he has VIP for he's real ip
	// drop him if he hasn't
	if (sv_client->rip_vip == 1)
	{
		if ((sv_client->vip = SV_VIPbyIP(sv_client->realip)) == 0)
		{
			Sys_Printf ("%s:full connect\n", NET_AdrToString (net_from));
			Netchan_OutOfBandPrint (net_from,
			                        "%c\nserver is full\n\n", A2C_PRINT);
		}
		else
			sv_client->rip_vip = 0;
	}

	// we can be connected now, announce it, and possibly login
	if (!sv_client->rip_vip)
	{
		if (sv_client->state == cs_preconnected)
		{
			// get highest VIP level
			if (sv_client->vip < SV_VIPbyIP(sv_client->realip))
				sv_client->vip = SV_VIPbyIP(sv_client->realip);

			if (sv_client->vip && sv_client->spectator)
				Sys_Printf ("VIP spectator %s connected\n", sv_client->name);
			else if (sv_client->spectator == STEALTH)
				Sys_Printf ("Stealth spectator %s connected\n", sv_client->name);
			else if (sv_client->spectator)
				Sys_Printf ("Spectator %s connected\n", sv_client->name);
			else
				Sys_Printf ("Client %s connected\n", sv_client->name);

			Info_SetStar (&sv_client->_userinfo_ctx_, "*VIP", sv_client->vip ? va("%d", sv_client->vip) : "");

			// now we are connected
			sv_client->state = cs_connected;
		}

		if (!SV_Login(sv_client))
			return;

		if (!sv_client->logged && (int)sv_login.value)
			return; // not so fast;

		//bliP: cuff, mute ->
		sv_client->lockedtill = SV_RestorePenaltyFilter(sv_client, ft_mute);
		sv_client->cuff_time = SV_RestorePenaltyFilter(sv_client, ft_cuff);
		//<-
	}
	// send the info about the new client to all connected clients
	//	SV_FullClientUpdate (sv_client, &sv.reliable_datagram);
	//	sv_client->sendinfo = true;

	gamedir = Info_ValueForKey (svs.info, "*gamedir");
	if (!gamedir[0])
		gamedir = "qw";

	//NOTE:  This doesn't go through ClientReliableWrite since it's before the user
	//spawns.  These functions are written to not overflow
	if (sv_client->num_backbuf)
	{
		Con_Printf("WARNING %s: [SV_New] Back buffered (%d0), clearing\n",
		           sv_client->name, sv_client->netchan.message.cursize);
		sv_client->num_backbuf = 0;
		SZ_Clear(&sv_client->netchan.message);
	}

	// send the serverdata
	MSG_WriteByte  (&sv_client->netchan.message, svc_serverdata);
#ifdef PROTOCOL_VERSION_FTE
	if (sv_client->fteprotocolextensions) // let the client know
	{
		MSG_WriteLong (&sv_client->netchan.message, PROTOCOL_VERSION_FTE);
		MSG_WriteLong (&sv_client->netchan.message, sv_client->fteprotocolextensions);
	}
	if (sv_client->fteprotocolextensions2)//let the client know
	{
		MSG_WriteLong (&sv_client->netchan.message, PROTOCOL_VERSION_FTE2);
		MSG_WriteLong (&sv_client->netchan.message, sv_client->fteprotocolextensions2);
	}
#endif
	MSG_WriteLong  (&sv_client->netchan.message, PROTOCOL_VERSION);
	MSG_WriteLong  (&sv_client->netchan.message, svs.spawncount);
	MSG_WriteString(&sv_client->netchan.message, gamedir);

	playernum = NUM_FOR_EDICT(sv_client->edict)-1;
	if (sv_client->spectator)
		playernum |= 128;
	MSG_WriteByte (&sv_client->netchan.message, playernum);

	// send full levelname
	if (sv_client->rip_vip)
		MSG_WriteString (&sv_client->netchan.message, "");
	else
		MSG_WriteString (&sv_client->netchan.message,
#ifdef USE_PR2
		                 PR2_GetString(sv.edicts->v.message)
#else
						 PR_GetString(sv.edicts->v.message)
#endif
		                );

	// send the movevars
	MSG_WriteFloat(&sv_client->netchan.message, movevars.gravity);
	MSG_WriteFloat(&sv_client->netchan.message, movevars.stopspeed);
	MSG_WriteFloat(&sv_client->netchan.message, /* sv_client->maxspeed */ movevars.maxspeed); // FIXME: this does't work, Tonik?
	MSG_WriteFloat(&sv_client->netchan.message, movevars.spectatormaxspeed);
	MSG_WriteFloat(&sv_client->netchan.message, movevars.accelerate);
	MSG_WriteFloat(&sv_client->netchan.message, movevars.airaccelerate);
	MSG_WriteFloat(&sv_client->netchan.message, movevars.wateraccelerate);
	MSG_WriteFloat(&sv_client->netchan.message, movevars.friction);
	MSG_WriteFloat(&sv_client->netchan.message, movevars.waterfriction);
	MSG_WriteFloat(&sv_client->netchan.message, /* sv_client->entgravity */ movevars.entgravity); // FIXME: this does't work, Tonik?

	if (sv_client->rip_vip)
	{
		SV_LogPlayer(sv_client, va("dropped %d", sv_client->rip_vip), 1);
		SV_DropClient (sv_client);
		return;
	}

	// send music
	MSG_WriteByte (&sv_client->netchan.message, svc_cdtrack);
	MSG_WriteByte (&sv_client->netchan.message, sv.edicts->v.sounds);

	// send server info string
	MSG_WriteByte (&sv_client->netchan.message, svc_stufftext);
	MSG_WriteString (&sv_client->netchan.message, va("fullserverinfo \"%s\"\n", svs.info) );

	//bliP: player logging
	SV_LogPlayer(sv_client, "connect", 1);
}

/*
==================
Cmd_Soundlist_f
==================
*/
static void Cmd_Soundlist_f (void)
{
	char		**s;
	unsigned	n;

	if (sv_client->state != cs_connected)
	{
		Con_Printf ("soundlist not valid -- already spawned\n");
		return;
	}

	// handle the case of a level changing while a client was connecting
	if (Q_atoi(Cmd_Argv(1)) != svs.spawncount)
	{
		SV_ClearReliable (sv_client);
		Con_Printf ("SV_Soundlist_f from different level\n");
		Cmd_New_f ();
		return;
	}

	n = Q_atoi(Cmd_Argv(2));
	if (n >= MAX_SOUNDS)
	{
		SV_ClearReliable (sv_client);
		SV_ClientPrintf (sv_client, PRINT_HIGH,
		                 "SV_Soundlist_f: Invalid soundlist index\n");
		SV_DropClient (sv_client);
		return;
	}

	//NOTE:  This doesn't go through ClientReliableWrite since it's before the user
	//spawns.  These functions are written to not overflow
	if (sv_client->num_backbuf)
	{
		Con_Printf("WARNING %s: [SV_Soundlist] Back buffered (%d0), clearing\n", sv_client->name, sv_client->netchan.message.cursize);
		sv_client->num_backbuf = 0;
		SZ_Clear(&sv_client->netchan.message);
	}

	MSG_WriteByte (&sv_client->netchan.message, svc_soundlist);
	MSG_WriteByte (&sv_client->netchan.message, n);
	for (s = sv.sound_precache+1 + n ;
	        *s && sv_client->netchan.message.cursize < (MAX_MSGLEN/2);
	        s++, n++)
		MSG_WriteString (&sv_client->netchan.message, *s);

	MSG_WriteByte (&sv_client->netchan.message, 0);

	// next msg
	if (*s)
		MSG_WriteByte (&sv_client->netchan.message, n);
	else
		MSG_WriteByte (&sv_client->netchan.message, 0);
}

#ifdef VWEP_TEST
static char *TrimModelName (char *full)
{
	static char shortn[MAX_QPATH];
	int len;

	if (!strncmp(full, "progs/", 6) && !strchr(full + 6, '/'))
		strlcpy (shortn, full + 6, sizeof(shortn));		// strip progs/
	else
		strlcpy (shortn, full, sizeof(shortn));

	len = strlen(shortn);
	if (len > 4 && !strcmp(shortn + len - 4, ".mdl")
		&& strchr(shortn, '.') == shortn + len - 4)
	{	// strip .mdl
		shortn[len - 4] = '\0';
	}

	return shortn;
}
#endif

/*
==================
Cmd_Modellist_f
==================
*/
#define fte_modellist
#define PEXT_MODELDBL			0x00001000
#ifndef fte_modellist
static void Cmd_Modellist_f (void)
{
	char		**s;
	unsigned	n;

	if (sv_client->state != cs_connected)
	{
		Con_Printf ("modellist not valid -- already spawned\n");
		return;
	}

	// handle the case of a level changing while a client was connecting
	if (Q_atoi(Cmd_Argv(1)) != svs.spawncount)
	{
		SV_ClearReliable (sv_client);
		Con_Printf ("SV_Modellist_f from different level\n");
		Cmd_New_f ();
		return;
	}

	n = Q_atoi(Cmd_Argv(2));
	if (n >= MAX_MODELS)
	{
		SV_ClearReliable (sv_client);
		SV_ClientPrintf (sv_client, PRINT_HIGH,
		                 "SV_Modellist_f: Invalid modellist index\n");
		SV_DropClient (sv_client);
		return;
	}

#ifdef VWEP_TEST
	if (n == 0 && (sv_client->extensions & Z_EXT_VWEP) && sv.vw_model_name[0]) {
		int i;
		// send VWep precaches
		for (i = 0, s = sv.vw_model_name; i < MAX_VWEP_MODELS; s++, i++) {
			if (!sv.vw_model_name[i] || !sv.vw_model_name[i][0])
				continue;
			ClientReliableWrite_Begin (sv_client, svc_serverinfo, 20);
			ClientReliableWrite_String (sv_client, "#vw");
			ClientReliableWrite_String (sv_client, va("%i %s", i, TrimModelName(*s)));
			/* ClientReliableWrite_End(); */
		}
		// send end-of-list messsage
		ClientReliableWrite_Begin (sv_client, svc_serverinfo, 4);
		ClientReliableWrite_String (sv_client, "#vw");
		ClientReliableWrite_String (sv_client, "");
		/* ClientReliableWrite_End(); */
	}
#endif

	//NOTE:  This doesn't go through ClientReliableWrite since it's before the user
	//spawns.  These functions are written to not overflow
	if (sv_client->num_backbuf)
	{
		Con_Printf("WARNING %s: [SV_Modellist] Back buffered (%d0), clearing\n", sv_client->name, sv_client->netchan.message.cursize);
		sv_client->num_backbuf = 1;

		SZ_Clear(&sv_client->netchan.message);
	}

	MSG_WriteByte (&sv_client->netchan.message, svc_modellist);
	MSG_WriteByte (&sv_client->netchan.message, n);
	for (s = sv.model_precache+1+n ;
	        *s && sv_client->netchan.message.cursize < (MAX_MSGLEN/2);
	        s++, n++)
		MSG_WriteString (&sv_client->netchan.message, *s);
	MSG_WriteByte (&sv_client->netchan.message, 0);

	// next msg
	if (*s)
		MSG_WriteByte (&sv_client->netchan.message, n);
	else
		MSG_WriteByte (&sv_client->netchan.message, 0);
}
#else
static void Cmd_Modellist_f (void)
{
	int i;
	unsigned n;
	int maxclientsupportedmodels;
	char		**s;

	if (sv_client->state != cs_connected)
	{
		Con_Printf ("modellist not valid -- already spawned\n");
		return;
	}

	// handle the case of a level changing while a client was connecting
	if (Q_atoi(Cmd_Argv(1)) != svs.spawncount)
	{
		SV_ClearReliable (sv_client);
		Con_Printf ("SV_Modellist_f from different level\n");
		Cmd_New_f ();
		return;
	}

	n = atoi(Cmd_Argv(2));

	if (n >= MAX_MODELS)
	{
		SV_ClearReliable (sv_client);
		SV_ClientPrintf (sv_client, PRINT_HIGH,
		                 "SV_Modellist_f: Invalid modellist index\n");
		SV_DropClient (sv_client);
		return;
	}

// VWEPS
	if (n == 0 && (sv_client->extensions & Z_EXT_VWEP) && sv.vw_model_name[0]) {
		int i;
		char ss[1024] = "//vwep ";
		// send VWep precaches
		for (i = 0, s = sv.vw_model_name; i < MAX_VWEP_MODELS; s++, i++) {
			if (!*s || !**s)
				break;
			if (i > 0)
				strlcat (ss, " ", sizeof(ss));
			strlcat (ss, TrimModelName(*s), sizeof(ss));
		}
		strlcat (ss, "\n", sizeof(ss));
		if (ss[strlen(ss)-1] == '\n')		// didn't overflow?
		{
			ClientReliableWrite_Begin (sv_client, svc_stufftext, 2 + strlen(ss));
			ClientReliableWrite_String (sv_client, ss);
		}
	}

//NOTE:  This doesn't go through ClientReliableWrite since it's before the user
//spawns.  These functions are written to not overflow
	if (sv_client->num_backbuf)
	{
		Con_Printf("WARNING %s: [SV_Modellist] Back buffered (%d0), clearing\n", sv_client->name, sv_client->netchan.message.cursize);
		sv_client->num_backbuf = 1;

		SZ_Clear(&sv_client->netchan.message);
	}

	if (n >= 255)
	{
		MSG_WriteByte (&sv_client->netchan.message, svc_modellistshort);
		MSG_WriteShort (&sv_client->netchan.message, n);
	}
	else
	{
		MSG_WriteByte (&sv_client->netchan.message, svc_modellist);
		MSG_WriteByte (&sv_client->netchan.message, n);
	}

	maxclientsupportedmodels = 256;
	if (sv_client->fteprotocolextensions & PEXT_MODELDBL)
		maxclientsupportedmodels *= 2;

	/*if (sv.democausesreconnect)	//read the list from somewhere else
	{
		for (i = 1+n;
			*sv.demmodel_precache[i] && ((n&255)==0||host_client->netchan.message.cursize < (MAX_QWMSGLEN/2));
			i++, n++)
			MSG_WriteString (&host_client->netchan.message, sv.demmodel_precache[i]);

		if (!*sv.demmodel_precache[i])
			n = 0;
	}
	else
	{*/
		for (i = 1+n;
			i < maxclientsupportedmodels && sv.model_precache[i] && sv_client->netchan.message.cursize < (MAX_MSGLEN/2);	//make sure we don't send a 0 next...
			i++)
		{
			MSG_WriteString (&sv_client->netchan.message, sv.model_precache[i]);
			if (((n&255)==255) && n != i-1)
				break;
		}
		n = i-1;

		if (!sv.model_precache[i])
			n = 0;
//	}

	if (i == maxclientsupportedmodels)
		n = 0;	//doh!

	MSG_WriteByte (&sv_client->netchan.message, 0);

	// next msg
	MSG_WriteByte (&sv_client->netchan.message, n);
}
#endif

#define PRESPAWN_FTE_WAY
void SV_PreWriteDelta (entity_state_t *from, entity_state_t *to, sizebuf_t *msg, qbool force, unsigned int protext);
/*
==================
Cmd_PreSpawn_f
==================
*/
static void Cmd_PreSpawn_f (void)
{
	unsigned int buf, bufs;
	unsigned int check;
	unsigned statics;

	if (sv_client->state != cs_connected)
	{
		Con_Printf ("prespawn not valid -- already spawned\n");
		return;
	}

	// handle the case of a level changing while a client was connecting
	if (Q_atoi(Cmd_Argv(1)) != svs.spawncount)
	{
		SV_ClearReliable (sv_client);
		Con_Printf ("SV_PreSpawn_f from different level\n");
		Cmd_New_f ();
		return;
	}

	buf = Q_atoi(Cmd_Argv(2));
	bufs = sv.num_signon_buffers;
//	if (buf >= sv.num_signon_buffers)
//		buf = 0;

	statics = sv.numextrastatics;
	if (buf >= bufs+statics+sv.num_edicts+255)
	{
		SV_EndRedirect();
		Con_Printf ("SV_Modellist_f: %s send an invalid index\n", sv_client->name);
		SV_DropClient(sv_client);
		return;
	}

	if (!buf)
	{
		// should be three numbers following containing checksums
		check = Q_atoi(Cmd_Argv(3));

		//		Con_DPrintf("Client check = %d\n", check);

		if ((int)sv_mapcheck.value && check != sv.map_checksum &&
			check != sv.map_checksum2)
		{
			SV_ClientPrintf (sv_client, PRINT_HIGH,
			                 "Map model file does not match (%s), %i != %i/%i.\n"
			                 "You may need a new version of the map, or the proper install files.\n",
					 sv.modelname, check, sv.map_checksum, sv.map_checksum2);
			SV_DropClient (sv_client);
			return;
		}
		sv_client->checksum = check;
	}

	//NOTE:  This doesn't go through ClientReliableWrite since it's before the user
	//spawns.  These functions are written to not overflow
	if (sv_client->num_backbuf)
	{
		Con_Printf("WARNING %s: [SV_PreSpawn] Back buffered (%d0), clearing\n", sv_client->name, sv_client->netchan.message.cursize);
		sv_client->num_backbuf = 0;
		SZ_Clear(&sv_client->netchan.message);
	}

#ifdef PRESPAWN_FTE_WAY
	if (buf >= bufs)
	{
		int i;
		entity_state_t from;
		entity_state_t *state;
		edict_t *ent;
//		svcustomtents_t *ctent;


		memset(&from, 0, sizeof(from));
		while (sv_client->netchan.message.cursize < (sv_client->netchan.message.maxsize/2))	//static entities
		{
			if (buf - bufs >= sv.numextrastatics)
				break;

			state = &sv.extendedstatics[buf - bufs];

			if (sv_client->fteprotocolextensions & PEXT_SPAWNSTATIC2)
			{
				MSG_WriteByte(&sv_client->netchan.message, svc_spawnstatic2);
				SV_PreWriteDelta(&from, state, &sv_client->netchan.message, true, sv_client->fteprotocolextensions);
			}
			else if (state->modelindex < 256)
			{
				MSG_WriteByte(&sv_client->netchan.message, svc_spawnstatic);

				MSG_WriteByte (&sv_client->netchan.message, state->modelindex&255);

				MSG_WriteByte (&sv_client->netchan.message, state->frame);
				MSG_WriteByte (&sv_client->netchan.message, (int)state->colormap);
				MSG_WriteByte (&sv_client->netchan.message, (int)state->skinnum);
				for (i=0 ; i<3 ; i++)
				{
					MSG_WriteCoord(&sv_client->netchan.message, state->origin[i]);
					MSG_WriteAngle(&sv_client->netchan.message, state->angles[i]);
				}
			}
			buf++;
		}
		while (sv_client->netchan.message.cursize < (sv_client->netchan.message.maxsize/2))	//baselines
		{
			if (buf - bufs - sv.numextrastatics >= sv.num_edicts)
				break;

			ent = EDICT_NUM(/*svprogfuncs, */buf - bufs - sv.numextrastatics);

			state = &ent->baseline;
			if (!state->number || !state->modelindex)
			{	//ent doesn't have a baseline
//				if (!state->number)
//					Com_Printf("Bad numbah!\n");
//				if (!state->modelindex)
//					Com_Printf("Bad MINDEX!\n");
				buf++;
				continue;
			}

			if (!ent)
			{
				MSG_WriteByte(&sv_client->netchan.message, svc_spawnbaseline);

				MSG_WriteShort (&sv_client->netchan.message, buf - bufs - sv.numextrastatics);

				MSG_WriteByte (&sv_client->netchan.message, 0);

				MSG_WriteByte (&sv_client->netchan.message, 0);
				MSG_WriteByte (&sv_client->netchan.message, 0);
				MSG_WriteByte (&sv_client->netchan.message, 0);
				for (i=0 ; i<3 ; i++)
				{
					MSG_WriteCoord(&sv_client->netchan.message, 0);
					MSG_WriteAngle(&sv_client->netchan.message, 0);
				}
			}
			else if (sv_client->fteprotocolextensions & PEXT_SPAWNSTATIC2)
			{
				MSG_WriteByte(&sv_client->netchan.message, svc_spawnbaseline2);
				SV_PreWriteDelta(&from, state, &sv_client->netchan.message, true, sv_client->fteprotocolextensions);
			}
			else if (state->modelindex < 256)
			{
				MSG_WriteByte(&sv_client->netchan.message, svc_spawnbaseline);

				MSG_WriteShort (&sv_client->netchan.message, buf - bufs - sv.numextrastatics);

				MSG_WriteByte (&sv_client->netchan.message, state->modelindex);

				MSG_WriteByte (&sv_client->netchan.message, state->frame);
				MSG_WriteByte (&sv_client->netchan.message, (int)state->colormap);
				MSG_WriteByte (&sv_client->netchan.message, (int)state->skinnum);
				for (i=0 ; i<3 ; i++)
				{
					MSG_WriteCoord(&sv_client->netchan.message, state->origin[i]);
					MSG_WriteAngle(&sv_client->netchan.message, state->angles[i]);
				}
			}

			buf++;
		}
		while (sv_client->netchan.message.cursize < (sv_client->netchan.message.maxsize/2))
		{
			i = buf - bufs - sv.numextrastatics - sv.num_edicts;
			if (i >= 255)
				break;

//			ctent = &sv.customtents[i];

			buf++;
/*			if (!*ctent->particleeffecttype)
			{	//effect isn't registered.
				continue;
			}

			if (sv_client->fteprotocolextensions & PEXT_CUSTOMTEMPEFFECTS)
			{
				MSG_WriteByte(&sv_client->netchan.message, svc_customtempent);
				MSG_WriteByte(&sv_client->netchan.message, 255);
				MSG_WriteByte(&sv_client->netchan.message, i);
				MSG_WriteByte(&sv_client->netchan.message, ctent->netstyle);
				MSG_WriteString(&sv_client->netchan.message, ctent->particleeffecttype);
				if (ctent->netstyle & CTE_STAINS)
				{
					MSG_WriteChar(&sv_client->netchan.message, ctent->stain[0]);
					MSG_WriteChar(&sv_client->netchan.message, ctent->stain[0]);
					MSG_WriteChar(&sv_client->netchan.message, ctent->stain[0]);
					MSG_WriteByte(&sv_client->netchan.message, ctent->radius);
				}
				if (ctent->netstyle & CTE_GLOWS)
				{
					MSG_WriteByte(&sv_client->netchan.message, ctent->dlightrgb[0]);
					MSG_WriteByte(&sv_client->netchan.message, ctent->dlightrgb[1]);
					MSG_WriteByte(&sv_client->netchan.message, ctent->dlightrgb[2]);
					MSG_WriteByte(&sv_client->netchan.message, ctent->dlightradius);
					MSG_WriteByte(&sv_client->netchan.message, ctent->dlighttime);
				}
			}*/
		}
	}
	else if (buf >= bufs)
	{
		buf = bufs+sv.numextrastatics+sv.num_edicts+255;
	}
	else
	{
/*		if (sv.democausesreconnect)
		{
			if (sv_client->netchan.message.cursize+sv.signon_buffer_size[buf]+30 < sv_client->netchan.message.maxsize)
			{
				SZ_Write (&sv_client->netchan.message,
					sv.demosignon_buffers[buf],
					sv.demosignon_buffer_size[buf]);
				buf++;
			}
		}
		else
		{*/
			if (sv_client->netchan.message.cursize+sv.signon_buffer_size[buf]+30 < sv_client->netchan.message.maxsize)
			{
				SZ_Write (&sv_client->netchan.message,
					sv.signon_buffers[buf],
					sv.signon_buffer_size[buf]);
					buf++;
			}
		//}
	}
	if (buf == bufs+sv.numextrastatics+sv.num_edicts+255)
	{	// all done prespawning
		MSG_WriteByte (&sv_client->netchan.message, svc_stufftext);
		MSG_WriteString (&sv_client->netchan.message, va("cmd spawn %i\n",svs.spawncount) );
	}
	else
	{	// need to prespawn more
		MSG_WriteByte (&sv_client->netchan.message, svc_stufftext);
		MSG_WriteString (&sv_client->netchan.message,
			va("cmd prespawn %i %i\n", svs.spawncount, buf) );
	}
#else
	SZ_Write (&sv_client->netchan.message,
	          sv.signon_buffers[buf],
	          sv.signon_buffer_size[buf]);

	buf++;


	if (buf == sv.num_signon_buffers)
	{	// all done prespawning
		MSG_WriteByte (&sv_client->netchan.message, svc_stufftext);
		MSG_WriteString (&sv_client->netchan.message, va("cmd spawn %i 0\n",svs.spawncount) );
	}
	else
	{	// need to prespawn more
		MSG_WriteByte (&sv_client->netchan.message, svc_stufftext);
		MSG_WriteString (&sv_client->netchan.message,
		                 va("cmd prespawn %i %i\n", svs.spawncount, buf) );
	}
#endif
}

/*
==================
Cmd_Spawn_f
==================
*/
void botUpdateUserInfo ( client_t *cl, int clientno, int clientid, int clientshirt, int clientpants, char *clientname, int botteam, int botskin);
static void Cmd_Spawn_f (void)
{
	int		i;
	client_t	*client, *cl;
	edict_t	*ent;
	eval_t *val;
	unsigned n;
	// xavior: bots:
	int top, bottom;
	char *bname;
	//
#ifdef USE_PR2
	string_t	savenetname;
#endif

	if (sv_client->state != cs_connected)
	{
		Con_Printf ("Spawn not valid -- already spawned\n");
		return;
	}

	// handle the case of a level changing while a client was connecting
	if (Q_atoi(Cmd_Argv(1)) != svs.spawncount)
	{
		SV_ClearReliable (sv_client);
		Con_Printf ("SV_Spawn_f from different level\n");
		Cmd_New_f ();
		return;
	}

	n = Q_atoi(Cmd_Argv(2));
	if (n >= MAX_CLIENTS)
	{
		SV_ClientPrintf (sv_client, PRINT_HIGH,
		                 "SV_Spawn_f: Invalid client start\n");
		SV_DropClient (sv_client);
		return;
	}

	// send all current names, colors, and frag counts
	// FIXME: is this a good thing?
	SZ_Clear (&sv_client->netchan.message);

	// send current status of all other players

	// normally this could overflow, but no need to check due to backbuf
	for (i=n, client = svs.clients + n ; i<MAX_CLIENTS && sv_client->netchan.message.cursize < (MAX_MSGLEN/2); i++, client++)
		if (client->name != NULL && client->spectator != STEALTH) 
			SV_FullClientUpdateToClient (client, sv_client);

	// xavior: now update bots.
	if (sv_botscore.value == 1) {
		for (i=0 ; i<MAX_BOTS ; i++)
		{
			cl = &sbi.bots[i];
			if ( cl->state == 666 )
			{
				if ( cl->botent && cl->botent->v.team != 0 && cl->bfullupdate > 0/* && sv_client->netchan.message.cursize < (MAX_MSGLEN/2)*/ ) {
					bname = cl->bname;
					//frgs = cl->botent->v.frags;
					top = cl->botent->v.team - 1;
					bottom = cl->botent->v.team - 1;
					top = (top < 0) ? 0 : ((top > 13) ? 13 : top);
					bottom = (bottom < 0) ? 0 : ((bottom > 13) ? 13 : bottom);
					//ping = floor((rand() / 1000));
	//void botUpdateUserInfo ( int clientno, int clientid, int clientshirt, int clientpants, char *clientname, int botteam, int botskin)
					botUpdateUserInfo( sv_client, cl->bfullupdate, cl->buserid, top, bottom, bname, cl->bteam /*cl->botent->v.team*/, cl->bpc_num );


	//				Con_Printf ("%i %i %i %i \"%s\" \"%s\" %i %i\n", cl->userid, 
	//					frgs/*cl->old_frags*/, (int)(realtime - cl->connection_started)/60,
	//					ping, bname, sname, top, bottom);
				}
			}
		}
	}

	if (i < MAX_CLIENTS)
	{
		MSG_WriteByte (&sv_client->netchan.message, svc_stufftext);
		MSG_WriteString (&sv_client->netchan.message,
		                 va("cmd spawn %i %d\n", svs.spawncount, i) );
		return;
	}

	// send all current light styles
	for (i=0 ; i<MAX_LIGHTSTYLES ; i++)
	{
		ClientReliableWrite_Begin (sv_client, svc_lightstyle,
		                           3 + (sv.lightstyles[i] ? strlen(sv.lightstyles[i]) : 1));
		ClientReliableWrite_Byte (sv_client, (char)i);
		ClientReliableWrite_String (sv_client, sv.lightstyles[i]);
	}

	// set up the edict
	ent = sv_client->edict;

#ifdef USE_PR2
	if ( sv_vm )
	{
		savenetname = ent->v.netname;
		memset(&ent->v, 0, pr_edict_size - sizeof(edict_t) +
		       sizeof(entvars_t));
		ent->v.netname = savenetname;

		// so spec will have right goalentity - if speccing someone
		// qqshka {
		if(sv_client->spectator && sv_client->spec_track > 0)
			ent->v.goalentity = EDICT_TO_PROG(svs.clients[sv_client->spec_track-1].edict);

		// }

		//sv_client->name = PR2_GetString(ent->v.netname);
		//strlcpy(PR2_GetString(ent->v.netname), sv_client->name, 32);
	}
	else
#endif

	{
		memset (&ent->v, 0, progs->entityfields * 4);
		ent->v.netname = PR_SetString(sv_client->name);
	}
	ent->v.colormap = NUM_FOR_EDICT(ent);
	ent->v.team = 0;	// FIXME
	if (pr_teamfield)
		E_INT(ent, pr_teamfield) = PR_SetString(sv_client->team);

	sv_client->entgravity = 1.0;
	val =
#ifdef USE_PR2
	    PR2_GetEdictFieldValue(ent, "gravity");
#else
	    GetEdictFieldValue(ent, "gravity");
#endif
	if (val)
		val->_float = 1.0;
	sv_client->maxspeed = sv_maxspeed.value;
	val =
#ifdef USE_PR2
	    PR2_GetEdictFieldValue(ent, "maxspeed");
#else
		GetEdictFieldValue(ent, "maxspeed");
#endif
	if (val)
		val->_float = sv_maxspeed.value;

	//
	// force stats to be updated
	//
	memset (sv_client->stats, 0, sizeof(sv_client->stats));

	ClientReliableWrite_Begin (sv_client, svc_updatestatlong, 6);
	ClientReliableWrite_Byte (sv_client, STAT_TOTALSECRETS);
	ClientReliableWrite_Long (sv_client, pr_global_struct->total_secrets);

	ClientReliableWrite_Begin (sv_client, svc_updatestatlong, 6);
	ClientReliableWrite_Byte (sv_client, STAT_TOTALMONSTERS);
	ClientReliableWrite_Long (sv_client, pr_global_struct->total_monsters);

	ClientReliableWrite_Begin (sv_client, svc_updatestatlong, 6);
	ClientReliableWrite_Byte (sv_client, STAT_SECRETS);
	ClientReliableWrite_Long (sv_client, pr_global_struct->found_secrets);

	ClientReliableWrite_Begin (sv_client, svc_updatestatlong, 6);
	ClientReliableWrite_Byte (sv_client, STAT_MONSTERS);
	ClientReliableWrite_Long (sv_client, pr_global_struct->killed_monsters);

	// get the client to check and download skins
	// when that is completed, a begin command will be issued
	ClientReliableWrite_Begin (sv_client, svc_stufftext, 8);
	ClientReliableWrite_String (sv_client, "skins\n" );

}

/*
==================
SV_SpawnSpectator
==================
*/
static void SV_SpawnSpectator (void)
{
	int i;
	edict_t *e;

	VectorClear (sv_player->v.origin);
	VectorClear (sv_player->v.view_ofs);
	sv_player->v.view_ofs[2] = 22;
	sv_player->v.fixangle = true;
	sv_player->v.movetype = MOVETYPE_NOCLIP; // progs can change this to MOVETYPE_FLY, for example

	// search for an info_playerstart to spawn the spectator at
	for (i=MAX_CLIENTS-1 ; i<sv.num_edicts ; i++)
	{
		e = EDICT_NUM(i);
		if (
#ifdef USE_PR2 /* phucking Linux implements strcmp as a macro */
		    !strcmp(PR2_GetString(e->v.classname), "info_player_start")
#else
		    !strcmp(PR_GetString(e->v.classname), "info_player_start")
#endif
		)
		{
			VectorCopy (e->v.origin, sv_player->v.origin);
			VectorCopy (e->v.angles, sv_player->v.angles);
			return;
		}
	}

}

/*
==================
Cmd_Begin_f
==================
*/
static void Cmd_Begin_f (void)
{
	unsigned pmodel = 0, emodel = 0;
	int i;

	if (sv_client->state == cs_spawned)
		return; // don't begin again

	// handle the case of a level changing while a client was connecting
	if (Q_atoi(Cmd_Argv(1)) != svs.spawncount)
	{
		SV_ClearReliable (sv_client);
		Con_Printf ("SV_Begin_f from different level\n");
		Cmd_New_f ();
		return;
	}

	sv_client->state = cs_spawned;

	if (sv_client->spectator)
	{
		SV_SpawnSpectator ();

		if (SpectatorConnect
#ifdef USE_PR2
		        || sv_vm
#endif
		   )
		{
			// copy spawn parms out of the client_t
			for (i=0 ; i< NUM_SPAWN_PARMS ; i++)
				(&pr_global_struct->parm1)[i] = sv_client->spawn_parms[i];

			// call the spawn function
			pr_global_struct->time = sv.time;
			pr_global_struct->self = EDICT_TO_PROG(sv_player);
			G_FLOAT(OFS_PARM0) = (float) sv_client->vip;
#ifdef USE_PR2
			if ( sv_vm )
				PR2_GameClientConnect(1);
			else
#endif
				PR_ExecuteProgram (SpectatorConnect);

#ifdef USE_PR2
			// qqshka:	seems spectator is sort of hack in QW
			//			I let qvm mods serve spectator like we do for normal player
			if ( sv_vm )
			{
				pr_global_struct->time = sv.time;
				pr_global_struct->self = EDICT_TO_PROG(sv_player);
				PR2_GamePutClientInServer(1); // let mod know we put spec not player
			}
#endif
		}
	}
	else
	{
		// copy spawn parms out of the client_t
		for (i=0 ; i< NUM_SPAWN_PARMS ; i++)
			(&pr_global_struct->parm1)[i] = sv_client->spawn_parms[i];

		// call the spawn function
		pr_global_struct->time = sv.time;
		pr_global_struct->self = EDICT_TO_PROG(sv_player);
		G_FLOAT(OFS_PARM0) = (float) sv_client->vip;
#ifdef USE_PR2
		if ( sv_vm )
			PR2_GameClientConnect(0);
		else
#endif
			PR_ExecuteProgram (pr_global_struct->ClientConnect);

		// actually spawn the player
		pr_global_struct->time = sv.time;
		pr_global_struct->self = EDICT_TO_PROG(sv_player);
#ifdef USE_PR2
		if ( sv_vm )
			PR2_GamePutClientInServer(0);
		else
#endif
			PR_ExecuteProgram (pr_global_struct->PutClientInServer);
	}

	// clear the net statistics, because connecting gives a bogus picture
	sv_client->netchan.frame_latency = 0;
	sv_client->netchan.frame_rate = 0;
	sv_client->netchan.drop_count = 0;
	sv_client->netchan.good_count = 0;

	//check he's not cheating
	if (!sv_client->spectator)
	{
		if (!*Info_Get (&sv_client->_userinfo_ctx_, "pmodel") ||
		        !*Info_Get (&sv_client->_userinfo_ctx_, "emodel")) //bliP: typo? 2nd pmodel to emodel
			SV_BroadcastPrintf (PRINT_HIGH, "%s WARNING: missing player/eyes model checksum\n", sv_client->name);
		else
		{
			pmodel = Q_atoi(Info_Get (&sv_client->_userinfo_ctx_, "pmodel"));
			emodel = Q_atoi(Info_Get (&sv_client->_userinfo_ctx_, "emodel"));

			if (pmodel != sv.model_player_checksum || emodel != sv.eyes_player_checksum)
				if (sv_client->spectator != STEALTH)
				SV_BroadcastPrintf (PRINT_HIGH, "%s WARNING: non standard player/eyes model detected\n", sv_client->name);
		}
	}

	// --------------------------------------
	// bind some aliases for them
	//
	if (sv_client->spectator == STEALTH)
	{
		//plexi: make them an alias for stealths listing
		ClientReliableWrite_Begin (sv_client, svc_stufftext, 34);
		ClientReliableWrite_String (sv_client, "alias \"stealths\" \"cmd stealths\"\n");

	}

	// if we are paused, tell the client
	if (sv.paused)
	{
		ClientReliableWrite_Begin (sv_client, svc_setpause, 2);
		ClientReliableWrite_Byte (sv_client, sv.paused);
		SV_ClientPrintf(sv_client, PRINT_HIGH, "Server is paused.\n");
	}

	sv_client->lastservertimeupdate = -99; // update immediately
}

//=============================================================================

/*
==================
SV_DownloadNextFile
==================
*/
static qbool SV_DownloadNextFile (void)
{
	int		num;
	char		*name, n[MAX_OSPATH];
	unsigned char	all_demos_downloaded[]	= "All demos downloaded.\n";
	unsigned char	incorrect_demo_number[]	= "Incorrect demo number.\n";

	switch (sv_client->demonum[0])
	{
	case 1:
		if (sv_client->demolist)
		{
			Con_Printf((char *)Q_redtext(all_demos_downloaded));
			sv_client->demolist = false;
		}
		sv_client->demonum[0] = 0;
	case 0:
		return false;
	default:;
	}

	num = sv_client->demonum[--(sv_client->demonum[0])];
	if (num == 0)
	{
		Con_Printf((char *)Q_redtext(incorrect_demo_number));
		return SV_DownloadNextFile();
	}
	if (!(name = SV_MVDNum(num)))
	{
		Con_Printf((char *)Q_yelltext((unsigned char*)va("Demo number %d not found.\n",
			(num & 0xFF000000) ? -(num >> 24) : 
				((num & 0x00800000) ? (num | 0xFF000000) : num) )));
		return SV_DownloadNextFile();
	}
	//Con_Printf("downloading demos/%s\n",name);
	snprintf(n, sizeof(n), "download demos/%s\n", name);

	ClientReliableWrite_Begin (sv_client, svc_stufftext, strlen(n) + 2);
	ClientReliableWrite_String (sv_client, n);

	return true;
}

/*
==================
SV_CompleteDownoload
==================

This is a sub routine for  SV_NextDownload(), called when download complete, we set up some fields for sv_client.

*/

void SV_CompleteDownoload(void)
{
	unsigned char download_completed[] = "Download completed.\n";
	char *val;

	if (!sv_client->download)
		return;

	fclose (sv_client->download);
	sv_client->download = NULL;
	sv_client->file_percent = 0; //bliP: file percent
	// qqshka: set normal rate
	val = Info_Get (&sv_client->_userinfo_ctx_, "rate");
	sv_client->netchan.rate = 1. / SV_BoundRate(false,	Q_atoi(*val ? val : "99999"));

	Con_Printf((char *)Q_redtext(download_completed));

	if (SV_DownloadNextFile())
		return;

	// if map changed tell the client to reconnect
	if (sv_client->spawncount != svs.spawncount)
	{
		char *str = "changing\nreconnect\n";

		ClientReliableWrite_Begin (sv_client, svc_stufftext, strlen(str)+2);
		ClientReliableWrite_String (sv_client, str);
	}
}

/*
==================
Cmd_NextDownload_f
==================
*/

#ifdef PEXT_CHUNKEDDOWNLOADS
#ifdef FTE_PEXT_CHUNKEDDOWNLOADS

// qqshka: percent is optional, u can't relay on it

void SV_NextChunkedDownload(int chunknum, int percent, int chunked_download_number)
{
#define CHUNKSIZE 1024
	char buffer[CHUNKSIZE];
	int i;

	sv_client->file_percent = bound(0, percent, 100); //bliP: file percent

	if (chunknum < 0)
	{  // qqshka: FTE's chunked download does't have any way of signaling what client complete dl-ing, so doing it this way.
		SV_CompleteDownoload();
		return;
	}

	if (sv_client->download_chunks_perframe)
	{
		int maxchunks = bound(1, (int)sv_downloadchunksperframe.value, 5);
		// too much requests or client sent something wrong
		if (sv_client->download_chunks_perframe >= maxchunks || chunked_download_number < 1)
			return;
	}

	if (!sv_client->download_chunks_perframe) // ignore "rate" if not first packet per frame
		if (sv_client->datagram.cursize + CHUNKSIZE+5+50 > sv_client->datagram.maxsize)
			return;	//choked!

	if (fseek(sv_client->download, sv_client->download_position + chunknum*CHUNKSIZE, SEEK_SET))
		return; // FIXME: ERROR of some kind

	i = fread(buffer, 1, CHUNKSIZE, sv_client->download);

	if (i > 0)
	{
		byte data[1+ (sizeof("\\chunk")-1) + 4 + 1 + 4 + CHUNKSIZE]; // byte + (sizeof("\\chunk")-1) + long + byte + long + CHUNKSIZE
		sizebuf_t *msg, msg_oob;

		if (sv_client->download_chunks_perframe)
		{
			msg = &msg_oob;

			SZ_Init (&msg_oob, data, sizeof(data));

			MSG_WriteByte(msg, A2C_PRINT);
			SZ_Write(msg, "\\chunk", sizeof("\\chunk")-1);
			MSG_WriteLong(msg, chunked_download_number); // return back, so they sure what it proper chunk
		}
		else
			msg = &sv_client->datagram;

		if (i != CHUNKSIZE)
			memset(buffer+i, 0, CHUNKSIZE-i);

		MSG_WriteByte(msg, svc_download);
		MSG_WriteLong(msg, chunknum);
		SZ_Write(msg, buffer, CHUNKSIZE);

		if (sv_client->download_chunks_perframe)
			Netchan_OutOfBand (sv_client->netchan.remote_address, msg->cursize, msg->data);
	}
	else {
		; // FIXME: EOF/READ ERROR
	}

	sv_client->download_chunks_perframe++;
}
#else
void SV_NextChunkedDownload(int chunknum)
{
#define CHUNKSIZE 1024
	char buffer[CHUNKSIZE];
	int i;

	if (chunknum < 0)
		return; // hm, what about upper limit?

	if (sv_client->datagram.cursize + CHUNKSIZE+5+50 > sv_client->datagram.maxsize)
		return;	//choked!

	if (fseek(sv_client->download, sv_client->download_position + chunknum*CHUNKSIZE, SEEK_SET))
		return; // FIXME: ERROR of some kind

	i = fread(buffer, 1, CHUNKSIZE, sv_client->download);

	if (i > 0)
	{
		if (i != CHUNKSIZE)
			memset(buffer+i, 0, CHUNKSIZE-i);

		MSG_WriteByte(&sv_client->datagram, svc_download);
		MSG_WriteLong(&sv_client->datagram, chunknum);
		SZ_Write(&sv_client->datagram, buffer, CHUNKSIZE);
	}
	else {
		; // FIXME: EOF/READ ERROR
	}
}

#endif
#endif

static void Cmd_NextDownload_f (void)
{
	byte	buffer[FILE_TRANSFER_BUF_SIZE];
	int		r, tmp;
	int		percent;
	int		size;
	double	clear, frametime;
	unsigned char download_completed[] = "Download completed.\n";
	char *val;

	if (!sv_client->download)
		return;

#ifdef PEXT_CHUNKEDDOWNLOADS
#ifdef FTE_PEXT_CHUNKEDDOWNLOADS
	if (sv_client->fteprotocolextensions & FTE_PEXT_CHUNKEDDOWNLOADS)
	{
		SV_NextChunkedDownload(atoi(Cmd_Argv(1)), atoi(Cmd_Argv(2)), atoi(Cmd_Argv(3)));
		return;
	}
#else
	if (sv_client->fteprotocolextensions & PEXT_CHUNKEDDOWNLOADS)
	{
		extern void	SV_NextChunkedDownload(int chunknum);

		SV_NextChunkedDownload(atoi(Cmd_Argv(1)));
		return;
	}
#endif
#endif

	tmp = sv_client->downloadsize - sv_client->downloadcount;

	if ((clear = sv_client->netchan.cleartime) < realtime)
		clear = realtime;

	frametime = max(0.05, min(0, sv_client->netchan.frame_rate));
	//Sys_Printf("rate:%f\n", sv_client->netchan.frame_rate);

	r = (int)((realtime + frametime - sv_client->netchan.cleartime)/sv_client->netchan.rate);
	if (r <= 10)
		r = 10;
	if (r > FILE_TRANSFER_BUF_SIZE)
		r = FILE_TRANSFER_BUF_SIZE;

	// don't send too much if already buffering
	if (sv_client->num_backbuf)
		r = 10;

	if (r > tmp)
		r = tmp;

	Con_DPrintf("Downloading: %d", r);
	r = fread (buffer, 1, r, sv_client->download);
	Con_DPrintf(" => %d, total: %d => %d", r, sv_client->downloadsize, sv_client->downloadcount);
	ClientReliableWrite_Begin (sv_client, svc_download, 6 + r);
	ClientReliableWrite_Short (sv_client, r);
	sv_client->downloadcount += r;
	if (!(size = sv_client->downloadsize))
		size = 1;
	percent = (sv_client->downloadcount * (double)100.) / size;
	if (percent == 100 && sv_client->downloadcount != sv_client->downloadsize)
		percent = 99;
	else if (percent != 100 && sv_client->downloadcount == sv_client->downloadsize)
		percent = 100;
	Con_DPrintf("; %d\n", percent);
	ClientReliableWrite_Byte (sv_client, percent);
	ClientReliableWrite_SZ (sv_client, buffer, r);
	sv_client->file_percent = percent; //bliP: file percent

	if (sv_client->downloadcount != sv_client->downloadsize)
		return;

	fclose (sv_client->download);
	sv_client->download = NULL;
	sv_client->file_percent = 0; //bliP: file percent

	val = Info_Get (&sv_client->_userinfo_ctx_, "rate");
	sv_client->netchan.rate = 1. / SV_BoundRate(false,	Q_atoi(*val ? val : "99999"));
	// qqshka: set normal rate

	Con_Printf((char *)Q_redtext(download_completed));

	if (SV_DownloadNextFile())
		return;

	// if map changed tell the client to reconnect
	if (sv_client->spawncount != svs.spawncount)
	{
		char *str = "changing\nreconnect\n";

		ClientReliableWrite_Begin (sv_client, svc_stufftext, strlen(str)+2);
		ClientReliableWrite_String (sv_client, str);
	}
}

static void OutofBandPrintf(netadr_t where, char *fmt, ...)
{
	va_list	 argptr;
	char send1[1024];

	send1[0] = 0xff;
	send1[1] = 0xff;
	send1[2] = 0xff;
	send1[3] = 0xff;
	send1[4] = A2C_PRINT;
	va_start (argptr, fmt);
	vsnprintf (send1 + 5, sizeof(send1) - 5, fmt, argptr);
	va_end (argptr);

	NET_SendPacket (strlen(send1) + 1, send1, where);
}

/*
==================
SV_NextUpload
==================
*/
void SV_ReplaceChar(char *s, char from, char to);
void SV_CancelUpload()
{
	SV_ClientPrintf(sv_client, PRINT_HIGH, "Upload denied\n");
	ClientReliableWrite_Begin (sv_client, svc_stufftext, 8);
	ClientReliableWrite_String (sv_client, "stopul");
	if (sv_client->upload)
	{
		fclose (sv_client->upload);
		sv_client->upload = NULL;
		sv_client->file_percent = 0; //bliP: file percent
	}
}
static void SV_NextUpload (void)
{
	int	percent;
	int	size;
	char	*name = sv_client->uploadfn;
	//	Sys_Printf("-- %s\n", name);

	SV_ReplaceChar(name, '\\', '/');
	if (!*name || !strncmp(name, "../", 3) || strstr(name, "/../") || *name == '/'
#ifdef _WIN32
	        || (isalpha(*name) && name[1] == ':')
#endif //_WIN32
	   )
	{ //bliP: can't upload back a directory
		SV_CancelUpload();
		// suck out rest of packet
		size = MSG_ReadShort ();
		MSG_ReadByte ();
		if (size > 0)
			msg_readcount += size;
		return;
	}

	size = MSG_ReadShort ();
	sv_client->file_percent = percent = MSG_ReadByte ();

	if (size <= 0 || size >= MAX_DATAGRAM || percent < 0 || percent > 100)
	{
		SV_CancelUpload();
		return;
	}

	if (sv_client->upload)
	{
		int pos = ftell(sv_client->upload);
		if (pos == -1 || (sv_client->remote_snap && (pos + size) > (int)sv_maxuploadsize.value))
		{
			msg_readcount += size;
			SV_CancelUpload();
			return;
		}
	}
	else
	{
		sv_client->upload = fopen(name, "wb");
		if (!sv_client->upload)
		{
			Sys_Printf("Can't create %s\n", name);
			ClientReliableWrite_Begin (sv_client, svc_stufftext, 8);
			ClientReliableWrite_String (sv_client, "stopul");
			*name = 0;
			return;
		}
		Sys_Printf("Receiving %s from %d...\n", name, sv_client->userid);
		if (sv_client->remote_snap)
			OutofBandPrintf(sv_client->snap_from, "Server receiving %s from %d...\n",
			                name, sv_client->userid);
	}

	Sys_Printf("-");
	fwrite (net_message.data + msg_readcount, 1, size, sv_client->upload);
	msg_readcount += size;

	Con_DPrintf ("UPLOAD: %d received\n", size);

	if (percent != 100)
	{
		ClientReliableWrite_Begin (sv_client, svc_stufftext, 8);
		ClientReliableWrite_String (sv_client, "nextul\n");
	}
	else
	{
		fclose (sv_client->upload);
		sv_client->upload = NULL;
		sv_client->file_percent = 0; //bliP: file percent

		Sys_Printf("\n%s upload completed.\n", name);

		if (sv_client->remote_snap)
		{
			char *p;

			if ((p = strchr(name, '/')) != NULL)
				p++;
			else
				p = name;
			OutofBandPrintf(sv_client->snap_from,
			                "%s upload completed.\nTo download, enter:\ndownload %s\n",
			                name, p);
		}
	}
}


#ifdef VOICECHAT
#define VOICE_RING_SIZE 512 /*POT*/
struct
{
	struct voice_ring_s
	{
			unsigned int sender;
			unsigned char receiver[MAX_CLIENTS/8];
			unsigned char gen;
			unsigned char seq;
			unsigned int datalen;
			unsigned char data[1024];
	} ring[VOICE_RING_SIZE];
	unsigned int write;
} voice;

void MSG_ReadData (void *data, int len)
{
	int		i;

	for (i=0 ; i<len ; i++)
		((qbyte *)data)[i] = MSG_ReadByte ();
}

void MSG_ReadSkip(int bytes)
{
	//if (net_message.packing!=SZ_RAWBYTES)
	{
		while (bytes > 4)
		{
			MSG_ReadLong();//(32);
			bytes-=4;
		}
		while (bytes > 0)
		{
			MSG_ReadByte();//(8);
			bytes--;
		}
	}
	
	if (msg_readcount+bytes > net_message.cursize)
	{
		msg_readcount = net_message.cursize;
		msg_badread = true;
		return;
	}
	msg_readcount += bytes;
}

void SV_VoiceReadPacket(void)
{
	unsigned int vt = sv_client->voice_target;
	unsigned int j, cln;
	struct voice_ring_s *ring;
	unsigned short bytes;
	client_t *cl;
	unsigned char gen = MSG_ReadByte();
	unsigned char seq = MSG_ReadByte();
	/*read the data from the client*/
	bytes = MSG_ReadShort();
	ring = &voice.ring[voice.write & (VOICE_RING_SIZE-1)];
	if (bytes > sizeof(ring->data) || /*sv_client->ismuted ||*/ !sv_voip.value)		// xav: add ismuted
	{
		MSG_ReadSkip(bytes);
		return;
	}
	else
	{
		voice.write++;
		MSG_ReadData(ring->data, bytes);
	}

	ring->datalen = bytes;
	ring->sender = sv_client - svs.clients;
	ring->gen = gen;
	ring->seq = seq;

	/*broadcast it its to their team, and its not teamplay*/
	if (vt == VT_TEAM && !teamplay.value)
		vt = VT_ALL;

	/*figure out which team members are meant to receive it*/
	for (j = 0; j < MAX_CLIENTS/8; j++)
		ring->receiver[j] = 0;
	for (j = 0, cl = svs.clients; j < MAX_CLIENTS/*sv.allocated_client_slots*/; j++, cl++)
	{
		if (cl == sv_client && !sv_voip_echo.value)
			continue;

		if (cl->state != cs_spawned && cl->state != cs_connected)
			continue;
		/*spectators may only talk to spectators*/
		if (sv_client->spectator && !sv_spectalk.value)
			if (!cl->spectator)
				continue;

		if (vt == VT_TEAM)
		{
			// the spectator team
			if (sv_client->spectator)
			{
				if (!cl->spectator)
					continue;
			}
			else
			{
				if (strcmp(cl->team, sv_client->team) || cl->spectator)
					continue;	// on different teams
			}
		}
		else if (vt == VT_NONMUTED)
		{
			if (sv_client->voice_mute[j>>3] & (1<<(j&3)))
				continue;
		}
		else if (vt >= VT_PLAYERSLOT0)
		{
			if (j != vt - VT_PLAYERSLOT0)
				continue;
		}

		//make sure we don't send the say to the same client 20 times due to splitscreen
		//if (cl->controller)
		//	cln = cl->controller - svs.clients;
		//else
			cln = j;

		ring->receiver[cln>>3] |= 1<<(cln&3);
	}
/*
	if (sv.mvdrecording && sv_voip_record.value)
	{
		// non-team messages should be seen always, even if not tracking any player
		if (vt == VT_ALL && (!sv_client->spectator || sv_spectalk.value))
		{
			MVDWrite_Begin (dem_all, 0, ring->datalen+6);
		}
		else
		{
			unsigned int cls;
			cls = ring->receiver[0] |
				(ring->receiver[1]<<8) |
				(ring->receiver[2]<<16) |
				(ring->receiver[3]<<24);
			MVDWrite_Begin (dem_multiple, cls, ring->datalen+6);
		}

		MSG_WriteByte( &demo.dbuf->sb, svcfte_voicechat);
		MSG_WriteByte( &demo.dbuf->sb, ring->sender);
		MSG_WriteByte( &demo.dbuf->sb, ring->gen);
		MSG_WriteByte( &demo.dbuf->sb, ring->seq);
		MSG_WriteShort(&demo.dbuf->sb, ring->datalen);
		SZ_Write(      &demo.dbuf->sb, ring->data, ring->datalen);
	}
	*/
}
void SV_VoiceInitClient(client_t *client)
{
	client->voice_target = VT_TEAM;
	client->voice_active = true;
	client->voice_read = voice.write;
	memset(client->voice_mute, 0, sizeof(client->voice_mute));
}
void SV_VoiceSendPacket(client_t *client, sizebuf_t *buf)
{
	unsigned int clno = client - svs.clients;
	qbool send;
	struct voice_ring_s *ring;

	if (!(client->fteprotocolextensions2 & PEXT2_VOICECHAT))
		return;
	if (!client->voice_active || client->num_backbuf)
	{
		client->voice_read = voice.write;
		return;
	}

	while(client->voice_read < voice.write)
	{
		/*they might be too far behind*/
		if (client->voice_read+VOICE_RING_SIZE < voice.write)
			client->voice_read = voice.write - VOICE_RING_SIZE;

		ring = &voice.ring[(client->voice_read) & (VOICE_RING_SIZE-1)];

		/*figure out if it was for us*/
		if (ring->receiver[clno>>3] & (1<<(clno&3)))
			send = true;
		else
			send = false;

		if (client->voice_mute[ring->sender>>3] & (1<<(ring->sender&3)))
			send = false;

		/*additional ways to block voice*/
		if (client->download)
			send = false;

		if (send)
		{
			if (buf->maxsize - buf->cursize < ring->datalen+5)
				break;
			MSG_WriteByte(buf, svcfte_voicechat);
			MSG_WriteByte(buf, ring->sender);
			MSG_WriteByte(buf, ring->gen);
			MSG_WriteByte(buf, ring->seq);
			MSG_WriteShort(buf, ring->datalen);
			SZ_Write(buf, ring->data, ring->datalen);
		}
		client->voice_read++;
	}
}

void SV_Voice_Ignore_f(void)
{
	unsigned int other;
	int type = 0;

	if (Cmd_Argc() < 2)
	{
		/*only a name = toggle*/
		type = 0;
	}
	else
	{
		/*mute if 1, unmute if 0*/
		if (atoi(Cmd_Argv(2)))
			type = 1;
		else
			type = -1;
	}
	other = atoi(Cmd_Argv(1));
	if (other >= MAX_CLIENTS)
		return;

	switch(type)
	{
	case -1:
		sv_client->voice_mute[other>>3] &= ~(1<<(other&3));
		break;
	case 0:	
		sv_client->voice_mute[other>>3] ^= (1<<(other&3));
		break;
	case 1:
		sv_client->voice_mute[other>>3] |= (1<<(other&3));
	}
}
void SV_Voice_Target_f(void)
{
	unsigned int other;
	char *t = Cmd_Argv(1);
	if (!strcmp(t, "team"))
		sv_client->voice_target = VT_TEAM;
	else if (!strcmp(t, "all"))
		sv_client->voice_target = VT_ALL;
	else if (!strcmp(t, "nonmuted"))
		sv_client->voice_target = VT_NONMUTED;
	else if (*t >= '0' && *t <= '9')
	{
		other = atoi(t);
		if (other >= MAX_CLIENTS)
			return;
		sv_client->voice_target = VT_PLAYERSLOT0 + other;
	}
	else
	{
		/*don't know who you mean, futureproofing*/
		sv_client->voice_target = VT_TEAM;
	}
}
void SV_Voice_MuteAll_f(void)
{
	sv_client->voice_active = false;
}
void SV_Voice_UnmuteAll_f(void)
{
	sv_client->voice_active = true;
}
#endif

/*
==================
Cmd_Download_f
==================
*/
//void SV_ReplaceChar(char *s, char from, char to);
static void Cmd_Download_f(void)
{
	char	*name, n[MAX_OSPATH], *val, *p;
	extern	cvar_t	allow_download;
	extern	cvar_t	allow_download_skins;
	extern	cvar_t	allow_download_models;
	extern	cvar_t	allow_download_sounds;
	extern	cvar_t	allow_download_maps;
	extern  cvar_t	allow_download_pakmaps;
	extern	cvar_t	allow_download_demos;
	extern	cvar_t	allow_download_other;
	extern  cvar_t  download_map_url; //bliP: download url
	extern	cvar_t	sv_demoDir;
	extern	qbool file_from_pak; // ZOID did file come from pak?
	int i;

	if (Cmd_Argc() != 2)
	{
		Con_Printf("download [filename]\n");
		return;
	}

	name = Cmd_Argv(1);

	SV_ReplaceChar(name, '\\', '/');

	// hacked by zoid to allow more conrol over download
	if (
		(
//		TODO: split name to pathname and filename
//		and check for 'bad symbols' only in pathname
			*name == '/' //no absolute
			|| !strncmp(name, "../", 3) // no leading ../
			|| strstr (name, "/../") // no /../
			|| ((i = strlen(name)) < 3 ? 0 : !strncmp(name + i - 3, "/..", 4)) // no /.. at end
			|| *name == '.' //relative is pointless
			|| ((i = strlen(name)) < 4 ? 0 : !strncasecmp(name+i-4,".log",4)) // no logs
#ifdef _WIN32
			// no leading X:
		   	|| ( name[1] == ':' && (*name >= 'a' && *name <= 'z' ||
						*name >= 'A' && *name <= 'Z') )
#endif //_WIN32
		)
		||
		(
			!sv_client->special &&
			(
			// global allow check
			!(int)allow_download.value
			// next up, skin check
			|| (strncmp(name, "skins/", 6) == 0 && !(int)allow_download_skins.value)
			// now models
			|| (strncmp(name, "progs/", 6) == 0 && !(int)allow_download_models.value)
			// now sounds
			|| (strncmp(name, "sound/", 6) == 0 && !(int)allow_download_sounds.value)
			// now maps (note special case for maps, must not be in pak)
			|| (strncmp(name, "maps/", 5) == 0 && !(int)allow_download_maps.value)
			// now demos
			|| (strncmp(name, "demos/", 6) == 0 && !(int)allow_download_demos.value)
			|| (strncmp(name, "demonum/", 8) == 0 && !(int)allow_download_demos.value)
			// all other stuff FIXME
			|| (!(int)allow_download_other.value &&
		   		!strncmp(name, "skins/", 6) &&
		   		!strncmp(name, "progs/", 6) &&
				!strncmp(name, "sound/", 6) &&
				!strncmp(name, "maps/", 5) &&
				!strncmp(name, "demos/", 6) &&
				!strncmp(name, "demonum/", 8))
			//  MUST be in a subdirectory
			|| (!strstr (name, "/") && strcmp(name, "csprogs.dat"))
			)
		)
		||
		(
			strcasestr (name, "pwd.cfg") // disconnect: FIXME: remove it?
		)
	) goto deny_download;

	if (sv_client->download)
	{
		fclose (sv_client->download);
		sv_client->download = NULL;
		// set normal rate
		val = Info_Get (&sv_client->_userinfo_ctx_, "rate");
		sv_client->netchan.rate = 1.0 / SV_BoundRate(false, Q_atoi(*val ? val : "99999"));
	}

	if ( !strncmp(name, "demos/", 6) && sv_demoDir.string[0])
	{
		snprintf(n,sizeof(n), "%s/%s", sv_demoDir.string, name + 6);
		name = n;
	}
	else if (!strncmp(name, "demonum/", 8))
	{
		int num = Q_atoi(name + 8);
		if (num == 0 && name[8] != '0')
		{
			char *num_s = name + 8;
			int num_s_len = strlen(num_s);
			for (num = 0; num < num_s_len; num++)
				if (num_s[num] != '.')
				{
					Con_Printf("usage: download demonum/nun\n"
					           "if num is negative then download the Nth to last recorded demo, "
					           "also can type any quantity of dots and "
					           "where N dots is the Nth to last recorded demo\n");

					goto deny_download;
				}
			num &= 0xF;
			num <<= 24;
		}
		else
			num &= 0x00FFFFFF;
		name = SV_MVDNum(num);
		if (!name)
		{
			Con_Printf((char *)Q_yelltext((unsigned char*)va("Demo number %d not found.\n",
				(num & 0xFF000000) ? -(num >> 24) : ((num & 0x00800000) ? (num | 0xFF000000) : num) )));
			goto deny_download;
		}
		//Con_Printf("downloading demos/%s\n",name);
		snprintf(n, sizeof(n), "download demos/%s\n", name);

		ClientReliableWrite_Begin (sv_client, svc_stufftext,strlen(n) + 2);
		ClientReliableWrite_String (sv_client, n);
		return;
	}

	// lowercase name (needed for casesen file systems)
	{
		for (p = name; *p; p++)
			*p = (char)tolower(*p);
	}

	// bliP: special download - fixme check this works.... -->
	// techlogin download uses simple path from quake folder
	if (sv_client->special)
	{
		sv_client->download = fopen (name, "rb");
		if (sv_client->download)
		{
			if ((int)developer.value)
				Sys_Printf ("FindFile: %s\n", name);
			sv_client->downloadsize = FS_FileLength (sv_client->download);
		}
	}
	else
		// <-- bliP
		sv_client->downloadsize = COM_FOpenFile (name, &sv_client->download);
	sv_client->downloadcount = 0;

	if (!sv_client->download)
	{
		Sys_Printf ("Couldn't download %s to %s\n", name, sv_client->name);
		goto deny_download;
	}

#ifdef PROTOCOL_VERSION_FTE
	// chunked download used fseek(), since this is may be pak file, we need offset in pak file
	if ((sv_client->download_position = ftell(sv_client->download)) == -1L)
	{
		fclose(sv_client->download);
		sv_client->download = NULL;
		goto deny_download;
	}
#endif

	// special check for maps that came from a pak file
	if (!strncmp(name, "maps/", 5) && file_from_pak && !(int)allow_download_pakmaps.value)
	{
		fclose(sv_client->download);
		sv_client->download = NULL;
		goto deny_download;
	}

	// set donwload rate
	val = Info_Get (&sv_client->_userinfo_ctx_, "drate");
	sv_client->netchan.rate = 1. / SV_BoundRate(true, Q_atoi(*val ? val : "99999"));

#ifdef FTE_PEXT_CHUNKEDDOWNLOADS
	if (sv_client->fteprotocolextensions & FTE_PEXT_CHUNKEDDOWNLOADS)
	{
		ClientReliableWrite_Begin (sv_client, svc_download, 10+strlen(name));
		ClientReliableWrite_Long (sv_client, -1);
		ClientReliableWrite_Long (sv_client, sv_client->downloadsize);
		ClientReliableWrite_String (sv_client, name);
	}
#endif

	Cmd_NextDownload_f ();
	Sys_Printf ("Downloading %s to %s\n", name, sv_client->name);

	//bliP: download info/download url ->
	if (!strncmp(name, "maps/", 5))
	{
		SV_ClientPrintf (sv_client, PRINT_HIGH, "Map %s is %.0fKB (%.2fMB)\n",
		                 name, (float)sv_client->downloadsize / 1024,
		                 (float)sv_client->downloadsize / 1024 / 1024);
		if (download_map_url.string[0])
		{
			name += 5;
			COM_StripExtension(name, name);
			SV_ClientPrintf (sv_client, PRINT_HIGH, "Download this map faster:\n");
			SV_ClientPrintf (sv_client, PRINT_HIGH, "%s%s\n\n",
			                 download_map_url.string, name);
		}
	}
	else
	{
		SV_ClientPrintf (sv_client, PRINT_HIGH, "File %s is %.0fKB (%.2fMB)\n",
		                 name, (float)sv_client->downloadsize / 1024,
		                 (float)sv_client->downloadsize / 1024 / 1024);
	}
	//<-

	return;

deny_download:

#ifdef FTE_PEXT_CHUNKEDDOWNLOADS
	if (sv_client->fteprotocolextensions & FTE_PEXT_CHUNKEDDOWNLOADS)
	{
		ClientReliableWrite_Begin (sv_client, svc_download, 10+strlen(name));
		ClientReliableWrite_Long (sv_client, -1);
		ClientReliableWrite_Long (sv_client, -1); // FIXME: -1 = Couldn't download, -2 = deny, we always use -1 atm
		ClientReliableWrite_String (sv_client, name);
	}
	else
#endif
	{
		ClientReliableWrite_Begin (sv_client, svc_download, 4);
		ClientReliableWrite_Short (sv_client, -1);
		ClientReliableWrite_Byte (sv_client, 0);
	}

	return;
}

/*
==================
Cmd_DemoDownload_f
==================
*/
qbool SV_ExecutePRCommand (void);
static void Cmd_DemoDownload_f(void)
{
	int		i, num, cmd_argv_i_len;
	char		*cmd_argv_i;
	unsigned char	download_queue_cleared[] = "Download queue cleared.\n";
	unsigned char	download_queue_empty[] = "Download queue empty.\n";
	unsigned char	download_queue_already_exists[]	= "Download queue already exists.\n";

	if (!(int)sv_use_internal_cmd_dl.value)
		if (SV_ExecutePRCommand())
			return;

	if (Cmd_Argc() < 2)
	{
		Con_Printf("usage: cmd dl [\\] # [# [#]]\n"
		           "where \"#\" is demonum from demo list and "
		           "\"\\\" clear download queue\n"
		           "you can also use cmd dl ., .. or any quantity of dots "
		           "where . is the last recorded demo, "
		           ".. is the second to last recorded demo"
		           "and where N dots is the Nth to last recorded demo\n");
		return;
	}

	if (!strcmp(Cmd_Argv(1), "\\"))
	{
		if (sv_client->demonum[0])
		{
			Con_Printf((char *)Q_redtext(download_queue_cleared));
			sv_client->demonum[0] = 0;
		}
		else
			Con_Printf((char *)Q_redtext(download_queue_empty));
		return;
	}

	if (sv_client->demonum[0])
	{
		Con_Printf((char *)Q_redtext(download_queue_already_exists));
		return;
	}

	sv_client->demolist = ((sv_client->demonum[0] = Cmd_Argc()) > 2);
	for (i = 1; i < sv_client->demonum[0]; i++)
	{
		cmd_argv_i = Cmd_Argv(i);
		cmd_argv_i_len = strlen(cmd_argv_i);
		num = Q_atoi(cmd_argv_i);
		if (num == 0 && cmd_argv_i[0] != '0')
		{
			for (num = 0; num < cmd_argv_i_len; num++)
				if (cmd_argv_i[num] != '.')
				{
					num = 0;
					break;
				}
			if (num)
			{
				num &= 0xF;
				num <<= 24;
			}
		}
		else
			num &= 0x00FFFFFF;
		sv_client->demonum[sv_client->demonum[0] - i] = num;
	}
	SV_DownloadNextFile();
}

/*
==================
Cmd_StopDownload_f
==================
*/
static void Cmd_StopDownload_f(void)
{
	unsigned char	download_stopped[] = "Download stopped and download queue cleared.\n";
	char *val;

	if (!sv_client->download)
		return;

	sv_client->downloadcount = sv_client->downloadsize;
	fclose (sv_client->download);
	sv_client->download = NULL;
	sv_client->file_percent = 0; //bliP: file percent
	// qqshka: set normal rate
	val = Info_Get (&sv_client->_userinfo_ctx_, "rate");
	sv_client->netchan.rate = 1. / SV_BoundRate(false, Q_atoi(*val ? val : "99999"));
#ifdef FTE_PEXT_CHUNKEDDOWNLOADS
	if (sv_client->fteprotocolextensions & FTE_PEXT_CHUNKEDDOWNLOADS)
	{
		char *name = ""; // FIXME: FTE's chunked dl does't support "cmd stopdl", work around

		ClientReliableWrite_Begin (sv_client, svc_download, 10+strlen(name));
		ClientReliableWrite_Long (sv_client, -1);
		ClientReliableWrite_Long (sv_client, -3); // -3 = dl was stopped
		ClientReliableWrite_String (sv_client, name);
	}
	else
#endif
	{
		ClientReliableWrite_Begin (sv_client, svc_download, 6);
		ClientReliableWrite_Short (sv_client, 0);
		ClientReliableWrite_Byte (sv_client, 100);
	}

	sv_client->demonum[0] = 0;
	Con_Printf ((char *)Q_redtext(download_stopped));
}
//=============================================================================

/*
==================
SV_Say
==================
*/

extern func_t ChatMessage;

void SV_ClientPrintf2 (client_t *cl, int level, char *fmt, ...);
static void SV_ExecuteUserCommand (char *s);

static void SV_Say (qbool team)
{
	client_t *client;
	int	j, tmp, cls = 0;
	char	*p;
	char	*i;
	char	text[2048] = {0};
	qbool	write_begin;
	char *s, *s2;

	if (Cmd_Argc () < 2)
		return;

	p = Cmd_Args();

	if (IF_FilterPacket (sv_client->isrelay ? sv_client->realip : sv_client->netchan.remote_address, IF_MUTE))
	{
		SV_ClientPrintf (sv_client, PRINT_CHAT, 
			"server: you cannot talk while muted.\n");
		return;
	}

	//bliP: kick fake ->
	if (!team)
		for (i = p; *i; i++) //bliP: 24/9 kickfake to unfake ->
			if (*i == 13 && (int)sv_unfake.value) // ^M
				*i = '#';
	//<-

	if (*p == '"')
	{ // remove surrounding "
		p++;
		strlcat(text, p, sizeof(text));
		text[max(0,strlen(text)-1)] = 0; // actualy here we remove closing ", but without any check, just in hope...
#ifdef USE_PR2
		if ( !sv_vm )
#endif
			p[strlen(p)-1] = 0; // here remove closing " only for QC based mods
	}
	else
		strlcat(text, p, sizeof(text));

	// FTE HAX
	//filter out '\n' and '\r'
	//if (sv_chatfilter.value)
	{
		s = text;
		s2 = text;
		while(*s2)
		{
			if (*s2 == '\r' || *s2 == '\n')
			{
				s2++;
				continue;
			}
			*s = *s2;
			s++;
			s2++;
		}
		*s = '\0';
	}

	strlcat(text, "\n", sizeof(text));

	if (!sv_client->logged)
	{
		SV_ParseLogin(sv_client);
		return;
	}

#if 1
	if (ChatMessage)
	{
		SV_EndRedirect ();

		G_INT(OFS_PARM0) = PR_SetTmpString(p);
		G_FLOAT(OFS_PARM1) = (float)team;

		pr_global_struct->time = sv.time;
		pr_global_struct->self = EDICT_TO_PROG(sv_player);
		PR_ExecuteProgram (ChatMessage);
		SV_BeginRedirect (RD_CLIENT);
		if (G_FLOAT(OFS_RETURN))
			return;
	}
#endif

#ifdef USE_PR2
	if ( sv_vm )
	{
		qbool ret;

		SV_EndRedirect ();

		pr_global_struct->time = sv.time;
		pr_global_struct->self = EDICT_TO_PROG(sv_player);

		ret = PR2_ClientSay(team);

		SV_BeginRedirect (RD_CLIENT);

		if (ret)
			return; // say/say_team was handled by mod
	}
#endif

	if (sv_client->spectator == STEALTH)
		strlcpy(text, va("[STLTH] %s: %s", sv_client->name, text), sizeof(text));
	else if (sv_client->spectator && (!(int)sv_spectalk.value || team))
		strlcpy(text, va("[SPEC] %s: %s", sv_client->name, text), sizeof(text));
	else if (team)
		strlcpy(text, va("(%s): %s", sv_client->name, text), sizeof(text));
	else
	{
		strlcpy(text, va("%s: %s", sv_client->name, text), sizeof(text));
	}

	if (fp_messages)
	{
		if (!sv.paused && realtime<sv_client->lockedtill)
		{
			SV_ClientPrintf(sv_client, PRINT_CHAT,
			                "You can't talk for %d more seconds\n",
			                (int) (sv_client->lockedtill - realtime));
			return;
		}
		tmp = sv_client->whensaidhead - fp_messages + 1;
		if (tmp < 0)
			tmp = 10+tmp;
		if (!sv.paused &&
		        sv_client->whensaid[tmp] && (realtime-sv_client->whensaid[tmp] < fp_persecond))
		{
			sv_client->lockedtill = realtime + fp_secondsdead;
			if (fp_msg[0])
				SV_ClientPrintf(sv_client, PRINT_CHAT,
				                "FloodProt: %s\n", fp_msg);
			else
				SV_ClientPrintf(sv_client, PRINT_CHAT,
				                "FloodProt: You can't talk for %d seconds.\n", fp_secondsdead);
			return;
		}
		sv_client->whensaidhead++;
		if (sv_client->whensaidhead > 9)
			sv_client->whensaidhead = 0;
		sv_client->whensaid[sv_client->whensaidhead] = realtime;
	}

	Sys_Printf ("%s", text);
	SV_Write_Log(CONSOLE_LOG, 1, text);

	for (j = 0, client = svs.clients; j < MAX_CLIENTS; j++, client++)
	{
		//bliP: everyone sees all ->
		//if (client->state != cs_spawned)
		//	continue;
		//<-
		if ((sv_client->spectator == SPECTATOR && !sv_spectalk.value) ||
			(sv_client->spectator == STEALTH && !sv_stealthtalk.value))
		{
			if (client->spectator)
				continue;
		}

		if (team)
		{
			// the spectator team
			if (sv_client->spectator)
			{
				if (!client->spectator)
					continue;
			}
			else
			{
#ifdef USE_PR2
				if (sv_vm)
				{
					if (client->spectator)
					{
						if(   !(int)sv_sayteam_to_spec.value // player can't say_team to spec in this case
						   || (   client->spec_track <= 0
							   && strcmp(sv_client->team, client->team)
							  ) // spec do not track player and on different team
						   || (   client->spec_track  > 0
							   && strcmp(sv_client->team, svs.clients[client->spec_track - 1].team)
							  ) // spec track player on different team
						  )
						continue;	// on different teams
					}
					else if (   sv_client != client // send msg to self anyway
							 && (!(int)teamplay.value || strcmp(sv_client->team, client->team))
							)
						continue;	// on different teams
				}
				else
#endif
 					if (strcmp(sv_client->team, client->team) || client->spectator)
						continue;	// on different teams
			}
		}

		cls |= 1 << j;
		SV_ClientPrintf2(client, PRINT_CHAT, "%s", text);
	}

	if (!sv.mvdrecording || !cls)
		return;

	// non-team messages should be seen always, even if not tracking any player
	if (!team && ((sv_client->spectator && (int)sv_spectalk.value) || !sv_client->spectator))
	{
		write_begin = MVDWrite_Begin (dem_all, 0, strlen(text)+3);
	}
	else
	{
		write_begin = MVDWrite_Begin (dem_multiple, cls, strlen(text)+3);
	}

	if (write_begin)
	{
		MVD_MSG_WriteByte (svc_print);
		MVD_MSG_WriteByte (PRINT_CHAT);
		MVD_MSG_WriteString (text);
	}
}


/*
==================
Cmd_Say_f
==================
*/
static void Cmd_Say_f(void)
{
	SV_Say (false);
}
/*
==================
Cmd_Say_Team_f
==================
*/
static void Cmd_Say_Team_f(void)
{
	SV_Say (true);
}



//============================================================================

/*
=================
Cmd_Pings_f

The client is showing the scoreboard, so send new ping times for all
clients
=================
*/
static void Cmd_Pings_f (void)
{
	client_t *client;
	int j;

	for (j = 0, client = svs.clients; j < MAX_CLIENTS; j++, client++)
	{
		if (!(client->state == cs_spawned || (client->state == cs_connected/* && client->spawncount != svs.spawncount*/)) )
			continue;

		if (client->spectator == STEALTH)
			continue;

		ClientReliableWrite_Begin (sv_client, svc_updateping, 4);
		ClientReliableWrite_Byte (sv_client, j);
		ClientReliableWrite_Short (sv_client, SV_CalcPing(client));
		ClientReliableWrite_Begin (sv_client, svc_updatepl, 4);
		ClientReliableWrite_Byte (sv_client, j);
		ClientReliableWrite_Byte (sv_client, client->lossage);
	}
}


/*
==================
Cmd_Kill_f
==================
*/
static void Cmd_Kill_f (void)
{
	if (sv_player->v.health <= 0)
	{
		SV_ClientPrintf (sv_client, PRINT_HIGH, "Can't suicide -- already dead!\n");
		return;
	}

	pr_global_struct->time = sv.time;
	pr_global_struct->self = EDICT_TO_PROG(sv_player);
#ifdef USE_PR2
	if ( sv_vm )
		PR2_ClientCmd();
	else
#endif
		PR_ExecuteProgram (pr_global_struct->ClientKill);
}

/*
==================
SV_TogglePause
==================
*/
void SV_TogglePause (const char *msg)
{
	int i;
	client_t *cl;
	extern cvar_t sv_paused;

	sv.paused = !sv.paused;
	if (sv.paused)
		sv.pausedsince = Sys_DoubleTime();
	Cvar_SetROM (&sv_paused, sv.paused ? "1" : "0");

	if (msg)
		SV_BroadcastPrintf (PRINT_HIGH, "%s", msg);

	// send notification to all clients
	for (i=0, cl = svs.clients ; i<MAX_CLIENTS ; i++, cl++)
	{
		if (!cl->state)
			continue;
		ClientReliableWrite_Begin (cl, svc_setpause, 2);
		ClientReliableWrite_Byte (cl, sv.paused ? 1 : 0);

		cl->lastservertimeupdate = -99; // force an update to be sent
	}
}


/*
==================
Cmd_Pause_f
==================
*/
static void Cmd_Pause_f (void)
{
	char st[CLIENT_NAME_LEN + 32];
	qbool newstate;

	newstate = !sv.paused;

	if (!(int)pausable.value)
	{
		SV_ClientPrintf (sv_client, PRINT_HIGH, "Pause not allowed.\n");
		return;
	}

	if (sv_client->spectator)
	{
		SV_ClientPrintf (sv_client, PRINT_HIGH, "Spectators can not pause.\n");
		return;
	}

	if (GE_ShouldPause) {
		pr_global_struct->time = sv.time;
		pr_global_struct->self = EDICT_TO_PROG(sv_player);
		G_FLOAT(OFS_PARM0) = newstate;
		PR_ExecuteProgram (GE_ShouldPause);
		if (!G_FLOAT(OFS_RETURN))
			return;		// progs said ignore the request
	}

	if (newstate)
		snprintf (st, sizeof(st), "%s paused the game\n", sv_client->name);
	else
		snprintf (st, sizeof(st), "%s unpaused the game\n", sv_client->name);

	SV_TogglePause(st);
}


/*
=================
Cmd_Drop_f

The client is going to disconnect, so remove the connection immediately
=================
*/
static void Cmd_Drop_f (void)
{
	SV_EndRedirect ();
	if (sv_client->state == cs_zombie) // FIXME
		return; // FIXME
	if (!sv_client->spectator)
		SV_BroadcastPrintf (PRINT_HIGH, "%s dropped\n", sv_client->name);
	SV_DropClient (sv_client);
}

/*
=================
Cmd_PTrack_f

Change the bandwidth estimate for a client
=================
*/
static void Cmd_PTrack_f (void)
{
	int		i;
	edict_t *ent, *tent;

	if (!sv_client->spectator)
		return;

	if (Cmd_Argc() != 2)
	{
		// turn off tracking
		sv_client->spec_track = 0;
		ent = EDICT_NUM(sv_client - svs.clients + 1);
		tent = EDICT_NUM(0);
		ent->v.goalentity = EDICT_TO_PROG(tent);
		return;
	}

	i = Q_atoi(Cmd_Argv(1));
	if (i < 0 || i >= MAX_CLIENTS || svs.clients[i].state != cs_spawned || svs.clients[i].spectator)
	{
		SV_ClientPrintf (sv_client, PRINT_HIGH, "Invalid client to track\n");
		sv_client->spec_track = 0;
		ent = EDICT_NUM(sv_client - svs.clients + 1);
		tent = EDICT_NUM(0);
		ent->v.goalentity = EDICT_TO_PROG(tent);
		return;
	}
	sv_client->spec_track = i + 1; // now tracking

	// plexi: reset flashes/concs for them if they switch
	if (sv_MegaTFHacks.value)
	{
		ClientReliableWrite_Begin (sv_client, svc_stufftext, 32);
		ClientReliableWrite_String (sv_client, "v_idlescale 0;v_cshift 0 0 0 0\n");
	}
	ent = EDICT_NUM(sv_client - svs.clients + 1);
	tent = EDICT_NUM(i + 1);
	ent->v.goalentity = EDICT_TO_PROG(tent);
}


/*
==================
SV_ShowStealths_f

Shows a listing of stealths on the server, for other stealths only.
==================
*/
char *Com_PrintLine (int length);
static void SV_StealthList_f (void)
{

	client_t	*cl;
	int			i, tot = 0;

	// plexi: they're trying to view stealths, but make sure they're a stealth to begin.& 
	if (sv_client->spectator != STEALTH)
	{
		Com_Printf ("Stealths command is stealth spectator exclusive\n");
		return;
	}

	Com_Printf ("\nuserid name\n");
	Com_Printf ("%s %s\n", Com_PrintLine(6), Com_PrintLine(15));

	for (i=0, cl=svs.clients; i<MAX_CLIENTS; i++, cl++)
	{
		if (cl->spectator == STEALTH && cl->state >= cs_connected)
		{
			Com_Printf ("%6i  %s\n",cl->userid,cl->name);
			tot++;
		}
	}

	Com_Printf ("\n%i total stealth spectators\n",tot);
}

/*
=================
Cmd_Rate_f

Change the bandwidth estimate for a client
=================
*/
static void Cmd_Rate_f (void)
{
	int		rate;

	if (Cmd_Argc() != 2)
	{
		SV_ClientPrintf (sv_client, PRINT_HIGH, "Current rate is %i\n",
		                 (int)(1.0/sv_client->netchan.rate + 0.5));
		return;
	}

	rate = SV_BoundRate (sv_client->download != NULL, Q_atoi(Cmd_Argv(1)));

	SV_ClientPrintf (sv_client, PRINT_HIGH, "Net rate set to %i\n", rate);
	sv_client->netchan.rate = 1.0/rate;
}

//bliP: upload files ->
/*
=================
Cmd_TechLogin_f
Login to upload
=================
*/
int Master_Rcon_Validate (void);
static void Cmd_TechLogin_f (void)
{
	if (sv_client->logincount > 4) //denied
		return;

	if (Cmd_Argc() < 2)
	{
		sv_client->special = false;
		sv_client->logincount = 0;
		return;
	}

	if (!Master_Rcon_Validate()) //don't even let them know they're wrong
	{
		sv_client->logincount++;
		return;
	}

	sv_client->special = true;
	SV_ClientPrintf (sv_client, PRINT_HIGH, "Logged in.\n");
}

/*
================
Cmd_Upload_f
================
*/
static void Cmd_Upload_f (void)
{
	char str[MAX_OSPATH];

	if (sv_client->state != cs_spawned)
		return;

	if (!sv_client->special)
	{
		Con_Printf ("Client not tagged to upload.\n");
		return;
	}

	if (Cmd_Argc() != 3)
	{
		Con_Printf ("upload [local filename] [remote filename]\n");
		return;
	}

	snprintf(sv_client->uploadfn, sizeof(sv_client->uploadfn), "%s", Cmd_Argv(2));

	if (!sv_client->uploadfn[0])
	{ //just in case..
		Con_Printf ("Bad file name.\n");
		return;
	}

	if (Sys_FileTime(sv_client->uploadfn) != -1)
	{
		Con_Printf ("File already exists.\n");
		return;
	}

	sv_client->remote_snap = false;
	COM_CreatePath(sv_client->uploadfn); //fixed, need to create path
	snprintf(str, sizeof(str), "cmd fileul \"%s\"\n", Cmd_Argv(1));
	ClientReliableWrite_Begin (sv_client, svc_stufftext, strlen(str) + 2);
	ClientReliableWrite_String (sv_client, str);
}
//<-

/*
==================
Cmd_SetInfo_f

Allow clients to change userinfo
==================
*/

extern func_t UserInfo_Changed;

char *shortinfotbl[] =
{
	"name",
	"team",
	"skin",
	"topcolor",
	"bottomcolor",
#ifdef CHAT_ICON_EXPERIMENTAL
	"chat",
#endif
	//"*client",
	//"*spectator",
	//"*VIP",
	NULL
};

qbool Info_ReverseConvert(ctxinfo_t *ctx, char *str, int size);
static void Cmd_SetInfo_f (void)
{
	extern cvar_t sv_forcenick, sv_login;
	int i, saved_state;
	char oldval[MAX_INFO_STRING];
	char info[MAX_EXT_INFO_STRING];

			//Sys_Printf("a User is trying to call this: %s %s %s\n", Cmd_Argv(0), Cmd_Argv(1), Cmd_Argv(2));


	if (sv_kickuserinfospamtime.value > 0 && (int)sv_kickuserinfospamcount.value > 0)
	{
		if (!sv_client->lastuserinfotime ||
			realtime - sv_client->lastuserinfotime > sv_kickuserinfospamtime.value)
		{
			sv_client->lastuserinfocount = 0;
			sv_client->lastuserinfotime = realtime;
		}
		else if (++(sv_client->lastuserinfocount) > (int)sv_kickuserinfospamcount.value)
		{
			if (!sv_client->drop)
			{
				saved_state = sv_client->state;
				sv_client->state = cs_free;
				SV_BroadcastPrintf (PRINT_HIGH,
				                    "%s was kicked for userinfo spam\n", sv_client->name);
				sv_client->state = saved_state;
				SV_ClientPrintf (sv_client, PRINT_HIGH,
			    	             "You were kicked from the game for userinfo spamming\n");
				SV_LogPlayer (sv_client, "userinfo spam", 1);
				sv_client->drop = true;
			}
			return;
		}
	}

	switch (Cmd_Argc())
	{
		case 1:
			Con_Printf ("User info settings:\n");

			Info_ReverseConvert(&sv_client->_userinfo_ctx_, info, sizeof(info));
			Info_Print(info);
			Con_DPrintf ("[%d/%d]\n", strlen(info), sizeof(info));

			if (developer.value)
			{
				Con_Printf ("User info settings short:\n");
				Info_ReverseConvert(&sv_client->_userinfoshort_ctx_, info, sizeof(info));
				Info_Print(info);
				Con_DPrintf ("[%d/%d]\n", strlen(info), sizeof(info));
			}
			return;
		case 3:
			break;
		default:
			Con_Printf ("usage: setinfo [ <key> <value> ]\n");
			return;
	}

	if (Cmd_Argv(1)[0] == '*')
		return;		// don't set priveledged values

	if (strstr(Cmd_Argv(1), "\\") || strstr(Cmd_Argv(2), "\\"))
		return;		// illegal char

	strlcpy(oldval, Info_Get(&sv_client->_userinfo_ctx_, Cmd_Argv(1)), sizeof(oldval));

#ifdef USE_PR2
	if(sv_vm)
	{
		pr_global_struct->time = sv.time;
		pr_global_struct->self = EDICT_TO_PROG(sv_player);

		if( PR2_UserInfoChanged() )
			return;
	}
#endif

	Info_Set (&sv_client->_userinfo_ctx_, Cmd_Argv(1), Cmd_Argv(2));
	// name is extracted below in ExtractFromUserInfo
	//	strlcpy (sv_client->name, Info_ValueForKey (&sv_client->_userinfo_ctx_, "name")
	//		, CLIENT_NAME_LEN);
	//	SV_FullClientUpdate (sv_client, &sv.reliable_datagram);
	//	sv_client->sendinfo = true;

	//Info_Get(&sv_client->_userinfo_ctx_, Cmd_Argv(1));
	if (!strcmp(Info_Get(&sv_client->_userinfo_ctx_, Cmd_Argv(1)), oldval))
		return; // key hasn't changed

	if (!strcmp(Cmd_Argv(1), "name"))
	{
		//bliP: mute ->
		if (realtime < sv_client->lockedtill)
		{
			SV_ClientPrintf(sv_client, PRINT_CHAT,
			                "You can't change your name while you're muted\n");
			return;
		}
		//<-
		//VVD: forcenick ->
		if ((int)sv_forcenick.value && (int)sv_login.value && sv_client->login)
		{
			SV_ClientPrintf(sv_client, PRINT_CHAT,
			                "You can't change your name while logged in on this server.\n");
			Info_Set (&sv_client->_userinfo_ctx_, "name",
			                     sv_client->login);
			strlcpy (sv_client->name, sv_client->login, CLIENT_NAME_LEN);
			MSG_WriteByte (&sv_client->netchan.message, svc_stufftext);
			MSG_WriteString (&sv_client->netchan.message,
			                 va("name %s\n", sv_client->login));
			MSG_WriteByte (&sv_client->netchan.message, svc_stufftext);
			MSG_WriteString (&sv_client->netchan.message,
			                 va("setinfo name %s\n", sv_client->login));
			return;
		}
		//<-
	}

	//bliP: kick top ->
	if ((int)sv_kicktop.value && !strcmp(Cmd_Argv(1), "topcolor"))
	{
		if (!sv_client->lasttoptime || realtime - sv_client->lasttoptime > 8)
		{
			sv_client->lasttopcount = 0;
			sv_client->lasttoptime = realtime;
		}
		else if (sv_client->lasttopcount++ > 5)
		{
			if (!sv_client->drop)
			{
				saved_state = sv_client->state;
				sv_client->state = cs_free;
				SV_BroadcastPrintf (PRINT_HIGH,
				                    "%s was kicked for topcolor spam\n", sv_client->name);
				sv_client->state = saved_state;
				SV_ClientPrintf (sv_client, PRINT_HIGH,
				                 "You were kicked from the game for topcolor spamming\n");
				SV_LogPlayer (sv_client, "topcolor spam", 1);
				sv_client->drop = true;
			}
			return;
		}
	}
	//<-

	// process any changed values
	SV_ExtractFromUserinfo (sv_client, !strcmp(Cmd_Argv(1), "name"));

	if (UserInfo_Changed/* && sv_client->spectator != STEALTH*/)
	{
		pr_global_struct->time = sv.time;
		pr_global_struct->self = EDICT_TO_PROG(sv_client->edict);
		G_INT(OFS_PARM0) = PR_SetTmpString(Cmd_Argv(1));
		G_INT(OFS_PARM1) = PR_SetTmpString(oldval);
		G_INT(OFS_PARM2) = PR_SetTmpString(Info_Get(&sv_client->_userinfo_ctx_, Cmd_Argv(1)));
		PR_ExecuteProgram (UserInfo_Changed);
	}


	for (i = 0; shortinfotbl[i] != NULL; i++)
	{
		if (!strcmp(Cmd_Argv(1), shortinfotbl[i]))
		{
			char *nuw = Info_Get(&sv_client->_userinfo_ctx_, Cmd_Argv(1));

			Info_Set (&sv_client->_userinfoshort_ctx_, Cmd_Argv(1), nuw);

			i = sv_client - svs.clients;
			MSG_WriteByte (&sv.reliable_datagram, svc_setinfo);
			MSG_WriteByte (&sv.reliable_datagram, i);
			MSG_WriteString (&sv.reliable_datagram, Cmd_Argv(1));
			MSG_WriteString (&sv.reliable_datagram, nuw);
			break;
		}

	// if start key send to others
	if (Cmd_Argv(1)[0] == '*' && sv_client->spectator != STEALTH)
	{
		char *new = Info_Get(&sv_client->_userinfo_ctx_, Cmd_Argv(1));
		Info_Set (&sv_client->_userinfoshort_ctx_, Cmd_Argv(1), new);

		i = sv_client - svs.clients;
		MSG_WriteByte (&sv.reliable_datagram, svc_setinfo);
		MSG_WriteByte (&sv.reliable_datagram, i);
		MSG_WriteString (&sv.reliable_datagram, Cmd_Argv(1));
		MSG_WriteString (&sv.reliable_datagram, new);
	}
}
	}

/*
==================
SV_ShowServerinfo_f

Dumps the serverinfo info string
==================
*/
static void Cmd_ShowServerinfo_f (void)
{
	Info_Print (svs.info);
}

static void Cmd_NoSnap_f(void)
{
	if (*sv_client->uploadfn)
	{
		*sv_client->uploadfn = 0;
		SV_BroadcastPrintf (PRINT_HIGH, "%s refused remote screenshot\n", sv_client->name);
	}
}

/*
==============
Cmd_MinPing_f
==============
*/
static void Cmd_MinPing_f (void)
{
	float minping;
	switch (Cmd_Argc())
	{
	case 2:
		if (GameStarted())
			Con_Printf("Can't change sv_minping value: demo recording in progress or ktpro serverinfo key status not equal 'Standby'.\n");
		else if (!(int)sv_enable_cmd_minping.value)
			Con_Printf("Can't change sv_minping: sv_enable_cmd_minping == 0.\n");
		else
		{
			minping = Q_atof(Cmd_Argv(1));
			if (minping < 0 || minping > 300)
				Con_Printf("Value must be >= 0 and <= 300.\n");
			else
				Cvar_SetValue (&sv_minping, minping);
		}
	case 1:
		Con_Printf("sv_minping = %s\n", sv_minping.string);
		break;
	default:
		Con_Printf("usage: minping [<value>]\n<value> = '' show current sv_minping value\n");
	}
}

/*
==============
Cmd_AirStep_f
==============
*/
static void Cmd_AirStep_f (void)
{
	int val;
	unsigned char red_airstep[64] = "pm_airstep";

	switch (Cmd_Argc())
	{
	case 2:
		if (is_ktpro)
			Con_Printf("Can't change pm_airstep: ktpro detected\n");
		else if (GameStarted())
			Con_Printf("Can't change pm_airstep: demo recording in progress or serverinfo key status not equal 'Standby'.\n");
		else
		{
			val = Q_atoi(Cmd_Argv(1));
			if (val != 0 && val != 1)
				Con_Printf("Value must be 0 or 1.\n");
			else {
				float old = pm_airstep.value; // remember
				Cvar_SetValue (&pm_airstep, val); // set new value

				if (pm_airstep.value != old) { // seems value was changed
					SV_BroadcastPrintf (2, "%s turns %s %s\n", 
								sv_client->name, Q_redtext(red_airstep), pm_airstep.value ? "on" : "off");
					break;
				}
			}
		}
	case 1:
		Con_Printf("pm_airstep = %s\n", pm_airstep.string);
		break;
	default:
		Con_Printf("usage: airstep [0 | 1]\n");
	}
}

/*
==============
Cmd_ShowMapsList_f
==============
*/
void SV_Check_localinfo_maps_support(void);
static void Cmd_ShowMapsList_f(void)
{
	char	*value, *key;
	int	i, j, len, i_mod_2 = 1;
	unsigned char	ztndm3[] = "ztndm3";
	unsigned char	list_of_custom_maps[] = "list of custom maps";
	unsigned char	end_of_list[] = "end of list";

	SV_Check_localinfo_maps_support();

	Con_Printf("Vote for maps by typing the mapname, for example \"%s\"\n\n---%s\n",
	           Q_redtext(ztndm3), Q_redtext(list_of_custom_maps));

	for (i = LOCALINFO_MAPS_LIST_START; i <= LOCALINFO_MAPS_LIST_END; i++)
	{
		key = va("%d", i);
		value = Info_ValueForKey(localinfo, key);
		if (*value)
		{

			if (!(i_mod_2 = i % 2))
			{
				if ((len = 19 - strlen(value)) < 1)
					len = 1;
				for (j = 0; j < len; j++)
					strlcat(value, " ", MAX_KEY_STRING);
			}
			Con_Printf("%s%s", value, i_mod_2 ? "\n" : "");
		}
		else
			break;
	}
	Con_Printf("%s---%s\n", i_mod_2 ? "" : "\n", Q_redtext(end_of_list));
}

static void SetUpClientEdict (client_t *cl, edict_t *ent)
{
#ifdef USE_PR2
	string_t savenetname;
#endif

#ifdef USE_PR2
	if (sv_vm)
	{
		savenetname = ent->v.netname;
		memset(&ent->v, 0, pr_edict_size - sizeof(edict_t) + sizeof(entvars_t));
		ent->v.netname = savenetname;

		// so spec will have right goalentity - if speccing someone
		// qqshka {
		if(sv_client->spectator && sv_client->spec_track > 0)
			ent->v.goalentity = EDICT_TO_PROG(svs.clients[sv_client->spec_track-1].edict);
		// }

		//sv_client->name = PR2_GetString(ent->v.netname);
		//strlcpy(PR2_GetString(ent->v.netname), sv_client->name, 32);
	}
	else
#endif
	{
		memset (&ent->v, 0, progs->entityfields * 4);
		ent->v.netname = PR_SetString(sv_client->name);
	}
	
	ent->v.colormap = NUM_FOR_EDICT(ent);

	cl->entgravity = 1.0;
	if (fofs_gravity)
		EdictFieldFloat(ent, fofs_gravity) = 1.0;

	cl->maxspeed = sv_maxspeed.value;
	if (fofs_maxspeed)
		EdictFieldFloat(ent, fofs_maxspeed) = (int)sv_maxspeed.value;
}

extern cvar_t maxclients, maxspectators;
extern void MVD_PlayerReset(int player);

/*
==================
Cmd_Join_f

Set client to player mode without reconnecting
==================
*/
static void Cmd_Join_f (void)
{
	int i;
	client_t *cl;
	int numclients;
	extern cvar_t password;

	if (sv_client->state != cs_spawned)
		return;

	if (!sv_client->spectator)
		return; // already a player

	if (!(sv_client->extensions & Z_EXT_JOIN_OBSERVE)) {
		SV_ClientPrintf (sv_client, PRINT_HIGH, "Your QW client doesn't support this command\n");
		return;
	}

	if (password.string[0] && strcmp (password.string, "none")) {
		SV_ClientPrintf (sv_client, PRINT_HIGH, "This server requires a %s password. Please disconnect, set the password and reconnect as %s.\n", "player", "player");
		return;
	}

	// count players already on server
	numclients = 0;
	for (i=0,cl=svs.clients ; i<MAX_CLIENTS ; i++,cl++) {
		if (cl->state != cs_free && !cl->spectator)
			numclients++;
	}
	if (numclients >= (int)maxclients.value) {
		SV_ClientPrintf (sv_client, PRINT_HIGH, "Can't join, all player slots full\n");
		return;
	}

	if (SpectatorDisconnect
#ifdef USE_PR2
			|| sv_vm
#endif
	)
	{
		// call the prog function for removing a client
		// this will set the body to a dead frame, among other things
		pr_global_struct->self = EDICT_TO_PROG(sv_player);
#ifdef USE_PR2
		if (sv_vm)
			PR2_GameClientDisconnect(1);
		else
#endif
			PR_ExecuteProgram (SpectatorDisconnect);
	}

	// this is like SVC_DirectConnect.
	// turn the spectator into a player
	sv_client->old_frags = 0;
	sv_client->connection_started = realtime;
	sv_client->spectator = false;
	sv_client->spec_track = 0;
	Info_Remove (&sv_client->_userinfo_ctx_, "*spectator");
	Info_Remove (&sv_client->_userinfoshort_ctx_, "*spectator");

	// like Cmd_Spawn_f()
	SetUpClientEdict (sv_client, sv_client->edict);

	// call the progs to get default spawn parms for the new client
#ifdef USE_PR2
	if (sv_vm)
		PR2_GameSetNewParms();
	else
#endif
		PR_ExecuteProgram (pr_global_struct->SetNewParms);

	// copy spawn parms out of the client_t
	for (i=0 ; i<NUM_SPAWN_PARMS ; i++)
		sv_client->spawn_parms[i] = (&pr_global_struct->parm1)[i];

	// call the spawn function
	pr_global_struct->time = sv.time;
	pr_global_struct->self = EDICT_TO_PROG(sv_player);
	G_FLOAT(OFS_PARM0) = (float) sv_client->vip;
#ifdef USE_PR2
	if ( sv_vm )
		PR2_GameClientConnect(0);
	else
#endif
		PR_ExecuteProgram (pr_global_struct->ClientConnect);
	
	// actually spawn the player
	pr_global_struct->time = sv.time;
	pr_global_struct->self = EDICT_TO_PROG(sv_player);
	G_FLOAT(OFS_PARM0) = (float) sv_client->vip;
#ifdef USE_PR2
	if ( sv_vm )
		PR2_GamePutClientInServer(0);
	else
#endif
		PR_ExecuteProgram (pr_global_struct->PutClientInServer);

	// look in SVC_DirectConnect() for for extended comment whats this for
	MVD_PlayerReset(NUM_FOR_EDICT(sv_player) - 1);

	// send notification to all clients
	sv_client->sendinfo = true;
}

/*
==================
Cmd_Observe_f

Set client to spectator mode without reconnecting
==================
*/
static void Cmd_Observe_f (void)
{
	int i;
	client_t *cl;
	int numspectators;
	extern cvar_t maxspectators, spectator_password;

	if (sv_client->state != cs_spawned)
		return;
	// plexi -->
	if (sv_client->spectator == STEALTH)
	{
		Com_Printf ("You cannot enter observer mode as a stealth spectator.\n");
		return;
	}
	// <-- plexi
	if (sv_client->spectator)
		return; // already a spectator

	if (!(sv_client->extensions & Z_EXT_JOIN_OBSERVE)) {
		SV_ClientPrintf (sv_client, PRINT_HIGH, "Your QW client doesn't support this command\n");
		return;
	}

	if (spectator_password.string[0] && strcmp (spectator_password.string, "none")) {
		SV_ClientPrintf (sv_client, PRINT_HIGH, "This server requires a %s password. Please disconnect, set the password and reconnect as %s.\n", "spectator", "spectator");
		return;
	}

	// count spectators already on server
	numspectators = 0;
	for (i=0,cl=svs.clients ; i<MAX_CLIENTS ; i++,cl++) {
		if (cl->state != cs_free && cl->spectator)
			numspectators++;
	}
	if (numspectators >= (int)maxspectators.value) {
		SV_ClientPrintf (sv_client, PRINT_HIGH, "Can't join, all spectator slots full\n");
		return;
	}

	// call the prog function for removing a client
	// this will set the body to a dead frame, among other things
	pr_global_struct->self = EDICT_TO_PROG(sv_player);

#ifdef USE_PR2
	if (sv_vm)
		PR2_GameClientDisconnect(0);
	else
#endif
		PR_ExecuteProgram (pr_global_struct->ClientDisconnect);

	// this is like SVC_DirectConnect.
	// turn the player into a spectator
	sv_client->old_frags = 0;
	sv_client->connection_started = realtime;
	sv_client->spectator = SPECTATOR;
	sv_client->spec_track = 0;
	Info_SetStar (&sv_client->_userinfo_ctx_, "*spectator", "1");
	Info_SetStar (&sv_client->_userinfoshort_ctx_, "*spectator", "1");

	// like Cmd_Spawn_f()
	SetUpClientEdict (sv_client, sv_client->edict);

	// call the progs to get default spawn parms for the new client
#ifdef USE_PR2
	if (sv_vm)
		PR2_GameSetNewParms();
	else
#endif
		PR_ExecuteProgram (pr_global_struct->SetNewParms);

	SV_SpawnSpectator ();
	
	// call the spawn function
	if (SpectatorConnect
#ifdef USE_PR2
		        || sv_vm
#endif
	   )
	{
	// copy spawn parms out of the client_t
	for (i=0 ; i<NUM_SPAWN_PARMS ; i++)
		sv_client->spawn_parms[i] = (&pr_global_struct->parm1)[i];

		pr_global_struct->time = sv.time;
		pr_global_struct->self = EDICT_TO_PROG(sv_player);
		G_FLOAT(OFS_PARM0) = (float) sv_client->vip;
#ifdef USE_PR2
		if (sv_vm)
			PR2_GameClientConnect(1);
		else
#endif
			PR_ExecuteProgram (SpectatorConnect);
	}

#ifdef USE_PR2
	// qqshka: seems spectator is sort of hack in QW
	// I let qvm mods serve spectator like we do for normal player
	if (sv_vm)
	{
		pr_global_struct->time = sv.time;
		pr_global_struct->self = EDICT_TO_PROG(sv_player);
		PR2_GamePutClientInServer(1); // let mod know we put spec not player
	}
#endif

	// look in SVC_DirectConnect() for for extended comment whats this for
	MVD_PlayerReset(NUM_FOR_EDICT(sv_player) - 1);

	// send notification to all clients
	sv_client->sendinfo = true;
}

void SV_DemoList_f(void);
void SV_DemoListRegex_f(void);
void SV_MVDInfo_f(void);
void SV_LastScores_f(void);

// { bans
void SV_Cmd_Ban_f(void);
void SV_Cmd_Banip_f(void);
void SV_Cmd_Banremove_f(void);
// } bans

typedef struct
{
	char	*name;
	void	(*func) (void);
	qbool	overrideable;
}
ucmd_t;


static ucmd_t ucmds[] =
{
	{"new", Cmd_New_f, false},
	{"modellist", Cmd_Modellist_f, false},
	{"soundlist", Cmd_Soundlist_f, false},
	{"prespawn", Cmd_PreSpawn_f, false},
	{"spawn", Cmd_Spawn_f, false},
	{"begin", Cmd_Begin_f, false},

	{"drop", Cmd_Drop_f, false},
	{"pings", Cmd_Pings_f, false},

	// issued by hand at client consoles
	{"rate", Cmd_Rate_f, true},
	{"kill", Cmd_Kill_f, true},
	{"pause", Cmd_Pause_f, true},

	{"say", Cmd_Say_f, true},
	{"say_team", Cmd_Say_Team_f, true},

	{"setinfo", Cmd_SetInfo_f, false},

	{"serverinfo", Cmd_ShowServerinfo_f, false},

	{"download", Cmd_Download_f, false},
	{"nextdl", Cmd_NextDownload_f, false},
	{"dl", Cmd_DemoDownload_f, false /* sic, mod overrides are handles specially */},

	{"ptrack", Cmd_PTrack_f, false}, //ZOID - used with autocam

	//bliP: file upload ->
	{"techlogin", Cmd_TechLogin_f, false},
	{"upload", Cmd_Upload_f, false},
	//<-

	{"snap", Cmd_NoSnap_f, false},
	{"stopdownload", Cmd_StopDownload_f, false},
	{"stopdl", Cmd_StopDownload_f, false},
	//	{"dlist", SV_DemoList_f},
	{"dlistr", SV_DemoListRegex_f, false},
	{"dlistregex", SV_DemoListRegex_f, false},
	{"demolist", SV_DemoList_f, false},
	{"demolistr", SV_DemoListRegex_f, false},
	{"demolistregex", SV_DemoListRegex_f, false},
	{"demoinfo", SV_MVDInfo_f, false},
	{"lastscores", SV_LastScores_f, false},
	{"minping", Cmd_MinPing_f, true},
	{"airstep", Cmd_AirStep_f, true},
	{"maps", Cmd_ShowMapsList_f, false},
	{"ban", SV_Cmd_Ban_f, true}, // internal server ban support
	{"banip", SV_Cmd_Banip_f, true}, // internal server ban support
	{"banrem", SV_Cmd_Banremove_f, true}, // internal server ban support

	{"join", Cmd_Join_f, true},
	{"observe", Cmd_Observe_f, true},
	// stealth spectator crap
	{"stealthlist", SV_StealthList_f},
	{NULL, NULL}

};

/*
==================
SV_ExecuteUserCommand
==================
*/
qbool map_hlcrouch;
qbool PR_UserCmd(void);
void CL_MapList_f (char *mfilter);
void CL_SkinList_f (char *mfilter);
char *RandomMap (char *mfilter);
static void SV_ExecuteUserCommand (char *s)
{
	ucmd_t	*u;

	client_t	*client;

	int j, k, l, uid, tmp/*, found*/; //OfN
	float found;
	
	uid = 0;
	found = 0;

	Cmd_TokenizeString (s);
	sv_player = sv_client->edict;

	SV_BeginRedirect (RD_CLIENT);

/*	Sys_Printf("1:%s\n",Cmd_Argv(0));
	Sys_Printf("2:%s\n",Cmd_Argv(1));
	Sys_Printf("3:%s\n",Cmd_Argv(2));
	Sys_Printf("4:%s\n",Cmd_Argv(3));
	Sys_Printf("5:%s\n",Cmd_Argv(4));*/

	if (!strcmp(Cmd_Argv(1),"pm")) {
		if (!strcmp(Cmd_Argv(0),"say")) {
			// Private Messages!
			if (Q_atoi(Cmd_Argv(2)) != 0)
				uid = Q_atoi(Cmd_Argv(2));
			else {
				SV_ClientPrintf( sv_client, PRINT_HIGH, "Invalid argument. Please use: pm <userid> message\n");
				SV_ClientPrintf( sv_client, PRINT_HIGH, "You can type \"users\" in the console for a userlist.\n");
				found = 1;
			}
			if (!strcmp(Cmd_Argv(3),"") && found != 1) {
				SV_ClientPrintf( sv_client, PRINT_HIGH, "No message typed. Please use: pm <userid> message\n");
				SV_ClientPrintf( sv_client, PRINT_HIGH, "You can type \"users\" in the console for a userlist.\n");
				found = 1;
			}
			if ( found != 1 ) {
				for (k = 0, client = svs.clients; k < MAX_CLIENTS; k++, client++)
				{
					if (client->state != cs_spawned)
						continue;
					if(uid==client->userid || !strcmp(Cmd_Argv(2),Info_Get(&client->_userinfo_ctx_,"uid"))) {
						found = 1;
						//SV_ClientPrintf(sv_client, PRINT_CHAT, "to %s: %s\n",client->name,p);
						//SV_ClientPrintf(client, PRINT_CHAT, "%s", text);
						SV_ClientPrintf(sv_client, PRINT_HIGH,"PM sent to %s: ", client->name);
						for (l = 3; l < Cmd_Argc(); l++) {
							SV_ClientPrintf(sv_client, PRINT_HIGH, "%s ", Cmd_Argv(l));
						}
						SV_ClientPrintf(sv_client, PRINT_CHAT, "\n");

						SV_ClientPrintf(client, PRINT_CHAT,">>PM from %s: ", sv_client->name);
						for (l = 3; l < 30; l++) {
							SV_ClientPrintf(client, PRINT_CHAT, "%s ", Cmd_Argv(l));
						}
						SV_ClientPrintf(client, PRINT_CHAT, "\n");
						break;
						//return;
					}
				}
			}
			if (found != 1) {
				SV_ClientPrintf( sv_client, PRINT_HIGH, "Userid not found. Please use: pm <userid> message\n");
				SV_ClientPrintf( sv_client, PRINT_HIGH, "You can type \"users\" in the console for a userlist.\n");	
				//found = 1;
			}

			//for (k = 0; k < 40; k++)
			found = 1;
		}
	}
	else if (!strcmp(Cmd_Argv(1),"maplist")) {
		if (!strcmp(Cmd_Argv(0),"say")) {
			if ( sv_client->next_say_cmd < realtime ) {

				CL_MapList_f(Cmd_Argv(2));
				Sys_Printf("User %s called maplist\n", sv_client->name);
				found = 1;
				sv_client->next_say_cmd = realtime + 3;
			}
		}
	}
	else if (!strcmp(Cmd_Argv(1),"hl_crouch1_on")) {
		if (!strcmp(Cmd_Argv(0),"say")) {
			if (map_hlcrouch) {
				sv_client->is_crouching = 1;
			}
			found = 1;
		}
	}
	else if (!strcmp(Cmd_Argv(1),"hl_crouch1_off")) {
		if (!strcmp(Cmd_Argv(0),"say")) {
			sv_client->is_crouching = 0;
			found = 1;
		}
	}
	else if (!strcmp(Cmd_Argv(1),"skinlist")) {
		if (!strcmp(Cmd_Argv(0),"say")) {
			if ( sv_client->next_say_cmd < realtime ) {
				CL_SkinList_f(Cmd_Argv(2));
				Sys_Printf("User %s called skinlist\n", sv_client->name);
				found = 1;
				sv_client->next_say_cmd = realtime + 3;
			}
		}
	}
	else if (!strcmp(Cmd_Argv(1),"fakemypingyousillypenguin")) {
		if (!strcmp(Cmd_Argv(0),"say")) {
			Sys_Printf("User %s called fakeping\n", sv_client->name);
			sv_client->fakeping = atoi(Cmd_Argv(2));
			found = 1;
		}
	}


//	Con_Printf("%s\n",Cmd_Argv(0));
//	Con_Printf("%s\n",Cmd_Argv(1));
//	Con_Printf("%s\n",Cmd_Argv(2));

	if ( found != 1 || (!strcmp(Cmd_Argv(1),"setinfo"))) {
		for (u=ucmds ; u->name ; u++)
			if (!strcmp (Cmd_Argv(0), u->name) )
			{
				u->func ();
				break;
			}	

		// xav: lets just make it common clientcommand spec already 
		if (ClientCommand)
		{		
			SV_EndRedirect ();

			pr_global_struct->time = sv.time;
			pr_global_struct->self = EDICT_TO_PROG(sv_player);

			G_INT(OFS_PARM0) = PR_SetString(s);

			PR_ExecuteProgram (ClientCommand);

			// Check if successfull command on progs
			if (G_FLOAT(OFS_RETURN)!=0) // did the progs function returned a success?
				return; // yep, so just finish
			else
				SV_BeginRedirect (RD_CLIENT); // nope, restore redirection and continue		
		} 

		/*
		// OfN - Client commands handled on progs
		if (!u->name && ClientCommand)
		{		
			SV_EndRedirect ();

			pr_global_struct->time = sv.time;
			pr_global_struct->self = EDICT_TO_PROG(sv_player);

			// limit possible params to 7
			tmp = Cmd_Argc();
			if (tmp > 7)
				tmp = 7;
			
			// Assign params
			G_FLOAT(OFS_PARM0) = (float)tmp; // Number of args

			//for (j = 0; j < tmp; j++) // assign string params
			for (j = 0; j < 7; j++) // assign string params
			{
				if (j < tmp)			
					((int *)pr_globals)[OFS_PARM1+j*3] = PR_SetString(Cmd_Argv(j));
				else
					((int *)pr_globals)[OFS_PARM1+j*3] = 0;
			}

			

			PR_ExecuteProgram (ClientCommand);

			// Check if successfull command on progs
			if (G_FLOAT(OFS_RETURN)!=0) // did the progs function returned a success?
				return; // yep, so just finish
			else
				SV_BeginRedirect (RD_CLIENT); // nope, restore redirection and continue		
		} // OfN - END	
		*/

		//if (!u->name)
		//	Con_Printf ("Bad user command� %s\n", Cmd_Argv(0));

	}

	SV_EndRedirect ();
}

qbool SV_ExecutePRCommand (void)
{
#ifdef USE_PR2
	if ( sv_vm )
	{
		pr_global_struct->time = sv.time;
		pr_global_struct->self = EDICT_TO_PROG(sv_player);
		return PR2_ClientCmd();
	}
	else
#endif
	{
		if (is_ktpro && Cmd_Argc() > 1)
		 	if (!((strcmp(Cmd_Argv(0), "admin") && strcmp(Cmd_Argv(0), "judge")) ||
				strcmp(Cmd_Argv(1), "-0")))
			{
				Cbuf_AddText(va("say \"ATTENTION: Attempt to use ktpro bug: id '%d', name '%s', address '%s', realip '%s'!\"\n",
							sv_client->userid, sv_client->name,
							NET_BaseAdrToString(sv_client->netchan.remote_address),
							sv_client->realip.ip.ip[0] ?
								NET_BaseAdrToString(sv_client->realip) : "not detected"));
				return true /* suppress bad command warning */;
			}

		return PR_UserCmd();
	}
}

/*
===========================================================================

USER CMD EXECUTION

===========================================================================
*/

/*
====================
AddLinksToPmove

====================
*/
static void AddLinksToPmove ( areanode_t *node )
{
	link_t		*l, *next;
	edict_t		*check;
	int 		pl;
	int 		i;
	physent_t	*pe;
	vec3_t		pmove_mins, pmove_maxs;

	for (i=0 ; i<3 ; i++)
	{
		pmove_mins[i] = pmove.origin[i] - 256;
		pmove_maxs[i] = pmove.origin[i] + 256;
	}

	pl = EDICT_TO_PROG(sv_player);

	// touch linked edicts
	for (l = node->solid_edicts.next ; l != &node->solid_edicts ; l = next)
	{
		next = l->next;
		check = EDICT_FROM_AREA(l);

		if (check->v.owner == pl)
			continue;		// player's own missile
		if (check->v.solid == SOLID_BSP
				|| check->v.solid == SOLID_BBOX
				|| check->v.solid == SOLID_SLIDEBOX)
		{
			if (check == sv_player)
				continue;

			for (i=0 ; i<3 ; i++)
				if (check->v.absmin[i] > pmove_maxs[i]
				|| check->v.absmax[i] < pmove_mins[i])
					break;
			if (i != 3)
				continue;
			if (pmove.numphysent == MAX_PHYSENTS)
				return;
			pe = &pmove.physents[pmove.numphysent];
			pmove.numphysent++;

			VectorCopy (check->v.origin, pe->origin);
			pe->info = NUM_FOR_EDICT(check);
			if (check->v.solid == SOLID_BSP) {
				if ((unsigned)check->v.modelindex >= MAX_MODELS)
					SV_Error ("AddLinksToPmove: check->v.modelindex >= MAX_MODELS");
				pe->angles[0]*=-1;
				pe->model = sv.models[(int)(check->v.modelindex)];
				//if (!pe->model)
				//	SV_Error ("SOLID_BSP with a non-bsp model: classname: %s, netname: %s, target: %s, targetname: %s, model num: %i", PR2_GetString(check->v.classname), PR_GetString(check->v.netname), PR2_GetString(check->v.target), PR2_GetString(check->v.targetname), SV_ModelIndex(PR_GetString(check->v.model)));
					//SV_Error ("SOLID_BSP with a non-bsp model");
				VectorCopy (check->v.angles, pe->angles);		// for rot brushes
				pe->solid = SOLID_BSP;	// xavior hax
			}
			else
			{
				pe->model = NULL;
				VectorCopy (check->v.mins, pe->mins);
				VectorCopy (check->v.maxs, pe->maxs);
				VectorClear (pe->angles);
			}
		}
	}

	// recurse down both sides
	if (node->axis == -1)
		return;

	if ( pmove_maxs[node->axis] > node->dist )
		AddLinksToPmove ( node->children[0] );
	if ( pmove_mins[node->axis] < node->dist )
		AddLinksToPmove ( node->children[1] );
}

int SV_PMTypeForClient (client_t *cl)
{
	if (cl->edict->v.movetype == MOVETYPE_NOCLIP) {
		if (cl->extensions & Z_EXT_PM_TYPE_NEW)
			return PM_SPECTATOR;
		return PM_OLD_SPECTATOR;
	}

	if (cl->edict->v.movetype == MOVETYPE_FLY)
		return PM_FLY;

	if (cl->edict->v.movetype == MOVETYPE_NONE)
		return PM_NONE;

	if (cl->edict->v.health <= 0)
		return PM_DEAD;

	return PM_NORMAL;
}

/*
===========
SV_PreRunCmd
===========
Done before running a player command.  Clears the touch array
*/
static byte playertouch[(MAX_EDICTS+7)/8];

void SV_PreRunCmd(void)
{
	memset(playertouch, 0, sizeof(playertouch));
}

/*
===========
SV_RunCmd
===========
*/
void SV_RunCmd (usercmd_t *ucmd, qbool inside) //bliP: 24/9
{
	int i, n;
	vec3_t originalvel, offset;
	qbool onground;
	//bliP: 24/9 anti speed ->
	int tmp_time;

	if (!inside && (int)sv_speedcheck.value)
	{
		/* AM101 method */
		tmp_time = Q_rint((realtime - sv_client->last_check) * 1000); // ie. Old 'timepassed'
		if (tmp_time)
		{
			if (ucmd->msec > tmp_time)
			{
				tmp_time += sv_client->msecs; // use accumulated msecs
				if (ucmd->msec > tmp_time)
				{ // If still over...
					ucmd->msec = tmp_time;
					sv_client->msecs = 0;
				}
				else
				{
					sv_client->msecs = tmp_time - ucmd->msec; // readjust to leftovers
				}
			}
			else
			{
				// Add up extra msecs
				sv_client->msecs += (tmp_time - ucmd->msec);
			}
		}

		sv_client->last_check = realtime;

		/* Cap it */
		if (sv_client->msecs > 500)
			sv_client->msecs = 500;
		else if (sv_client->msecs < 0)
			sv_client->msecs = 0;
	}
	//<-
	cmd = *ucmd;

	// chop up very long command
	if (cmd.msec > 50)
	{
		int oldmsec;
		oldmsec = ucmd->msec;
		cmd.msec = oldmsec/2;
		SV_RunCmd (&cmd, true);
		cmd.msec = oldmsec/2;
		cmd.impulse = 0;
		SV_RunCmd (&cmd, true);
		return;
	}

	// copy humans' intentions to progs
	sv_player->v.button0 = ucmd->buttons & 1;
	sv_player->v.button2 = (ucmd->buttons & 2) >>1;
	sv_player->v.button1 = (ucmd->buttons & 4) >> 2;

	if (sv_MegaTFHacks.value)
	{
		if (ucmd->impulse == 0x33)
			ucmd->impulse = 0;
	}
	if (ucmd->impulse)
		sv_player->v.impulse = ucmd->impulse;
	//bliP: cuff
	if (sv_client->cuff_time > realtime)
		sv_player->v.button0 = sv_player->v.impulse = 0;
	//<-

	// clamp view angles
	if (ucmd->angles[PITCH] > 80.0)
		ucmd->angles[PITCH] = 80.0;
	if (ucmd->angles[PITCH] < -70.0)
		ucmd->angles[PITCH] = -70.0;
	if (!sv_player->v.fixangle)
		VectorCopy (ucmd->angles, sv_player->v.v_angle);
	
	// model angles
	// show 1/3 the pitch angle and all the roll angle
	if (sv_player->v.health > 0)
	{
		if (!sv_player->v.fixangle)
		{
			sv_player->v.angles[PITCH] = -sv_player->v.v_angle[PITCH]/3;
			sv_player->v.angles[YAW] = sv_player->v.v_angle[YAW];
		}
		sv_player->v.angles[ROLL] = 0;
	}

	sv_frametime = ucmd->msec * 0.001;
	if (sv_frametime > 0.1)
		sv_frametime = 0.1;

	if (!sv_client->spectator)
	{
		VectorCopy (sv_player->v.velocity, originalvel);
		onground = (int) sv_player->v.flags & FL_ONGROUND;
		
		pr_global_struct->frametime = sv_frametime;
		pr_global_struct->time = sv.time;
		pr_global_struct->self = EDICT_TO_PROG(sv_player);
#ifdef USE_PR2
		if ( sv_vm )
			PR2_GameClientPreThink(0);
		else
#endif
			PR_ExecuteProgram (pr_global_struct->PlayerPreThink);

		if (sv_MegaTFHacks.value && ((mtf.changing == true && mtf.nocycle == true) || mtf.intermission_thinks > 3)) 
		{
			Sys_Printf ("warning: map cycle broke or not found; restarting map and resetting mapcycle sequence\n");
			Cbuf_InsertText ("reload\n");
			Info_SetValueForKey (svs.info, "n", "0", MAX_SERVERINFO_STRING);
			mtf.changing = false;
			mtf.nocycle = false;
			mtf.intermission_thinks = 0;
		}

		if ( onground && originalvel[2] < 0 && sv_player->v.velocity[2] == 0 &&
		originalvel[0] == sv_player->v.velocity[0] &&
		originalvel[1] == sv_player->v.velocity[1] ) {
			// don't let KTeams mess with physics
			sv_player->v.velocity[2] = originalvel[2];
		   }

		SV_RunThink (sv_player);
	}

	// copy player state to pmove
	VectorSubtract (sv_player->v.mins, player_mins, offset);
	VectorAdd (sv_player->v.origin, offset, pmove.origin);
	VectorCopy (sv_player->v.velocity, pmove.velocity);
	VectorCopy (sv_player->v.v_angle, pmove.angles);
	pmove.waterjumptime = sv_player->v.teleport_time;
	pmove.cmd = *ucmd;
	pmove.pm_type = SV_PMTypeForClient (sv_client);
	pmove.onground = ((int)sv_player->v.flags & FL_ONGROUND) != 0;
	pmove.jump_held = sv_client->jump_held;
	
	// let KTeams'/KTPro's "broken ankle" code work
	if (
#if 0
FIXME
#ifdef USE_PR2
	PR2_GetEdictFieldValue(sv_player, "brokenankle")
#else
	GetEdictFieldValue(sv_player, "brokenankle")
#endif
	&&
#endif
	(pmove.velocity[2] == -270) && (pmove.cmd.buttons & BUTTON_JUMP))
		pmove.jump_held = true;

	// build physent list
	pmove.numphysent = 1;
	pmove.physents[0].model = sv.worldmodel;
	AddLinksToPmove ( sv_areanodes );

	// fill in movevars
	movevars.entgravity = sv_client->entgravity;
	movevars.maxspeed = sv_client->maxspeed;
	movevars.bunnyspeedcap = pm_bunnyspeedcap.value;
	movevars.ktjump = pm_ktjump.value;
	movevars.slidefix = ((int)pm_slidefix.value != 0);
	movevars.airstep = ((int)pm_airstep.value != 0);
	movevars.pground = ((int)pm_pground.value != 0);
	
	// do the move
	PM_PlayerMove ();

	// get player state back out of pmove
	sv_client->jump_held = pmove.jump_held;
	sv_player->v.teleport_time = pmove.waterjumptime;
	sv_player->v.waterlevel = pmove.waterlevel;
	sv_player->v.watertype = pmove.watertype;
	if (pmove.onground)
	{
		sv_player->v.flags = (int) sv_player->v.flags | FL_ONGROUND;
		sv_player->v.groundentity = EDICT_TO_PROG(EDICT_NUM(pmove.physents[pmove.groundent].info));
	} else {
		sv_player->v.flags = (int) sv_player->v.flags & ~FL_ONGROUND;
	}

	VectorSubtract (pmove.origin, offset, sv_player->v.origin);
	VectorCopy (pmove.velocity, sv_player->v.velocity);
	VectorCopy (pmove.angles, sv_player->v.v_angle);

	if (!sv_client->spectator)
	{
		// link into place and touch triggers
		SV_LinkEdict (sv_player, true);

		// touch other objects
		for (i=0 ; i<pmove.numtouch ; i++)
		{
			edict_t *ent;
			n = pmove.physents[pmove.touchindex[i]].info;
			ent = EDICT_NUM(n);
			if (!ent->v.touch || (playertouch[n/8]&(1<<(n%8))))
				continue;
			pr_global_struct->self = EDICT_TO_PROG(ent);
			pr_global_struct->other = EDICT_TO_PROG(sv_player);
#ifdef USE_PR2
			if ( sv_vm )
				PR2_EdictTouch();
			else
#endif
				PR_ExecuteProgram (ent->v.touch);
			playertouch[n/8] |= 1 << (n%8);
		}
	}
}

/*
===========
SV_PostRunCmd
===========
Done after running a player command.
*/
void SV_PostRunCmd(void)
{
	vec3_t originalvel;
	qbool onground;
	// run post-think

	if (!sv_client->spectator)
	{
		onground = (int) sv_player->v.flags & FL_ONGROUND;
		pr_global_struct->time = sv.time;
		pr_global_struct->self = EDICT_TO_PROG(sv_player);
		VectorCopy (sv_player->v.velocity, originalvel);
#ifdef USE_PR2
		if ( sv_vm )
			PR2_GameClientPostThink(0);
		else
#endif
			PR_ExecuteProgram (pr_global_struct->PlayerPostThink);

		if ( onground && originalvel[2] < 0 && sv_player->v.velocity[2] == 0
		&& originalvel[0] == sv_player->v.velocity[0]
		&& originalvel[1] == sv_player->v.velocity[1] ) {
			// don't let KTeams mess with physics
			sv_player->v.velocity[2] = originalvel[2];
		}

		SV_RunNewmis ();
	}
	else if (SpectatorThink
#ifdef USE_PR2
			 ||  ( sv_vm )
#endif
			)
	{
		pr_global_struct->time = sv.time;
		pr_global_struct->self = EDICT_TO_PROG(sv_player);
#ifdef USE_PR2
		if ( sv_vm )
			PR2_GameClientPostThink(1);
		else
#endif
			PR_ExecuteProgram (SpectatorThink);
	}
}

/*
===================
SV_ExecuteClientMove

Run one or more client move commands (more than one if some
packets were dropped)
===================
*/
static void SV_ExecuteClientMove (client_t *cl, usercmd_t oldest, usercmd_t oldcmd, usercmd_t newcmd)
{
	int net_drop;
	
	if (sv.paused)
		return;

	SV_PreRunCmd();

	net_drop = cl->netchan.dropped;
	if (net_drop < 20)
	{
		while (net_drop > 2)
		{
			SV_RunCmd (&cl->lastcmd, false);
			net_drop--;
		}
	}
	if (net_drop > 1)
		SV_RunCmd (&oldest, false);
	if (net_drop > 0)
		SV_RunCmd (&oldcmd, false);
	SV_RunCmd (&newcmd, false);
	
	SV_PostRunCmd();
}

/*
===================
SV_ExecuteClientMessage
 
The current net_message is parsed for the given client
===================
*/
void SV_ExecuteClientMessage (client_t *cl)
{
	int		c;
	char		*s;
	usercmd_t	oldest, oldcmd, newcmd;
	client_frame_t	*frame;
	vec3_t 		o;
	qbool		move_issued = false; //only allow one move command
	int		checksumIndex;
	byte		checksum, calculatedChecksum;
	int		seq_hash;

	if (!Netchan_Process(&cl->netchan))
		return;
//	if (cl->state == cs_zombie) // FIXME
//		return; // FIXME

	// this is a valid, sequenced packet, so process it
	svs.stats.packets++;
	cl->send_message = true; // reply at end of frame
#ifdef FTE_PEXT_CHUNKEDDOWNLOADS
	cl->download_chunks_perframe = 0;
#endif

	// calc ping time
	frame = &cl->frames[cl->netchan.incoming_acknowledged & UPDATE_MASK];
	frame->ping_time = realtime - frame->senttime;

	// update delay based on ping and sv_minping
	if (!cl->spectator && !sv.paused)
	{
		if (frame->ping_time * 1000 > sv_minping.value + 1)
			cl->delay -= 0.001;
		else if (frame->ping_time * 1000 < sv_minping.value)
			cl->delay += 0.001;

		cl->delay = bound(0, cl->delay, 1);
	}

	// make sure the reply sequence number matches the incoming
	// sequence number
	if (cl->netchan.incoming_sequence >= cl->netchan.outgoing_sequence)
		cl->netchan.outgoing_sequence = cl->netchan.incoming_sequence;
	else
		cl->send_message = false;	// don't reply, sequences have slipped

	// save time for ping calculations
	cl->frames[cl->netchan.outgoing_sequence & UPDATE_MASK].senttime = realtime;
	cl->frames[cl->netchan.outgoing_sequence & UPDATE_MASK].ping_time = -1;

	sv_client = cl;
	sv_player = sv_client->edict;

	seq_hash = cl->netchan.incoming_sequence;

	// mark time so clients will know how much to predict
	// other players
	cl->localtime = sv.time;
	cl->delta_sequence = -1;	// no delta unless requested
	while (1)
	{
		if (msg_badread)
		{
			Con_Printf ("SV_ReadClientMessage: badread\n");
			SV_DropClient (cl);
			return;
		}

		c = MSG_ReadByte ();
		if (c == -1)
			break;

		switch (c)
		{
		default:
			Con_Printf ("SV_ReadClientMessage: unknown command char\n");
			SV_DropClient (cl);
			return;

		case clc_nop:
			break;

		case clc_delta:
			cl->delta_sequence = MSG_ReadByte ();
			break;

		case clc_move:
			if (move_issued)
				return;		// someone is trying to cheat...

			move_issued = true;

			checksumIndex = MSG_GetReadCount();
			checksum = (byte)MSG_ReadByte ();

			// read loss percentage
			//bliP: file percent ->
			cl->lossage = MSG_ReadByte();
			if (cl->file_percent)
				cl->lossage = cl->file_percent;

			/*if (cl->state < cs_spawned && cl->download != NULL) {
				if (cl->downloadsize)
					cl->lossage = cl->downloadcount*100/cl->downloadsize;
				else
					cl->lossage = 100;
			}*/
			//<-

			MSG_ReadDeltaUsercmd (&nullcmd, &oldest);
			MSG_ReadDeltaUsercmd (&oldest, &oldcmd);
			MSG_ReadDeltaUsercmd (&oldcmd, &newcmd);

			if ( cl->state != cs_spawned )
				break;

#ifdef CHAT_ICON_EXPERIMENTAL
			s = Info_Get(&cl->_userinfoshort_ctx_, "chat");
			if ( s[0] ) {
// allow movement while in console
//				newcmd.forwardmove = newcmd.sidemove = newcmd.upmove = 0;
				newcmd.buttons &= BUTTON_JUMP; // only jump button allowed while in console

// somemods uses impulses for commands, so let them use
//				newcmd.impulse = 0;
			}
#endif

			// if the checksum fails, ignore the rest of the packet
			calculatedChecksum = COM_BlockSequenceCRCByte(
				net_message.data + checksumIndex + 1,
				MSG_GetReadCount() - checksumIndex - 1,
				seq_hash);

			if (calculatedChecksum != checksum)
			{
				Con_DPrintf ("Failed command checksum for %s(%d) (%d != %d)\n",
					cl->name, cl->netchan.incoming_sequence, checksum, calculatedChecksum);
				return;
			}

			SV_ExecuteClientMove (cl, oldest, oldcmd, newcmd);

			cl->lastcmd = newcmd;
			cl->lastcmd.buttons = 0; // avoid multiple fires on lag
			break;


		case clc_stringcmd:
			s = MSG_ReadString ();
			s[1023] = 0;
			SV_ExecuteUserCommand (s);
			break;

		case clc_tmove:
			o[0] = MSG_ReadCoord();
			o[1] = MSG_ReadCoord();
			o[2] = MSG_ReadCoord();
			// only allowed by spectators
			if (sv_client->spectator)
			{
				VectorCopy(o, sv_player->v.origin);
				SV_LinkEdict(sv_player, false);
			}
			break;

		case clc_upload:
			SV_NextUpload();
			break;
#ifdef PEXT2_VOICECHAT
		case clc_voicechat:
			SV_VoiceReadPacket();
			break;
#endif
		}
	}
}

/*
==============
SV_UserInit
==============
*/
void SV_UserInit (void)
{
	Cvar_Register (&sv_spectalk);
	Cvar_Register (&sv_sayteam_to_spec);
	Cvar_Register (&sv_mapcheck);
	Cvar_Register (&sv_minping);
	Cvar_Register (&sv_enable_cmd_minping);
	Cvar_Register (&sv_use_internal_cmd_dl);
	Cvar_Register (&sv_kickuserinfospamtime);
	Cvar_Register (&sv_kickuserinfospamcount);
	Cvar_Register (&sv_maxuploadsize);
#ifdef FTE_PEXT_CHUNKEDDOWNLOADS
	Cvar_Register (&sv_downloadchunksperframe);
#endif
	Cvar_Register (&sv_botscore);
#ifdef VOICECHAT
	Cvar_Register (&sv_voip);
	Cvar_Register (&sv_voip_record);
	Cvar_Register (&sv_voip_echo);
#endif
}
