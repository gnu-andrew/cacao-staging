/* vm/jit/code.c - codeinfo struct for representing compiled code

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

   Authors: Edwin Steiner

   Changes:

   $Id$

*/


#include "vm/jit/code.h"
#include "mm/memory.h"

codeinfo *code_codeinfo_new(methodinfo *m)
{
	codeinfo *code;

	code = NEW(codeinfo);

	memset(code,0,sizeof(codeinfo));

	code->m = m;
	
	return code;
}

void code_codeinfo_free(codeinfo *code)
{
	if (!code)
		return;

	if (code->mcode)
		CFREE((void *) (ptrint) code->mcode, code->mcodelength);

	replace_free_replacement_points(code);

	FREE(code,codeinfo);
}

void code_free_code_of_method(methodinfo *m)
{
	codeinfo *nextcode;
	codeinfo *code;
	
	nextcode = m->code;
	while (nextcode) {
		code = nextcode;
		nextcode = code->prev;
		code_codeinfo_free(code);
	}
}

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
