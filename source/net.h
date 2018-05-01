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

	$Id: net.h 705 2007-10-02 23:54:14Z disconn3ct $
*/

// net.h -- quake's interface to the networking layer
#ifndef __NET_H__
#define __NET_H__

#ifdef _WIN32
#include <Winsock2.h>

#define EWOULDBLOCK		WSAEWOULDBLOCK
#define EMSGSIZE		WSAEMSGSIZE
#define ECONNRESET		WSAECONNRESET
#define ECONNABORTED	WSAECONNABORTED
#define ECONNREFUSED	WSAECONNREFUSED
#define EADDRNOTAVAIL	WSAEADDRNOTAVAIL
#define EAFNOSUPPORT	WSAEAFNOSUPPORT

#define qerrno	WSAGetLastError()
#else //_WIN32
#define qerrno	errno

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <arpa/inet.h>
#include <unistd.h>

#ifdef __linux__
#include <netinet/tcp.h>
#endif

#ifdef __sun__
#include <sys/filio.h>
#endif //__sun__

#ifdef NeXT
#include <libc.h>
#endif //NeXT

#define closesocket	close
#define ioctlsocket	ioctl
#endif //_WIN32


#include <errno.h>

#ifndef INVALID_SOCKET
#define INVALID_SOCKET	-1
#endif

#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif

#ifdef _WIN32
typedef int socklen_t;
#endif

#define	PORT_ANY -1

typedef enum {NA_INVALID, NA_IP} netadrtype_t;

typedef struct
{
	netadrtype_t	type;

	union {
		byte	ip[4];
		byte	ip6[16];
		byte	ipx[10];
	} ip;

	unsigned short	port;
} netadr_t;

struct sockaddr_qstorage
{
	short dontusesa_family;
	unsigned char dontusesa_pad[6];
#if defined(_MSC_VER) || defined(MINGW)
	__int64 sa_align;
#elif defined(__GNUC__)
	long long sa_align;
#else
	int sa_align[2];
#endif
	unsigned char sa_pad2[112];
};

extern	netadr_t	net_local_adr;
extern	netadr_t	net_from;		// address of who sent the packet
extern	sizebuf_t	net_message;

extern	cvar_t	hostname;

extern	int	net_socket;
extern	int	net_telnetsocket;
extern	int	telnetport;
extern	int	sv_port;
extern	int	telnet_iosock;
extern	qbool	telnet_connected;
extern	cvar_t	not_auth_timeout;
extern	cvar_t	auth_timeout;

void	NET_Init (int *serverport, int *telnetport);
void	NET_Shutdown (void);
int		NET_GetPacket (void);
void	NET_SendPacket (int length, const void *data, netadr_t to);
//qbool	NET_Sleep ();

qbool	NET_CompareAdr (const netadr_t a, const netadr_t b);
qbool	NET_CompareBaseAdr (const netadr_t a, const netadr_t b);
char	*NET_AdrToString (const netadr_t a);
char	*NET_BaseAdrToString (const netadr_t a);
qbool	NET_StringToAdr (const char *s, netadr_t *a);

// I turned this off atm, probably cause problems
//#define SOCKET_CLOSE_TIME 30

qbool	TCP_Set_KEEPALIVE(int sock);

//============================================================================

#define	OLD_AVG		0.99		// total = oldtotal*OLD_AVG + new*(1-OLD_AVG)

#define	MAX_LATENT	32

typedef struct
{
	qbool		fatal_error;

	int			dropped; // between last packet and previous

	float		last_received; // for timeouts

	// the statistics are cleared at each client begin, because
	// the server connecting process gives a bogus picture of the data
	float		frame_latency; // rolling average
	float		frame_rate;

	int			drop_count;	// dropped packets, cleared each level
	int			good_count; // cleared each level

	netadr_t	remote_address;
	int			qport;

	// bandwidth estimator
	double		cleartime; // if realtime > nc->cleartime, free to go
	double		rate; // seconds / byte

	// sequencing variables
	int			incoming_sequence;
	int			incoming_acknowledged;
	int			incoming_reliable_acknowledged; // single bit

	int			incoming_reliable_sequence; // single bit, maintained local

	int			outgoing_sequence;
	int			reliable_sequence; // single bit
	int			last_reliable_sequence;	// sequence number of last send

	// reliable staging and holding areas
	sizebuf_t	message; // writing buffer to send to server
	byte		message_buf[MAX_MSGLEN];

	int			reliable_length;
	byte		reliable_buf[MAX_MSGLEN]; // unacked reliable message

	// time and size data to calculate bandwidth
	int			outgoing_size[MAX_LATENT];
	double		outgoing_time[MAX_LATENT];
} netchan_t;

void Netchan_Init (void);
void Netchan_Transmit (netchan_t *chan, int length, byte *data);
void Netchan_OutOfBand (netadr_t adr, int length, byte *data);
void Netchan_OutOfBandPrint (netadr_t adr, const char *format, ...);
qbool Netchan_Process (netchan_t *chan);
void Netchan_Setup (netchan_t *chan, netadr_t adr, int qport);

qbool Netchan_CanPacket (netchan_t *chan);
qbool Netchan_CanReliable (netchan_t *chan);

void SockadrToNetadr (const struct sockaddr_qstorage *s, netadr_t *a);

#endif /* !__NET_H__ */
