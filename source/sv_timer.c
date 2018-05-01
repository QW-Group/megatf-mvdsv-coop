/*
Copyright (C) 2002 Frederick Lascelles, Jr.

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

// Dynamic Timers
// Timers are hashed and are very similar to cvars

#include "qwsvdef.h"
#include "timer.h"

#define			MAX_TIMERS		16	// enough? too many?
static int		timercount;			// initializes to 0
	

/*
=============
Timer_Find

Finds an active timer based on a case-insensitive name
=============
*/
qtimer_t *Timer_Find (char *name)
{
	qtimer_t		*timer = NULL;
	int				key;

	key = Com_HashKey (name);
	
	for (timer=timer_hash[key] ; timer ; timer=timer->hash_next)
		if (!stricmp (name, timer->name))
			return timer;

	return NULL;
}


/*
=============
Timer_Add

Adds a new timer to the hash
=============
*/
qbool Timer_Add (char *name, int repetitions, float interval, char *commands)
{
	qtimer_t			*newtimer = NULL;
	int					key;


	newtimer = Timer_Find (Cmd_Argv(2));
	if (newtimer)
	{
		Com_Printf ("TimerAdd: error: timer name already exists. use another name.\n");
		return false;
	}
	
	if (timercount == MAX_TIMERS)
	{
		Com_Printf ("TimerAdd: error: too many timers.\n");
		return false;
	}

	newtimer = (qtimer_t *)Z_Malloc (sizeof(qtimer_t));
	newtimer->next = timers;
	timers = newtimer;

	key = Com_HashKey (Cmd_Argv(1));
	newtimer->hash_next = timer_hash[key];
	timer_hash[key] = newtimer;

	// copy into zone; Z_Free in Timer_Delete
	newtimer->name = Z_CopyString (name);
	newtimer->value = Z_CopyString (commands);
	newtimer->repetitions = repetitions;
	newtimer->interval = interval;
	newtimer->lasttime = realtime; // start counting from this moment
	newtimer->executions = 0; // never executed yet
	newtimer->rcon_account = NULL;

	/* remember the rcon access level of whos creating this timer */
	if (pRconCurPos != NULL)
	{
		newtimer->rcon_account = pRconCurPos;
		
		Com_Printf ("TimerAdd: level %d rcon user \"%s\" successfully created timer \"%s\".\n",
			newtimer->rcon_account->accesslevel, newtimer->rcon_account->name, newtimer->name);
	}
	else
	Com_Printf ("TimerAdd: successfully created timer \"%s\".\n",
		newtimer->name);

	timercount++;

	return true;
}

/*
===========
Timer_Delete

Deletes a timer
===========
*/
qbool Timer_Delete (char *name)
{
	qtimer_t		*timer = NULL, *prev = NULL;
	int				key;

	key = Com_HashKey (name);

	prev = NULL;
	for (timer = timer_hash[key] ; timer ; timer=timer->hash_next)
	{
		if (!stricmp(timer->name, name)) 
		{
			// unlink from hash
			if (prev)
				prev->hash_next = timer->next;
			else
				timer_hash[key] = timer->next;
			break;
		}
		prev = timer;
	}

	if (!timer)
		return false;

	prev = NULL;
	for (timer = timers ; timer ; timer=timer->next)
	{
		if (!stricmp(timer->name, name))
		{
			// unlink from timer list
			if (prev)
				prev->next = timer->next;
			else
				timers = timer->next;

			// free
			Com_DPrintf ("freeing timer \"%s\"\n", timer->name);
			Z_Free (timer->value);
			Z_Free (timer->name);
			Z_Free (timer);
			
			timercount--;
			return true;
		}
		prev = timer;
	}

	return false;	// shut up compiler
}

/*
=============
Timer_DestroyOld

Destroys timers who have met the maximum execution rate
=============
*/
void Timer_DestroyOld (void)
{
	qtimer_t		*timer = NULL;
	int				i;

	for (timer = timers, i = 0 ; timer ; timer = timer->next, i++)
	{
		if (timer->repetitions < 1) // 0 means the timer never dies
			continue;

		if (timer->executions >= timer->repetitions)
			Timer_Delete (timer->name);
	}
}

/*
=============
Timer_AccountRemoved

An account is being deleted, so any timers created by this account
will also be deleted for security reasons.
=============
*/
void Timer_AccountRemoved (rcon_struct *account)
{
	qtimer_t		*timer = NULL;
	int				i;

	for (timer=timers, i=0 ; timer ; timer=timer->next, i++)
	{
		if (timer->rcon_account &&
			timer->rcon_account == account)
		{
			Com_Printf ("TimerDel: timer user-created timer \"%s\" is automatically being removed because of account deletion.\n",
				timer->name);
			Timer_Delete (timer->name);
		}
	}
}

