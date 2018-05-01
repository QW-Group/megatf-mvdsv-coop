/*
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

    $Id: fs.c,v 1.4 2006/10/26 20:47:13 disconn3ct Exp $
*/

#include "qwsvdef.h"

extern cvar_t	sv_MegaTFHacks;
extern char *Q_stristr (char *strc1, char *strc2);

/*
All of Quake's data access is through a hierchal file system, but the contents
of the file system can be transparently merged from several sources.

The "base directory" is the path to the directory holding the quake.exe and
all game directories.  The sys_* files pass this to host_init in
quakeparms_t->basedir.  This can be overridden with the "-basedir" command
line parm to allow code debugging in a different directory.  The base
directory is only used during filesystem initialization.

The "game directory" is the first tree on the search path and directory that
all generated files (savegames, screenshots, demos, config files) will be
saved to.  This can be overridden with the "-game" command line parameter.
The game directory can never be changed while quake is executing.  This is a
precaution against having a malicious server instruct clients to write files
over areas they shouldn't.
*/


/*
=============================================================================

VARIABLES

=============================================================================
*/


char fs_gamedir[MAX_OSPATH];
char fs_basedir[MAX_OSPATH];

searchpath_t *com_searchpaths = NULL;
searchpath_t *com_base_searchpaths = NULL; // without gamedirs

int fs_filesize;

char gamedirfile[MAX_OSPATH];


/*
=============================================================================

PRIVATE FUNCTIONS

=============================================================================
*/


/*
============
FS_Path_f

============
*/
static void FS_Path_f (void)
{
	searchpath_t *s;

	Con_Printf ("Current search path:\n");
	for (s=com_searchpaths ; s ; s=s->next)
	{
		if (s == com_base_searchpaths) // disconnect: i dont like com_base_searchpaths
			Con_Printf ("----------\n");
		if (s->pack)
			Con_Printf ("%s (%i files)\n", s->pack->filename, s->pack->numfiles);
		else
			Con_Printf ("%s\n", s->filename);
	}
}


/*
================
FS_AddGameDirectory

Sets fs_gamedir, adds the directory to the head of the path,
then loads and adds pak1.pak pak2.pak ...
================
*/
static void FS_AddGameDirectory (char *dir)
{
	int i;
	searchpath_t *search;
	pack_t *pak;
	char pakfile[MAX_OSPATH];
	char *p;

	if ((p = strrchr(dir, '/')) != NULL)
		strlcpy(gamedirfile, ++p, MAX_OSPATH);
	else
		strlcpy(gamedirfile, p, MAX_OSPATH);
	strlcpy (fs_gamedir, dir, MAX_OSPATH);

	// add the directory to the search path
	search = (searchpath_t *) Hunk_Alloc (sizeof(searchpath_t));
	strlcpy (search->filename, dir, MAX_OSPATH);
	search->pack = NULL;
	search->next = com_searchpaths;
	com_searchpaths = search;

	// add any pak files in the format pak0.pak pak1.pak, ...
	for (i=0 ; ; i++)
	{
		snprintf (pakfile, MAX_OSPATH, "%s/pak%i.pak", dir, i);
		pak = COM_LoadPackFile (pakfile);
		if (!pak)
			break;
		search = (searchpath_t *) Hunk_Alloc (sizeof(searchpath_t));
		search->pack = pak;
		search->next = com_searchpaths;
		com_searchpaths = search;
	}

}

/*
================
FS_Init

================
*/
void FS_Init (void)
{
	int i;

	// -basedir <path>
	// Overrides the system supplied base directory (under id1)
	i = COM_CheckParm ("-basedir");
	if (i && i < com_argc-1)
		strlcpy (fs_basedir, com_argv[i + 1], MAX_OSPATH);
	else
		strlcpy (fs_basedir, ".", MAX_OSPATH);

	i = strlen(fs_basedir)-1;
	if ((i >= 0) && (fs_basedir[i]=='/' || fs_basedir[i]=='\\'))
		fs_basedir[i] = '\0';

	// start up with id1 by default
	FS_AddGameDirectory (va("%s/id1", fs_basedir) );
	FS_AddGameDirectory (va("%s/qw", fs_basedir) );

	// any set gamedirs will be freed up to here
	com_base_searchpaths = com_searchpaths;

	FS_Init_Commands();
}

