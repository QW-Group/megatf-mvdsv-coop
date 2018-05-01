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
 
	$Id: sv_sys_win.c,v 1.36 2007/01/14 20:02:33 tonik Exp $
*/

#include "qwsvdef.h"
#include <conio.h>
#include <ctype.h>
#include <winsock2.h> // OfN

void Sys_ConsoleColor(int forecolor, int backcolor);
// OfN - Color values
#define COLOR_BLACK    0x00
#ifdef _WIN32 // Windows oriented values (Win32 API friendly)
	#define COLOR_RED      0x04
	#define COLOR_GREEN    0x02
	#define COLOR_BLUE     0x01
#else // Unix oriented values (ANSI friendly)
	#define COLOR_RED      0x01
	#define COLOR_GREEN    0x02
	#define COLOR_BLUE     0x04
#endif 
#define COLOR_BROWN    COLOR_RED | COLOR_GREEN
#define COLOR_ORANGE   COLOR_BROWN
#define COLOR_MAGENTA  COLOR_BLUE | COLOR_RED
#define COLOR_CYAN     COLOR_GREEN | COLOR_BLUE
#define COLOR_GRAY     COLOR_RED | COLOR_GREEN | COLOR_BLUE
#define COLOR_INTENSE  0x08
#define COLOR_PINK     COLOR_MAGENTA | COLOR_INTENSE
#define COLOR_YELLOW   COLOR_BROWN | COLOR_INTENSE
#define COLOR_TEAL     COLOR_CYAN | COLOR_INTENSE
#define COLOR_LRED     COLOR_RED | COLOR_INTENSE
#define COLOR_LGREEN   COLOR_GREEN | COLOR_INTENSE
#define COLOR_LBLUE    COLOR_BLUE | COLOR_INTENSE
#define COLOR_WHITE    COLOR_GRAY | COLOR_INTENSE

#define COLOR_DEFAULT  -1
#define COLOR_CURRENT  -2
#define COLOR_NOCHANGE COLOR_CURRENT
#define COLOR_RESET    COLOR_DEFAULT

// Custom color states
#define CCSTATE_NORMAL 0
#define CCSTATE_CUSTOM 1
#define CCSTATE_BEBACK 2

// Color Settings

//- Connection messages 
#define COLOR_FORE_CONNECTION COLOR_GREEN
#define COLOR_BACK_CONNECTION COLOR_DEFAULT
//- Drop messages
#define COLOR_FORE_DROP COLOR_RED
#define COLOR_BACK_DROP COLOR_DEFAULT
//- Command Prompt
#define COLOR_FORE_PROMPT COLOR_WHITE
#define COLOR_BACK_PROMPT COLOR_DEFAULT
//- Input Command
#define COLOR_FORE_COMMANDLINE COLOR_TEAL
#define COLOR_BACK_COMMANDLINE COLOR_DEFAULT
//- Chat text
#define COLOR_FORE_CHAT COLOR_WHITE
#define COLOR_BACK_CHAT COLOR_DEFAULT
//- Admin (Rcon's)
#define COLOR_FORE_ADMIN COLOR_CYAN
#define COLOR_BACK_ADMIN COLOR_DEFAULT
//- Console say's
#define COLOR_FORE_CONSAY COLOR_BROWN
#define COLOR_BACK_CONSAY COLOR_DEFAULT

extern cvar_t sys_restart_on_error;
extern cvar_t not_auth_timeout;
extern cvar_t auth_timeout;

struct timeval select_timeout;

cvar_t	sys_nostdout = {"sys_nostdout", "0"};
cvar_t	sys_sleep = {"sys_sleep", "8"};

// OfN
extern cvar_t con_color;
extern cvar_t con_prompt;
extern cvar_t con_cleanin;
extern cvar_t con_talk; // OfN

#define PROMPT_STR "] "
#define PROMPT_LEN 2

#define IN_MAX   256

#define INLIST_STRLEN 70
#define INLIST_NUMITEMS 20
char in_list[INLIST_NUMITEMS][INLIST_STRLEN];
int in_listcur;
int in_listlast;

char in_text[IN_MAX];
int in_len;
qbool in_prompt;
HANDLE hConsoleOut;
HANDLE hConsoleIn;
WORD DefaultColorWinAPI;
int DefaultColorANSIfore;
int DefaultColorANSIback;
int CurrentForeColor;
int CurrentBackColor;

int CustomColor_state;

static char title[16];

