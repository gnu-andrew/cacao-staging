/* jit/lsra.inc - linear scan register allocator header

   Copyright (C) 1996-2005 R. Grafl, A. Krall, C. Kruegel, C. Oates,
   R. Obermaisser, M. Platter, M. Probst, S. Ring, E. Steiner,
   C. Thalinger, D. Thuernbeck, P. Tomsich, C. Ullrich, J. Wenninger,
   Institut f. Computersprachen - TU Wien

   This file is part of CACAO.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA.

   Contact: cacao@complang.tuwien.ac.at

   Authors: Christian Ullrich

   $Id: lsra.h 1735 2004-12-07 14:33:27Z twisti $

*/


#ifndef _LSRA_H
#define _LSRA_H

#include "loop/loop.h"

#define LSRA_STORE 1
#define LSRA_LOAD 0
#define LSRA_POP -1

struct lifetime {
	int i_start; /* instruction number of first use */
	int i_end; /* instruction number of last use */
	int v_index; /* local variable index or negative for stackslots */
	int type;
	int usagecount; /* number of references*/
	int reg; /* regoffset durch lsra zugewiesen */
	int savedvar;
	struct stackslot *passthrough; /* List of Stackslots in Lifetime, which are passed through a Basic Block */
	struct stackslot *local_ss; /* Stackslots for this Lifetime or NULL (=="pure" Local Var) */
	struct _i_list *i_list; /* list of instructions with references to var */
	struct lifetime *next;
};

struct active_lt {
	struct lifetime *lt;
	struct active_lt *next;
};

struct l_loop {
	int b_first;
	int b_last;
	int nesting;
};

struct b_loop {
	int loop;
	int instr;
};

struct _i_list {
	int b_index;
	int instr;
	int store;
	struct _i_list *next;
};

struct stackslot {
	stackptr s;
	int bb;
	struct stackslot *next;
};

struct lsra_reg {
	int reg_index;
	int use;
};

struct lsradata {
	struct lifetime **ss_lifetimes;
	struct lifetime **locals_lifetimes;
	struct lifetime *lifetimes;
	struct stackslot *stackslots;
	struct active_lt *active_tmp, *active_sav;
	int active_sav_count, active_tmp_count;
	int var_index;
	bool back_edge_panic;
};

struct freemem {
	int off;
	int end;
	struct freemem *next;
};

struct dup {
	struct stackslot *ss;
	struct dup *next;
};

typedef struct lsradata lsradata;

/* function prototypes */
void lsra(methodinfo *, codegendata *, registerdata *, loopdata *, t_inlining_globals *);
void lsra_init(methodinfo *, codegendata *, t_inlining_globals *, lsradata *);
void lsra_setup(methodinfo *, codegendata *, registerdata *, lsradata *, loopdata *);
void lsra_clean_Graph( methodinfo *, codegendata *, lsradata *, loopdata *);

#ifdef LSRA_DEBUG
void lsra_dump_Graph(methodinfo *, struct depthElement **);
void lsra_dump_stack(stackptr );
#endif
#ifdef LSRA_PRINTLIFETIMES
void print_lifetimes(registerdata *, struct lifetime *);
#endif

int lsra_get_sbr_end(methodinfo *, loopdata *, int , int *);
void lsra_scan_registers_canditates(methodinfo *, lsradata *);
void lsra_join_lifetimes( methodinfo *, codegendata *, lsradata *, loopdata *);
int lsra_getmaxblock(methodinfo *, loopdata *, int );
void lsra_calc_lifetime_length(methodinfo *, lsradata *, codegendata *, loopdata *);

void lsra_merge_i_lists(struct lifetime *, struct lifetime *);
void lsra_merge_local_ss(struct lifetime *, struct lifetime *);

void _lsra_new_stack( lsradata *, stackptr , int , int, int);
void _lsra_from_stack(lsradata *, stackptr , int , int, int);
void lsra_add_ss(struct lifetime *, stackptr );
void lsra_usage_local(lsradata *, s4 , int , int , int , int );
void lsra_new_local(lsradata *, s4 , int );
struct _i_list *lsra_add_i_list(struct _i_list *, int, int ,int );

void lsra_sort_lt(struct lifetime **);
void lsra_main(methodinfo *, lsradata *, registerdata *);
void _lsra_main( methodinfo *, lsradata *, struct lifetime *, struct lsra_reg *, int , int , int *, int *);
void lsra_expire_old_intervalls(lsradata *, struct lifetime *, struct lsra_reg *);
void _lsra_expire_old_intervalls(struct lifetime *, struct lsra_reg *, struct active_lt **, int *);
void spill_at_intervall(lsradata *, struct lifetime *);
void _spill_at_intervall(struct lifetime *, struct active_lt **, int *);
void lsra_add_active(struct lifetime *, struct active_lt **, int *);
void lsra_alloc(methodinfo *, registerdata *, struct lifetime *, int *);
int lsra_getmem(struct lifetime *, struct freemem *, int *);
struct freemem *lsra_getnewmem(int *);
void lsra_align_stackslots(struct lsradata *, stackptr, stackptr);
void lsra_setflags(int *, int);
#endif /* _LSRA_H */


/*
 * These are local overrides for various environment variables in Emacs.
 * Please do not remove this and leave it at the end of the file, where
 * Emacs will automagically detect them.
 * ---------------------------------------------------------------------
 * Local variables:
 * mode: c
 * indent-tabs-mode: t
 * c-basic-offset: 4
 * tab-width: 4
 * End:
 */
