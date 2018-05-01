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

    1$Id: common.h,v 1.23 2007/01/04 07:38:12 qqshka Exp $
*/
// common.h  -- general definitions
typedef unsigned char 		qbyte;

#define SPECTATOR				1
#define STEALTH					2

#ifndef __COMMON_H__
#define __COMMON_H__

//============================================================================
typedef struct sizebuf_s
{
	qbool	allowoverflow;	// if false, do a Sys_Error
	qbool	overflowed;	// set to true if the buffer size failed
	byte	*data;
	int	maxsize;
	int	cursize;
} sizebuf_t;

void SZ_Clear (sizebuf_t *buf);
void SZ_InitEx (sizebuf_t *buf, byte *data, const int length, qbool allowoverflow);
void SZ_Init (sizebuf_t *buf, byte *data, const int length);
void *SZ_GetSpace (sizebuf_t *buf, const int length);
void SZ_Write (sizebuf_t *buf, const void *data, int length);
void SZ_Print (sizebuf_t *buf, const char *data);

//============================================================================

typedef struct link_s
{
	struct link_s	*prev, *next;
} link_t;


//============================================================================

struct usercmd_s;

extern struct usercmd_s nullcmd;

void MSG_WriteChar (sizebuf_t *sb, const int c);
void MSG_WriteByte (sizebuf_t *sb, const int c);
void MSG_WriteShort (sizebuf_t *sb, const int c);
void MSG_WriteLong (sizebuf_t *sb, const int c);
void MSG_WriteFloat (sizebuf_t *sb, const float f);
void MSG_WriteString (sizebuf_t *sb, const char *s);
void MSG_WriteCoord (sizebuf_t *sb, const float f);
void MSG_WriteAngle (sizebuf_t *sb, const float f);
void MSG_WriteAngle16 (sizebuf_t *sb, const float f);
void MSG_WriteDeltaUsercmd (sizebuf_t *sb, const struct usercmd_s *from, const struct usercmd_s *cmd);

extern int msg_readcount;
extern qbool msg_badread; // set if a read goes beyond end of message

void MSG_BeginReading (void);
int MSG_GetReadCount(void);
int MSG_ReadChar (void);
int MSG_ReadByte (void);
int MSG_ReadShort (void);
int MSG_ReadLong (void);
float MSG_ReadFloat (void);
char *MSG_ReadString (void);
char *MSG_ReadStringLine (void);

float MSG_ReadCoord (void);
float MSG_ReadAngle (void);
float MSG_ReadAngle16 (void);
void MSG_ReadDeltaUsercmd (struct usercmd_s *from, struct usercmd_s *cmd);

#define Q_memset(d, f, c) memset((d), (f), (c))
#define Q_memcpy(d, s, c) memcpy((d), (s), (c))
#define Q_memcmp(m1, m2, c) memcmp((m1), (m2), (c))
#define Q_strcpy(d, s) strcpy((d), (s))
#define Q_strncpy(d, s, n) strncpy((d), (s), (n))
#define Q_strlen(s) ((int)strlen(s))
#define Q_strrchr(s, c) strrchr((s), (c))
#define Q_strcat(d, s) strcat((d), (s))
#define Q_strcmp(s1, s2) strcmp((s1), (s2))
#define Q_strncmp(s1, s2, n) strncmp((s1), (s2), (n))
#define Q_bzero(a, b) memset((a), 0, (b))

#define Q_strcasecmp(s1, s2) _stricmp((s1), (s2))
#define Q_strncasecmp(s1, s2, n) _strnicmp((s1), (s2), (n))

//============================================================================

unsigned char *Q_normalizetext(unsigned char *name); //bliP: red to white text
unsigned char *Q_redtext(unsigned char *str); //bliP: white to red text
unsigned char *Q_yelltext(unsigned char *str); //VVD: white to red text and yellow numbers

//============================================================================

#define MAX_COM_TOKEN	1024
extern char com_token[MAX_COM_TOKEN];
typedef enum {TTP_UNKNOWN, TTP_STRING} com_tokentype_t;

char *COM_Parse (char *data);
char *COM_ParseToken (const char *data, const char *punctuation);

extern int com_argc;
extern char **com_argv;

int COM_CheckParm (char *parm);

void COM_InitArgv (int argc, char **argv);

//============================================================
// Alternative variant manipulation with info strings
//============================================================

#define INFO_HASHPOOL_SIZE 256

#define MAX_CLIENT_INFOS 128

typedef struct info_s {
	struct info_s	*hash_next;
	struct info_s	*next;

	char				*name;
	char				*value;

} info_t;

typedef struct ctxinfo_s {

	info_t	*info_hash[INFO_HASHPOOL_SIZE];
	info_t	*info_list;

	int		cur; // current infos
	int		max; // max    infos

} ctxinfo_t;

// return value for given key
char			*Info_Get(ctxinfo_t *ctx, const char *name);
// set value for given key
qbool			Info_Set (ctxinfo_t *ctx, const char *name, const char *value);
// set value for given star key
qbool			Info_SetStar (ctxinfo_t *ctx, const char *name, const char *value);
// remove given key
qbool			Info_Remove (ctxinfo_t *ctx, const char *name);
// remove all infos
void			Info_RemoveAll (ctxinfo_t *ctx);
// convert old way infostring to new way: \name\qqshka\noaim\1 to hashed variant
//qbool			Info_Convert(ctxinfo_t *ctx, char *str);
// convert new way to old way
//qbool			Info_ReverseConvert(ctxinfo_t *ctx, char *str, int size);
// copy star keys from ont ctx to other
qbool			Info_CopyStar(ctxinfo_t *ctx_from, ctxinfo_t *ctx_to);
// just print all key value pairs
void			Info_PrintList(ctxinfo_t *ctx);

//============================================================================

char *Info_ValueForKey (char *s, const char *key);
char *Info_KeyNameForKeyNum (char *s, int key);
void Info_RemoveKey (char *s, char *key);
void Info_RemovePrefixedKeys (char *start, char prefix);
void Info_SetValueForKey (char *s, char *key, char *value, unsigned int maxsize);
void Info_SetValueForStarKey (char *s, char *key, char *value, unsigned int maxsize);
void Info_Print (char *s);
void Info_CopyStarKeys (char *from, char *to);

unsigned Com_BlockChecksum (void *buffer, int length);
void Com_BlockFullChecksum (void *buffer, int len, unsigned char *outbuf);
byte COM_BlockSequenceCRCByte (byte *base, int length, int sequence);

#endif /* !__COMMON_H__ */

void Com_Printf (char *fmt, ...);
void *Z_Malloc (int size);
char *Z_CopyString (char *in);

#define GENERIC_BUFFER			1024

#define MAX_PRINT_STRING 1024
int	doom_map;				// HAAAAACk