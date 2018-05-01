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

	$Id: progs.h,v 1.16 2007/01/07 22:22:30 disconn3ct Exp $
*/

#ifndef __PROGS_H__
#define __PROGS_H__

typedef union eval_s
{
	string_t	string;
	float		_float;
	float		vector[3];
	func_t		function;
	int		_int;
	int		edict;
} eval_t;

#define	MAX_ENT_LEAFS	16
typedef struct edict_s
{
	qbool		free;
	link_t		area;			// linked to a division node or leaf

	int		num_leafs;
	short		leafnums[MAX_ENT_LEAFS];

	entity_state_t	baseline;

	float		freetime;		// sv.time when the object was freed
	double		lastruntime;		// sv.time when SV_RunEntity was last
						// called for this edict (Tonik)

	entvars_t	v;			// C exported fields from progs
// other fields from progs come immediately after
} edict_t;

// (type *)STRUCT_FROM_LINK(link_t *link, type, member)
// ent = STRUCT_FROM_LINK(link,entity_t,order)
// FIXME: remove this mess!
#define	STRUCT_FROM_LINK(l,t,m) ((t *)((byte *)l - (int)&(((t *)0)->m)))
#define	EDICT_FROM_AREA(l) STRUCT_FROM_LINK(l,edict_t,area)

//============================================================================

extern	dprograms_t	*progs;
extern	dfunction_t	*pr_functions;
extern	char		*pr_strings;
extern	ddef_t		*pr_globaldefs;
extern	ddef_t		*pr_fielddefs;
extern	dstatement_t	*pr_statements;
extern	globalvars_t	*pr_global_struct;
extern	float		*pr_globals;	// same as pr_global_struct

extern	int		pr_edict_size;	// in bytes
extern	int		pr_teamfield;
extern	cvar_t		sv_progsname; 

//============================================================================

void PR_Init (void);

void PR_ExecuteProgram (func_t fnum);
void PR_LoadProgs (void);

void PR_Profile_f (void);

edict_t *ED_Alloc (void);
void ED_Free (edict_t *ed);

char *ED_NewString (char *string);
// returns a copy of the string allocated from the server's string heap

void ED_Print (edict_t *ed);
void ED_Write (FILE *f, edict_t *ed);
char *ED_ParseEdict (char *data, edict_t *ent);

void ED_WriteGlobals (FILE *f);
void ED_ParseGlobals (char *data);

void ED_LoadFromFile (char *data);

//define EDICT_NUM(n) ((edict_t *)(sv.edicts+ (n)*pr_edict_size))
//define NUM_FOR_EDICT(e) (((byte *)(e) - sv.edicts)/pr_edict_size)

edict_t *EDICT_NUM(int n);
int NUM_FOR_EDICT(edict_t *e);

#define	NEXT_EDICT(e) ((edict_t *)( (byte *)e + pr_edict_size))

#define	EDICT_TO_PROG(e) ((byte *)e - (byte *)sv.edicts)
#define PROG_TO_EDICT(e) ((edict_t *)((byte *)sv.edicts + e))

//============================================================================

#define	G_FLOAT(o) (pr_globals[o])
#define	G_FLOAT_FTE(o) (((float *)pr_globals)[o])		// FTE
#define	G_INT(o) (*(int *)&pr_globals[o])
#define	G_INT_FTE(o) (((int *)pr_globals)[o])
#define	G_EDICT(o) ((edict_t *)((byte *)sv.edicts+ *(int *)&pr_globals[o]))
#define G_EDICTNUM(o) NUM_FOR_EDICT(G_EDICT(o))
#define	G_VECTOR(o) (&pr_globals[o])
#define	G_STRING(o) (PR_GetString(*(string_t *)&pr_globals[o]))
#define	G_STRING_TOMAZ(o) (pr_strings + *(string_t *)&pr_globals[o])

#define	G_FUNCTION(o) (*(func_t *)&pr_globals[o])

#define	E_FLOAT(e,o) (((float*)&e->v)[o])
#define	E_INT(e,o) (*(int *)&((float*)&e->v)[o])
#define	E_VECTOR(e,o) (&((float*)&e->v)[o])
#define	E_STRING(e,o) (PR_GetString(*(string_t *)&((float*)&e->v)[o]))

extern	int		type_size[8];

typedef void		(*builtin_t) (void);
//typedef void (*builtin_t) (progfuncs_t, globalvars_s2);
extern	builtin_t	*pr_builtins;
extern	int		pr_numbuiltins;

extern	int		pr_argc;

extern	qbool	pr_trace;
extern	dfunction_t	*pr_xfunction;
extern	int		pr_xstatement;

extern func_t SpectatorConnect, SpectatorDisconnect, SpectatorThink;
extern func_t GE_ClientCommand, GE_PausedTic, GE_ShouldPause;

#ifdef VWEP_TEST
extern int fofs_vw_index, fofs_vw_frame;
#endif
extern int fofs_items2; // ZQ_ITEMS2 extension
extern int fofs_gravity, fofs_maxspeed;

#define EdictFieldFloat(ed, fieldoffset) ((eval_t *)((byte *)&(ed)->v + (fieldoffset)))->_float

extern func_t ClientCommand; // OfN

void PR_RunError (char *error, ...);

void ED_PrintEdicts (void);
void ED_PrintNum (int ent);

eval_t *GetEdictFieldValue(edict_t *ed, char *field);

int ED_FindFieldOffset (char *field);

//
// PR STrings stuff
//
#define MAX_PRSTR 4096/*1024*/	// xav: increasing this do anything? :|

extern char *pr_strtbl[MAX_PRSTR];
extern char *pr_newstrtbl[MAX_PRSTR];
extern int num_prstr;

char *PR_GetString(int num);
int PR_SetString(char *s);
int PR_SetTmpString(char *s);

// pr_cmds.c
void PR_InitBuiltins (void);

#endif /* !__PROGS_H__ */



typedef struct globalvars_s2
{
	int null;
	union {
		vec3_t vec;
		float f;
		int i;
	} ret;
	union {
		vec3_t vec;
		float f;
		int i;
	} param[8];
} globalvars_t2;

// xav: finish adding stuff here..
typedef struct progexterns_s {
	void (/*VARGS */*Abort) (char *, ...);
} progparms_t, progexterns_t;

struct progfuncs_s {
	int progsversion;	//PROGSTRUCT_VERSION

	char *stringtable;	//qc strings are all relative. add to a qc string. this is required for support of frikqcc progs that strip string immediates.
	int fieldadjust;	//FrikQCC style arrays can cause problems due to field remapping. This causes us to leave gaps but offsets identical.

	struct globalvars_s2	*(*globals) (progfuncs_t, progsnum_t);	//get the globals of a progs

	int		*pr_trace;	//start calling the editor for each line executed

	struct	progexterns_s *parms;	//these are the initial parms, they may be changed

	void	(*StackTrace)				(progfuncs_t/* *prinst*/);

	void	(*AbortStack)				(progfuncs_t/* *prinst*/);	//annigilates the current stack, positioning on a return statement. It is expected that this is only used via a builtin!

	string_t (*TempString)				(progfuncs_t/* *prinst*//* *str*/);

	char	*tempstringbase;				//for engine's use. Store your base tempstring pointer here.
	int		tempstringnum;			//for engine's use.
};

typedef struct progfuncs_s progfuncs_t;