/*
=============
SV_CheckTimers

called by SV_Frame - updates timers that need updated
=============
*/
void SV_CheckTimers (void)
{
	qtimer_t		*timer = NULL;
	int				i;
	
	for (timer=timers, i=0 ; timer ; timer=timer->next, i++)
	{
		if ((float)(realtime - timer->lasttime) >= timer->interval)
		{
			/* 
			 FIXME: accounts can create timer and execute commands
			 they dont have access to?
			 Make all timer commands access level 6 for now. *sigh*

			-FIXED-
			 */
			if (timer->rcon_account != NULL)
			{
				Com_DPrintf ("executing timer \"%s\" created by level %d account \"%s\"\n",
				timer->name, timer->rcon_account->accesslevel, timer->rcon_account->name);
				
				pRconCurPos = timer->rcon_account;

				Cmd_ExecuteString (timer->value);

				pRconCurPos = NULL; // safe
			}
			else
			{
				Com_DPrintf ("executing timer \"%s\"\n",
				timer->name);

				Cmd_ExecuteString (timer->value);
			}

			timer->executions += 1;
			timer->lasttime = realtime;
		}
	}

	Timer_DestroyOld ();
}

/*
=============
SV_TimerAdd_f

Adds a new timer. 
=============
*/
void SV_TimerAdd_f (void)
{
	char			remaining[GENERIC_BUFFER];
	float			interval, repetitions;
	int				i, len = 0;


#define TIMER_NUMARGS 5

	if (Cmd_Argc() < TIMER_NUMARGS)
	{
		// print help
		Com_Printf ("usage: addtimer <name> <repetitions> <interval> <commands>\n");
		Com_Printf ("  <name>:        The name to identify this timer.\n");
		Com_Printf ("  <repetitions>: Use 0 for an endless timer.\n");
		Com_Printf ("  <interval>:    Interval in seconds between the timer's executions.\n");
		Com_Printf ("  <commands>:    The commands to have this timer execute.\n\n");
		return;
	}
	
	remaining[0] = 0;

	for (i = TIMER_NUMARGS-1; i<Cmd_Argc(); i++)
	{
		if (i != TIMER_NUMARGS-1)
				strncat (remaining, " ", sizeof(remaining) - len - 2);
			strncat (remaining, Cmd_Argv(i), sizeof(remaining) - len - 1);
			len += (strlen (Cmd_Argv(i)) + 1);
	}

	interval = atof (Cmd_Argv(3));
	repetitions = atoi (Cmd_Argv(2));

	if (repetitions < 1) repetitions = 0;

	if (interval <= 0)
	{
		Com_Printf ("TimerAdd: error: interval must be a positive integer.\n");
		return;
	}

	Timer_Add (Cmd_Argv(1), atoi(Cmd_Argv(2)), interval,
		remaining);
}

/*
=============
SV_TimerDel_f

Removes a timer
=============
*/
void SV_TimerDel_f (void)
{	
	if (Cmd_Argc() < 2)
	{
		// help
		Com_Printf ("usage: removetimer <name>\n");
		Com_Printf ("   use \"timerlist\" to view all active timers' names.\n");
		return;
	}

	if (Timer_Delete(Cmd_Argv(1)))
		Com_Printf ("TimerDel: successfully deleted timer \"%s\"\n",
		Cmd_Argv (1));
	else
		Com_Printf ("TimerDel: couldn't find or delete timer \"%s\"\n",
		Cmd_Argv (1));
}

/*
=============
SV_TimerDelAll_f

Removes all timers
=============
*/
void SV_TimerDelAll_f (void)
{	
	qtimer_t		*timer = NULL;
	int				i;
	
	for (timer=timers, i=0 ; timer ; timer=timer->next, i++)
	{
		Com_Printf ("TimerDel: removing timer \"%s\".\n", timer->name);
		Timer_Delete (timer->name);
	}

	if (i)	Com_Printf ("TimerDel: removed %i timers.\n", i);
	else	Com_Printf ("TimerDel: no active timers to delete.\n");
}


/*
=============
SV_TimerList_f

Lists currently running timers
=============
*/
void SV_TimerList_f (void)
{	
	qtimer_t		*timer = NULL;
	int				i;


	Com_Printf ("num name            rep    int commands\n");
	Com_Printf ("--- --------------- --- ------ -------------------\n");

	for (timer=timers, i=0 ; timer ; timer=timer->next, i++)
		Com_Printf ("%3i %-15s %3i %6.1f %s\n", i+1, timer->name,
		timer->repetitions, timer->interval, timer->value);

	if (!i)
		Com_Printf ("no timers.\n");
}
