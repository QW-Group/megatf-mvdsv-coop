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

// dynamic timers
#include "rcon.h"

typedef struct qtimer_s
{
	char			*name;			// name of the timer (0 = endless)
	int				repetitions;	// repetitions of execution
	float			interval;		// interval in seconds
	char			*value;			// buffer command(s) to execute

	double			lasttime;		// last time it was executed
	unsigned int	executions;		// current total executions (limited by repetitions)
//	qbool			paused;
	rcon_struct		*rcon_account;	// pointer to the rcon account struct of the user who created this timer
	
	struct qtimer_s	*hash_next;
	struct qtimer_s	*next;
} qtimer_t;

qtimer_t			*timers;
static qtimer_t		*timer_hash[32];

void Timer_AccountRemoved (rcon_struct *account);