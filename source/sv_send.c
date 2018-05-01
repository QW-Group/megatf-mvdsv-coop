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
 
	$Id: sv_send.c,v 1.32 2007/04/06 21:15:13 qqshka Exp $
*/

#include "qwsvdef.h"

#define CHAN_AUTO   0
#define CHAN_WEAPON 1
#define CHAN_VOICE  2
#define CHAN_ITEM   3
#define CHAN_BODY   4

/*
=============================================================================
 
Con_Printf redirection
 
=============================================================================
*/

char	outputbuf[OUTPUTBUF_SIZE];

redirect_t	sv_redirected;
static int	sv_redirectbufcount;

extern cvar_t sv_phs;

/*
================
Com_PrintLine

Returns a text line
================
*/
char *Com_PrintLine (int length)
{
	int					len, i;
	static char			string[GENERIC_BUFFER];

	if (length > GENERIC_BUFFER)
		len = GENERIC_BUFFER;
	else
		len = length;

	Q_bzero (string, sizeof(string));
	for (i = 1 ; i < len ; i++) 
		strncat (string, "-", sizeof(string));

	return string;
}

/*
==================
SV_FlushRedirect
==================
*/
void SV_FlushRedirect (void)
{
	char send1[OUTPUTBUF_SIZE + 6];

	if (sv_redirected == RD_PACKET)
	{
		send1[0] = 0xff;
		send1[1] = 0xff;
		send1[2] = 0xff;
		send1[3] = 0xff;
		send1[4] = A2C_PRINT;
		memcpy (send1 + 5, outputbuf, strlen(outputbuf) + 1);

		NET_SendPacket (strlen(send1) + 1, send1, net_from);
	}
	else if (sv_redirected == RD_CLIENT && sv_redirectbufcount < MAX_REDIRECTMESSAGES)
	{
		ClientReliableWrite_Begin (sv_client, svc_print, strlen(outputbuf)+3);
		ClientReliableWrite_Byte (sv_client, PRINT_HIGH);
		ClientReliableWrite_String (sv_client, outputbuf);
		sv_redirectbufcount++;
	}
	else if (sv_redirected == RD_MOD)
	{
		//return;
	}
	else if (sv_redirected > RD_MOD && sv_redirectbufcount < MAX_REDIRECTMESSAGES)
	{
		client_t *cl;

		cl = svs.clients + sv_redirected - RD_MOD - 1;

		if (cl->state == cs_spawned)
		{
			ClientReliableWrite_Begin (cl, svc_print, strlen(outputbuf)+3);
			ClientReliableWrite_Byte (cl, PRINT_HIGH);
			ClientReliableWrite_String (cl, outputbuf);
			sv_redirectbufcount++;
		}
	}

	// clear it
	outputbuf[0] = 0;
}


/*
==================
SV_BeginRedirect
 
  Send Con_Printf data to the remote client
  instead of the console
==================
*/
void SV_BeginRedirect (redirect_t rd)
{
	sv_redirected = rd;
	outputbuf[0] = 0;
	sv_redirectbufcount = 0;

}

void SV_EndRedirect (void)
{
	SV_FlushRedirect ();
	sv_redirected = RD_NONE;
}



/*
================
Con_Printf

Handles cursor positioning, line wrapping, etc
================
*/
#define	MAXPRINTMSG	4096
void Con_Printf (char *fmt, ...)
{
	va_list argptr;
	char msg[MAXPRINTMSG];

	va_start (argptr,fmt);
	vsnprintf (msg, MAXPRINTMSG, fmt, argptr);
	va_end (argptr);
	// add to redirected message
	if (sv_redirected)
	{
		if (strlen (msg) + strlen(outputbuf) > /*sizeof(outputbuf) - 1*/ MAX_MSGLEN - 10)
			SV_FlushRedirect ();
		strlcat (outputbuf, msg, sizeof(outputbuf));
		return;
	}

	Sys_Printf ("%s", msg);	// also echo to debugging console
	SV_Write_Log(CONSOLE_LOG, 0, msg);

	// dumb error message to log file if
	if (sv_error)
		SV_Write_Log(ERROR_LOG, 1, msg);
}

/*
================
Con_DPrintf
 
A Con_Printf that only shows up if the "developer" cvar is set
================
*/
void Con_DPrintf (char *fmt, ...)
{
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	if (!(int)developer.value)
		return;

	va_start (argptr,fmt);
	vsnprintf (msg, MAXPRINTMSG, fmt, argptr);
	va_end (argptr);

	Con_Printf ("%s", msg);
}

/*
=============================================================================
 
EVENT MESSAGES
 
=============================================================================
*/

static void SV_PrintToClient(client_t *cl, int level, char *string)
{
	ClientReliableWrite_Begin (cl, svc_print, strlen(string)+3);
	ClientReliableWrite_Byte (cl, level);
	ClientReliableWrite_String (cl, string);
}


/*
=================
SV_ClientPrintf
 
Sends text across to be displayed if the level passes
=================
*/
void SV_ClientPrintf (client_t *cl, int level, char *fmt, ...)
{
	va_list		argptr;
	char		string[1024];

	if (level < cl->messagelevel)
		return;

	va_start (argptr,fmt);
	vsnprintf (string, sizeof(string), fmt, argptr);
	va_end (argptr);

	if (sv.mvdrecording)
	{
		if (MVDWrite_Begin (dem_single, cl - svs.clients, strlen(string)+3))
		{
			MVD_MSG_WriteByte (svc_print);
			MVD_MSG_WriteByte (level);
			MVD_MSG_WriteString (string);
		}
	}

	SV_PrintToClient(cl, level, string);
}