static qbool	iosock_ready = false;
static qbool	authenticated = false;
static double	cur_time_auth;
static qbool	isdaemon = false;




/*
================
Sys_FileTime
================
*/
int	Sys_FileTime (char *path)
{
	struct	_stat	buf;
	return _stat(path, &buf) == -1 ? -1 : buf.st_mtime;
}

/*
================
Sys_mkdir
================
*/
void Sys_mkdir (char *path)
{
	_mkdir(path);
}

/*
================
Sys_remove
================
*/
int Sys_remove (char *path)
{
	return remove(path);
}

//bliP: rmdir ->
/*
================
Sys_rmdir
================
*/
int Sys_rmdir (char *path)
{
	return _rmdir(path);
}
//<-

/*
================
Sys_listdir
================
*/

dir_t Sys_listdir (char *path, char *ext, int sort_type)
{
	static file_t	list[MAX_DIRFILES];
	dir_t	dir;
	HANDLE	h;
	WIN32_FIND_DATA fd;
	char	pathname[MAX_DEMO_NAME];
	qbool all;

	int	r;
	pcre	*preg;
	const char	*errbuf;

	memset(list, 0, sizeof(list));
	memset(&dir, 0, sizeof(dir));

	dir.files = list;
	all = !strncmp(ext, ".*", 3);
	if (!all)
		if (!(preg = pcre_compile(ext, PCRE_CASELESS, &errbuf, &r, NULL)))
		{
			Con_Printf("Sys_listdir: pcre_compile(%s) error: %s at offset %d\n",
			           ext, errbuf, r);
			Q_free(preg);
			return dir;
		}

	snprintf(pathname, sizeof(pathname), "%s/*.*", path);
	if ((h = FindFirstFile (pathname , &fd)) == INVALID_HANDLE_VALUE)
	{
		if (!all)
			Q_free(preg);
		return dir;
	}

	do
	{
		if (!strncmp(fd.cFileName, ".", 2) || !strncmp(fd.cFileName, "..", 3))
			continue;
		if (!all)
		{
			switch (r = pcre_exec(preg, NULL, fd.cFileName,
			                      strlen(fd.cFileName), 0, 0, NULL, 0))
			{
			case 0: break;
			case PCRE_ERROR_NOMATCH: continue;
			default:
				Con_Printf("Sys_listdir: pcre_exec(%s, %s) error code: %d\n",
				           ext, fd.cFileName, r);
				if (!all)
					Q_free(preg);
				return dir;
			}
		}

		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) //bliP: list dir
		{
			dir.numdirs++;
			list[dir.numfiles].isdir = true;
			list[dir.numfiles].size = list[dir.numfiles].time = 0;
		}
		else
		{
			list[dir.numfiles].isdir = false;
			snprintf(pathname, sizeof(pathname), "%s/%s", path, fd.cFileName);
			list[dir.numfiles].time = Sys_FileTime(pathname);
			dir.size += (list[dir.numfiles].size = fd.nFileSizeLow);
		}
		strlcpy (list[dir.numfiles].name, fd.cFileName, sizeof(list[0].name));

		if (++dir.numfiles == MAX_DIRFILES - 1)
			break;

	}
	while (FindNextFile(h, &fd));

	FindClose (h);
	if (!all)
		Q_free(preg);

	switch (sort_type)
	{
	case SORT_NO: break;
	case SORT_BY_DATE:
		qsort((void *)list, dir.numfiles, sizeof(file_t), Sys_compare_by_date);
		break;
	case SORT_BY_NAME:
		qsort((void *)list, dir.numfiles, sizeof(file_t), Sys_compare_by_name);
		break;
	}
	return dir;
}

/*
================
Sys_Exit
================
*/
void Sys_Exit(int code)
{
#ifndef _CONSOLE
	RemoveNotifyIcon();
#endif
	exit(code);
}

/*
================
Sys_Quit
================
*/
char *argv0;
void Sys_Quit (qbool restart)
{
	if (restart)
		if (execv(argv0, com_argv) == -1)
		{
#ifdef _CONSOLE
			if (!((int)sys_nostdout.value || isdaemon))
				printf("Restart failed: (%i): %s\n", qerrno, strerror(qerrno));
#else
			if (!(COM_CheckParm("-noerrormsgbox") || isdaemon))
				MessageBox(NULL, strerror(qerrno), "Restart failed", 0 /* MB_OK */ );
#endif
			Sys_Exit(1);
		}
	Sys_Exit(0);
}

