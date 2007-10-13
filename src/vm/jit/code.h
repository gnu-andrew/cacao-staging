/* src/vm/jit/code.h - codeinfo struct for representing compiled code

   Copyright (C) 1996-2005, 2006, 2007 R. Grafl, A. Krall, C. Kruegel,
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

*/


#ifndef _CODE_H
#define _CODE_H

#include "config.h"

#include <stdint.h>

#include "vm/types.h"

#include "toolbox/list.h"

#include "vm/global.h"

#include "vm/jit/replace.h"

#include "vmcore/method.h"


/* constants ******************************************************************/

#define CODE_FLAG_INVALID         0x0001
#define CODE_FLAG_LEAFMETHOD      0x0002
#define CODE_FLAG_SYNCHRONIZED    0x0004


/* codeinfo *******************************************************************

   A codeinfo represents a particular realization of a method in
   machine code.

   ATTENTION: The methodinfo entry in the code-structure MUST have the
   offset 0, otherwise we have a problem in our compiler stub. This is
   checked with an assert in code_init().

*******************************************************************************/

struct codeinfo {
	methodinfo   *m;                    /* method this is a realization of    */
	codeinfo     *prev;                 /* previous codeinfo of this method   */

	uint32_t      flags;                /* OR of CODE_FLAG_ constants         */

	u1            optlevel;             /* optimization level of this code    */
	s4            basicblockcount;      /* number of basic blocks             */

	int32_t       synchronizedoffset;   /* stack offset of synchronized obj.  */

	/* machine code */
	u1           *mcode;                /* pointer to machine code            */
	u1           *entrypoint;           /* machine code entry point           */
	s4            mcodelength;          /* length of generated machine code   */

	/* patcher list */
	list_t       *patchers;

	/* replacement */				    
#if defined(ENABLE_REPLACEMENT)
	rplpoint     *rplpoints;            /* replacement points                 */
	rplalloc     *regalloc;             /* register allocation info           */
	s4            rplpointcount;        /* number of replacement points       */
	s4            globalcount;          /* number of global allocations       */
	s4            regalloccount;        /* number of total allocations        */
	s4            memuse;               /* number of arg + local slots        */
	s4            stackframesize;       /* size of the stackframe in slots    */
	u1            savedintcount;        /* number of callee saved int regs    */
	u1            savedfltcount;        /* number of callee saved flt regs    */
# if defined(HAS_ADDRESS_REGISTER_FILE)
	u1            savedadrcount;        /* number of callee saved adr regs    */
# endif
	u1           *savedmcode;           /* saved code under patches           */
#endif

#if defined(ENABLE_PROFILING)
	u4            frequency;            /* number of method invocations       */
	u4           *bbfrequency;		    
	s8            cycles;               /* number of cpu cycles               */
#endif
};


/* inline functions ***********************************************************/

/* code_xxx_invalid ************************************************************

   Functions for CODE_FLAG_INVALID.

*******************************************************************************/

inline static int code_is_invalid(codeinfo *code)
{
	return (code->flags & CODE_FLAG_INVALID);
}

inline static void code_flag_invalid(codeinfo *code)
{
	code->flags |= CODE_FLAG_INVALID;
}

inline static void code_unflag_invalid(codeinfo *code)
{
	code->flags &= ~CODE_FLAG_INVALID;
}


/* code_xxx_leafmethod *********************************************************

   Functions for CODE_FLAG_LEAFMETHOD.

*******************************************************************************/

inline static int code_is_leafmethod(codeinfo *code)
{
	return (code->flags & CODE_FLAG_LEAFMETHOD);
}

inline static void code_flag_leafmethod(codeinfo *code)
{
	code->flags |= CODE_FLAG_LEAFMETHOD;
}

inline static void code_unflag_leafmethod(codeinfo *code)
{
	code->flags &= ~CODE_FLAG_LEAFMETHOD;
}


/* code_xxx_synchronized *******************************************************

   Functions for CODE_FLAG_SYNCHRONIZED.

*******************************************************************************/

inline static int code_is_synchronized(codeinfo *code)
{
	return (code->flags & CODE_FLAG_SYNCHRONIZED);
}

inline static void code_flag_synchronized(codeinfo *code)
{
	code->flags |= CODE_FLAG_SYNCHRONIZED;
}

inline static void code_unflag_synchronized(codeinfo *code)
{
	code->flags &= ~CODE_FLAG_SYNCHRONIZED;
}


/* function prototypes ********************************************************/

bool code_init(void);

codeinfo *code_codeinfo_new(methodinfo *m);
void code_codeinfo_free(codeinfo *code);

codeinfo *code_find_codeinfo_for_pc(u1 *pc);
codeinfo *code_find_codeinfo_for_pc_nocheck(u1 *pc);

codeinfo   *code_get_codeinfo_for_pv(u1 *pv);
methodinfo *code_get_methodinfo_for_pv(u1 *pv);

#if defined(ENABLE_REPLACEMENT)
int code_get_sync_slot_count(codeinfo *code);
#endif /* defined(ENABLE_REPLACEMENT) */

void code_free_code_of_method(methodinfo *m);

#endif /* _CODE_H */


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