void SV_ClientPrintf2 (client_t *cl, int level, char *fmt, ...)
{
	va_list		argptr;
	char		string[1024];

	if (level < cl->messagelevel)
		return;

	va_start (argptr,fmt);
	vsnprintf (string, sizeof(string), fmt, argptr);
	va_end (argptr);

	SV_PrintToClient(cl, level, string);
}

/*
================
Com_DPrintf

A Com_Printf that only shows up if the "developer" cvar is set
================
*/
void Com_Printf (char *fmt, ...);
void Com_DPrintf (char *fmt, ...)
{
	va_list		argptr;
	char		msg[1024+1];

	if (!developer.value)
		return;

	va_start (argptr,fmt);
	vsnprintf (msg, sizeof(msg), fmt, argptr);
	va_end (argptr);
	
	Com_Printf ("%s", msg);
}

/*
=================
SV_BroadcastPrintf

Sends text to all active clients
=================
*/
char *parse_mod_string(char *str);
void SV_BroadcastPrintf (int level, char *fmt, ...)
{
	va_list		argptr;
	char		string[1024], *fraglog;
	static char	string2[1024] = {0};
	client_t	*cl;
	int			i;

	va_start (argptr,fmt);
	vsnprintf (string, sizeof(string), fmt, argptr);
	va_end (argptr);

	Sys_Printf ("%s", string);	// print to the console

	for (i=0, cl = svs.clients ; i<MAX_CLIENTS ; i++, cl++)
	{
		if (level < cl->messagelevel)
			continue;
		if (cl->state < cs_connected)
			continue;

		SV_PrintToClient(cl, level, string);
	}

	if (sv.mvdrecording)
	{
		if (MVDWrite_Begin (dem_all, 0, strlen(string)+3))
		{
			MVD_MSG_WriteByte (svc_print);
			MVD_MSG_WriteByte (level);
			MVD_MSG_WriteString (string);
		}
	}

	//	SV_Write_Log(MOD_FRAG_LOG, 1, "=== SV_BroadcastPrintf ===\n");
	//	SV_Write_Log(MOD_FRAG_LOG, 1, va("%d\n===>", time(NULL)));
	//	SV_Write_Log(MOD_FRAG_LOG, 1, string);
	//	SV_Write_Log(MOD_FRAG_LOG, 1, "<===\n");
	if (string[0] && logs[MOD_FRAG_LOG].sv_logfile)
	{
		if (string[strlen(string) - 1] == '\n')
		{
			strlcat(string2, string, sizeof(string2));
			//			SV_Write_Log(MOD_FRAG_LOG, 1, "=== SV_BroadcastPrintf ==={\n");
			//			SV_Write_Log(MOD_FRAG_LOG, 1, string2);
			//			SV_Write_Log(MOD_FRAG_LOG, 1, "}==========================\n");
			if ((fraglog = parse_mod_string(string2)))
			{
				SV_Write_Log(MOD_FRAG_LOG, 1, fraglog);
				Q_free(fraglog);
			}
			string2[0] = 0;
		}
		else
			strlcat(string2, string, sizeof(string2));
	}
	//	SV_Write_Log(MOD_FRAG_LOG, 1, "==========================\n\n");
}

/*
=================
SV_BroadcastCommand
 
Sends text to all active clients
=================
*/
void SV_BroadcastCommand (char *fmt, ...)
{
	va_list		argptr;
	char		string[1024];

	if (!sv.state)
		return;
	va_start (argptr,fmt);
	vsnprintf (string, sizeof(string), fmt, argptr);
	va_end (argptr);

	MSG_WriteByte (&sv.reliable_datagram, svc_stufftext);
	MSG_WriteString (&sv.reliable_datagram, string);
}


