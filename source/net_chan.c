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

   $Id: net_chan.c,v 1.16 2007/01/14 19:17:57 tonik Exp $
*/

#include "qwsvdef.h"

#define	PACKET_HEADER 8

/*

packet header
-------------
31  sequence
1   does this message contain a reliable payload
31  acknowledge sequence
1   acknowledge receipt of even/odd message
16  qport

The remote connection never knows if it missed a reliable message, the
local side detects that it has been dropped by seeing a sequence acknowledge
higher than the last reliable sequence, but without the correct evon/odd
bit for the reliable set.

If the sender notices that a reliable message has been dropped, it will be
retransmitted.  It will not be retransmitted again until a message after
the retransmit has been acknowledged and the reliable still failed to get there.

If the sequence number is -1, the packet should be handled without a netcon.

The reliable message can be added to at any time by doing
MSG_Write* (&netchan->message, <data>).

If the message buffer is overflowed, either by a single message, or by
multiple frames worth piling up while the last reliable transmit goes
unacknowledged, the netchan signals a fatal error.

Reliable messages are always placed first in a packet, then the unreliable
message is included if there is sufficient room.

To the receiver, there is no distinction between the reliable and unreliable
parts of the message, they are just processed out as a single larger message.

Illogical packet sequence numbers cause the packet to be dropped, but do
not kill the connection.  This, combined with the tight window of valid
reliable acknowledgement numbers provides protection against malicious
address spoofing.

The qport field is a workaround for bad address translating routers that
sometimes remap the client's source port on a packet during gameplay.

If the base part of the net address matches and the qport matches, then the
channel matches even if the IP port differs.  The IP port should be updated
to the new value before sending out any replies.


*/

cvar_t showpackets = {"showpackets", "0"};
cvar_t showdrop = {"showdrop", "0"};
cvar_t qport = {"qport", "0"};

/*
===============
Netchan_Init

===============
*/
void Netchan_Init (void)
{
	int port = 0xffff;

	// pick a port value that should be nice and random
	srand((unsigned)time(NULL));
	port &= rand();
	/*
	#ifdef _WIN32
		port = ((int)(timeGetTime()*1000) * time(NULL)) & 0xffff;
	#else
		port = ((int)(getpid()+getuid()*1000) * time(NULL)) & 0xffff;
	#endif
	*/
	Cvar_Register (&showpackets);
	Cvar_Register (&showdrop);
	Cvar_Register (&qport);
	Cvar_SetValue (&qport, port);
}

/*
===============
Netchan_OutOfBand

Sends an out-of-band datagram
================
*/
void Netchan_OutOfBand (netadr_t adr, int length, byte *data)
{
	sizebuf_t send1;
	byte send_buf[MAX_MSGLEN + PACKET_HEADER];

	// write the packet header
	send1.data = send_buf;
	send1.maxsize = sizeof(send_buf);
	send1.cursize = 0;

	MSG_WriteLong (&send1, -1);	// -1 sequence means out of band
	SZ_Write (&send1, data, length);

	// send the datagram
	NET_SendPacket (send1.cursize, send1.data, adr);
}

/*
===============
Netchan_OutOfBandPrint

Sends a text message in an out-of-band datagram
================
*/
void Netchan_OutOfBandPrint (netadr_t adr, const char *format, ...)
{
	char string[8192];
	va_list argptr;

	va_start (argptr, format);
	vsnprintf (string, sizeof (string), format, argptr);
	va_end (argptr);

	Netchan_OutOfBand (adr, strlen (string), (byte *) string);
}


/*
==============
Netchan_Setup

called to open a channel to a remote system
==============
*/
void Netchan_Setup (netchan_t *chan, netadr_t adr, int qport1)
{
	memset (chan, 0, sizeof(*chan));

	chan->remote_address = adr;
	chan->last_received = realtime;
	chan->message.data = chan->message_buf;
	chan->message.allowoverflow = true;
	chan->message.maxsize = sizeof(chan->message_buf);
	chan->qport = qport1;
	chan->rate = 1.0/2500;
}


/*
===============
Netchan_CanPacket

Returns true if the bandwidth choke isn't active
================
*/
#define	MAX_BACKUP	400
qbool Netchan_CanPacket (netchan_t *chan)
{
	if (chan->cleartime < realtime + MAX_BACKUP*chan->rate)
		return true;
	return false;
}


/*
===============
Netchan_CanReliable

Returns true if the bandwidth choke isn't 
================
*/
qbool Netchan_CanReliable (netchan_t *chan)
{
	if (chan->reliable_length)
		return false; // waiting for ack
	return Netchan_CanPacket (chan);
}