/*
================
Sys_Error
================
*/
void Sys_Error (char *error, ...)
{
	va_list		argptr;
	char		text[1024];

	va_start (argptr,error);
	vsnprintf (text, sizeof(text), error,argptr);
	va_end (argptr);
#ifdef _CONSOLE
	if (!((int)sys_nostdout.value || isdaemon))
		printf ("ERROR: %s\n", text);
#else
	if (!(COM_CheckParm("-noerrormsgbox") || isdaemon))
		MessageBox(NULL, text, "Error", 0 /* MB_OK */ );
	else
		Sys_Printf("ERROR: %s\n", text);

#endif
	if (logs[ERROR_LOG].sv_logfile)
	{
		SV_Write_Log(ERROR_LOG, 1, va("ERROR: %s\n", text));
		fclose(logs[ERROR_LOG].sv_logfile);
	}
	if ((int)sys_restart_on_error.value)
		Sys_Quit(true);
	Sys_Exit (1);
}

static double pfreq;
static qbool hwtimer = false;
static __int64 startcount;
void Sys_InitDoubleTime (void)
{
	__int64 freq;

	if (!COM_CheckParm("-nohwtimer") &&
		QueryPerformanceFrequency ((LARGE_INTEGER *)&freq) && freq > 0)
	{
		// hardware timer available
		pfreq = (double)freq;
		hwtimer = true;
		QueryPerformanceCounter ((LARGE_INTEGER *)&startcount);
	}
	else
	{
		// make sure the timer is high precision, otherwise
		// NT gets 18ms resolution
		timeBeginPeriod (1);
	}
}

double Sys_DoubleTime (void)
{
	__int64 pcount;

	static DWORD starttime;
	static qbool first = true;
	DWORD now;

	if (hwtimer)
	{
		QueryPerformanceCounter ((LARGE_INTEGER *)&pcount);
		if (first) {
			first = false;
			startcount = pcount;
			return 0.0;
		}
		// TODO: check for wrapping; is it necessary?
		return (pcount - startcount) / pfreq;
	}

	now = timeGetTime();

	if (first)
	{
		first = false;
		starttime = now;
		return 0.0;
	}

	if (now < starttime) // wrapped?
		return (now / 1000.0) + (LONG_MAX - starttime / 1000.0);

	if (now - starttime == 0)
		return 0.0;

	return (now - starttime) / 1000.0;
}
#if 0
double Sys_DoubleTime (void)
{
	double t;
	struct _timeb tstruct;
	static int	starttime;

	_ftime( &tstruct );
 
	if (!starttime)
		starttime = tstruct.time;
	t = (tstruct.time-starttime) + tstruct.millitm*0.001;
	
	return t;
}
#endif

void Sys_ClearCommandPrompt (qbool prompt_too)
{
	//static char tempstr[IN_MAX+PROMPT_LEN];
	static DWORD retval;
	static COORD coord;
	static CONSOLE_SCREEN_BUFFER_INFO conInfo;

	if (in_len || prompt_too)
	{
		//memset(&tempstr,32,in_len + ((int)prompt_too*PROMPT_LEN));

		GetConsoleScreenBufferInfo(hConsoleOut,&conInfo);
		coord.X = (short int)!(prompt_too)*PROMPT_LEN;
		coord.Y = conInfo.dwCursorPosition.Y;
		
		FillConsoleOutputCharacter(hConsoleOut,' ',in_len + ((int)prompt_too*PROMPT_LEN),coord,&retval);
		SetConsoleCursorPosition(hConsoleOut,coord);
	}
}