/*
=================
SV_Multicast

Sends the contents of sv.multicast to a subset of the clients,
then clears sv.multicast.

MULTICAST_ALL	same as broadcast
MULTICAST_PVS	send to clients potentially visible from org
MULTICAST_PHS	send to clients potentially hearable from org
=================
*/
void SV_Multicast (vec3_t origin, int to, int with, int without)
{
	client_t	*client;
	byte		*mask;
	int		leafnum;
	int		j;
	qbool		reliable;
	vec3_t		vieworg;

	reliable = false;

	switch (to)
	{
	case MULTICAST_ALL_R:
		reliable = true;	// intentional fallthrough
	case MULTICAST_ALL:
		mask = NULL;		// everything
		break;

	case MULTICAST_PHS_R:
		reliable = true;	// intentional fallthrough
	case MULTICAST_PHS:
		mask = CM_LeafPHS (CM_PointInLeaf(origin));
		break;

	case MULTICAST_PVS_R:
		reliable = true;	// intentional fallthrough
	case MULTICAST_PVS:
		mask = CM_LeafPVS (CM_PointInLeaf (origin));
		break;

	default:
		mask = NULL;
		SV_Error ("SV_Multicast: bad to:%i", to, 0, 0);
	}

	// send the data to all relevent clients
	for (j = 0, client = svs.clients; j < MAX_CLIENTS; j++, client++)
	{
		if (client->state != cs_spawned)
			continue;

		if (client->fteprotocolextensions & without)
		{
//			Con_Printf ("Version supressed multicast - without pext\n");
			continue;
		}
		if (!(~client->fteprotocolextensions & ~with))
		{
//			Con_Printf ("Version supressed multicast - with pext\n");
			continue;
		}

		if (!mask)
			goto inrange; // multicast to all

		VectorAdd (client->edict->v.origin, client->edict->v.view_ofs, vieworg);

		if (to == MULTICAST_PHS_R || to == MULTICAST_PHS)
		{
			vec3_t delta;
			VectorSubtract(origin, vieworg, delta);
			if (VectorLength(delta) <= 1024)
				goto inrange;
		}

		leafnum = CM_Leafnum(CM_PointInLeaf(vieworg));
		if (leafnum)
		{
			// -1 is because pvs rows are 1 based, not 0 based like leafs
			leafnum = leafnum - 1;
			if ( !(mask[leafnum>>3] & (1<<(leafnum&7)) ) )
			{
				// Con_Printf ("supressed multicast\n");
				continue;
			}
		}

inrange:
		if (reliable)
		{
			ClientReliableCheckBlock(client, sv.multicast.cursize);
			ClientReliableWrite_SZ(client, sv.multicast.data, sv.multicast.cursize);
		}
		else
			SZ_Write (&client->datagram, sv.multicast.data, sv.multicast.cursize);
	}

	if (sv.mvdrecording)
	{
		if (reliable)
		{
			if (MVDWrite_Begin(dem_all, 0, sv.multicast.cursize))
			{
				MVD_SZ_Write(sv.multicast.data, sv.multicast.cursize);
			}
		}
		else
			SZ_Write(&demo.datagram, sv.multicast.data, sv.multicast.cursize);
	}


	SZ_Clear (&sv.multicast);
}


/*
==================
SV_StartSound
 
Each entity can have eight independant sound sources, like voice,
weapon, feet, etc.
 
Channel 0 is an auto-allocate channel, the others override anything
already running on that entity/channel pair.
 
An attenuation of 0 will play full volume everywhere in the level.
Larger attenuations will drop off.  (max 4 attenuation)
 
==================
*/
void SV_StartSound (edict_t *entity, int channel, char *sample, int volume,
                    float attenuation)
{
	int	sound_num;
	int	field_mask;
	int	i;
	int	ent;
	vec3_t	origin;
	qbool	use_phs;
	qbool	reliable = false;
	int requiredextensions = 0;

	if (volume < 0 || volume > 255) {
		SV_EndRedirect();
		SV_Error ("SV_StartSound: volume = %i", volume);
	}

	if (attenuation < 0 || attenuation > 4)
		SV_Error ("SV_StartSound: attenuation = %f", attenuation);

	if (channel < 0 || channel > 15)
		SV_Error ("SV_StartSound: channel = %i", channel);

	// find precache number for sound
	for (sound_num=1 ; sound_num<MAX_SOUNDS
	        && sv.sound_precache[sound_num] ; sound_num++)
		if (!strcmp(sample, sv.sound_precache[sound_num]))
			break;

	if ( sound_num == MAX_SOUNDS || !sv.sound_precache[sound_num] )
	{
		Con_Printf ("SV_StartSound: %s not precacheed\n", sample);
		return;
	}

	ent = NUM_FOR_EDICT(entity);

	if ((channel & 8) || !(int)sv_phs.value)	// no PHS flag
	{
		if (channel & 8)
			reliable = true; // sounds that break the phs are reliable
		use_phs = false;
		channel &= 7;
	}
	else
		use_phs = true;

	//	if (channel == CHAN_BODY || channel == CHAN_VOICE)
	//		reliable = true;

	channel = (ent<<3) | channel;

	field_mask = 0;
	if (volume != DEFAULT_SOUND_PACKET_VOLUME)
		channel |= SND_VOLUME;
	if (attenuation != DEFAULT_SOUND_PACKET_ATTENUATION)
		channel |= SND_ATTENUATION;

	// use the entity origin unless it is a bmodel
	if (entity->v.solid == SOLID_BSP)
	{
		for (i=0 ; i<3 ; i++)
			origin[i] = entity->v.origin[i]+0.5*(entity->v.mins[i]+entity->v.maxs[i]);
	}
	else
	{
		VectorCopy (entity->v.origin, origin);
	}

	MSG_WriteByte (&sv.multicast, svc_sound);
	MSG_WriteShort (&sv.multicast, channel);
	if (channel & SND_VOLUME)
		MSG_WriteByte (&sv.multicast, volume);
	if (channel & SND_ATTENUATION)
		MSG_WriteByte (&sv.multicast, attenuation*64);
	MSG_WriteByte (&sv.multicast, sound_num);
	for (i=0 ; i<3 ; i++)
		MSG_WriteCoord (&sv.multicast, origin[i]);

	if (ent > 512)
		requiredextensions |= PEXT_ENTITYDBL;
	if (ent > 1024)
		requiredextensions |= PEXT_ENTITYDBL2;

	if (use_phs)
		SV_Multicast (origin, reliable ? MULTICAST_PHS_R : MULTICAST_PHS, requiredextensions, 0);
	else
		SV_Multicast (origin, reliable ? MULTICAST_ALL_R : MULTICAST_ALL, requiredextensions, 0);
}


