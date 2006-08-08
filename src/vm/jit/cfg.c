/* src/vm/cfg.c - build a control-flow graph

   Copyright (C) 2006 R. Grafl, A. Krall, C. Kruegel, C. Oates,
   R. Obermaisser, M. Platter, M. Probst, S. Ring, E. Steiner,
   C. Thalinger, D. Thuernbeck, P. Tomsich, C. Ullrich, J. Wenninger,
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

   Changes:

   $Id: cacao.c 4357 2006-01-22 23:33:38Z twisti $

*/


#include "config.h"

#include <assert.h>

#include "vm/types.h"

#include "mm/memory.h"
#include "vm/jit/jit.h"


/* cfg_allocate_predecessors ***************************************************

   Allocates the predecessor array, if there is none, and resets the
   predecessor count.

*******************************************************************************/

static void cfg_allocate_predecessors(basicblock *bptr)
{
	if (bptr->predecessors == NULL) {
		bptr->predecessors = DMNEW(basicblock*, bptr->predecessorcount);

		bptr->predecessorcount = 0;
	}
}


/* cfg_allocate_successors *****************************************************

   Allocates the succecessor array, if there is none, and resets the
   predecessor count.

*******************************************************************************/

static void cfg_allocate_successors(basicblock *bptr)
{
	if (bptr->successors == NULL) {
		bptr->successors = DMNEW(basicblock*, bptr->successorcount);

		bptr->successorcount = 0;
	}
}


/* cfg_insert_predecessor ******************************************************

   Inserts a predecessor into the array, but checks for duplicate
   entries.  This is used for TABLESWITCH and LOOKUPSWITCH.

*******************************************************************************/

static void cfg_insert_predecessors(basicblock *bptr, basicblock *pbptr)
{
	basicblock **tbptr;
	s4           i;

	tbptr = bptr->predecessors;

	/* check if the predecessors is already stored in the array */

	for (i = 0; i < bptr->predecessorcount; i++, tbptr++)
		if (*tbptr == pbptr)
			return;

	/* not found, insert it */

	bptr->predecessors[bptr->predecessorcount] = pbptr;
	bptr->predecessorcount++;
}


/* cfg_build *******************************************************************

   Build a control-flow graph in finding all predecessors and
   successors for the basic blocks.

*******************************************************************************/

