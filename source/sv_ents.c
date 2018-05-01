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

	$Id: sv_ents.c,v 1.14 2006/11/25 23:32:37 disconn3ct Exp $
*/

#include "qwsvdef.h"


//=============================================================================

// because there can be a lot of nails, there is a special
// network protocol for them
#define MAX_NAILS 32
static edict_t *nails[MAX_NAILS];
static int numnails;
static int nailcount = 0;

extern	int sv_nailmodel, sv_supernailmodel, sv_playermodel;

cvar_t	sv_nailhack	= {"sv_nailhack", "1"};
cvar_t	sv_sendcmd = {"sv_sendcmd", "0"};		// by default don't send cmd info, as it probably wastes some bandwidth


static qbool SV_AddNailUpdate (edict_t *ent)
{
	if ((int)sv_nailhack.value)
		return false;

	if (ent->v.modelindex != sv_nailmodel
	        && ent->v.modelindex != sv_supernailmodel)
		return false;
	if (numnails == MAX_NAILS)
		return true;
	nails[numnails] = ent;
	numnails++;
	return true;
}

static void SV_EmitNailUpdate (sizebuf_t *msg, qbool recorder)
{
	int x, y, z, p, yaw, n, i;
	byte bits[6]; // [48 bits] xyzpy 12 12 12 4 8
	edict_t *ent;


	if (!numnails)
		return;

	if (recorder)
		MSG_WriteByte (msg, svc_nails2);
	else
		MSG_WriteByte (msg, svc_nails);

	MSG_WriteByte (msg, numnails);

	for (n=0 ; n<numnails ; n++)
	{
		ent = nails[n];
		if (recorder)
		{
			if (!ent->v.colormap)
			{
				if (!((++nailcount)&255)) nailcount++;
				ent->v.colormap = nailcount&255;
			}

			MSG_WriteByte (msg, (byte)ent->v.colormap);
		}

		x = ((int)(ent->v.origin[0] + 4096 + 1) >> 1) & 4095;
		y = ((int)(ent->v.origin[1] + 4096 + 1) >> 1) & 4095;
		z = ((int)(ent->v.origin[2] + 4096 + 1) >> 1) & 4095;
		p = Q_rint(ent->v.angles[0]*(16.0/360.0)) & 15;
		yaw = Q_rint(ent->v.angles[1]*(256.0/360.0)) & 255;

		bits[0] = x;
		bits[1] = (x>>8) | (y<<4);
		bits[2] = (y>>4);
		bits[3] = z;
		bits[4] = (z>>8) | (p<<4);
		bits[5] = yaw;

		for (i=0 ; i<6 ; i++)
			MSG_WriteByte (msg, bits[i]);
	}
}

//=============================================================================