/*
===============================================================================
 
FRAME UPDATES
 
===============================================================================
*/

int		sv_nailmodel, sv_supernailmodel, sv_playermodel;

void SV_FindModelNumbers (void)
{
	int		i;

	sv_nailmodel = -1;
	sv_supernailmodel = -1;
	sv_playermodel = -1;

	for (i=0 ; i<MAX_MODELS ; i++)
	{
		if (!sv.model_precache[i])
			break;
		if (!strcmp(sv.model_precache[i],"progs/spike.mdl"))
			sv_nailmodel = i;
		if (!strcmp(sv.model_precache[i],"progs/s_spike.mdl"))
			sv_supernailmodel = i;
		if (!strcmp(sv.model_precache[i],"progs/player.mdl"))
			sv_playermodel = i;
	}
}


/*
==================
SV_WriteClientdataToMessage
 
==================
*/
void SV_WriteClientdataToMessage (client_t *client, sizebuf_t *msg)
{
	int		i, clnum;
	edict_t	*other;
	edict_t	*ent;

	ent = client->edict;

	clnum = NUM_FOR_EDICT(ent) - 1;

	// send the chokecount for r_netgraph
	if (client->chokecount)
	{
		MSG_WriteByte (msg, svc_chokecount);
		MSG_WriteByte (msg, client->chokecount);
		client->chokecount = 0;
	}

	// send a damage message if the player got hit this frame
	if (ent->v.dmg_take || ent->v.dmg_save)
	{
		other = PROG_TO_EDICT(ent->v.dmg_inflictor);
		MSG_WriteByte (msg, svc_damage);
		MSG_WriteByte (msg, ent->v.dmg_save);
		MSG_WriteByte (msg, ent->v.dmg_take);
		for (i=0 ; i<3 ; i++)
			MSG_WriteCoord (msg, other->v.origin[i] + 0.5*(other->v.mins[i] + other->v.maxs[i]));

		ent->v.dmg_take = 0;
		ent->v.dmg_save = 0;
	}

	// add this to server demo
	if (sv.mvdrecording && msg->cursize)
	{
		if (MVDWrite_Begin(dem_single, clnum, msg->cursize))
		{
			MVD_SZ_Write(msg->data, msg->cursize);
		}
	}

	// a fixangle might get lost in a dropped packet.  Oh well.
	if (ent->v.fixangle)
	{
		ent->v.fixangle = 0;
		demo.fixangle[clnum] = true;

		MSG_WriteByte (msg, svc_setangle);
		for (i=0 ; i < 3 ; i++)
			MSG_WriteAngle (msg, ent->v.angles[i] );

		if (sv.mvdrecording)
		{
			MSG_WriteByte (&demo.datagram, svc_setangle);
			MSG_WriteByte (&demo.datagram, clnum);
			for (i=0 ; i < 3 ; i++)
				MSG_WriteAngle (&demo.datagram, ent->v.angles[i] );
		}
	}

	// Z_EXT_TIME protocol extension
	// every now and then, send an update so that extrapolation
	// on client side doesn't stray too far off
	if ((SERVER_EXTENSIONS & Z_EXT_SERVERTIME) && (client->extensions & Z_EXT_SERVERTIME))
	{
		if (realtime - client->lastservertimeupdate > 5) {
			MSG_WriteByte(msg, svc_updatestatlong);
			MSG_WriteByte(msg, STAT_TIME);
			MSG_WriteLong(msg, (int) (sv.time * 1000));

			client->lastservertimeupdate = realtime;
		}
	}
}

/*
=======================
SV_UpdateClientStats
 
Performs a delta update of the stats array.  This should only be performed
when a reliable message can be delivered this frame.
=======================
*/
void SV_UpdateClientStats (client_t *client)
{
	edict_t *ent;
	int stats[MAX_CL_STATS], i;

	ent = client->edict;
	memset (stats, 0, sizeof(stats));

	// if we are a spectator and we are tracking a player, we get his stats
	// so our status bar reflects his
	if (client->spectator && client->spec_track > 0)
		ent = svs.clients[client->spec_track - 1].edict;

	stats[STAT_HEALTH] = ent->v.health;
	stats[STAT_WEAPON] = SV_ModelIndex(
#ifdef USE_PR2
	                         PR2_GetString(ent->v.weaponmodel)
#else
							 PR_GetString(ent->v.weaponmodel)
#endif
	                     );
	if (client->fteprotocolextensions & PEXT_MODELDBL)
	{
		if ((unsigned)stats[STAT_WEAPON] >= 512)
			stats[STAT_WEAPON] = 0;
	}
	else
	{
		if ((unsigned)stats[STAT_WEAPON] >= 256)
			stats[STAT_WEAPON] = 0;
	}

	stats[STAT_AMMO] = ent->v.currentammo;
	stats[STAT_ARMOR] = ent->v.armorvalue;
	stats[STAT_SHELLS] = ent->v.ammo_shells;
	stats[STAT_NAILS] = ent->v.ammo_nails;
	stats[STAT_ROCKETS] = ent->v.ammo_rockets;
	stats[STAT_CELLS] = ent->v.ammo_cells;
	if (!client->spectator || client->spec_track > 0)
		stats[STAT_ACTIVEWEAPON] = ent->v.weapon;
	// stuff the sigil bits into the high bits of items for sbar
	stats[STAT_ITEMS] = (int) ent->v.items | ((int) pr_global_struct->serverflags << 28);
	if (fofs_items2)	// ZQ_ITEMS2 extension
		stats[STAT_ITEMS] |= (int)EdictFieldFloat(ent, fofs_items2) << 23;

	if (ent->v.health > 0 || client->spectator) // viewheight for PF_DEAD & PF_GIB is hardwired
		stats[STAT_VIEWHEIGHT] = ent->v.view_ofs[2];

	for (i=0 ; i<MAX_CL_STATS ; i++)
		if (stats[i] != client->stats[i])
		{
			client->stats[i] = stats[i];
			if (stats[i] >=0 && stats[i] <= 255)
			{
				ClientReliableWrite_Begin(client, svc_updatestat, 3);
				ClientReliableWrite_Byte(client, i);
				ClientReliableWrite_Byte(client, stats[i]);
			}
			else
			{
				ClientReliableWrite_Begin(client, svc_updatestatlong, 6);
				ClientReliableWrite_Byte(client, i);
				ClientReliableWrite_Long(client, stats[i]);
			}
		}
}