/*
================
Sys_ConsoleInput
================
*/
char *Sys_ConsoleInput (void)
{
	// OfN - Stuff below made global
	//static char	text[256];
	//static int		len;
	int		c, i;
	char* p;
	static char clipb_c[256];
	static HANDLE clipb_h;
	
	// OfN
	if (con_prompt.value)
	{
		if (!in_prompt)
		{
			Sys_ConsoleColor(COLOR_FORE_PROMPT,COLOR_BACK_PROMPT);
			CustomColor_state = CCSTATE_BEBACK;
						
			fputs(PROMPT_STR,stdout);			
			in_prompt = true;

			Sys_ConsoleColor(COLOR_FORE_COMMANDLINE,COLOR_BACK_COMMANDLINE);
			
			if (con_cleanin.value)
			{		
				if (in_len)
					fputs(in_text,stdout);
			}			
		}		
	}	
	else
	{
		if (CurrentForeColor != (COLOR_FORE_COMMANDLINE) 
			|| CurrentBackColor != COLOR_BACK_COMMANDLINE)
			Sys_ConsoleColor(COLOR_FORE_COMMANDLINE,COLOR_BACK_COMMANDLINE);
	}

	// OfN For typen items scroll working (remove in buffer)
	if (!in_len)
		in_text[0] = '\0';
	

	// read a line out
	while (_kbhit())
	{			
		Sys_Printf("NOTHING HAPPENS!\n");
		c = _getch();

		// First handle any composed input (like arrow keys)
		if (!c || c == 224)
		if (_kbhit())
		{
			c = _getch();

			if (c == 72) // up arrow
			{				
				Sys_ClearCommandPrompt(false);				
				in_len = 0; // <-- reused
				
				// Scroll up command list items, skipping empty ones
				for (c = in_listcur; in_len != 2; c--)
				{
					if (c < 0)
						c = INLIST_NUMITEMS - 1;
					
					if (c == in_listcur)
						in_len++;						
					
					if (in_list[c][0])
					if (!in_text[0] || Q_strcmp(in_text,in_list[c]))
						break;					
				}

				if (in_len == 2)
				{
					in_len = 0;
					in_text[0] = 0;
					continue;
				}

				in_listcur = c;
				
				fputs(in_list[c],stdout);
				Q_strcpy(in_text,in_list[c]);
				in_len = Q_strlen(in_text);
				continue;
			}
			else if (c == 80) // down arrow
			{
				Sys_ClearCommandPrompt(false);				
				in_len = 0;

				in_listcur ++;
				
				// Scroll down command list items, skipping empty ones
				for (c = in_listcur; in_len != 2; c++)
				{
					if (c >= INLIST_NUMITEMS)
						c = 0;
					
					if (c == in_listcur)
						in_len++;						
					
					if (in_list[c][0])
					if (!in_text[0] || Q_strcmp(in_text,in_list[c]))
						break;					
				}

				if (in_len == 2)
				{
					in_len = 0;
					in_text[0] = 0;
					continue;
				}

				in_listcur = c;
				
				fputs(in_list[c],stdout);
				Q_strcpy(in_text,in_list[c]);
				in_len = Q_strlen(in_text);

				continue;
			}
			else if (c == 75) // left arrow
				c = 8; // Convert to backspace
			else if (c == 77) // right
				c = 32; // Convert to space
			else if (c == 71) // home
				c = 27; // Convert to ESC
		}

		if (c == 27) // Escape
		{			
			if (!in_len)
				continue;

			Sys_ClearCommandPrompt(false);

			in_text[0] = 0;
			in_len = 0;
			
			continue;
		}

		if (c == 22) // Ctrl-V, windows clipboard content paste
		{
			if (OpenClipboard(NULL))
			{
				if (clipb_h = GetClipboardData(CF_TEXT))
				{
					p = (char*)clipb_h;

					i = 0;
					for (c = 0; c < (IN_MAX - in_len) && p[c]; c++)
					{
						if (p[c] == '\r' || p[c] == '\n')
							i++;
						else
							in_text[(in_len + c) - i] = p[c];
					}

					in_text[((in_len + c) - i)] = '\0';

					fputs(&in_text[in_len],stdout);
										
					in_len += c - i;
				}
				else
					Sys_Printf("Win32 PASTE: Clipboard does not contain text!\n");
				
				CloseClipboard();
				continue;
			}
			else
			{
				Sys_Printf("Win32 PASTE: Unable to open clipboard!\n");
				continue;
			}
		}

		if (c == 8 && !in_len && in_prompt) // Do not allow prompt to be deleted
			continue;
		if (c == '\r' && !in_len) // Ignore carriage return with empty command
			continue;
		if (in_len > (IN_MAX - 2) && c != '\r' && c != 8) // Impide user to type beyond maximum
			continue;
		if (c == 9) // Tab
		{
			if (in_len) // Anything typen?
			{
				p = Cmd_CompleteCommand(in_text); // Check with commands

				if (p) // Partial command?
				if (Q_strlen(p)>in_len) // incomplete?
				{
					// Ok, complete it
					p += in_len;
					fputs(p,stdout);
					_fputchar(32);
					in_len += Q_strlen(p)+1;
					Q_strcat(in_text,p);
					Q_strcat(in_text," ");
					continue;
				}
				
				p = Cvar_CompleteVariable(in_text); // Check with cvars

				if (p) // Partial cvar?
				if (Q_strlen(p)>in_len) // incomplete?
				{
					// Ok, complete it
					p += in_len;
					fputs(p,stdout);
					_fputchar(32);
					in_len += Q_strlen(p)+1;
					Q_strcat(in_text,p);
					Q_strcat(in_text," ");
				}
			}

			continue;
		}

		//putch (c); // <-- ORiginal
		_fputchar(c);

		if (c == '\r')
		{
			in_text[in_len] = 0;
			putch ('\n');
			in_len = 0;
						
			in_prompt = false; // OfN

			// Add this to the list of used commands if different than last one
			if (Q_strcmp(in_text,in_list[in_listcur]))
			{
				in_listcur++;
				if (in_listcur >= INLIST_NUMITEMS)
					in_listcur = 0;

				memcpy(in_list[in_listcur],in_text,INLIST_STRLEN);
				in_list[in_listcur][INLIST_STRLEN - 1] = 0;				
			}
			
			return in_text;
		}
		if (c == 8)
		{
			if (in_len)
			{
				putch (' ');
				putch (c);
				in_len--;
				in_text[in_len] = 0;
			}
			continue;
		}
		in_text[in_len] = c;
		in_len++;
		in_text[in_len] = 0;
		if (in_len == sizeof(in_text))
			in_len = 0;
	}

	return NULL;
}

