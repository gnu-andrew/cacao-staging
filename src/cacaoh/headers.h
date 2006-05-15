/* src/cacaoh/headers.h - export functions for header generation

   Copyright (C) 1996-2005, 2006 R. Grafl, A. Krall, C. Kruegel,
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

   Contact: cacao@cacaojvm.org

   Authors: Christian Thalinger

   $Id: headers.h 4921 2006-05-15 14:24:36Z twisti $

*/

#ifndef _HEADERS_H
#define _HEADERS_H

#include "config.h"

#if defined(ENABLE_THREADS)
# include "threads/native/threads.h"
#endif

#include "toolbox/chain.h"
#include "vm/class.h"
#include "vm/global.h"
#include "vm/method.h"
#include "vm/utf8.h"


/* export variables ***********************************************************/

extern THREADSPECIFIC java_objectheader *_exceptionptr;
extern chain *nativemethod_chain;
extern chain *nativeclass_chain;
extern chain *ident_chain;
extern FILE *file;


/* function prototypes ********************************************************/

void printID(utf *u);
void printOverloadPart(utf *desc);
void literalstring_free(java_objectheader *o);
void printmethod(methodinfo *m);
void gen_header_filename(char *buffer, utf *u);
void headerfile_generate(classinfo *c, char *opt_directory);
void print_classname(classinfo *clazz);

#endif /* _HEADERS_H */


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