/*
=======================
SV_SendClientDatagram
=======================
*/
void SV_SendClientDatagram (client_t *client, int client_num)
{
	byte		buf[MAX_DATAGRAM];
	sizebuf_t	msg;
	//	packet_t	*pack;

	msg.data = buf;
	msg.maxsize = sizeof(buf);
	msg.cursize = 0;
	msg.allowoverflow = true;
	msg.overflowed = false;

	// for faster downloading skip half the frames
	/*if (client->download && client->netchan.outgoing_sequence & 1)
	{
		// we're sending fake invalid delta update, so that client won't update screen
		MSG_WriteByte (&msg, svc_deltapacketentities);
		MSG_WriteByte (&msg, 0);
		MSG_WriteShort (&msg, 0);

		Netchan_Transmit (&client->netchan, msg.cursize, buf);
	}
	*/

	// add the client specific data to the datagram
	SV_WriteClientdataToMessage (client, &msg);

	// send over all the objects that are in the PVS
	// this will include clients, a packetentities, and
	// possibly a nails update
	SV_WriteEntitiesToClient (client, &msg, false);

#ifdef PEXT2_VOICECHAT
		SV_VoiceSendPacket(client, &msg);
#endif

	// copy the accumulated multicast datagram
	// for this client out to the message
	if (client->datagram.overflowed)
		Con_Printf ("WARNING: datagram overflowed for %s\n", client->name);
	else
		SZ_Write (&msg, client->datagram.data, client->datagram.cursize);
	SZ_Clear (&client->datagram);

	// send deltas over reliable stream
	if (Netchan_CanReliable (&client->netchan))
		SV_UpdateClientStats (client);

	if (msg.overflowed)
	{
		Con_Printf ("WARNING: msg overflowed for %s\n", client->name);
		SZ_Clear (&msg);
	}

	// send the datagram
	Netchan_Transmit (&client->netchan, msg.cursize, buf);
}