/*
================
FS_Init_Commands

================
*/
void FS_Init_Commands(void)
{
	Cmd_AddCommand ("path", FS_Path_f);
}

/*
================
FS_Shutdown

================
*/
/*
void FS_Shutdown (void)
{
--> disconnect
TODO: fclose all fopen'ed files && free allocated memory
<-- disconnect
}
*/


/*
=============================================================================

MAIN PUBLIC FUNCTIONS

=============================================================================
*/


/*
================
FS_FileLength

================
*/
int FS_FileLength (FILE *f)
{
	int pos;
	int end;

	pos = ftell (f);
	fseek (f, 0, SEEK_END);
	end = ftell (f);
	fseek (f, pos, SEEK_SET);

	return end;
}

/*
============
COM_FileBase
============
*/
void COM_FileBase (char *in, char *out)
{
	char *begin, *end;
	int len;

	if (!(end = strrchr(in, '.')))
		end = in + strlen(in);

	if (!(begin = strchr(in, '/')))
		begin = in;
	else
		begin++;

	len = end - begin + 1;
	if (len < 1)
		strlcpy (out, "?model?", 8);
	else
		strlcpy (out, begin, min(len, MAX_OSPATH));
}

/*
================
COM_FileOpenRead

================
*/
int COM_FileOpenRead (char *path, FILE **hndl)
{
	FILE *f;

	f = fopen(path, "rb");
	if (!f)
	{
		*hndl = NULL;
		return -1;
	}
	*hndl = f;

	return FS_FileLength(f);
}

/*
============
COM_WriteFile

The filename will be prefixed by the current game directory
============
*/
void COM_WriteFile (char *filename, void *data, int len)
{
	FILE *f;
	char name[MAX_OSPATH];

	snprintf (name, MAX_OSPATH, "%s/%s", fs_gamedir, filename);

	f = fopen (name, "wb");
	if (!f)
	{
		Sys_mkdir(fs_gamedir);
		f = fopen (name, "wb");
		if (!f)
			Sys_Error ("Error opening %s", filename);
	}

	Sys_Printf ("COM_WriteFile: %s\n", name);
	fwrite (data, 1, len, f);
	fclose (f);
}


/*
============
COM_CreatePath

Only used for CopyFile and download
============
*/
void COM_CreatePath (char *path)
{
	char *ofs;

	for (ofs = path+1 ; *ofs ; ofs++)
	{
		if (*ofs == '/')
		{	// create the directory
			*ofs = 0;
			Sys_mkdir (path);
			*ofs = '/';
		}
	}
}

/*
===========
COM_FindFile

Finds the file in the search path.
Sets fs_filesize and one of handle or file
===========
*/
qbool file_from_pak; // global indicating file came from pack file ZOID
// disconnect: file_from_pak is only needed for allow_download_pakmaps.
// I think its OK to remove them both.
int COM_FOpenFile (char *filename, FILE **file)
{
	searchpath_t *search;
	char netpath[MAX_OSPATH];
	pack_t *pak;
	int i;

	*file = NULL;
	file_from_pak = false;
	fs_filesize = -1;

	// search through the path, one element at a time
	for (search = com_searchpaths ; search ; search = search->next)
	{
		// is the element a pak file?
		if (search->pack)
		{
			// look through all the pak file elements
			pak = search->pack;
			for (i=0 ; i<pak->numfiles ; i++)
				if (!strcmp (pak->files[i].name, filename))
			{	// found it!
				if ((int)developer.value)
					Sys_Printf ("PackFile: %s : %s\n", pak->filename, filename);
					// open a new file on the pakfile
				*file = fopen (pak->filename, "rb");
				if (!*file)
					Sys_Error ("Couldn't reopen %s", pak->filename);
				fseek (*file, pak->files[i].filepos, SEEK_SET);
				fs_filesize = pak->files[i].filelen;
				file_from_pak = true;
				return fs_filesize;
			}
		}
		else
		{
			snprintf (netpath, sizeof(netpath), "%s/%s", search->filename, filename);

			*file = fopen (netpath, "rb");
			if (!*file)
				continue;

			if ((int)developer.value)
				Sys_Printf ("FindFile: %s\n",netpath);

			fs_filesize = FS_FileLength (*file);
			return fs_filesize;
		}
	}

	if (sv_MegaTFHacks.value && Q_stristr(filename, "qwmcycle/map"))
		mtf.nocycle = true;

	if ((int)developer.value)
		Sys_Printf ("FindFile: can't find %s\n", filename);

	return -1;
}