/* OfN
================
Sys_ConsoleColor
================
*/

void Sys_ConsoleColor(int forecolor, int backcolor)
{
	static char ansi_code[64];
	
	if (!con_color.value) return;

	if (forecolor == -1 && backcolor == -1) // Reset default
	{
		// Build the ANSI color code
		sprintf(ansi_code,"%c[%s;3%u;4%um",(unsigned char)27,(DefaultColorANSIfore & COLOR_INTENSE)? "01" : "00",(unsigned int)(DefaultColorANSIfore - (DefaultColorANSIfore & COLOR_INTENSE)),(unsigned int)(DefaultColorANSIback - (DefaultColorANSIback & COLOR_INTENSE)));
		
		// Output color ANSI color code change
		printf(ansi_code);
		
		return;// Job done
	}
	else if (forecolor == -1 || backcolor == -1)
	{
		if (forecolor == -1)
		{			
			// Build the ANSI color code
			sprintf(ansi_code,"%c[%s;3%u;4%um",(unsigned char)27,(DefaultColorANSIfore & COLOR_INTENSE)? "01" : "00",(unsigned int)(DefaultColorANSIfore - (DefaultColorANSIfore & COLOR_INTENSE)),(unsigned int)(backcolor));
			
			// Output color ANSI color code change
			printf(ansi_code);
		}
		else
		{
	
			// Build the ANSI color code
			sprintf(ansi_code,"%c[%s;3%u;4%um",(unsigned char)27,(forecolor & COLOR_INTENSE)? "01" : "00",(unsigned int)(forecolor - (forecolor & COLOR_INTENSE)),(unsigned int)(DefaultColorANSIback - (DefaultColorANSIback & COLOR_INTENSE)));
			
			// Output color ANSI color code change
			printf(ansi_code);
		}
			
		return; // Job done
	}
		
	// Build the ANSI color code
	sprintf(ansi_code,"%c[%s;3%u;4%um",(unsigned char)27,(forecolor & COLOR_INTENSE)? "01" : "00",(unsigned int)(forecolor - (forecolor & COLOR_INTENSE)),(unsigned int)(backcolor));

	// Output color ANSI color code change
	printf(ansi_code);	
}
void Com_Log (logType_t logType, int logOptions, char *text, ...);
/*
================
Sys_Printf
================
*/
void Sys_Printf (char *fmt, ...)
{
	extern char	chartbl2[];
	va_list		argptr;
	unsigned char	text[MAXCMDBUF];
	unsigned char	*p;

	if ((
#ifdef _CONSOLE
	            (int)sys_nostdout.value ||
#endif //_CONSOLE
	            isdaemon) && !(telnetport && telnet_connected && authenticated))
		return;

	va_start (argptr, fmt);
	vsnprintf (text, MAXCMDBUF, fmt, argptr);
	va_end (argptr);

#ifndef _CONSOLE
	if (!isdaemon) ConsoleAddText(text);
#endif //_CONSOLE

	for (p = text; *p; p++)
	{
		*p = chartbl2[*p];
		if (telnetport && telnet_connected && authenticated)
		{
			send (telnet_iosock, p, 1, 0);
			if (*p == '\n') // demand for M$ WIN 2K telnet support
				send (telnet_iosock, "\r", 1, 0);
		}
#ifdef _CONSOLE
		if (!((int)sys_nostdout.value || isdaemon))
			putc(*p, stdout);
#endif //_CONSOLE

	}

	if (telnetport && telnet_connected && authenticated)
		SV_Write_Log(TELNET_LOG, 3, text);
#ifdef _CONSOLE
	if (!((int)sys_nostdout.value || isdaemon))
		fflush(stdout);
#endif //_CONSOLE
	Com_Log (CONSOLELOG, LO_FORMAT|LO_TIMESTAMP, "%s", text);
}