/*
=======================
SV_UpdateToReliableMessages
=======================
*/
void botUpdateEnterTime ( int clientno, int cliententertime );
void botUpdateFrags ( int clientno, int clientfrags );
void botUpdatePing ( int clientno, int clientping );
void botUpdateUserInfo ( client_t *client, int clientno, int clientid, int clientshirt, int clientpants, char *clientname, int botteam, int botskin);
static void SV_UpdateToReliableMessages (void)
{
	int i, j;
	client_t *client, *cl;
	edict_t *ent;
	int frgs, ping, entertime, k;		// xavior: bot

	// check for changes to be sent over the reliable streams to all clients
	for (i=0, sv_client = svs.clients ; i<MAX_CLIENTS ; i++, sv_client++)
	{
		if (sv_client->state != cs_spawned)
			continue;

		// added 10/04/11
		if (sv_client->sendinfo && sv_client->spectator != STEALTH)
		{
			sv_client->sendinfo = false;
			SV_FullClientUpdate (sv_client, &sv.reliable_datagram);
		}
		ent = sv_client->edict;

		if (sv_client->old_frags != (int)ent->v.frags)
		{
			for (j=0, client = svs.clients ; j<MAX_CLIENTS ; j++, client++)
			{
				if (client->state < cs_preconnected)
					continue;
				ClientReliableWrite_Begin(client, svc_updatefrags, 4);
				ClientReliableWrite_Byte(client, i);
				ClientReliableWrite_Short(client, (int) ent->v.frags);
			}

			if (sv.mvdrecording)
			{
				if (MVDWrite_Begin(dem_all, 0, 4))
				{
					MVD_MSG_WriteByte(svc_updatefrags);
					MVD_MSG_WriteByte(i);
					MVD_MSG_WriteShort((int)ent->v.frags);
				}
			}

			sv_client->old_frags = (int) ent->v.frags;
		}

		// maxspeed/entgravity changes
		if (fofs_gravity && sv_client->entgravity != EdictFieldFloat(ent, fofs_gravity))
		{
			sv_client->entgravity = EdictFieldFloat(ent, fofs_gravity);
			ClientReliableWrite_Begin(sv_client, svc_entgravity, 5);
			ClientReliableWrite_Float(sv_client, sv_client->entgravity);
			if (sv.mvdrecording)
			{
				if (MVDWrite_Begin(dem_single, i, 5))
				{
					MVD_MSG_WriteByte(svc_entgravity);
					MVD_MSG_WriteFloat(sv_client->entgravity);
				}
			}
		}

		if (fofs_maxspeed && sv_client->maxspeed != EdictFieldFloat(ent, fofs_maxspeed))
		{
			sv_client->maxspeed = EdictFieldFloat(ent, fofs_maxspeed);
			ClientReliableWrite_Begin(sv_client, svc_maxspeed, 5);
			ClientReliableWrite_Float(sv_client, sv_client->maxspeed);
			if (sv.mvdrecording)
			{
				if (MVDWrite_Begin(dem_single, i, 5))
				{
					MVD_MSG_WriteByte(svc_maxspeed);
					MVD_MSG_WriteFloat(sv_client->maxspeed);
				}
			}
		}

	}




	if (sv.datagram.overflowed)
		SZ_Clear (&sv.datagram);

	// append the broadcast messages to each client messages
	for (j=0, client = svs.clients ; j<MAX_CLIENTS ; j++, client++)
	{
		if (client->state < cs_preconnected)
			continue; // reliables go to all connected or spawned

		ClientReliableCheckBlock(client, sv.reliable_datagram.cursize);
		ClientReliableWrite_SZ(client, sv.reliable_datagram.data, sv.reliable_datagram.cursize);

		if (client->state != cs_spawned)
			continue; // datagrams only go to spawned

		SZ_Write (&client->datagram, sv.datagram.data, sv.datagram.cursize);
	}

	if (sv.mvdrecording && sv.reliable_datagram.cursize)
	{
		if (MVDWrite_Begin(dem_all, 0, sv.reliable_datagram.cursize))
		{
			MVD_SZ_Write(sv.reliable_datagram.data, sv.reliable_datagram.cursize);
		}
	}

	if (sv.mvdrecording)
		SZ_Write(&demo.datagram, sv.datagram.data, sv.datagram.cursize); // FIZME: ???

	SZ_Clear (&sv.reliable_datagram);
	SZ_Clear (&sv.datagram);

	//xavior: send in the bots!
	if (sv_botscore.value == 1) {
		for (i=0 ; i<MAX_BOTS ; i++)
		{
			cl = &sbi.bots[i];
			if ( cl->state == 666 )
			{
				if ( cl->botent && cl->bfullupdate > 0 ) {
					if ( cl->botent->v.team == 0 )
					{
						cl->isBot = 1;
						for (k=0, client = svs.clients ; k<MAX_CLIENTS ; k++, client++)
						{
							if (client->state != cs_spawned)
								continue;
							//if (client->spectator != STEALTH)
							//	continue;
							botUpdateUserInfo( client, cl->bfullupdate, 0, 0, 0, "", 0, 0 );
						}
						//botUpdateUserInfo( sv_client, cl->bfullupdate, 0, 0, 0, "", 0, 0 );
						cl->bfullupdate = 0;
						cl->old_frags = 0;
						cl->state = 0;
						//botUpdateUserInfo( sv_client, cl->bfullupdate, cl->buserid, top, bottom, bname, cl->bteam /*cl->botent->v.team*/, cl->bpc_num );
					}
					else
					{
						frgs = cl->botent->v.frags;
						//ping = floor((rand() / 1000));
						ping = 1;	// XavioR: no need to really fake it ingame now is there?
						entertime = (int)(realtime - cl->connection_started)/60;

		//				ent = sv_client->edict;
		//				if (cl->old_frags != (int)ent->v.frags)
		//				{
						//botUpdateFrags( cl->bfullupdate, frgs );
		//				}
						//botUpdatePing( cl->bfullupdate, ping );
						//botUpdateEnterTime( cl->bfullupdate, entertime );
					}
				}
			}
		}
	}
}

//#ifdef _WIN32
//#pragma optimize( "", off )
//#endif