/*
==================
SV_WriteDelta

Writes part of a packetentities message.
Can delta from either a baseline or a previous packet_entity
==================
*/
static void SV_WriteDelta (entity_state_t *from, entity_state_t *to, sizebuf_t *msg, qbool force, unsigned int protext)
{
#ifdef PROTOCOLEXTENSIONS
	int evenmorebits=0;
	int fromeffects;
#endif
	int bits, i;


	// send an update
	bits = 0;

	if (to->number >= 512)
	{
		if (to->number >= 1024)
		{
			if (to->number >= 1024+512)
				evenmorebits |= U_ENTITYDBL;

			evenmorebits |= U_ENTITYDBL2;
			if (to->number >= 2048)
				SV_Error ("Entity number >= 2048");
		}
		else
			evenmorebits |= U_ENTITYDBL;
	}

	for (i=0 ; i<3 ; i++)
		if ( to->origin[i] != from->origin[i] )
			bits |= U_ORIGIN1<<i;

	if ( to->angles[0] != from->angles[0] )
		bits |= U_ANGLE1;

	if ( to->angles[1] != from->angles[1] )
		bits |= U_ANGLE2;

	if ( to->angles[2] != from->angles[2] )
		bits |= U_ANGLE3;

	if ( to->colormap != from->colormap )
		bits |= U_COLORMAP;

	if ( to->skinnum != from->skinnum )
		bits |= U_SKIN;

	if ( to->frame != from->frame )
		bits |= U_FRAME;

#ifdef PROTOCOLEXTENSIONS
	if (force && !(protext & PEXT_SPAWNSTATIC2))
		fromeffects = 0;	//force is true if we're going from baseline
	else					//old quakeworld protocols do not include effects in the baseline
		fromeffects = from->effects;	//so old clients will see the effects baseline as 0
	if ( (to->effects&0x00ff) != (fromeffects&0x00ff) )
		bits |= U_EFFECTS;
//	if ( (to->effects&0xff00) != (fromeffects&0xff00) )
//		evenmorebits |= U_EFFECTS16;

	if ( to->modelindex != from->modelindex )
	{
		bits |= U_MODEL;
		if (to->modelindex > 255)
			evenmorebits |= U_MODELDBL;
	}
#else
	if ( to->effects != from->effects )
		bits |= U_EFFECTS;

	if ( to->modelindex != from->modelindex )
		bits |= U_MODEL;
#endif

#ifdef PROTOCOLEXTENSIONS
#ifdef U_TRANS
	if ( to->trans != from->trans && protext & PEXT_TRANS)
		evenmorebits |= U_TRANS;
#endif

	if (evenmorebits&0xff00)
		evenmorebits |= U_YETMORE;
	if (evenmorebits&0x00ff)
		bits |= U_EVENMORE;
	if (bits & 511)
		bits |= U_MOREBITS;
#else
	//xavior: uh, i think so..
	if (bits & U_CHECKMOREBITS)
		bits |= U_MOREBITS;
#endif

	if (to->flags & U_SOLID)
		bits |= U_SOLID;

	if (msg->cursize + 40 > msg->maxsize)
	{	//not enough space in the buffer, don't send the entity this frame. (not sending means nothing changes, and it takes no bytes!!)
		*to = *from;
		return;
	}

	//
	// write the message
	//
	if (!to->number)
		SV_Error ("Unset entity number");
/*	if (to->number >= MAX_EDICTS)
	{
		//SV_Error
		Con_Printf ("Entity number >= MAX_EDICTS (%d), set to MAX_EDICTS - 1\n", MAX_EDICTS);
		to->number = MAX_EDICTS - 1;
	}
	*/

	// XavioR: THE NEXT 6 LINES FIXES THE MSG_BADREAD BUG!!! :D
	if (!bits && !force)
		return;		// nothing to send!
	i = (to->number&511) | (bits&~511);
	if (i & U_REMOVE)
		Sys_Error ("U_REMOVE");
	MSG_WriteShort (msg, i);

	if (bits & U_MOREBITS)
		MSG_WriteByte (msg, bits&255);
#ifdef PROTOCOLEXTENSIONS
	if (bits & U_EVENMORE)
		MSG_WriteByte (msg, evenmorebits&255);
	if (evenmorebits & U_YETMORE)
		MSG_WriteByte (msg, (evenmorebits>>8)&255);
#endif

	if (bits & U_MODEL)
		MSG_WriteByte (msg, to->modelindex&255);
	if (bits & U_FRAME)
		MSG_WriteByte (msg, to->frame);
	if (bits & U_COLORMAP)
		MSG_WriteByte (msg, to->colormap);
	if (bits & U_SKIN)
		MSG_WriteByte (msg, to->skinnum);
	if (bits & U_EFFECTS)
		MSG_WriteByte (msg, to->effects);
	if (bits & U_ORIGIN1)
		MSG_WriteCoord (msg, to->origin[0]);
	if (bits & U_ANGLE1)
		MSG_WriteAngle(msg, to->angles[0]);
	if (bits & U_ORIGIN2)
		MSG_WriteCoord (msg, to->origin[1]);
	if (bits & U_ANGLE2)
		MSG_WriteAngle(msg, to->angles[1]);
	if (bits & U_ORIGIN3)
		MSG_WriteCoord (msg, to->origin[2]);
	if (bits & U_ANGLE3)
		MSG_WriteAngle(msg, to->angles[2]);

#ifdef U_TRANS
	if (evenmorebits & U_TRANS)
		MSG_WriteByte (msg, (qbyte)(to->trans));
#endif
}

