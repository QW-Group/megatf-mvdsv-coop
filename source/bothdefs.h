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

    $Id: bothdefs.h,v 1.23 2006/08/10 10:34:49 vvd0 Exp $
*/

// defs common to client and server

#ifndef __BOTHDEFS_H__
#define __BOTHDEFS_H__


#define	MSG_BUF_SIZE	8192

// MAX_MVD_SIZE - max size of single mvd message _WITHOUT_ header
#define	MAX_MVD_SIZE			(MSG_BUF_SIZE - 100)

#define	MINIMUM_MEMORY	0x550000


#define	MAX_QPATH		64		// max length of a quake game pathname
#define	MAX_OSPATH		128		// max length of a filesystem pathname

#define	MAX_MSGLEN		1450		// max length of a reliable message
#define	MAX_DATAGRAM		1450		// max length of unreliable message
#define	FILE_TRANSFER_BUF_SIZE	(MAX_MSGLEN - 100)
//
// per-level limits
//
#define	MAX_EDICTS		2048//1024/*512*/		// FIXME: ouch! ouch! ouch! - trying to fix...
#define	MAX_LIGHTSTYLES		64
#define	MAX_MODELS		512		// these are sent over the net as bytes
#define VWEP_TEST
#ifdef VWEP_TEST
#define MAX_VWEP_MODELS		32		// could be increased to 256
#endif
#define	MAX_SOUNDS		256		// so they cannot be blindly increased

#define	MAX_STYLESTRING		64

//
// stats are integers communicated to the client by the server
//
#define	MAX_CL_STATS		32
#define	STAT_HEALTH		0
//define	STAT_FRAGS		1
#define	STAT_WEAPON		2
#define	STAT_AMMO		3
#define	STAT_ARMOR		4
//define	STAT_WEAPONFRAME	5
#define	STAT_SHELLS		6
#define	STAT_NAILS		7
#define	STAT_ROCKETS		8
#define	STAT_CELLS		9
#define	STAT_ACTIVEWEAPON	10
#define	STAT_TOTALSECRETS	11
#define	STAT_TOTALMONSTERS	12
#define	STAT_SECRETS		13		// bumped on client side by svc_foundsecret
#define	STAT_MONSTERS		14		// bumped by svc_killedmonster
#define	STAT_ITEMS		15
#define	STAT_VIEWHEIGHT		16		// Z_EXT_VIEWHEIGHT extension
#define STAT_TIME		17		// Z_EXT_TIME extension


//
// item flags
//
#define	IT_SHOTGUN		1
#define	IT_SUPER_SHOTGUN	2
#define	IT_NAILGUN		4
#define	IT_SUPER_NAILGUN	8

#define	IT_GRENADE_LAUNCHER	16
#define	IT_ROCKET_LAUNCHER	32
#define	IT_LIGHTNING		64
#define	IT_SUPER_LIGHTNING	128

#define	IT_SHELLS		256
#define	IT_NAILS		512
#define	IT_ROCKETS		1024
#define	IT_CELLS		2048

#define	IT_AXE			4096

#define	IT_ARMOR1		8192
#define	IT_ARMOR2		16384
#define	IT_ARMOR3		32768

#define	IT_SUPERHEALTH		65536

#define	IT_KEY1			131072
#define	IT_KEY2			262144

#define	IT_INVISIBILITY		524288

#define	IT_INVULNERABILITY	1048576
#define	IT_SUIT			2097152
#define	IT_QUAD			4194304

#define	IT_SIGIL1		(1<<28)

#define	IT_SIGIL2		(1<<29)
#define	IT_SIGIL3		(1<<30)
#define	IT_SIGIL4		(1<<31)

//
// print flags
//
#define	PRINT_LOW		0		// pickup messages
#define	PRINT_MEDIUM		1		// death messages
#define	PRINT_HIGH		2		// critical messages
#define	PRINT_CHAT		3		// chat messages


#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif

#define bound(a,b,c) ((a) >= (c) ? (a) : \
					(b) < (a) ? (a) : (b) > (c) ? (c) : (b))