/*
=============
Sys_Init
 
Quake calls this so the system can register variables before host_hunklevel
is marked
=============
*/
void Sys_Init (void)
{
	qbool	WinNT;
	OSVERSIONINFO	vinfo;

	// make sure the timer is high precision, otherwise
	// NT gets 18ms resolution
	timeBeginPeriod( 1 );

	vinfo.dwOSVersionInfoSize = sizeof(vinfo);

	if (!GetVersionEx (&vinfo))
		Sys_Error ("Couldn't get OS info");

	if (vinfo.dwMajorVersion < 4 || vinfo.dwPlatformId == VER_PLATFORM_WIN32s)
		Sys_Error (SERVER_NAME " requires at least Win95 or NT 4.0");

	WinNT = (vinfo.dwPlatformId == VER_PLATFORM_WIN32_NT ? true : false);

	Cvar_Register (&sys_nostdout);
	Cvar_Register (&sys_sleep);

	if (COM_CheckParm ("-nopriority"))
	{
		Cvar_Set (&sys_sleep, "0");
	}
	else
	{
		if ( ! SetPriorityClass (GetCurrentProcess(), HIGH_PRIORITY_CLASS))
			Con_Printf ("SetPriorityClass() failed\n");
		else
			Con_Printf ("Process priority class set to HIGH\n");

		// sys_sleep > 0 seems to cause packet loss on WinNT (why?)
		if (WinNT)
			Cvar_Set (&sys_sleep, "0");
	}

	Sys_InitDoubleTime ();
}

__inline void Sys_Telnet (void);
qbool NET_Sleep ()
{
	struct timeval timeout_cur;
	fd_set	fdset;
	int j = net_socket;

	FD_ZERO (&fdset);
	FD_SET(j, &fdset); // network socket

	// Added by VVD {
	if (telnetport)
	{
		Sys_Telnet();
		FD_SET(net_telnetsocket, &fdset);
		j = max(j, net_telnetsocket);
		if (telnet_connected)
		{
			FD_SET(telnet_iosock, &fdset);
			j = max(j, telnet_iosock);
		}
	}
	// Added by VVD }
	timeout_cur.tv_sec  = select_timeout.tv_sec;
	timeout_cur.tv_usec = select_timeout.tv_usec;

	switch (select (++j, &fdset, NULL, NULL, &timeout_cur))
	{
		case -1: return true;
		case 0: break;
		default:
			if (telnetport && telnet_connected)
				iosock_ready = FD_ISSET(telnet_iosock, &fdset);
#ifdef _CONSOLE
			if (do_stdin)
				stdin_ready = FD_ISSET(0, &fdset);
#endif //_CONSOLE
	}
	return false;
}

void Sys_Sleep(unsigned long ms)
{
	Sleep(ms);
}