/*
=============
SV_EmitPacketEntities

Writes a delta update of a packet_entities_t to the message.

=============
*/
static void SV_EmitPacketEntities (client_t *client, packet_entities_t *to, sizebuf_t *msg)
{
	int oldindex, newindex, oldnum, newnum, oldmax;
	client_frame_t	*fromframe;
	packet_entities_t *from1;
	edict_t	*ent;


	// this is the frame that we are going to delta update from
	if (client->delta_sequence != -1)
	{
		fromframe = &client->frames[client->delta_sequence & UPDATE_MASK];
		from1 = &fromframe->entities;
		oldmax = from1->num_entities;

		MSG_WriteByte (msg, svc_deltapacketentities);
		MSG_WriteByte (msg, client->delta_sequence);
	}
	else
	{
		oldmax = 0;	// no delta update
		from1 = NULL;

		MSG_WriteByte (msg, svc_packetentities);
	}

	newindex = 0;
	oldindex = 0;
	//Con_Printf ("---%i to %i ----\n", client->delta_sequence & UPDATE_MASK
	//			, client->netchan.outgoing_sequence & UPDATE_MASK);
	while (newindex < to->num_entities || oldindex < oldmax)
	{
		newnum = newindex >= to->num_entities ? 9999 : to->entities[newindex].number;
		oldnum = oldindex >= oldmax ? 9999 : from1->entities[oldindex].number;

		if (newnum == oldnum)
		{	// delta update from old position
			//Con_Printf ("delta %i\n", newnum);
			SV_WriteDelta (&from1->entities[oldindex], &to->entities[newindex], msg, false, client->fteprotocolextensions);
			oldindex++;
			newindex++;
			continue;
		}

		if (newnum < oldnum)
		{	// this is a new entity, send it from the baseline
			if (newnum == 9999)
			{
				Sys_Printf("LOL, %d, %d, %d, %d %d %d\n", // nice message
				           newnum, oldnum, to->num_entities, oldmax,
				           client->netchan.incoming_sequence & UPDATE_MASK,
				           client->delta_sequence & UPDATE_MASK);
				if (client->edict == NULL)
					Sys_Printf("demo\n");
			}
			ent = EDICT_NUM(newnum);
			//Con_Printf ("baseline %i\n", newnum);
			SV_WriteDelta (&ent->baseline, &to->entities[newindex], msg, true, client->fteprotocolextensions);
			newindex++;
			continue;
		}

		if (newnum > oldnum)
		{	// the old entity isn't present in the new message
			if (oldnum > 512)
			{
				//yup, this is expensive.
				MSG_WriteShort (msg, oldnum | U_REMOVE|U_MOREBITS);
				MSG_WriteByte (msg, U_EVENMORE);
				if (oldnum >= 1024)
				{
					if (oldnum >= 1024+512)
						MSG_WriteByte (msg, U_ENTITYDBL2);
					else
						MSG_WriteByte (msg, U_ENTITYDBL|U_ENTITYDBL2);
				}
				else
					MSG_WriteByte (msg, U_ENTITYDBL);
			}
			else
				MSG_WriteShort (msg, oldnum | U_REMOVE);

			oldindex++;
			continue;
		}
	}

	MSG_WriteShort (msg, 0);	// end of packetentities
}

/*
=============
SV_WritePlayersToClient
=============
*/