typedef unsigned char byte;

// KJB Undefined true and false defined in SciTech's DEBUG.H header
#undef true
#undef false

#ifndef __cplusplus
typedef enum qbool_e {false, true} qbool;
#else
typedef bool qbool;
#endif

//#define	MAX_INFO_STRING		196
//#define	MAX_SERVERINFO_STRING	512
#define	MAX_INFO_STRING		1280				// XavioR: Hopefully stop DP clients from crashing the server
#define	MAX_SERVERINFO_STRING	1280			////
#define	MAX_LOCALINFO_STRING	32768
#define	MAX_KEY_STRING		64

#define	MAX_EXT_INFO_STRING     1024

#ifndef NULL
#define NULL ((void *)0)
#endif

#define MAX_NUM_ARGVS	50

void Host_ClientCommands (char *fmt, ...);

//============================================================================

short	ShortSwap (short s);
int		LongSwap (int l);
float	FloatSwap (float f);

#ifdef __BIG_ENDIAN__Q__
#define BigShort(x)		(x)
#define BigLong(x)		(x)
#define BigFloat(x)		(x)
#define LittleShort(x)	ShortSwap(x)
#define LittleLong(x)	LongSwap(x)
#define LittleFloat(x)	FloatSwap(x)
#elif defined(__LITTLE_ENDIAN__Q__)
#define BigShort(x)		ShortSwap(x)
#define BigLong(x)		LongSwap(x)
#define BigFloat(x)		FloatSwap(x)
#define LittleShort(x)	(x)
#define LittleLong(x)	(x)
#define LittleFloat(x)	(x)
#elif defined(__PDP_ENDIAN__Q__)
int		LongSwapPDP2Big (int l);
int		LongSwapPDP2Lit (int l);
float	FloatSwapPDP2Big (float f);
float	FloatSwapPDP2Lit (float f);
#define BigShort(x)		ShortSwap(x)
#define BigLong(x)		LongSwapPDP2Big(x)
#define BigFloat(x)		FloatSwapPDP2Big(x)
#define LittleShort(x)	(x)
#define LittleLong(x)	LongSwapPDP2Lit(x)
#define LittleFloat(x)	FloatSwapPDP2Lit(x)
#else
#error Unknown byte order type!
#endif

//============================================================================

#ifdef _WIN32
#define strcasecmp(s1, s2) _stricmp  ((s1),   (s2))
#define strncasecmp(s1, s2, n) _strnicmp ((s1),   (s2),   (n))
int		snprintf(char *str, size_t n, char const *fmt, ...);
//int		vsnprintf(char *buffer, size_t count, const char *format, va_list argptr);
#define vsnprintf(a, b, c, d) vsnprintf_s(a, b, _TRUNCATE, c, d)
#endif

#if defined(__linux__) || defined(_WIN32)
size_t	strlcpy (char *dst, const char *src, size_t siz);
size_t	strlcat (char *dst, const char *src, size_t siz);
#endif
#if !defined(__FreeBSD__) && !defined(__APPLE__) && !defined(__DragonFly__)
char	*strnstr (char *s, char *find, size_t slen);
char	*strcasestr(const char *s, const char *find);
#endif
int		strchrn(char* str, char c);

int		Q_atoi (char *str);
float	Q_atof (char *str);
//char	*Q_ftos (float value);
#define Q_rint(x) ((x) > 0 ? (int)((x) + 0.5) : (int)((x) - 0.5))

// does a varargs printf into a temp buffer

#define MAX_STRINGS 16 // well, this used not only for va, anyway, static buffers is evil...

char	*va(char *format, ...);

void	*Q_malloc (size_t size);
#define	Q_free(ptr)	free(ptr)

void	COM_StripExtension (char *in, char *out);
char	*COM_FileExtension (char *in);
void	COM_DefaultExtension (char *path, char *extension);

float	adjustangle (const float current, const float ideal, const float fraction);

#endif /* !__BOTHDEFS_H__ */

#define PROGRAM		"mdvsv"
#define SV_VERSION	"1.41"

#define VOICECHAT