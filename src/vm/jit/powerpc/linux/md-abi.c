/* src/vm/jit/powerpc/linux/md-abi.c - functions for PowerPC Linux ABI

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

   Authors: Christian Thalinger

   Changes: Christian Ullrich

   $Id: md-abi.c 2813 2005-06-23 14:30:44Z christian $

*/


#include "vm/jit/powerpc/types.h"
#include "vm/jit/powerpc/linux/md-abi.h"

#include "vm/descriptor.h"
#include "vm/global.h"


#define _ALIGN(a)    do { if ((a) & 1) (a)++; } while (0)


/* md_param_alloc **************************************************************

 Allocate Arguments to Stackslots according the Calling Conventions

--- in
md->paramcount:           Number of arguments for this method
md->paramtypes[].type:    Argument types

--- out
md->params[].inmemory:    Argument spilled on stack
md->params[].regoff:      Stack offset or rd->arg[int|flt]regs index
md->memuse:               Stackslots needed for argument spilling
md->argintreguse:         max number of integer arguments used
md->argfltreguse:         max number of float arguments used

*******************************************************************************/

void md_param_alloc(methoddesc *md)
{
	paramdesc *pd;
	s4         i;
	s4         iarg;
	s4         farg;
	s4         stacksize;

	/* set default values */

	iarg = 0;
	farg = 0;
	stacksize = LA_WORD_SIZE;

	/* get params field of methoddesc */

	pd = md->params;

	for (i = 0; i < md->paramcount; i++, pd++) {
		switch (md->paramtypes[i].type) {
		case TYPE_INT:
		case TYPE_ADR:
			if (iarg < INT_ARG_CNT) {
				pd->inmemory = false;
				pd->regoff = iarg;

			} else {
				pd->inmemory = true;
				pd->regoff = stacksize;
			}
			if (iarg < INT_ARG_CNT)
				iarg++;
			else
				stacksize++;
			break;
		case TYPE_LNG:
			if (iarg < INT_ARG_CNT - 1)
				_ALIGN(iarg);
			else
				_ALIGN(stacksize);
			if (iarg < INT_ARG_CNT - 1) {
				pd->inmemory = false;
				                             /* rd->arg[int|flt]regs index !! */
				pd->regoff = PACK_REGS(iarg + 1, iarg); 
			} else {
				pd->inmemory = true;
				pd->regoff = stacksize;
			}
			if (iarg < INT_ARG_CNT - 1)
				iarg += 2;
			else {
				iarg = INT_ARG_CNT;
				stacksize += 2;
			}
			break;
		case TYPE_FLT:
		case TYPE_DBL:
			if ((farg >= FLT_ARG_CNT) && IS_2_WORD_TYPE(md->paramtypes[i].type))
				_ALIGN(stacksize);
			if (farg < FLT_ARG_CNT) {
				pd->inmemory = false;
				pd->regoff = farg;
			} else {
				pd->inmemory = true;
				pd->regoff = stacksize;
			}
			if (farg < FLT_ARG_CNT)
				farg++;
			else
				if (IS_2_WORD_TYPE(md->paramtypes[i].type))
					stacksize += 2;
				else
					stacksize++;
			break;
		}
	}

	/* fill register and stack usage */

	md->argintreguse = iarg;
	md->argfltreguse = farg;
	md->memuse = stacksize;
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
 */
