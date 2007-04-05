/* mm/cacao-gc/final.h - GC header for finalization and weak references

   Copyright (C) 2006 R. Grafl, A. Krall, C. Kruegel,
   C. Oates, R. Obermaisser, M. Platter, M. Probst, S. Ring,
   E. Steiner, C. Thalinger, D. Thuernbeck, P. Tomsich, C. Ullrich,
   J. Wenninger, Institut f. Computersprachen - TU Wien

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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.

   $Id$

*/


#ifndef _FINAL_H
#define _FINAL_H

#include "vm/types.h"

#include "toolbox/list.h"
#include "vmcore/method.h"


/* Global Variables ***********************************************************/

extern list *final_list;


/* Structures *****************************************************************/

typedef struct final_entry final_entry;

#define FINAL_REACHABLE   1
#define FINAL_RECLAIMABLE 2
#define FINAL_FINALIZING  3
#define FINAL_FINALIZED   4

struct final_entry {
	listnode           linkage;
	u4                 type;
	java_objectheader *o;
	methodinfo        *finalizer;
};


/* Prototypes *****************************************************************/

void final_init();
void final_register(java_objectheader *o, methodinfo *finalizer);
void final_invoke();
void final_set_all_reclaimable();


#endif /* _FINAL_H */

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
 * vim:noexpandtab:sw=4:ts=4:
 */