/*
=======================
SV_SendClientMessages
=======================
*/
void SV_SendClientMessages (void)
{
	int			i, j;
	client_t	*c;

	// update frags, names, etc
	SV_UpdateToReliableMessages ();

	// build individual updates
	for (i=0, c = svs.clients ; i<MAX_CLIENTS ; i++, c++)
	{
		if (!c->state)
			continue;

		if (c->drop)
		{
			SV_DropClient(c);
			c->drop = false;
			continue;
		}

		// check to see if we have a backbuf to stick in the reliable
		if (c->num_backbuf)
		{
			// will it fit?
			if (c->netchan.message.cursize + c->backbuf_size[0] <
			        c->netchan.message.maxsize)
			{

				Con_DPrintf("%s: backbuf %d bytes\n",
				            c->name, c->backbuf_size[0]);

				// it'll fit
				SZ_Write(&c->netchan.message, c->backbuf_data[0],
				         c->backbuf_size[0]);

				//move along, move along
				for (j = 1; j < c->num_backbuf; j++)
				{
					memcpy(c->backbuf_data[j - 1], c->backbuf_data[j],
					       c->backbuf_size[j]);
					c->backbuf_size[j - 1] = c->backbuf_size[j];
				}

				c->num_backbuf--;
				if (c->num_backbuf)
				{
					memset(&c->backbuf, 0, sizeof(c->backbuf));
					c->backbuf.data = c->backbuf_data[c->num_backbuf - 1];
					c->backbuf.cursize = c->backbuf_size[c->num_backbuf - 1];
					c->backbuf.maxsize = sizeof(c->backbuf_data[c->num_backbuf - 1]);
				}
			}
		}

#ifdef USE_PR2
		if(c->isBot)
		{
			SZ_Clear (&c->netchan.message);
			SZ_Clear (&c->datagram);
			c->num_backbuf = 0;
			continue;
		}
#endif
		// if the reliable message overflowed,
		// drop the client
		if (c->netchan.message.overflowed)
		{
			SZ_Clear (&c->netchan.message);
			SZ_Clear (&c->datagram);
			//plexi: no print for stealth spectators
			if (c->spectator != STEALTH)
				SV_BroadcastPrintf (PRINT_HIGH, "%s overflowed\n", c->name);
			Con_Printf ("WARNING: reliable overflow for %s\n",c->name);
			SV_DropClient (c);
			c->send_message = true;
			c->netchan.cleartime = 0;	// don't choke this message
		}

		// only send messages if the client has sent one
		// and the bandwidth is not choked
		if (!c->send_message)
			continue;
		c->send_message = false;	// try putting this after choke?
		if (!sv.paused && !Netchan_CanPacket (&c->netchan))
		{
			c->chokecount++;
			continue;		// bandwidth choke
		}

		if (c->state == cs_spawned)
			SV_SendClientDatagram (c, i);
		else {
			Netchan_Transmit (&c->netchan, c->datagram.cursize, c->datagram.data);	// just update reliable
			c->datagram.cursize = 0;
		}
	}
}

void SV_MVDPings (void)
{
	client_t *client;
	int j;

	for (j = 0, client = svs.clients; j < MAX_CLIENTS; j++, client++)
	{
		if (client->state != cs_spawned)
			continue;

		if (MVDWrite_Begin (dem_all, 0, 7))
		{
			MVD_MSG_WriteByte (svc_updateping);
			MVD_MSG_WriteByte (j);
			MVD_MSG_WriteShort(SV_CalcPing(client));
			MVD_MSG_WriteByte (svc_updatepl);
			MVD_MSG_WriteByte (j);
			MVD_MSG_WriteByte (client->lossage);
		}
	}
}

void MVD_WriteStats(void)
{
	client_t	*c;
	int i, j;
	edict_t		*ent;
	int			stats[MAX_CL_STATS];

	for (i = 0, c = svs.clients ; i < MAX_CLIENTS ; i++, c++)
	{
		if (c->state != cs_spawned)
			continue;	// datagrams only go to spawned

		if (c->spectator)
			continue;

		ent = c->edict;
		memset (stats, 0, sizeof(stats));

		stats[STAT_HEALTH] = ent->v.health;
		stats[STAT_WEAPON] = SV_ModelIndex(
#ifdef USE_PR2
		                         PR2_GetString(ent->v.weaponmodel)
#else
								 PR_GetString(ent->v.weaponmodel)
#endif
		                     );
		stats[STAT_AMMO] = ent->v.currentammo;
		stats[STAT_ARMOR] = ent->v.armorvalue;
		stats[STAT_SHELLS] = ent->v.ammo_shells;
		stats[STAT_NAILS] = ent->v.ammo_nails;
		stats[STAT_ROCKETS] = ent->v.ammo_rockets;
		stats[STAT_CELLS] = ent->v.ammo_cells;
		stats[STAT_ACTIVEWEAPON] = ent->v.weapon;

		if (ent->v.health > 0) // viewheight for PF_DEAD & PF_GIB is hardwired
			stats[STAT_VIEWHEIGHT] = ent->v.view_ofs[2];

		// stuff the sigil bits into the high bits of items for sbar
		stats[STAT_ITEMS] = (int) ent->v.items | ((int) pr_global_struct->serverflags << 28);

		for (j = 0 ; j < MAX_CL_STATS; j++)
		{
			if (stats[j] != demo.stats[i][j])
			{
				demo.stats[i][j] = stats[j];
				if (stats[j] >= 0 && stats[j] <= 255)
				{
					if (MVDWrite_Begin(dem_stats, i, 3))
					{
						MVD_MSG_WriteByte(svc_updatestat);
						MVD_MSG_WriteByte(j);
						MVD_MSG_WriteByte(stats[j]);
					}
				}
				else
				{
					if (MVDWrite_Begin(dem_stats, i, 6))
					{
						MVD_MSG_WriteByte(svc_updatestatlong);
						MVD_MSG_WriteByte(j);
						MVD_MSG_WriteLong(stats[j]);
					}
				}
			}
		}
	}
}

void MVDSetMsgBuf(demobuf_t *prev,demobuf_t *cur);
void DemoWriteQTVTimePad(int msecs);
void DestFlush(qbool compleate);
void SV_MVD_RunPendingConnections(void);
void QTV_ReadDests( void );