/*#define DF_ORIGIN	1
#define DF_ANGLES	(1<<3)
#define DF_EFFECTS	(1<<6)
#define DF_SKINNUM	(1<<7)
#define DF_DEAD		(1<<8)
#define DF_GIB		(1<<9)
#define DF_WEAPONFRAME	(1<<10)
#define DF_MODEL	(1<<11)
*/
#define TruePointContents(p) CM_HullPointContents(&sv.worldmodel->hulls[0], 0, p)

#define ISUNDERWATER(x) ((x) == CONTENTS_WATER || (x) == CONTENTS_SLIME || (x) == CONTENTS_LAVA)

static qbool disable_updates; // disables sending entities to the client


int SV_PMTypeForClient (client_t *cl);
static void SV_WritePlayersToClient (client_t *client, edict_t *clent, byte *pvs, sizebuf_t *msg)
{
	int msec, pflags, pm_type = 0, pm_code = 0, i, j;
	demo_frame_t *demo_frame;
	demo_client_t *dcl;
	usercmd_t cmd;
	client_t *cl;
	edict_t *ent;


	demo_frame = &demo.frames[demo.parsecount&DEMO_FRAMES_MASK];

	for (j=0,cl=svs.clients, dcl = demo_frame->clients; j<MAX_CLIENTS ; j++,cl++, dcl++)
	{
		if (cl->state != cs_spawned)
			continue;

		ent = cl->edict;

		if (clent == NULL)
		{
			if (cl->spectator)
				continue;

			dcl->parsecount = demo.parsecount;

			VectorCopy(ent->v.origin, dcl->origin);
			VectorCopy(ent->v.angles, dcl->angles);
			dcl->angles[0] *= -3;
#ifdef USE_PR2
			if( cl->isBot )
				VectorCopy(ent->v.v_angle, dcl->angles);
#endif
			dcl->angles[2] = 0; // no roll angle

			if (ent->v.health <= 0)
			{	// don't show the corpse looking around...
				dcl->angles[0] = 0;
				dcl->angles[1] = ent->v.angles[1];
				dcl->angles[2] = 0;
			}

			dcl->skinnum = ent->v.skin;
			dcl->effects = ent->v.effects;
			dcl->weaponframe = ent->v.weaponframe;
			dcl->model = ent->v.modelindex;
			dcl->sec = sv.time - cl->localtime;
			dcl->frame = ent->v.frame;
			dcl->flags = 0;
			dcl->cmdtime = cl->localtime;
			dcl->fixangle = demo.fixangle[j];
			demo.fixangle[j] = 0;

			if (ent->v.health <= 0)
				dcl->flags |= DF_DEAD;
			if (ent->v.mins[2] != -24)
				dcl->flags |= DF_GIB;
			continue;
		}


		// ZOID visibility tracking
		if (ent != clent &&
		        !(client->spec_track && client->spec_track - 1 == j))
		{
			if (cl->spectator)
				continue;

			// ignore if not touching a PV leaf
			for (i=0 ; i < ent->num_leafs ; i++)
				if (pvs[ent->leafnums[i] >> 3] & (1 << (ent->leafnums[i]&7) ))
					break;
			if (i == ent->num_leafs)
				continue; // not visable
		}

		if (disable_updates && client != cl)
		{ // Vladis
			continue;
		}

		pflags = PF_MSEC | PF_COMMAND;

		if (ent->v.modelindex != sv_playermodel)
			pflags |= PF_MODEL;
		for (i=0 ; i<3 ; i++)
			if (ent->v.velocity[i])
				pflags |= PF_VELOCITY1<<i;
		if (ent->v.effects)
			pflags |= PF_EFFECTS;
		if (ent->v.skin)
			pflags |= PF_SKINNUM;
		if (ent->v.health <= 0)
			pflags |= PF_DEAD;
		if (ent->v.mins[2] != -24)
			pflags |= PF_GIB;

		if (cl->spectator)
		{	// only sent origin and velocity to spectators
			pflags &= PF_VELOCITY1 | PF_VELOCITY2 | PF_VELOCITY3;
		}
		else if (ent == clent)
		{	// don't send a lot of data on personal entity
			pflags &= ~(PF_MSEC|PF_COMMAND);
			if (ent->v.weaponframe)
				pflags |= PF_WEAPONFRAME;

			if ((int)sv_sendcmd.value)
				pflags |= PF_COMMAND;	// xavior: I want to send vwep info for third-person viewing
		}

		// Z_EXT_PM_TYPE protocol extension
		// encode pm_type and jump_held into pm_code
		pm_type = SV_PMTypeForClient (cl);
		switch (pm_type)
		{
			case PM_DEAD:
				pm_code = PMC_NORMAL; // plus PF_DEAD
				break;
			case PM_NORMAL:
				pm_code = PMC_NORMAL;
				if (cl->jump_held)
					pm_code = PMC_NORMAL_JUMP_HELD;
				break;
			case PM_OLD_SPECTATOR:
				pm_code = PMC_OLD_SPECTATOR;
				break;
			case PM_SPECTATOR:
				pm_code = PMC_SPECTATOR;
				break;
			case PM_FLY:
				pm_code = PMC_FLY;
				break;
			case PM_NONE:
				pm_code = PMC_NONE;
				break;
			case PM_FREEZE:
				pm_code = PMC_FREEZE;
				break;
			default:
				assert (false);
		}

		pflags |= pm_code << PF_PMC_SHIFT;

		// Z_EXT_PF_ONGROUND protocol extension
		if ((int)ent->v.flags & FL_ONGROUND)
			pflags |= PF_ONGROUND;

		if (client->spec_track && client->spec_track - 1 == j &&
		        ent->v.weaponframe)
			pflags |= PF_WEAPONFRAME;

		MSG_WriteByte (msg, svc_playerinfo);
		MSG_WriteByte (msg, j);
		MSG_WriteShort (msg, pflags);

		for (i=0 ; i<3 ; i++)
			MSG_WriteCoord (msg, ent->v.origin[i]);

		MSG_WriteByte (msg, ent->v.frame);

		if (pflags & PF_MSEC)
		{
			msec = 1000*(sv.time - cl->localtime);
			if (msec > 255)
				msec = 255;
			MSG_WriteByte (msg, msec);
		}

		if (pflags & PF_COMMAND)
		{
			cmd = cl->lastcmd;

			if (ent->v.health <= 0)
			{	// don't show the corpse looking around...
				cmd.angles[0] = 0;
				cmd.angles[1] = ent->v.angles[1];
				cmd.angles[0] = 0;
			}

			cmd.buttons = 0;	// never send buttons
			cmd.impulse = 0;	// never send impulses

#ifdef VWEP_TEST
			// @@VWep test
			/*if ((client->extensions & Z_EXT_VWEP) && sv.vw_model_name[0]
					&& fofs_vw_index && fofs_vw_frame) {
				cmd.impulse = EdictFieldFloat (ent, fofs_vw_index);
				cmd.msec = EdictFieldFloat (ent, fofs_vw_frame);
			}*/
			//if (strcmp(PR_GetString(ent->v.model), "progs/eyes.mdl"))
			if ((client->extensions & Z_EXT_VWEP) && sv.vw_model_name[0]
					&& fofs_vw_index) {
				cmd.impulse = EdictFieldFloat (ent, fofs_vw_index);
			}
#endif

			MSG_WriteDeltaUsercmd (msg, &nullcmd, &cmd);
		}

		for (i=0 ; i<3 ; i++)
			if (pflags & (PF_VELOCITY1<<i) )
				MSG_WriteShort (msg, ent->v.velocity[i]);

		if (pflags & PF_MODEL)
			MSG_WriteByte (msg, ent->v.modelindex);

		if (pflags & PF_SKINNUM)
			MSG_WriteByte (msg, ent->v.skin);

		if (pflags & PF_EFFECTS)
			MSG_WriteByte (msg, ent->v.effects);

		if (pflags & PF_WEAPONFRAME)
			MSG_WriteByte (msg, ent->v.weaponframe);
	}
}


