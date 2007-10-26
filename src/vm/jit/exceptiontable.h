/* src/vm/jit/exceptiontable.h - method exception table

   Copyright (C) 2007
   CACAOVM - Verein zu Foerderung der freien virtuellen Machine CACAO

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

*/


#ifndef _EXCEPTIONTABLE_H
#define _EXCEPTIONTABLE_H

/* forward typedefs ***********************************************************/

typedef struct exceptiontable_t       exceptiontable_t;
typedef struct exceptiontable_entry_t exceptiontable_entry_t;


#include "config.h"

#include <stdint.h>

#include "vm/jit/code.h"
#include "vm/jit/jit.h"


/* exceptiontable_t ***********************************************************/

struct exceptiontable_t {
	int32_t                 length;
	exceptiontable_entry_t *entries;
};


/* exceptiontable_entry_t *****************************************************/

struct exceptiontable_entry_t {
	void                  *endpc;
	void                  *startpc;
	void                  *handlerpc;
	classref_or_classinfo  catchtype;
};


/* function prototypes ********************************************************/

void exceptiontable_create(jitdata *jd);
void exceptiontable_free(codeinfo *code);

#if !defined(NDEBUG)
void exceptiontable_print(codeinfo *code);
#endif

#endif /* _EXCEPTIONTABLE_H */


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