/*
============
COM_LoadFile

Filename are relative to the quake directory.
Always appends a 0 byte to the loaded data.
============
*/
static byte *loadbuf; // FIXME
static int loadsize; // FIXME
void *Hunk_AllocName_f (int size, char *name, qbool clean);
byte *COM_LoadFile (char *path, int usehunk)
{
	FILE *h;
	byte *buf=NULL;
	char base[MAX_OSPATH];
	int len;
	extern cvar_t sv_cpserver;
	int l, count;

#define READMAX 50000
#define READSIZE 1024

	// look for it in the filesystem or pack files
	len = fs_filesize = COM_FOpenFile (path, &h);
	if (!h)
		return NULL;

	// extract the filename base name for hunk tag
	COM_FileBase (path, base);

	if (usehunk == 1)
		buf = (byte *) Hunk_AllocName_f (len + 1, base, false);
	else if (usehunk == 2)
		buf = (byte *) Hunk_TempAlloc (len + 1);
	else if (usehunk == 4)
	{
		if (len + 1 > loadsize)
			buf = (byte *) Hunk_TempAlloc (len + 1);
		else
			buf = loadbuf;
	}
	else if (usehunk == 5)
		buf = (byte*)/*B*/Z_Malloc(len+1);
//	else if (usehunk == 6)
//		buf = (qbyte*)Hunk_TempAllocMore (len+1);
	else
		Sys_Error ("COM_LoadFile: bad usehunk");

	if (!buf)
		Sys_Error ("COM_LoadFile: not enough space for %s", path);

	((byte *)buf)[len] = 0;

	l = 0;
	count = 0;

	while (!feof(h))
	{
		if (l + READSIZE > len)
		{
			fread(buf + l, 1, len - l, h);
			break;
		}

		fread(buf + l, 1, READSIZE, h);
		l += READSIZE;
		if (l - count > READMAX && ((int)sv_cpserver.value > 0) && ((int)sv_cpserver.value < 100))
		{
			Sys_Sleep((unsigned long)sv_cpserver.value);
			count = l;
		}
	}

	fclose (h);

	return buf;
}

byte *COM_LoadHunkFile (char *path)
{
	return COM_LoadFile (path, 1);
}

byte *COM_LoadTempFile (char *path)
{
	return COM_LoadFile (path, 2);
}

// uses temp hunk if larger than bufsize
byte *COM_LoadStackFile (char *path, void *buffer, int bufsize)
{
	byte *buf;

	loadbuf = (byte *)buffer;
	loadsize = bufsize;
	buf = COM_LoadFile (path, 4);

	return buf;
}