bool cfg_build(jitdata *jd)
{
	methodinfo  *m;
	basicblock  *bptr;
	basicblock  *tbptr;
	basicblock  *ntbptr;
	instruction *iptr;
	s4          *s4ptr;
	s4           i;
	s4           n;

	/* get required compiler data */

	m = jd->m;

	/* process all basic blocks to find the predecessor/successor counts */

	bptr = m->basicblocks;

	for (i = 0; i < m->basicblockcount; i++, bptr++) {
		if (bptr->icount == 0)
			continue;

		iptr = bptr->iinstr + bptr->icount - 1;

		switch (iptr->opc) {
		case ICMD_RET:
		case ICMD_RETURN:
		case ICMD_IRETURN:
		case ICMD_LRETURN:
		case ICMD_FRETURN:
		case ICMD_DRETURN:
		case ICMD_ARETURN:
		case ICMD_ATHROW:
			break;

		case ICMD_IFEQ:
		case ICMD_IFNE:
		case ICMD_IFLT:
		case ICMD_IFGE:
		case ICMD_IFGT:
		case ICMD_IFLE:

		case ICMD_IFNULL:
		case ICMD_IFNONNULL:

		case ICMD_IF_ICMPEQ:
		case ICMD_IF_ICMPNE:
		case ICMD_IF_ICMPLT:
		case ICMD_IF_ICMPGE:
		case ICMD_IF_ICMPGT:
		case ICMD_IF_ICMPLE:

		case ICMD_IF_ACMPEQ:
		case ICMD_IF_ACMPNE:
			bptr->successorcount += 2;

			tbptr  = m->basicblocks + m->basicblockindex[iptr->op1];
			ntbptr = bptr->next;

			tbptr->predecessorcount++;
			ntbptr->predecessorcount++;
			break;

		case ICMD_GOTO:
			bptr->successorcount++;

			tbptr = m->basicblocks + m->basicblockindex[iptr->op1];
			tbptr->predecessorcount++;
			break;

		case ICMD_TABLESWITCH:
			s4ptr = iptr->val.a;

			bptr->successorcount++;

			tbptr = m->basicblocks + m->basicblockindex[*s4ptr++];
			tbptr->predecessorcount++;

			n = *s4ptr++;                               /* low     */
			n = *s4ptr++ - n + 1;                       /* high    */

			while (--n >= 0) {
				bptr->successorcount++;

				tbptr = m->basicblocks + m->basicblockindex[*s4ptr++];
				tbptr->predecessorcount++;
			}
			break;
					
		case ICMD_LOOKUPSWITCH:
			s4ptr = iptr->val.a;

			bptr->successorcount++;

			tbptr = m->basicblocks + m->basicblockindex[*s4ptr++];
			tbptr->predecessorcount++;

			n = *s4ptr++;                               /* count   */

			while (--n >= 0) {
				bptr->successorcount++;

				tbptr = m->basicblocks + m->basicblockindex[s4ptr[1]];
				tbptr->predecessorcount++;

				s4ptr += 2;
			}
			break;

		default:
			bptr->successorcount++;

			tbptr = bptr + 1;
			tbptr->predecessorcount++;
			break;
		}
	}

	/* Second iteration to allocate the arrays and insert the basic
	   block pointers. */

	bptr = m->basicblocks;

	for (i = 0; i < m->basicblockcount; i++, bptr++) {
		if (bptr->icount == 0)
			continue;

		iptr = bptr->iinstr + bptr->icount - 1;

		switch (iptr->opc) {
		case ICMD_RET:
		case ICMD_RETURN:
		case ICMD_IRETURN:
		case ICMD_LRETURN:
		case ICMD_FRETURN:
		case ICMD_DRETURN:
		case ICMD_ARETURN:
		case ICMD_ATHROW:
			break;

		case ICMD_IFEQ:
		case ICMD_IFNE:
		case ICMD_IFLT:
		case ICMD_IFGE:
		case ICMD_IFGT:
		case ICMD_IFLE:

		case ICMD_IFNULL:
		case ICMD_IFNONNULL:

		case ICMD_IF_ICMPEQ:
		case ICMD_IF_ICMPNE:
		case ICMD_IF_ICMPLT:
		case ICMD_IF_ICMPGE:
		case ICMD_IF_ICMPGT:
		case ICMD_IF_ICMPLE:

		case ICMD_IF_ACMPEQ:
		case ICMD_IF_ACMPNE:
			tbptr  = m->basicblocks + m->basicblockindex[iptr->op1];
			ntbptr = bptr->next;

			cfg_allocate_successors(bptr);

			bptr->successors[0] = tbptr;
			bptr->successors[1] = ntbptr;
			bptr->successorcount += 2;

			cfg_allocate_predecessors(tbptr);
			cfg_allocate_predecessors(ntbptr);

			tbptr->predecessors[tbptr->predecessorcount] = bptr;
			tbptr->predecessorcount++;

			ntbptr->predecessors[ntbptr->predecessorcount] = bptr;
			ntbptr->predecessorcount++;
			break;

		case ICMD_GOTO:
			tbptr = m->basicblocks + m->basicblockindex[iptr->op1];

			cfg_allocate_successors(bptr);

			bptr->successors[0] = tbptr;
			bptr->successorcount++;

			cfg_allocate_predecessors(tbptr);

			tbptr->predecessors[tbptr->predecessorcount] = bptr;
			tbptr->predecessorcount++;
			break;

		case ICMD_TABLESWITCH:
			s4ptr = iptr->val.a;

			tbptr = m->basicblocks + m->basicblockindex[*s4ptr++];

			cfg_allocate_successors(bptr);

			bptr->successors[0] = tbptr;
			bptr->successorcount++;

			cfg_allocate_predecessors(tbptr);

			tbptr->predecessors[tbptr->predecessorcount] = bptr;
			tbptr->predecessorcount++;

			n = *s4ptr++;                               /* low     */
			n = *s4ptr++ - n + 1;                       /* high    */

			while (--n >= 0) {
				tbptr = m->basicblocks + m->basicblockindex[*s4ptr++];

				bptr->successors[bptr->successorcount] = tbptr;
				bptr->successorcount++;

				cfg_allocate_predecessors(tbptr);
				cfg_insert_predecessors(tbptr, bptr);
			}
			break;
					
		case ICMD_LOOKUPSWITCH:
			s4ptr = iptr->val.a;

			tbptr = m->basicblocks + m->basicblockindex[*s4ptr++];

			cfg_allocate_successors(bptr);

			bptr->successors[0] = tbptr;
			bptr->successorcount++;

			cfg_allocate_predecessors(tbptr);

			tbptr->predecessors[tbptr->predecessorcount] = bptr;
			tbptr->predecessorcount++;

			n = *s4ptr++;                               /* count   */

			while (--n >= 0) {
				tbptr = m->basicblocks + m->basicblockindex[s4ptr[1]];

				bptr->successors[bptr->successorcount] = tbptr;
				bptr->successorcount++;

				cfg_allocate_predecessors(tbptr);
				cfg_insert_predecessors(tbptr, bptr);

				s4ptr += 2;
			}
			break;

		default:
			tbptr = bptr + 1;

			cfg_allocate_successors(bptr);

			bptr->successors[0] = tbptr;
			bptr->successorcount++;

			cfg_allocate_predecessors(tbptr);

			tbptr->predecessors[tbptr->predecessorcount] = bptr;
			tbptr->predecessorcount++;
			break;
		}
	}

	/* everything's ok */

	return true;
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