/*
=============
SV_WriteEntitiesToClient

Encodes the current state of the world as
a svc_packetentities messages and possibly
a svc_nails message and
svc_playerinfo messages
=============
*/
#define PEXT_MODELDBL			0x00001000
#define PEXT_ENTITYDBL			0x00002000	//max of 1024 ents instead of 512
#define PEXT_ENTITYDBL2			0x00004000	//max of 1024 ents instead of 512
void SV_WriteEntitiesToClient (client_t *client, sizebuf_t *msg, qbool recorder)
{
	int e, i, max_packet_entities;
	packet_entities_t *pack;
	client_frame_t *frame;
	entity_state_t *state;

#define DEPTHOPTIMISE
#ifdef DEPTHOPTIMISE
	vec3_t norg;
	float distances[MAX_EXTENDED_PACKET_ENTITIES];
	float dist;
#endif

	edict_t	*clent;
	client_t *cl;
	edict_t *ent;
	vec3_t org;
	byte *pvs;

	if (client->fteprotocolextensions & PEXT_256PACKETENTITIES)
		if (doom_map)
			max_packet_entities = 64;		// sprites don't like > 64 for some reason :(
		else
			max_packet_entities = 256;
	else
		max_packet_entities = MAX_PACKET_ENTITIES;

	// this is the frame we are creating
	frame = &client->frames[client->netchan.incoming_sequence & UPDATE_MASK];

	// find the client's PVS
	clent = client->edict;
	pvs = NULL;
	if (!recorder)
	{
		VectorAdd (clent->v.origin, clent->v.view_ofs, org);
		pvs = CM_FatPVS (org);
	}
	else
	{
		max_packet_entities = MAX_DEMO_PACKET_ENTITIES;

		for (i=0, cl=svs.clients ; i<MAX_CLIENTS ; i++,cl++)
		{
			if (cl->state != cs_spawned)
				continue;

			if (cl->spectator)
				continue;

			VectorAdd (cl->edict->v.origin, cl->edict->v.view_ofs, org);

			// disconnect --> "is it correct?"
			//if (pvs == NULL)
				pvs = CM_FatPVS (org);
			//else
				//	SV_AddToFatPVS (org, sv.worldmodel->nodes, false);
			// <-- disconnect
		}
	}
	if (clent && client->disable_updates_stop > realtime)
	{ // Vladis
		int where = TruePointContents(clent->v.origin); // server flash should not work underwater
		disable_updates = !ISUNDERWATER(where);
	}
	else
	{
		disable_updates = false;
	}

#ifdef DEPTHOPTIMISE
		distances[0] = 0;
#endif

	// send over the players in the PVS
	SV_WritePlayersToClient (client, clent, pvs, msg);

	// put other visible entities into either a packet_entities or a nails message
	pack = &frame->entities;
	pack->num_entities = 0;

	numnails = 0;

	if (!disable_updates)
	{// Vladis, server flash

		// QW protocol can only handle 512 entities. Any entity with number >= 512 will be invisible
		// From ZQuake.
		// max_edicts = min(sv.num_edicts, MAX_EDICTS);

		for (e = MAX_CLIENTS + 1, ent = EDICT_NUM(e); e < sv.num_edicts/*max_edicts*/; e++, ent = NEXT_EDICT(ent))
		{
			// ignore ents without visible models
			if (!ent->v.modelindex || !*
#ifdef USE_PR2
			        PR2_GetString(ent->v.model)
#else
					PR_GetString(ent->v.model)
#endif
			   )
				continue;

			if (!(int)sv_demoNoVis.value || !recorder)
			{
				// ignore if not touching a PV leaf
				for (i=0 ; i < ent->num_leafs ; i++)
					if (pvs[ent->leafnums[i] >> 3] & (1 << (ent->leafnums[i]&7) ))
						break;

				if (i == ent->num_leafs)
					continue;		// not visible
			}

			if (SV_AddNailUpdate (ent))
				continue; // added to the special update list

			if (e >= 512)
			{
				if (!(client->fteprotocolextensions & PEXT_ENTITYDBL))
				{
					continue;
				}
				else if (e >= 1024)
				{
					if (!(client->fteprotocolextensions & PEXT_ENTITYDBL2))
						continue;
					else if (e >= 2048)
						continue;
				}
			}
			// Ok this should fix the game spawning the map instead of invis model where precache'd models >= 256
			if (ent->v.modelindex >= 256 && !(client->fteprotocolextensions & PEXT_MODELDBL))
				continue;

#ifdef DEPTHOPTIMISE
		if (clent)
		{
			//find distance based upon absolute mins/maxs so bsps are treated fairly.
			//norg = clentnorg + -0.5*(max+min)
			VectorAdd(ent->v.absmin, ent->v.absmax, norg);
			VectorMA(clent->v.origin, -0.5, norg, norg);
			dist = DotProduct(norg, norg);	//Length

			// add to the packetentities
			if (pack->num_entities == max_packet_entities/*pack->max_entities*/)
			{
				float furthestdist = -1;
				int best=-1;
				for (i = 0; i < max_packet_entities/*pack->max_entities*/; i++)
					if (furthestdist < distances[i])
					{
						furthestdist = distances[i];
						best = i;
					}

				if (furthestdist > dist && best != -1)
				{
					state = &pack->entities[best];
	//				Con_Printf("Dropping ent %s\n", sv.model_precache[state->modelindex]);
					memmove(&distances[best], &distances[best+1], sizeof(*distances)*(pack->num_entities-best-1));
					memmove(state, state+1, sizeof(*state)*(pack->num_entities-best-1));

					best = pack->num_entities-1;

					distances[best] = dist;
					state = &pack->entities[best];
				}
				else
					continue;	// all full
			}
			else
			{
				state = &pack->entities[pack->num_entities];
				distances[pack->num_entities] = dist;
				pack->num_entities++;
			}
		}
		else
#endif

		{
			// add to the packetentities
			if (pack->num_entities == max_packet_entities)//pack->max_entities)// max_packet_entities)
				continue;	// all full

			state = &pack->entities[pack->num_entities];
			pack->num_entities++;
		}
			state->number = e;
			state->flags = 0;
			VectorCopy (ent->v.origin, state->origin);
			VectorCopy (ent->v.angles, state->angles);
			state->modelindex = ent->v.modelindex;
			state->frame = ent->v.frame;
			state->colormap = ent->v.colormap;
			state->skinnum = ent->v.skin;
			state->effects = ent->v.effects;
#ifdef PEXT_TRANS
			state->trans = ent->v.alpha*255;//trans;
#endif
		}
#ifdef PEXT_TRANS
		state->trans = ent->v.alpha*255;
		if (!ent->v.alpha)
			state->trans = 255;

		//QSG_DIMENSION_PLANES - if the only shared dimensions are ghost dimensions, Set half alpha.
		if (client->edict)
			if (((int)client->edict->v.dimension_see & (int)ent->v.dimension_ghost))
				if (!((int)client->edict->v.dimension_see & ((int)ent->v.dimension_seen & ~(int)ent->v.dimension_ghost)) )
				{
					if (ent->v.dimension_ghost_alpha)
						state->trans *= ent->v.dimension_ghost_alpha;
					else
						state->trans *= 0.5;
				}
#endif
	} // server flash
	// encode the packet entities as a delta from the
	// last packetentities acknowledged by the client

	SV_EmitPacketEntities (client, pack, msg);

	// now add the specialized nail update
	SV_EmitNailUpdate (msg, recorder);
}

void SV_PreWriteDelta (entity_state_t *from, entity_state_t *to, sizebuf_t *msg, qbool force, unsigned int protext)
{
	SV_WriteDelta (from, to, msg, force, protext);
}

// More extension stuff:
// byte = bound(0, s->alpha * 255, 255)
#define E5_ALPHA (1<<9)