void SV_SendDemoMessage(void)
{
	int			i, cls = 0;
	client_t	*c;
	sizebuf_t	msg;
	byte		msg_buf[MAX_MVD_SIZE]; // data without mvd header

	float		min_fps;
	extern		cvar_t sv_demofps, sv_demoIdlefps;
	extern		cvar_t sv_demoPings;

	SV_MVD_RunPendingConnections();

	if (!sv.mvdrecording)
	{
		DestFlush(false); // well, this may help close some fucked up dests
		return;
	}

	for (i = 0, c = svs.clients; i < MAX_CLIENTS; i++, c++)
	{
		if (c->state != cs_spawned)
			continue;	// datagrams only go to spawned

		cls |= 1 << i;
	}

	// if no players, use idle fps
	if (cls)
		min_fps = max(4.0, (int)sv_demofps.value ? (int)sv_demofps.value : 20.0);
	else
		min_fps = bound(4.0, (int)sv_demoIdlefps.value, 30);

	if (!demo.forceFrame && (sv.time - demo.time < 1.0/min_fps))
		return;

	demo.forceFrame = 0;

	if ((int)sv_demoPings.value)
	{
		if (sv.time - demo.pingtime > sv_demoPings.value)
		{
			SV_MVDPings();
			demo.pingtime = sv.time;
		}
	}

	MVD_WriteStats();

	// send over all the objects that are in the PVS
	// this will include clients, a packetentities, and
	// possibly a nails update
	SZ_InitEx(&msg, msg_buf, sizeof(msg_buf), true);

	if (!demo.recorder.delta_sequence)
		demo.recorder.delta_sequence = -1;

	SV_WriteEntitiesToClient (&demo.recorder, &msg, true);

	if (msg.overflowed)
	{
		Sys_Printf("WARNING: msg overflowed in SV_SendDemoMessage\n");
	}
	else
	{
		if (msg.cursize)
		{
			if (MVDWrite_Begin(dem_all, 0, msg.cursize))
			{
				MVD_SZ_Write(msg.data, msg.cursize);
			}
		}
	}

	SZ_Clear(&msg);

	// copy the accumulated multicast datagram
	// for this client out to the message
	if (demo.datagram.overflowed)
	{
		Sys_Printf("WARNING: demo.datagram overflowed in SV_SendDemoMessage\n");
	}
	else
	{
		if (demo.datagram.cursize)
		{
			if (MVDWrite_Begin(dem_all, 0, demo.datagram.cursize))
			{
				MVD_SZ_Write (demo.datagram.data, demo.datagram.cursize);
			}
		}
	}

	SZ_Clear (&demo.datagram);

	demo.recorder.delta_sequence = demo.recorder.netchan.incoming_sequence&255;
	demo.recorder.netchan.incoming_sequence++;
	demo.frames[demo.parsecount&UPDATE_MASK].time = demo.time = sv.time;

//	if (demo.parsecount - demo.lastwritten > 60) // that's a backup of 3sec in 20fps, should be enough
	if (demo.parsecount - demo.lastwritten > 5)  // lets not wait so much time
	{
		SV_MVDWritePackets(1);
	}

	// flush once per demo frame
	DestFlush(false);

	// read QTV input once per demo frame
	QTV_ReadDests();

	if (!sv.mvdrecording)
		return;

	demo.parsecount++;
}


//#ifdef _WIN32
//#pragma optimize( "", on )
//#endif



/*
=======================
SV_SendMessagesToAll
 
FIXME: does this sequence right?
=======================
*/
void SV_SendMessagesToAll (void)
{
	int			i;
	client_t	*c;

	for (i=0, c = svs.clients ; i<MAX_CLIENTS ; i++, c++)
		if (c->state >= cs_connected)		// FIXME: should this only send to active?
			c->send_message = true;

	SV_SendClientMessages ();
}

/*
================
Com_Printf

Handles line wrapping, logging and timestamping
================
*/
static char		sv_outputbuf[MAX_MSGLEN-1];
void Com_Printf (char *fmt, ...)
{
	// FIXME: add this def somewhere:
#ifdef EQDS_PRINTF			// eqds printf doesnt print to remote rcon for whatever reason..
	va_list		argptr;
	char		msg[MAX_PRINT_STRING+1];

	// xavior:temp:
	//Con_Printf( fmt );

	va_start (argptr,fmt);
	vsnprintf (msg, sizeof(msg), fmt, argptr);
	va_end (argptr);

	// add to redirected message
	if (sv_redirected)
	{
		if (strlen (msg) + strlen(sv_outputbuf) > sizeof(sv_outputbuf) - 1)
			SV_FlushRedirect ();
		strcat (sv_outputbuf, msg);
		return;
	}
	Sys_Printf (msg);	// also echo to debugging console
#else
	va_list argptr;
	char msg[MAXPRINTMSG];

	va_start (argptr,fmt);
	vsnprintf (msg, MAXPRINTMSG, fmt, argptr);
	va_end (argptr);
	// add to redirected message
	if (sv_redirected)
	{
		if (strlen (msg) + strlen(outputbuf) > /*sizeof(outputbuf) - 1*/ MAX_MSGLEN - 10)
			SV_FlushRedirect ();
		strlcat (outputbuf, msg, sizeof(outputbuf));
		return;
	}

	Sys_Printf ("%s", msg);	// also echo to debugging console
	SV_Write_Log(CONSOLE_LOG, 0, msg);

	// dumb error message to log file if
	if (sv_error)
		SV_Write_Log(ERROR_LOG, 1, msg);
#endif
}