/*
=================
COM_LoadPackFile

Takes an explicit (not game tree related) path to a pak file.

Loads the header and directory, adding the files at the beginning
of the list so they override previous pack files.
=================
*/
pack_t *COM_LoadPackFile (char *packfile)
{
	dpackheader_t header;
	int i;
	packfile_t *newfiles;
	int numpackfiles;
	pack_t *pack;
	FILE *packhandle;
	dpackfile_t info[MAX_FILES_IN_PACK];

	if (COM_FileOpenRead (packfile, &packhandle) == -1)
		return NULL;

	fread (&header, 1, sizeof(header), packhandle);
	if (header.id[0] != 'P' || header.id[1] != 'A'
		   || header.id[2] != 'C' || header.id[3] != 'K')
		Sys_Error ("%s is not a packfile", packfile);
	header.dirofs = LittleLong (header.dirofs);
	header.dirlen = LittleLong (header.dirlen);

	numpackfiles = header.dirlen / sizeof(dpackfile_t);

	if (numpackfiles > MAX_FILES_IN_PACK)
		Sys_Error ("%s has %i files", packfile, numpackfiles);

	newfiles = (packfile_t *) Q_malloc (numpackfiles * sizeof(packfile_t));

	fseek (packhandle, header.dirofs, SEEK_SET);
	fread (&info, 1, header.dirlen, packhandle);

	// parse the directory
	for (i=0 ; i<numpackfiles ; i++)
	{
		strlcpy (newfiles[i].name, info[i].name, MAX_QPATH);
		newfiles[i].filepos = LittleLong(info[i].filepos);
		newfiles[i].filelen = LittleLong(info[i].filelen);
	}

	pack = (pack_t *) Q_malloc (sizeof (pack_t));
	strlcpy (pack->filename, packfile, MAX_OSPATH);
	pack->handle = packhandle;
	pack->numfiles = numpackfiles;
	pack->files = newfiles;

	Con_Printf ("Added packfile %s (%i files)\n", packfile, numpackfiles);
	return pack;
}

char *COM_NextPath (char *prevpath)
{
	searchpath_t *s;
	char *prev;

	if (!prevpath)
		return fs_gamedir;

	prev = fs_gamedir;
	for (s=com_searchpaths ; s ; s=s->next)
	{
		if (s->pack)
			continue;
		if (prevpath == prev)
			return s->filename;
		prev = s->filename;
	}

	return NULL;
}

/*
================
COM_Gamedir

Sets the gamedir and path to a different directory.
================
*/
void COM_Gamedir (char *dir)
{
	searchpath_t *search, *next;
	int i;
	pack_t *pak;
	char pakfile[MAX_OSPATH];

	if (strnstr(dir, "..", MAX_OSPATH) || strnstr(dir, "/", MAX_OSPATH)
		   || strnstr(dir, "\\", MAX_OSPATH) || strnstr(dir, ":", MAX_OSPATH) )
	{
		Con_Printf ("Gamedir should be a single filename, not a path\n");
		return;
	}

	if (!strncmp(gamedirfile, dir, MAX_OSPATH))
		return; // still the same
	strlcpy (gamedirfile, dir, MAX_OSPATH);

	// free up any current game dir info
	while (com_searchpaths != com_base_searchpaths)
	{
		if (com_searchpaths->pack)
		{
			fclose (com_searchpaths->pack->handle);
			Q_free (com_searchpaths->pack->files);
			Q_free (com_searchpaths->pack);
		}
		next = com_searchpaths->next;
		Q_free (com_searchpaths);
		com_searchpaths = next;
	}

	snprintf (fs_gamedir, MAX_OSPATH, "%s/%s", fs_basedir, dir);

	if (!strncmp(dir, "id1", 4) || !strncmp(dir, "qw", 3))
		return;

	// add the directory to the search path
	search = (searchpath_t *) Q_malloc (sizeof(searchpath_t));
	strlcpy (search->filename, fs_gamedir, MAX_OSPATH);
	search->pack = NULL;
	search->next = com_searchpaths;
	com_searchpaths = search;

	// add any pak files in the format pak0.pak pak1.pak, ...
	for (i=0 ; ; i++)
	{
		snprintf (pakfile, MAX_OSPATH, "%s/pak%i.pak", fs_gamedir, i);
		pak = COM_LoadPackFile (pakfile);
		if (!pak)
			break;
		search = (searchpath_t *) Q_malloc (sizeof(searchpath_t));
		search->pack = pak;
		search->next = com_searchpaths;
		com_searchpaths = search;
	}
}


/*
=============================================================================

OTHERS PUBLIC FUNCTIONS

=============================================================================
*/

// FTE
#ifndef __GNUC__
typedef unsigned char 		qbyte;
#endif

qbyte *COM_LoadMallocFile (char *path)	//used for temp info along side temp hunk
{
	return COM_LoadFile (path, 5);
}