int Sys_Script(char *path, char *args)
{
	STARTUPINFO			si;
	PROCESS_INFORMATION	pi;
	char cmdline[1024], curdir[MAX_OSPATH];

	memset (&si, 0, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_SHOWMINNOACTIVE;

	GetCurrentDirectory(sizeof(curdir), curdir);


	snprintf(cmdline, sizeof(cmdline), "%s\\sh.exe %s.qws %s", curdir, path, args);
	strlcat(curdir, va("\\%s", fs_gamedir+2), MAX_OSPATH);

	return CreateProcess (NULL, cmdline, NULL, NULL,
	                      FALSE, 0/*DETACHED_PROCESS /*CREATE_NEW_CONSOLE*/ , NULL, curdir, &si, &pi);
}

DL_t Sys_DLOpen(const char *path)
{
	return LoadLibrary(path);
}

qbool Sys_DLClose(DL_t dl)
{
	return FreeLibrary(dl);
}

void *Sys_DLProc(DL_t dl, const char *name)
{
	return GetProcAddress(dl, name);
}

__inline void Sys_Telnet (void)
{
	static int			tempsock;
	static struct		sockaddr_in remoteaddr, remoteaddr_temp;
	static socklen_t	sockaddr_len = sizeof(struct sockaddr_in);
	static double		cur_time_not_auth;
	if (telnet_connected)
	{
		if ((tempsock = accept (net_telnetsocket, (struct sockaddr*)&remoteaddr_temp, &sockaddr_len)) > 0)
		{
			//			if (remoteaddr_temp.sin_addr.s_addr == inet_addr ("127.0.0.1"))
			send (tempsock, "Console busy by another user.\n", 31, 0);
			closesocket (tempsock);
			SV_Write_Log(TELNET_LOG, 1, va("Console busy by: %s. Refuse connection from: %s\n",
										   inet_ntoa(remoteaddr.sin_addr), inet_ntoa(remoteaddr_temp.sin_addr)));
		}
		if (	(!authenticated && (int)not_auth_timeout.value &&
				realtime - cur_time_not_auth > not_auth_timeout.value) ||
				(authenticated && (int)auth_timeout.value &&
				 realtime - cur_time_auth > auth_timeout.value))
		{
			telnet_connected = false;
			send (telnet_iosock, "Time for authentication finished.\n", 34, 0);
			closesocket (telnet_iosock);
			SV_Write_Log(TELNET_LOG, 1, va("Time for authentication finished. Refuse connection from: %s\n", inet_ntoa(remoteaddr.sin_addr)));
		}
	}
	else
	{
		if ((telnet_iosock = accept (net_telnetsocket, (struct sockaddr*)&remoteaddr, &sockaddr_len)) > 0)
		{
			//			if (remoteaddr.sin_addr.s_addr == inet_addr ("127.0.0.1"))
			//			{
			telnet_connected = true;
			cur_time_not_auth = realtime;
			SV_Write_Log(TELNET_LOG, 1, va("Accept connection from: %s\n", inet_ntoa(remoteaddr.sin_addr)));
			send (telnet_iosock, "# ", 2, 0);
			/*			}
			else
			{
			closesocket (telnet_iosock);
			SV_Write_Log(TELNET_LOG, 1, va("IP not match. Refuse connection from: %s\n", inet_ntoa(remoteaddr.sin_addr)));
			}
			*/
		}
	}
}

#ifdef _CONSOLE
/*
==================
main
 
==================
*/
char	*newargv[256];

int main (int argc, char **argv)
{
	quakeparms_t	parms;
	double		newtime, time, oldtime;
	static	char	cwd[1024];
	int		t;
	int		sleep_msec;

#if defined (_WIN32) && !defined(_DEBUG)
Win32StructuredException::RegisterWin32ExceptionHandler();
#endif
 
#if defined (__linux__)
RegisterLinuxSegvHandler();
#endif

	GetConsoleTitle(title, sizeof(title));
	COM_InitArgv (argc, argv);
	argv0 = com_argv[0];
	parms.argc = com_argc;
	parms.argv = com_argv;

	parms.memsize = DEFAULT_MEM_SIZE;

	if ((t = COM_CheckParm ("-heapsize")) != 0 &&
	        t + 1 < com_argc)
		parms.memsize = Q_atoi (com_argv[t + 1]) * 1024;

	if ((t = COM_CheckParm ("-mem")) != 0 &&
	        t + 1 < com_argc)
		parms.memsize = Q_atoi (com_argv[t + 1]) * 1024 * 1024;

	parms.membase = Q_malloc (parms.memsize);

	SV_Init (&parms);

	// run one frame immediately for first heartbeat
	SV_Frame (0.1);

	//
	// main loop
	//
	oldtime = Sys_DoubleTime () - 0.1;

	while (1)
	{
		sleep_msec = (int)sys_sleep.value;
		if (sleep_msec > 0)
		{
			if (sleep_msec > 13)
				sleep_msec = 13;
			Sleep (sleep_msec);
		}

		// select on the net socket and stdin
		// the only reason we have a timeout at all is so that if the last
		// connected client times out, the message would not otherwise
		// be printed until the next event.
		if (NET_Sleep ())
			continue;

		// find time passed since last cycle
		newtime = Sys_DoubleTime ();
		time = newtime - oldtime;
		oldtime = newtime;

		SV_Frame (time);
	}

	return true;
}

#else  // _CONSOLE

void PR_CleanLogText_Init(void);
int APIENTRY WinMain(   HINSTANCE   hInstance,
						HINSTANCE   hPrevInstance,
						LPSTR       lpCmdLine,
						int         nCmdShow)
{

	static MSG		msg;
	static quakeparms_t	parms;
	static double		newtime, time, oldtime;
	static char		cwd[1024];
	register int		sleep_msec;

	static struct		timeval	timeout;
	static fd_set		fdset;
	//Added by VVD {
	static int			j;
	char				*_argv[MAX_NUM_ARGVS];
	static qbool		disable_gpf = false;

	// get the command line parameters
	_argv[0] = GetCommandLine();
	if (_argv[0][0] == '"')
	{
		for (j = 1; _argv[0][j] != '"' && _argv[0][j]; j++);
		argv0 = (char *) Q_malloc (j);
		for (j = 1; _argv[0][j] != '"' && _argv[0][j]; j++)
			argv0[j - 1] = _argv[0][j];
		argv0[j] = 0;
		if (_argv[0][j] == '"') _argv[0][j + 1] = 0;
		parms.argc = 1;
	}
	else
	{
		parms.argc = 0;
		argv0 = lpCmdLine = _argv[0];
	}
	//Added by VVD }

	while (*lpCmdLine && (parms.argc < MAX_NUM_ARGVS))
	{
		while (*lpCmdLine && ((*lpCmdLine <= 32) || (*lpCmdLine > 126)))
			lpCmdLine++;

		if (*lpCmdLine)
		{
			_argv[parms.argc] = lpCmdLine;
			parms.argc++;

			while (*lpCmdLine && ((*lpCmdLine > 32) && (*lpCmdLine <= 126)))
				lpCmdLine++;

			if (*lpCmdLine)
			{
				*lpCmdLine = 0;
				lpCmdLine++;
			}
		}
	}

	parms.argv = _argv;

	PR_CleanLogText_Init();

	COM_InitArgv (parms.argc, parms.argv);

	// create main window
	if (!CreateMainWindow(hInstance, nCmdShow))
		return 1;

	parms.memsize = DEFAULT_MEM_SIZE;

	j = COM_CheckParm ("-heapsize");
	if (j && j + 1 < com_argc)
		parms.memsize = Q_atoi (com_argv[j + 1]) * 1024;

	j = COM_CheckParm ("-mem");
	if (j && j + 1 < com_argc)
		parms.memsize = Q_atoi (com_argv[j + 1]) * 1024 * 1024;

	if (COM_CheckParm("-noerrormsgbox"))
		disable_gpf = true;

	if (COM_CheckParm ("-d"))
	{
		isdaemon = disable_gpf = true;
		//close(0); close(1); close(2);
	}

	if (disable_gpf)
	{
		DWORD dwMode = SetErrorMode(SEM_NOGPFAULTERRORBOX);
		SetErrorMode(dwMode | SEM_NOGPFAULTERRORBOX);
	}

	parms.membase = Q_malloc (parms.memsize);
	
	SV_Init (&parms);

	// if stared miminize update notify icon message (with correct port)
	if (minimized)
		UpdateNotifyIconMessage(va(SERVER_NAME ":%d", sv_port));

	// run one frame immediately for first heartbeat
	SV_Frame (0.1);

	//
	// main loop
	//
	oldtime = Sys_DoubleTime () - 0.1;

	while(1)
	{
		// get messeges sent to windows
		if( PeekMessage( &msg, NULL, 0, 0, PM_NOREMOVE ) )
		{
			if( !GetMessage( &msg, NULL, 0, 0 ) )
				break;
			if(!IsDialogMessage(DlgHwnd, &msg))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}

		CheckIdle();

		// server frame

		sleep_msec = (int)sys_sleep.value;
		if (sleep_msec > 0)
		{
			if (sleep_msec > 13)
				sleep_msec = 13;
			Sleep (sleep_msec);
		}

		// select on the net socket and stdin
		// the only reason we have a timeout at all is so that if the last
		// connected client times out, the message would not otherwise
		// be printed until the next event.

		if (NET_Sleep ())
			continue;

		// find time passed since last cycle
		newtime = Sys_DoubleTime ();
		time = newtime - oldtime;
		oldtime = newtime;

		SV_Frame (time);
	}


	Sys_Exit(msg.wParam);

	return msg.wParam;
}

#endif // _CONSOLE