/*
===============
Netchan_Transmit

tries to send an unreliable message to a connection, and handles the
transmition / retransmition of the reliable messages.

A 0 length will still generate a packet and deal with the reliable messages.
================
*/
void Netchan_Transmit (netchan_t *chan, int length, byte *data)
{
	sizebuf_t send1;
	byte send_buf[MAX_MSGLEN + PACKET_HEADER];
	qbool send_reliable;
	unsigned w1, w2;
	int i;
	static double last_error_time = 0;
	double current_time;

	// check for message overflow
	current_time = Sys_DoubleTime();
	if (chan->message.overflowed)
	{
		chan->fatal_error = true;
		if (last_error_time - current_time > 5.0 || (int)developer.value)
		{
			Con_Printf ("%s:Outgoing message overflow\n"
			            , NET_AdrToString (chan->remote_address));
			last_error_time = current_time;
		}
		return;
	}

	// if the remote side dropped the last reliable message, resend it
	send_reliable = false;

	if (chan->incoming_acknowledged > chan->last_reliable_sequence
	        && chan->incoming_reliable_acknowledged != chan->reliable_sequence)
		send_reliable = true;

	// if the reliable transmit buffer is empty, copy the current message out
	if (!chan->reliable_length && chan->message.cursize)
	{
		memcpy (chan->reliable_buf, chan->message_buf, chan->message.cursize);
		chan->reliable_length = chan->message.cursize;
		chan->message.cursize = 0;
		chan->reliable_sequence ^= 1;
		send_reliable = true;
	}

	// write the packet header
	send1.data = send_buf;
	send1.maxsize = sizeof(send_buf);
	send1.cursize = 0;

	w1 = chan->outgoing_sequence | (send_reliable<<31);
	w2 = chan->incoming_sequence | (chan->incoming_reliable_sequence<<31);

	chan->outgoing_sequence++;

	MSG_WriteLong (&send1, w1);
	MSG_WriteLong (&send1, w2);

	// copy the reliable message to the packet first
	if (send_reliable)
	{
		SZ_Write (&send1, chan->reliable_buf, chan->reliable_length);
		chan->last_reliable_sequence = chan->outgoing_sequence;
	}

	// add the unreliable part if space is available
	if (send1.maxsize - send1.cursize >= length)
		SZ_Write (&send1, data, length);

	// send the datagram
	i = chan->outgoing_sequence & (MAX_LATENT-1);
	chan->outgoing_size[i] = send1.cursize;
	chan->outgoing_time[i] = realtime;

	NET_SendPacket (send1.cursize, send1.data, chan->remote_address);

	if (chan->cleartime < realtime)
		chan->cleartime = realtime + send1.cursize*chan->rate;
	else
		chan->cleartime += send1.cursize*chan->rate;

	if (sv.paused)
		chan->cleartime = realtime;

	if ((int)showpackets.value)
		Con_Printf ("--> s=%i(%i) a=%i(%i) %i\n"
		            , chan->outgoing_sequence
		            , send_reliable
		            , chan->incoming_sequence
		            , chan->incoming_reliable_sequence
		            , send1.cursize);

}

/*
=================
Netchan_Process

called when the current net_message is from remote_address
modifies net_message so that it points to the packet payload
=================
*/
qbool Netchan_Process (netchan_t *chan)
{
	unsigned sequence, sequence_ack;
	unsigned reliable_ack, reliable_message;
	int qport1;

	if (!NET_CompareAdr (net_from, chan->remote_address))
		return false;

	// get sequence numbers
	MSG_BeginReading ();
	sequence = MSG_ReadLong ();
	sequence_ack = MSG_ReadLong ();

	// read the qport
	qport1 = MSG_ReadShort ();

	reliable_message = sequence >> 31;
	reliable_ack = sequence_ack >> 31;

	sequence &= ~(1<<31);
	sequence_ack &= ~(1<<31);

	if ((int)showpackets.value)
		Con_Printf ("<-- s=%i(%i) a=%i(%i) %i\n"
		            , sequence
		            , reliable_message
		            , sequence_ack
		            , reliable_ack
		            , net_message.cursize);

	// get a rate estimation
#if 0
	if (chan->outgoing_sequence - sequence_ack < MAX_LATENT)
	{
		int				i;
		double			time, rate;

		i = sequence_ack & (MAX_LATENT - 1);
		time = realtime - chan->outgoing_time[i];
		time -= 0.1;	// subtract 100 ms
		if (time <= 0)
		{	// gotta be a digital link for <100 ms ping
			if (chan->rate > 1.0/5000)
				chan->rate = 1.0/5000;
		}
		else
		{
			if (chan->outgoing_size[i] < 512)
			{	// only deal with small messages
				rate = chan->outgoing_size[i]/time;
				if (rate > 5000)
					rate = 5000;
				rate = 1.0/rate;
				if (chan->rate > rate)
					chan->rate = rate;
			}
		}
	}
#endif

	//
	// discard stale or duplicated packets
	//
	if (sequence <= (unsigned)chan->incoming_sequence)
	{
		if ((int)showdrop.value)
			Con_Printf ("%s:Out of order packet %i at %i\n"
			            , NET_AdrToString (chan->remote_address)
			            ,  sequence
			            , chan->incoming_sequence);
		return false;
	}

	//
	// dropped packets don't keep the message from being used
	//
	chan->dropped = sequence - (chan->incoming_sequence+1);
	if (chan->dropped > 0)
	{
		chan->drop_count += 1;

		if ((int)showdrop.value)
			Con_Printf ("%s:Dropped %i packets at %i\n"
			, NET_AdrToString (chan->remote_address)
			, chan->dropped
			, sequence);
	}

	//
	// if the current outgoing reliable message has been acknowledged
	// clear the buffer to make way for the next
	//
	if (reliable_ack == (unsigned)chan->reliable_sequence)
		chan->reliable_length = 0;	// it has been received

	//
	// if this message contains a reliable message, bump incoming_reliable_sequence
	//
	chan->incoming_sequence = sequence;
	chan->incoming_acknowledged = sequence_ack;
	chan->incoming_reliable_acknowledged = reliable_ack;
	if (reliable_message)
		chan->incoming_reliable_sequence ^= 1;

	//
	// the message can now be read from the current message pointer
	// update statistics counters
	//
	chan->frame_latency = chan->frame_latency*OLD_AVG
		+ (chan->outgoing_sequence-sequence_ack)*(1.0-OLD_AVG);
	chan->frame_rate = chan->frame_rate*OLD_AVG
		+ (realtime-chan->last_received)*(1.0-OLD_AVG);
	chan->good_count += 1;

	chan->last_received = realtime;

	return true;
}
