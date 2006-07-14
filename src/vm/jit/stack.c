/* src/vm/jit/stack.c - stack analysis

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

   Authors: Andreas Krall

   Changes: Edwin Steiner
            Christian Thalinger
            Christian Ullrich

   $Id: stack.c 5136 2006-07-14 17:05:12Z edwin $

*/


#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "vm/types.h"

#include "arch.h"
#include "md-abi.h"

#include "mm/memory.h"
#include "native/native.h"
#include "toolbox/logging.h"
#include "vm/global.h"
#include "vm/builtin.h"
#include "vm/options.h"
#include "vm/resolve.h"
#include "vm/statistics.h"
#include "vm/stringlocal.h"
#include "vm/jit/codegen-common.h"
#include "vm/jit/abi.h"
#include "vm/jit/show.h"

#if defined(ENABLE_DISASSEMBLER)
# include "vm/jit/disass.h"
#endif

#include "vm/jit/jit.h"
#include "vm/jit/stack.h"

#if defined(ENABLE_LSRA)
# include "vm/jit/allocator/lsra.h"
#endif

/*#define STACK_VERBOSE*/


/* macro for saving #ifdefs ***************************************************/

#if defined(ENABLE_INTRP)
#define IF_INTRP(x) if (opt_intrp) { x }
#define IF_NO_INTRP(x) if (!opt_intrp) { x }
#else
#define IF_INTRP(x)
#define IF_NO_INTRP(x) { x }
#endif

#if defined(ENABLE_INTRP)
#if defined(ENABLE_JIT)
#define IF_JIT(x) if (!opt_intrp) { x }
#else
#define IF_JIT(x)
#endif
#else /* !defined(ENABLE_INTRP) */
#define IF_JIT(x) { x }
#endif /* defined(ENABLE_INTRP) */

#if defined(ENABLE_STATISTICS)
#define STATISTICS_STACKDEPTH_DISTRIBUTION(distr)                    \
	do {                                                             \
		if (opt_stat) {                                              \
			if (stackdepth >= 10)                                    \
				count_store_depth[10]++;                             \
			else                                                     \
				count_store_depth[stackdepth]++;                     \
		}                                                            \
	} while (0)
#else /* !defined(ENABLE_STATISTICS) */
#define STATISTICS_STACKDEPTH_DISTRIBUTION(distr)
#endif

/* stack_init ******************************************************************

   Initialized the stack analysis subsystem (called by jit_init).

*******************************************************************************/

bool stack_init(void)
{
	return true;
}


/* stack_analyse ***************************************************************

   Analyse_stack uses the intermediate code created by parse.c to
   build a model of the JVM operand stack for the current method.
   
   The following checks are performed:
     - check for operand stack underflow (before each instruction)
     - check for operand stack overflow (after[1] each instruction)
     - check for matching stack depth at merging points
     - check for matching basic types[2] at merging points
     - check basic types for instruction input (except for BUILTIN*
           opcodes, INVOKE* opcodes and MULTIANEWARRAY)
   
   [1]) Checking this after the instruction should be ok. parse.c
   counts the number of required stack slots in such a way that it is
   only vital that we don't exceed `maxstack` at basic block
   boundaries.
   
   [2]) 'basic types' means the distinction between INT, LONG, FLOAT,
   DOUBLE and ADDRESS types. Subtypes of INT and different ADDRESS
   types are not discerned.

*******************************************************************************/

#define BLOCK_OF(index)                                              \
    (jd->new_basicblocks + jd->new_basicblockindex[index])

#define CLR_S1                                                       \
    (iptr->s1.var = NULL)

#define USE_S1_LOCAL(type1)

#define USE_S1(type1)                                                \
    do {                                                             \
        REQUIRE_1;                                                   \
        CHECK_BASIC_TYPE(type1, curstack->type);                     \
        iptr->s1.var = curstack;                                     \
    } while (0)

#define USE_S1_ANY                                                   \
    do {                                                             \
        REQUIRE_1;                                                   \
        iptr->s1.var = curstack;                                     \
    } while (0)

#define USE_S1_S2(type1, type2)                                      \
    do {                                                             \
        REQUIRE_2;                                                   \
        CHECK_BASIC_TYPE(type1, curstack->prev->type);               \
        CHECK_BASIC_TYPE(type2, curstack->type);                     \
        iptr->sx.s23.s2.var = curstack;                              \
        iptr->s1.var = curstack->prev;                               \
    } while (0)

#define USE_S1_S2_ANY_ANY                                            \
    do {                                                             \
        REQUIRE_2;                                                   \
        iptr->sx.s23.s2.var = curstack;                              \
        iptr->s1.var = curstack->prev;                               \
    } while (0)

#define USE_S1_S2_S3(type1, type2, type3)                            \
    do {                                                             \
        REQUIRE_3;                                                   \
        CHECK_BASIC_TYPE(type1, curstack->prev->prev->type);         \
        CHECK_BASIC_TYPE(type2, curstack->prev->type);               \
        CHECK_BASIC_TYPE(type3, curstack->type);                     \
        iptr->sx.s23.s3.var = curstack;                              \
        iptr->sx.s23.s2.var = curstack->prev;                        \
        iptr->s1.var = curstack->prev->prev;                         \
    } while (0)

#define POP_S1(type1)                                                \
    do {                                                             \
        USE_S1(type1);                                               \
        if (curstack->varkind == UNDEFVAR)                           \
            curstack->varkind = TEMPVAR;                             \
        curstack = curstack->prev;                                   \
    } while (0)

#define POP_S1_ANY                                                   \
    do {                                                             \
        USE_S1_ANY;                                                  \
        if (curstack->varkind == UNDEFVAR)                           \
            curstack->varkind = TEMPVAR;                             \
        curstack = curstack->prev;                                   \
    } while (0)

#define POP_S1_S2(type1, type2)                                      \
    do {                                                             \
        USE_S1_S2(type1, type2);                                     \
        if (curstack->varkind == UNDEFVAR)                           \
            curstack->varkind = TEMPVAR;                             \
        if (curstack->prev->varkind == UNDEFVAR)                     \
            curstack->prev->varkind = TEMPVAR;                       \
        curstack = curstack->prev->prev;                             \
    } while (0)

#define POP_S1_S2_ANY_ANY                                            \
    do {                                                             \
        USE_S1_S2_ANY_ANY;                                           \
        if (curstack->varkind == UNDEFVAR)                           \
            curstack->varkind = TEMPVAR;                             \
        if (curstack->prev->varkind == UNDEFVAR)                     \
            curstack->prev->varkind = TEMPVAR;                       \
        curstack = curstack->prev->prev;                             \
    } while (0)

#define POP_S1_S2_S3(type1, type2, type3)                            \
    do {                                                             \
        USE_S1_S2_S3(type1, type2, type3);                           \
        if (curstack->varkind == UNDEFVAR)                           \
            curstack->varkind = TEMPVAR;                             \
        if (curstack->prev->varkind == UNDEFVAR)                     \
            curstack->prev->varkind = TEMPVAR;                       \
        if (curstack->prev->prev->varkind == UNDEFVAR)               \
            curstack->prev->prev->varkind = TEMPVAR;                 \
        curstack = curstack->prev->prev->prev;                       \
    } while (0)

#define CLR_SX                                                       \
    (iptr->sx.val.l = 0)

#define CLR_DST                                                      \
    (iptr->dst.var = NULL)

#define NEW_DST(typed, depth)                                        \
    do {                                                             \
        NEWSTACKn(typed, (depth));                                   \
        iptr->dst.var = curstack;                                    \
    } while (0)

#define NEW_DST_LOCALVAR(typed, index)                               \
    do {                                                             \
        NEWSTACK(typed, LOCALVAR, (index));                          \
        iptr->dst.var = curstack;                                    \
    } while (0)

#define NEW_OP0_0                                                    \
    do {                                                             \
        CLR_S1;                                                      \
        CLR_DST;                                                     \
    } while (0)

#define NEW_OP0_BRANCH                                               \
    do {                                                             \
        CLR_S1;                                                      \
    } while (0)

#define NEW_OP0_1(typed)                                             \
    do {                                                             \
        CLR_S1;                                                      \
        NEW_DST(typed, stackdepth);                                  \
        stackdepth++;                                                \
    } while (0)

#define NEW_OP1_0(type1)                                             \
    do {                                                             \
        POP_S1(type1);                                               \
        CLR_DST;                                                     \
        stackdepth--;                                                \
    } while (0)

#define NEW_OP1_0_ANY                                                \
    do {                                                             \
        POP_S1_ANY;                                                  \
        CLR_DST;                                                     \
        stackdepth--;                                                \
    } while (0)

#define NEW_OP1_BRANCH(type1)                                        \
    do {                                                             \
        POP_S1(type1);                                               \
        stackdepth--;                                                \
    } while (0)

#define NEW_OP1_1(type1, typed)                                      \
    do {                                                             \
        POP_S1(type1);                                               \
        NEW_DST(typed, stackdepth - 1);                              \
    } while (0)

#define NEW_OP2_0(type1, type2)                                      \
    do {                                                             \
        POP_S1_S2(type1, type2);                                     \
        CLR_DST;                                                     \
        stackdepth -= 2;                                             \
    } while (0)

#define NEW_OP2_BRANCH(type1, type2)                                 \
    do {                                                             \
        POP_S1_S2(type1, type2);                                     \
        stackdepth -= 2;                                             \
    } while (0)

#define NEW_OP2_0_ANY_ANY                                            \
    do {                                                             \
        POP_S1_S2_ANY_ANY;                                           \
        CLR_DST;                                                     \
        stackdepth -= 2;                                             \
    } while (0)

#define NEW_OP2_1(type1, type2, typed)                               \
    do {                                                             \
        POP_S1_S2(type1, type2);                                     \
        NEW_DST(typed, stackdepth - 2);                              \
        stackdepth--;                                                \
    } while (0)

#define NEW_OP3_0(type1, type2, type3)                               \
    do {                                                             \
        POP_S1_S2_S3(type1, type2, type3);                           \
        CLR_DST;                                                     \
        stackdepth -= 3;                                             \
    } while (0)

#define NEW_LOAD(type1, index)                                       \
    do {                                                             \
        NEW_DST_LOCALVAR(type1, index);                              \
        stackdepth++;                                                \
    } while (0)

#define NEW_STORE(type1, index)                                      \
    do {                                                             \
        POP_S1(type1);                                               \
        stackdepth--;                                                \
    } while (0)

#define BRANCH_TARGET(bt, tempbptr, tempsp)                          \
    do {                                                             \
        (bt).block = tempbptr = BLOCK_OF((bt).insindex);             \
        MARKREACHED(tempbptr, tempsp);                               \
    } while (0)

#define BRANCH(tempbptr, tempsp)                                     \
    do {                                                             \
        iptr->dst.block = tempbptr = BLOCK_OF(iptr->dst.insindex);   \
        MARKREACHED(tempbptr, tempsp);                               \
    } while (0)

#define DUP_SLOT(sp)                                                 \
    do {                                                             \
        if ((sp)->varkind != TEMPVAR)                                \
            NEWSTACK((sp)->type, TEMPVAR, stackdepth);               \
        else                                                         \
            NEWSTACK((sp)->type, (sp)->varkind, (sp)->varnum);       \
    } while(0)

bool new_stack_analyse(jitdata *jd)
{
	methodinfo   *m;              /* method being analyzed                    */
	codeinfo     *code;
	codegendata  *cd;
	registerdata *rd;
	int           b_count;        /* basic block counter                      */
	int           b_index;        /* basic block index                        */
	int           stackdepth;
	stackptr      curstack;       /* current stack top                        */
	stackptr      new;
	stackptr      copy;
	int           opcode;         /* opcode of current instruction            */
	int           i, j;
	int           len;            /* # of instructions after the current one  */
	bool          superblockend;  /* if true, no fallthrough to next block    */
	bool          repeat;         /* if true, outermost loop must run again   */
	bool          deadcode;       /* true if no live code has been reached    */
	new_instruction *iptr;        /* the current instruction                  */
	basicblock   *bptr;           /* the current basic block                  */
	basicblock   *tbptr;
	s4           *last_store;     /* instruction index of last XSTORE         */
	                              /* [ local_index * 5 + type ]               */
	s4            last_pei;       /* ins. index of last possible exception    */
	                              /* used for conflict resolution for copy    */
                                  /* elimination (XLOAD, IINC, XSTORE)        */
	s4            last_dupx;
	branch_target_t *table;
	lookup_target_t *lookup;
#if defined(ENABLE_VERIFIER)
	int           expectedtype;   /* used by CHECK_BASIC_TYPE                 */
#endif
	builtintable_entry *bte;
	methoddesc         *md;
	constant_FMIref    *fmiref;
#if defined(ENABLE_STATISTICS)
	int           iteration_count;  /* number of iterations of analysis       */
#endif

#if defined(STACK_VERBOSE)
	new_show_method(jd, SHOW_PARSE);
#endif

	/* get required compiler data - initialization */

	m    = jd->m;
	code = jd->code;
	cd   = jd->cd;
	rd   = jd->rd;

#if defined(ENABLE_LSRA)
	m->maxlifetimes = 0;
#endif

#if defined(ENABLE_STATISTICS)
	iteration_count = 0;
#endif

	last_store = DMNEW(s4 , cd->maxlocals * 5);

	/* initialize in-stack of first block */

	new = jd->new_stack;
	jd->new_basicblocks[0].flags = BBREACHED;
	jd->new_basicblocks[0].instack = 0;
	jd->new_basicblocks[0].indepth = 0;

	/* initialize in-stack of exception handlers */

	for (i = 0; i < cd->exceptiontablelength; i++) {
		bptr = BLOCK_OF(cd->exceptiontable[i].handlerpc);
		bptr->flags = BBREACHED;
		bptr->type = BBTYPE_EXH;
		bptr->instack = new;
		bptr->indepth = 1;
		bptr->pre_count = 10000;
		STACKRESET;
		NEWXSTACK;
	}

	/* count predecessors of each block **************************************/

#if CONDITIONAL_LOADCONST
	/* XXX move this to a separate function */
	{
		b_count = jd->new_basicblockcount;
		bptr = jd->new_basicblocks;
		for (; --b_count >= 0; bptr++) {
			if (bptr->icount == 0)
				continue;

			/* get the last instruction of the block */

			iptr = /* XXX */ (new_instruction *) bptr->iinstr + (bptr->icount - 1);

			switch (iptr->opc) {
				/* instruction stopping control flow */
				case ICMD_RET:
				case ICMD_RETURN:
				case ICMD_IRETURN:
				case ICMD_LRETURN:
				case ICMD_FRETURN:
				case ICMD_DRETURN:
				case ICMD_ARETURN:
				case ICMD_ATHROW:
					break;

					/* conditional branches */
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
					/* XXX add missing conditional branches */
					bptr[1].pre_count++;
					/* FALLTHROUGH */

					/* unconditional branch */
				case ICMD_GOTO:
					BLOCK_OF(iptr->dst.insindex)->pre_count++;
					break;

					/* switches */
				case ICMD_TABLESWITCH:
					table = iptr->dst.table;
					BLOCK_OF((table++)->insindex)->pre_count++;
					i = iptr->sx.s23.s3.tablehigh
						- iptr->sx.s23.s2.tablelow + 1;
					while (--i >= 0) {
						BLOCK_OF((table++)->insindex)->pre_count++;
					}
					break;

				case ICMD_LOOKUPSWITCH:
					lookup = iptr->dst.lookup;
					BLOCK_OF(iptr->sx.s23.s3.lookupdefault.insindex)->pre_count++;
					i = iptr->sx.s23.s2.lookupcount;
					while (--i >= 0) {
						BLOCK_OF((lookup++)->target.insindex)->pre_count++;
					}
					break;

					/* default - fall into next block */
				default:
					bptr[1].pre_count++;
					break;
			} /* end switch */
		} /* end basic block loop */
	}
#endif /* CONDITIONAL_LOADCONST */

	/* stack analysis loop (until fixpoint reached) **************************/

	do {
#if defined(ENABLE_STATISTICS)
		iteration_count++;
#endif

		/* initialize loop over basic blocks */

		b_count = jd->new_basicblockcount;
		bptr = jd->new_basicblocks;
		superblockend = true;
		repeat = false;
		STACKRESET;
		deadcode = true;

		/* iterate over basic blocks *****************************************/

		while (--b_count >= 0) {
#if defined(STACK_VERBOSE)
			printf("ANALYZING BLOCK L%03d\n", bptr->debug_nr);
#endif

			if (bptr->flags == BBDELETED) {
				/* This block has been deleted - do nothing. */
			}
			else if (superblockend && (bptr->flags < BBREACHED)) {
				/* This block has not been reached so far, and we      */
				/* don't fall into it, so we'll have to iterate again. */
				repeat = true;
			}
			else if (bptr->flags <= BBREACHED) {
				if (superblockend) {
					/* We know that bptr->flags == BBREACHED. */
					/* This block has been reached before.    */
					stackdepth = bptr->indepth;
				}
				else if (bptr->flags < BBREACHED) {
					/* This block is reached for the first time now */
					/* by falling through from the previous block.  */
					COPYCURSTACK(copy);
					bptr->instack = copy;
					bptr->indepth = stackdepth;
				}
				else {
					/* This block has been reached before. now we are */
					/* falling into it from the previous block.       */
					/* Check that stack depth is well-defined.        */
					CHECK_STACK_DEPTH(bptr->indepth, stackdepth);
				}

				/* set up local variables for analyzing this block */

				curstack = bptr->instack;
				deadcode = false;
				superblockend = false;
				bptr->flags = BBFINISHED;
				len = bptr->icount;
				iptr = /* XXX */ (new_instruction *) bptr->iinstr;
				b_index = bptr - jd->new_basicblocks;

				/* reset variables for dependency checking */

				last_pei = -1;
				last_dupx = -1;
				for( i = 0; i < cd->maxlocals; i++)
					for( j = 0; j < 5; j++)
						last_store[5 * i + j] = -1;

				/* XXX store the start of the block's stack representation */

				bptr->stack = new;

				/* iterate over ICMDs ****************************************/

				while (--len >= 0)  {
#if defined(STACK_VERBOSE)
					new_show_icmd(jd, iptr, false, SHOW_PARSE); printf("\n");
					for( copy = curstack; copy; copy = copy->prev ) {
						printf("%d ", copy->type);
					}
					printf("\n");
#endif

					/* fetch the current opcode */

					opcode = iptr->opc;

					/* automatically replace some ICMDs with builtins */

#if defined(USEBUILTINTABLE)
					IF_NO_INTRP(
						bte = builtintable_get_automatic(opcode);

						if (bte && bte->opcode == opcode) {
							iptr->opc           = ICMD_BUILTIN;
							iptr->flags.bits    = INS_FLAG_NOCHECK;
							iptr->sx.s23.s3.bte = bte;
							/* iptr->line is already set */
							jd->isleafmethod = false;
							goto icmd_BUILTIN;
						}
					);
#endif /* defined(USEBUILTINTABLE) */

					/* main opcode switch *************************************/

					switch (opcode) {

						/* pop 0 push 0 */

					case ICMD_NOP:
icmd_NOP:
						CLR_SX;
						NEW_OP0_0;
						break;

					case ICMD_CHECKNULL:
						COUNT(count_check_null);
						USE_S1(TYPE_ADR);
						CLR_SX;
						CLR_DST; /* XXX live through? */
						break;

					case ICMD_IFEQ_ICONST:
					case ICMD_IFNE_ICONST:
					case ICMD_IFLT_ICONST:
					case ICMD_IFGE_ICONST:
					case ICMD_IFGT_ICONST:
					case ICMD_IFLE_ICONST:
					case ICMD_ELSE_ICONST:
						USE_S1(TYPE_INT);
						CLR_SX;
						CLR_DST; /* XXX live through? */
						break;

					case ICMD_RET:
						USE_S1_LOCAL(TYPE_ADR);
						CLR_SX;
						CLR_DST;
						IF_NO_INTRP( rd->locals[iptr->s1.localindex][TYPE_ADR].type = TYPE_ADR; );
						superblockend = true;
						break;

					case ICMD_RETURN:
						COUNT(count_pcmd_return);
						CLR_SX;
						NEW_OP0_0;
						superblockend = true;
						break;


						/* pop 0 push 1 const */

	/************************** ICONST OPTIMIZATIONS **************************/

					case ICMD_ICONST:
						COUNT(count_pcmd_load);
						if (len == 0)
							goto normal_ICONST;

						switch (iptr[1].opc) {
							case ICMD_IADD:
								iptr->opc = ICMD_IADDCONST;
								/* FALLTHROUGH */

							icmd_iconst_tail:
								iptr[1].opc = ICMD_NOP;
								NEW_OP1_1(TYPE_INT, TYPE_INT);
								COUNT(count_pcmd_op);
								break;

							case ICMD_ISUB:
								iptr->opc = ICMD_ISUBCONST;
								goto icmd_iconst_tail;
#if SUPPORT_CONST_MUL
							case ICMD_IMUL:
								iptr->opc = ICMD_IMULCONST;
								goto icmd_iconst_tail;
#else /* SUPPORT_CONST_MUL */
							case ICMD_IMUL:
								if (iptr->sx.val.i == 0x00000002)
									iptr->sx.val.i = 1;
								else if (iptr->sx.val.i == 0x00000004)
									iptr->sx.val.i = 2;
								else if (iptr->sx.val.i == 0x00000008)
									iptr->sx.val.i = 3;
								else if (iptr->sx.val.i == 0x00000010)
									iptr->sx.val.i = 4;
								else if (iptr->sx.val.i == 0x00000020)
									iptr->sx.val.i = 5;
								else if (iptr->sx.val.i == 0x00000040)
									iptr->sx.val.i = 6;
								else if (iptr->sx.val.i == 0x00000080)
									iptr->sx.val.i = 7;
								else if (iptr->sx.val.i == 0x00000100)
									iptr->sx.val.i = 8;
								else if (iptr->sx.val.i == 0x00000200)
									iptr->sx.val.i = 9;
								else if (iptr->sx.val.i == 0x00000400)
									iptr->sx.val.i = 10;
								else if (iptr->sx.val.i == 0x00000800)
									iptr->sx.val.i = 11;
								else if (iptr->sx.val.i == 0x00001000)
									iptr->sx.val.i = 12;
								else if (iptr->sx.val.i == 0x00002000)
									iptr->sx.val.i = 13;
								else if (iptr->sx.val.i == 0x00004000)
									iptr->sx.val.i = 14;
								else if (iptr->sx.val.i == 0x00008000)
									iptr->sx.val.i = 15;
								else if (iptr->sx.val.i == 0x00010000)
									iptr->sx.val.i = 16;
								else if (iptr->sx.val.i == 0x00020000)
									iptr->sx.val.i = 17;
								else if (iptr->sx.val.i == 0x00040000)
									iptr->sx.val.i = 18;
								else if (iptr->sx.val.i == 0x00080000)
									iptr->sx.val.i = 19;
								else if (iptr->sx.val.i == 0x00100000)
									iptr->sx.val.i = 20;
								else if (iptr->sx.val.i == 0x00200000)
									iptr->sx.val.i = 21;
								else if (iptr->sx.val.i == 0x00400000)
									iptr->sx.val.i = 22;
								else if (iptr->sx.val.i == 0x00800000)
									iptr->sx.val.i = 23;
								else if (iptr->sx.val.i == 0x01000000)
									iptr->sx.val.i = 24;
								else if (iptr->sx.val.i == 0x02000000)
									iptr->sx.val.i = 25;
								else if (iptr->sx.val.i == 0x04000000)
									iptr->sx.val.i = 26;
								else if (iptr->sx.val.i == 0x08000000)
									iptr->sx.val.i = 27;
								else if (iptr->sx.val.i == 0x10000000)
									iptr->sx.val.i = 28;
								else if (iptr->sx.val.i == 0x20000000)
									iptr->sx.val.i = 29;
								else if (iptr->sx.val.i == 0x40000000)
									iptr->sx.val.i = 30;
								else if (iptr->sx.val.i == 0x80000000)
									iptr->sx.val.i = 31;
								else
									goto normal_ICONST;

								iptr->opc = ICMD_IMULPOW2;
								goto icmd_iconst_tail;
#endif /* SUPPORT_CONST_MUL */
							case ICMD_IDIV:
								if (iptr->sx.val.i == 0x00000002)
									iptr->sx.val.i = 1;
								else if (iptr->sx.val.i == 0x00000004)
									iptr->sx.val.i = 2;
								else if (iptr->sx.val.i == 0x00000008)
									iptr->sx.val.i = 3;
								else if (iptr->sx.val.i == 0x00000010)
									iptr->sx.val.i = 4;
								else if (iptr->sx.val.i == 0x00000020)
									iptr->sx.val.i = 5;
								else if (iptr->sx.val.i == 0x00000040)
									iptr->sx.val.i = 6;
								else if (iptr->sx.val.i == 0x00000080)
									iptr->sx.val.i = 7;
								else if (iptr->sx.val.i == 0x00000100)
									iptr->sx.val.i = 8;
								else if (iptr->sx.val.i == 0x00000200)
									iptr->sx.val.i = 9;
								else if (iptr->sx.val.i == 0x00000400)
									iptr->sx.val.i = 10;
								else if (iptr->sx.val.i == 0x00000800)
									iptr->sx.val.i = 11;
								else if (iptr->sx.val.i == 0x00001000)
									iptr->sx.val.i = 12;
								else if (iptr->sx.val.i == 0x00002000)
									iptr->sx.val.i = 13;
								else if (iptr->sx.val.i == 0x00004000)
									iptr->sx.val.i = 14;
								else if (iptr->sx.val.i == 0x00008000)
									iptr->sx.val.i = 15;
								else if (iptr->sx.val.i == 0x00010000)
									iptr->sx.val.i = 16;
								else if (iptr->sx.val.i == 0x00020000)
									iptr->sx.val.i = 17;
								else if (iptr->sx.val.i == 0x00040000)
									iptr->sx.val.i = 18;
								else if (iptr->sx.val.i == 0x00080000)
									iptr->sx.val.i = 19;
								else if (iptr->sx.val.i == 0x00100000)
									iptr->sx.val.i = 20;
								else if (iptr->sx.val.i == 0x00200000)
									iptr->sx.val.i = 21;
								else if (iptr->sx.val.i == 0x00400000)
									iptr->sx.val.i = 22;
								else if (iptr->sx.val.i == 0x00800000)
									iptr->sx.val.i = 23;
								else if (iptr->sx.val.i == 0x01000000)
									iptr->sx.val.i = 24;
								else if (iptr->sx.val.i == 0x02000000)
									iptr->sx.val.i = 25;
								else if (iptr->sx.val.i == 0x04000000)
									iptr->sx.val.i = 26;
								else if (iptr->sx.val.i == 0x08000000)
									iptr->sx.val.i = 27;
								else if (iptr->sx.val.i == 0x10000000)
									iptr->sx.val.i = 28;
								else if (iptr->sx.val.i == 0x20000000)
									iptr->sx.val.i = 29;
								else if (iptr->sx.val.i == 0x40000000)
									iptr->sx.val.i = 30;
								else if (iptr->sx.val.i == 0x80000000)
									iptr->sx.val.i = 31;
								else
									goto normal_ICONST;

								iptr->opc = ICMD_IDIVPOW2;
								goto icmd_iconst_tail;

							case ICMD_IREM:
								/*log_text("stack.c: ICMD_ICONST/ICMD_IREM");*/
								if ((iptr->sx.val.i == 0x00000002) ||
									(iptr->sx.val.i == 0x00000004) ||
									(iptr->sx.val.i == 0x00000008) ||
									(iptr->sx.val.i == 0x00000010) ||
									(iptr->sx.val.i == 0x00000020) ||
									(iptr->sx.val.i == 0x00000040) ||
									(iptr->sx.val.i == 0x00000080) ||
									(iptr->sx.val.i == 0x00000100) ||
									(iptr->sx.val.i == 0x00000200) ||
									(iptr->sx.val.i == 0x00000400) ||
									(iptr->sx.val.i == 0x00000800) ||
									(iptr->sx.val.i == 0x00001000) ||
									(iptr->sx.val.i == 0x00002000) ||
									(iptr->sx.val.i == 0x00004000) ||
									(iptr->sx.val.i == 0x00008000) ||
									(iptr->sx.val.i == 0x00010000) ||
									(iptr->sx.val.i == 0x00020000) ||
									(iptr->sx.val.i == 0x00040000) ||
									(iptr->sx.val.i == 0x00080000) ||
									(iptr->sx.val.i == 0x00100000) ||
									(iptr->sx.val.i == 0x00200000) ||
									(iptr->sx.val.i == 0x00400000) ||
									(iptr->sx.val.i == 0x00800000) ||
									(iptr->sx.val.i == 0x01000000) ||
									(iptr->sx.val.i == 0x02000000) ||
									(iptr->sx.val.i == 0x04000000) ||
									(iptr->sx.val.i == 0x08000000) ||
									(iptr->sx.val.i == 0x10000000) ||
									(iptr->sx.val.i == 0x20000000) ||
									(iptr->sx.val.i == 0x40000000) ||
									(iptr->sx.val.i == 0x80000000))
								{
									iptr->opc = ICMD_IREMPOW2;
									iptr->sx.val.i -= 1;
									goto icmd_iconst_tail;
								}
								goto normal_ICONST;
#if SUPPORT_CONST_LOGICAL
							case ICMD_IAND:
								iptr->opc = ICMD_IANDCONST;
								goto icmd_iconst_tail;

							case ICMD_IOR:
								iptr->opc = ICMD_IORCONST;
								goto icmd_iconst_tail;

							case ICMD_IXOR:
								iptr->opc = ICMD_IXORCONST;
								goto icmd_iconst_tail;

#endif /* SUPPORT_CONST_LOGICAL */
							case ICMD_ISHL:
								iptr->opc = ICMD_ISHLCONST;
								goto icmd_iconst_tail;

							case ICMD_ISHR:
								iptr->opc = ICMD_ISHRCONST;
								goto icmd_iconst_tail;

							case ICMD_IUSHR:
								iptr->opc = ICMD_IUSHRCONST;
								goto icmd_iconst_tail;
#if SUPPORT_LONG_SHIFT
							case ICMD_LSHL:
								iptr->opc = ICMD_LSHLCONST;
								goto icmd_lconst_tail;

							case ICMD_LSHR:
								iptr->opc = ICMD_LSHRCONST;
								goto icmd_lconst_tail;

							case ICMD_LUSHR:
								iptr->opc = ICMD_LUSHRCONST;
								goto icmd_lconst_tail;
#endif /* SUPPORT_LONG_SHIFT */
							case ICMD_IF_ICMPEQ:
								iptr[1].opc = ICMD_IFEQ;
								/* FALLTHROUGH */

							icmd_if_icmp_tail:
								/* set the constant for the following icmd */
								iptr[1].sx.val.i = iptr->sx.val.i;

								/* this instruction becomes a nop */
								iptr->opc = ICMD_NOP;
								goto icmd_NOP;

							case ICMD_IF_ICMPLT:
								iptr[1].opc = ICMD_IFLT;
								goto icmd_if_icmp_tail;

							case ICMD_IF_ICMPLE:
								iptr[1].opc = ICMD_IFLE;
								goto icmd_if_icmp_tail;

							case ICMD_IF_ICMPNE:
								iptr[1].opc = ICMD_IFNE;
								goto icmd_if_icmp_tail;

							case ICMD_IF_ICMPGT:
								iptr[1].opc = ICMD_IFGT;
								goto icmd_if_icmp_tail;

							case ICMD_IF_ICMPGE:
								iptr[1].opc = ICMD_IFGE;
								goto icmd_if_icmp_tail;

#if SUPPORT_CONST_STORE
							case ICMD_IASTORE:
							case ICMD_BASTORE:
							case ICMD_CASTORE:
							case ICMD_SASTORE:
								IF_INTRP( goto normal_ICONST; )
# if SUPPORT_CONST_STORE_ZERO_ONLY
								if (iptr->sx.val.i != 0)
									goto normal_ICONST;
# endif
								switch (iptr[1].opc) {
									case ICMD_IASTORE:
										iptr->opc = ICMD_IASTORECONST;
										break;
									case ICMD_BASTORE:
										iptr->opc = ICMD_BASTORECONST;
										break;
									case ICMD_CASTORE:
										iptr->opc = ICMD_CASTORECONST;
										break;
									case ICMD_SASTORE:
										iptr->opc = ICMD_SASTORECONST;
										break;
								}

								iptr[1].opc = ICMD_NOP;

								/* copy the constant to s3 */
								/* XXX constval -> astoreconstval? */
								iptr->sx.s23.s3.constval = iptr->sx.val.i;
								NEW_OP2_0(TYPE_ADR, TYPE_INT);
								COUNT(count_pcmd_op);
								break;

							case ICMD_PUTSTATIC:
							case ICMD_PUTFIELD:
								IF_INTRP( goto normal_ICONST; )
# if SUPPORT_CONST_STORE_ZERO_ONLY
								if (iptr->sx.val.i != 0)
									goto normal_ICONST;
# endif
								/* XXX check field type? */

								/* copy the constant to s2 */
								/* XXX constval -> fieldconstval? */
								iptr->sx.s23.s2.constval = iptr->sx.val.i;

putconst_tail:
								/* set the field reference (s3) */
								if (iptr[1].flags.bits & INS_FLAG_UNRESOLVED) {
									iptr->sx.s23.s3.uf = iptr[1].sx.s23.s3.uf;
									iptr->flags.bits |= INS_FLAG_UNRESOLVED;
								}
								else {
									iptr->sx.s23.s3.fmiref = iptr[1].sx.s23.s3.fmiref;
								}
								
								switch (iptr[1].opc) {
									case ICMD_PUTSTATIC:
										iptr->opc = ICMD_PUTSTATICCONST;
										NEW_OP0_0;
										break;
									case ICMD_PUTFIELD:
										iptr->opc = ICMD_PUTFIELDCONST;
										NEW_OP1_0(TYPE_ADR);
										break;
								}

								iptr[1].opc = ICMD_NOP;
								COUNT(count_pcmd_op);
								break;
#endif /* SUPPORT_CONST_STORE */

							default:
								goto normal_ICONST;
						}

						/* if we get here, the ICONST has been optimized */
						break;

normal_ICONST:
						/* normal case of an unoptimized ICONST */
						NEW_OP0_1(TYPE_INT);
						break;

	/************************** LCONST OPTIMIZATIONS **************************/

					case ICMD_LCONST:
						COUNT(count_pcmd_load);
						if (len == 0)
							goto normal_LCONST;

						/* switch depending on the following instruction */

						switch (iptr[1].opc) {
#if SUPPORT_LONG_ADD
							case ICMD_LADD:
								iptr->opc = ICMD_LADDCONST;
								/* FALLTHROUGH */

							icmd_lconst_tail:
								/* instruction of type LONG -> LONG */
								iptr[1].opc = ICMD_NOP;
								NEW_OP1_1(TYPE_LNG, TYPE_LNG);
								COUNT(count_pcmd_op);
								break;

							case ICMD_LSUB:
								iptr->opc = ICMD_LSUBCONST;
								goto icmd_lconst_tail;

#endif /* SUPPORT_LONG_ADD */
#if SUPPORT_LONG_MUL && SUPPORT_CONST_MUL
							case ICMD_LMUL:
								iptr->opc = ICMD_LMULCONST;
								goto icmd_lconst_tail;
#else /* SUPPORT_LONG_MUL && SUPPORT_CONST_MUL */
# if SUPPORT_LONG_SHIFT
							case ICMD_LMUL:
								if (iptr->sx.val.l == 0x00000002)
									iptr->sx.val.i = 1;
								else if (iptr->sx.val.l == 0x00000004)
									iptr->sx.val.i = 2;
								else if (iptr->sx.val.l == 0x00000008)
									iptr->sx.val.i = 3;
								else if (iptr->sx.val.l == 0x00000010)
									iptr->sx.val.i = 4;
								else if (iptr->sx.val.l == 0x00000020)
									iptr->sx.val.i = 5;
								else if (iptr->sx.val.l == 0x00000040)
									iptr->sx.val.i = 6;
								else if (iptr->sx.val.l == 0x00000080)
									iptr->sx.val.i = 7;
								else if (iptr->sx.val.l == 0x00000100)
									iptr->sx.val.i = 8;
								else if (iptr->sx.val.l == 0x00000200)
									iptr->sx.val.i = 9;
								else if (iptr->sx.val.l == 0x00000400)
									iptr->sx.val.i = 10;
								else if (iptr->sx.val.l == 0x00000800)
									iptr->sx.val.i = 11;
								else if (iptr->sx.val.l == 0x00001000)
									iptr->sx.val.i = 12;
								else if (iptr->sx.val.l == 0x00002000)
									iptr->sx.val.i = 13;
								else if (iptr->sx.val.l == 0x00004000)
									iptr->sx.val.i = 14;
								else if (iptr->sx.val.l == 0x00008000)
									iptr->sx.val.i = 15;
								else if (iptr->sx.val.l == 0x00010000)
									iptr->sx.val.i = 16;
								else if (iptr->sx.val.l == 0x00020000)
									iptr->sx.val.i = 17;
								else if (iptr->sx.val.l == 0x00040000)
									iptr->sx.val.i = 18;
								else if (iptr->sx.val.l == 0x00080000)
									iptr->sx.val.i = 19;
								else if (iptr->sx.val.l == 0x00100000)
									iptr->sx.val.i = 20;
								else if (iptr->sx.val.l == 0x00200000)
									iptr->sx.val.i = 21;
								else if (iptr->sx.val.l == 0x00400000)
									iptr->sx.val.i = 22;
								else if (iptr->sx.val.l == 0x00800000)
									iptr->sx.val.i = 23;
								else if (iptr->sx.val.l == 0x01000000)
									iptr->sx.val.i = 24;
								else if (iptr->sx.val.l == 0x02000000)
									iptr->sx.val.i = 25;
								else if (iptr->sx.val.l == 0x04000000)
									iptr->sx.val.i = 26;
								else if (iptr->sx.val.l == 0x08000000)
									iptr->sx.val.i = 27;
								else if (iptr->sx.val.l == 0x10000000)
									iptr->sx.val.i = 28;
								else if (iptr->sx.val.l == 0x20000000)
									iptr->sx.val.i = 29;
								else if (iptr->sx.val.l == 0x40000000)
									iptr->sx.val.i = 30;
								else if (iptr->sx.val.l == 0x80000000)
									iptr->sx.val.i = 31;
								else {
									goto normal_LCONST;
								}
								iptr->opc = ICMD_LMULPOW2;
								goto icmd_lconst_tail;
# endif /* SUPPORT_LONG_SHIFT */
#endif /* SUPPORT_LONG_MUL && SUPPORT_CONST_MUL */
#if SUPPORT_LONG_DIV_POW2
							case ICMD_LDIV:
								if (iptr->sx.val.l == 0x00000002)
									iptr->sx.val.i = 1;
								else if (iptr->sx.val.l == 0x00000004)
									iptr->sx.val.i = 2;
								else if (iptr->sx.val.l == 0x00000008)
									iptr->sx.val.i = 3;
								else if (iptr->sx.val.l == 0x00000010)
									iptr->sx.val.i = 4;
								else if (iptr->sx.val.l == 0x00000020)
									iptr->sx.val.i = 5;
								else if (iptr->sx.val.l == 0x00000040)
									iptr->sx.val.i = 6;
								else if (iptr->sx.val.l == 0x00000080)
									iptr->sx.val.i = 7;
								else if (iptr->sx.val.l == 0x00000100)
									iptr->sx.val.i = 8;
								else if (iptr->sx.val.l == 0x00000200)
									iptr->sx.val.i = 9;
								else if (iptr->sx.val.l == 0x00000400)
									iptr->sx.val.i = 10;
								else if (iptr->sx.val.l == 0x00000800)
									iptr->sx.val.i = 11;
								else if (iptr->sx.val.l == 0x00001000)
									iptr->sx.val.i = 12;
								else if (iptr->sx.val.l == 0x00002000)
									iptr->sx.val.i = 13;
								else if (iptr->sx.val.l == 0x00004000)
									iptr->sx.val.i = 14;
								else if (iptr->sx.val.l == 0x00008000)
									iptr->sx.val.i = 15;
								else if (iptr->sx.val.l == 0x00010000)
									iptr->sx.val.i = 16;
								else if (iptr->sx.val.l == 0x00020000)
									iptr->sx.val.i = 17;
								else if (iptr->sx.val.l == 0x00040000)
									iptr->sx.val.i = 18;
								else if (iptr->sx.val.l == 0x00080000)
									iptr->sx.val.i = 19;
								else if (iptr->sx.val.l == 0x00100000)
									iptr->sx.val.i = 20;
								else if (iptr->sx.val.l == 0x00200000)
									iptr->sx.val.i = 21;
								else if (iptr->sx.val.l == 0x00400000)
									iptr->sx.val.i = 22;
								else if (iptr->sx.val.l == 0x00800000)
									iptr->sx.val.i = 23;
								else if (iptr->sx.val.l == 0x01000000)
									iptr->sx.val.i = 24;
								else if (iptr->sx.val.l == 0x02000000)
									iptr->sx.val.i = 25;
								else if (iptr->sx.val.l == 0x04000000)
									iptr->sx.val.i = 26;
								else if (iptr->sx.val.l == 0x08000000)
									iptr->sx.val.i = 27;
								else if (iptr->sx.val.l == 0x10000000)
									iptr->sx.val.i = 28;
								else if (iptr->sx.val.l == 0x20000000)
									iptr->sx.val.i = 29;
								else if (iptr->sx.val.l == 0x40000000)
									iptr->sx.val.i = 30;
								else if (iptr->sx.val.l == 0x80000000)
									iptr->sx.val.i = 31;
								else {
									goto normal_LCONST;
								}
								iptr->opc = ICMD_LDIVPOW2;
								goto icmd_lconst_tail;
#endif /* SUPPORT_LONG_DIV_POW2 */

#if SUPPORT_LONG_REM_POW2
							case ICMD_LREM:
								if ((iptr->sx.val.l == 0x00000002) ||
									(iptr->sx.val.l == 0x00000004) ||
									(iptr->sx.val.l == 0x00000008) ||
									(iptr->sx.val.l == 0x00000010) ||
									(iptr->sx.val.l == 0x00000020) ||
									(iptr->sx.val.l == 0x00000040) ||
									(iptr->sx.val.l == 0x00000080) ||
									(iptr->sx.val.l == 0x00000100) ||
									(iptr->sx.val.l == 0x00000200) ||
									(iptr->sx.val.l == 0x00000400) ||
									(iptr->sx.val.l == 0x00000800) ||
									(iptr->sx.val.l == 0x00001000) ||
									(iptr->sx.val.l == 0x00002000) ||
									(iptr->sx.val.l == 0x00004000) ||
									(iptr->sx.val.l == 0x00008000) ||
									(iptr->sx.val.l == 0x00010000) ||
									(iptr->sx.val.l == 0x00020000) ||
									(iptr->sx.val.l == 0x00040000) ||
									(iptr->sx.val.l == 0x00080000) ||
									(iptr->sx.val.l == 0x00100000) ||
									(iptr->sx.val.l == 0x00200000) ||
									(iptr->sx.val.l == 0x00400000) ||
									(iptr->sx.val.l == 0x00800000) ||
									(iptr->sx.val.l == 0x01000000) ||
									(iptr->sx.val.l == 0x02000000) ||
									(iptr->sx.val.l == 0x04000000) ||
									(iptr->sx.val.l == 0x08000000) ||
									(iptr->sx.val.l == 0x10000000) ||
									(iptr->sx.val.l == 0x20000000) ||
									(iptr->sx.val.l == 0x40000000) ||
									(iptr->sx.val.l == 0x80000000))
								{
									iptr->opc = ICMD_LREMPOW2;
									iptr->sx.val.l -= 1;
									goto icmd_lconst_tail;
								}
								goto normal_LCONST;
#endif /* SUPPORT_LONG_REM_POW2 */

#if SUPPORT_LONG_LOGICAL && SUPPORT_CONST_LOGICAL

							case ICMD_LAND:
								iptr->opc = ICMD_LANDCONST;
								goto icmd_lconst_tail;

							case ICMD_LOR:
								iptr->opc = ICMD_LORCONST;
								goto icmd_lconst_tail;

							case ICMD_LXOR:
								iptr->opc = ICMD_LXORCONST;
								goto icmd_lconst_tail;
#endif /* SUPPORT_LONG_LOGICAL && SUPPORT_CONST_LOGICAL */

#if SUPPORT_LONG_CMP_CONST
							case ICMD_LCMP:
								if ((len <= 1) || (iptr[2].sx.val.i != 0))
									goto normal_LCONST;

								/* switch on the instruction after LCONST - LCMP */

								switch (iptr[2].opc) {
									case ICMD_IFEQ:
										iptr->opc = ICMD_IF_LEQ;
										/* FALLTHROUGH */

									icmd_lconst_lcmp_tail:
										/* convert LCONST, LCMP, IFXX to IF_LXX */
										iptr->dst.insindex = iptr[2].dst.insindex;
										iptr[1].opc = ICMD_NOP;
										iptr[2].opc = ICMD_NOP;

										NEW_OP1_BRANCH(TYPE_LNG);
										BRANCH(tbptr, copy);
										COUNT(count_pcmd_bra);
										COUNT(count_pcmd_op);
										break;

									case ICMD_IFNE:
										iptr->opc = ICMD_IF_LNE;
										goto icmd_lconst_lcmp_tail;

									case ICMD_IFLT:
										iptr->opc = ICMD_IF_LLT;
										goto icmd_lconst_lcmp_tail;

									case ICMD_IFGT:
										iptr->opc = ICMD_IF_LGT;
										goto icmd_lconst_lcmp_tail;

									case ICMD_IFLE:
										iptr->opc = ICMD_IF_LLE;
										goto icmd_lconst_lcmp_tail;

									case ICMD_IFGE:
										iptr->opc = ICMD_IF_LGE;
										goto icmd_lconst_lcmp_tail;

									default:
										goto normal_LCONST;
								} /* end switch on opcode after LCONST - LCMP */
								break;
#endif /* SUPPORT_LONG_CMP_CONST */

#if SUPPORT_CONST_STORE
							case ICMD_LASTORE:
								IF_INTRP( goto normal_LCONST; )
# if SUPPORT_CONST_STORE_ZERO_ONLY
								if (iptr->sx.val.l != 0)
									goto normal_LCONST;
# endif
#if SIZEOF_VOID_P == 4
								/* the constant must fit into a ptrint */
								if (iptr->sx.val.l < -0x80000000L || iptr->sx.val.l >= 0x80000000L)
									goto normal_LCONST;
#endif
								/* move the constant to s3 */
								iptr->sx.s23.s3.constval = iptr->sx.val.l;

								iptr->opc = ICMD_LASTORECONST;
								NEW_OP2_0(TYPE_ADR, TYPE_INT);

								iptr[1].opc = ICMD_NOP;
								COUNT(count_pcmd_op);
								break;

							case ICMD_PUTSTATIC:
							case ICMD_PUTFIELD:
								IF_INTRP( goto normal_LCONST; )
# if SUPPORT_CONST_STORE_ZERO_ONLY
								if (iptr->sx.val.l != 0)
									goto normal_LCONST;
# endif
#if SIZEOF_VOID_P == 4
								/* the constant must fit into a ptrint */
								if (iptr->sx.val.l < -0x80000000L || iptr->sx.val.l >= 0x80000000L)
									goto normal_LCONST;
#endif
								/* XXX check field type? */

								/* copy the constant to s2 */
								/* XXX constval -> fieldconstval? */
								iptr->sx.s23.s2.constval = iptr->sx.val.l;

								goto putconst_tail;

#endif /* SUPPORT_CONST_STORE */

							default:
								goto normal_LCONST;
						} /* end switch opcode after LCONST */

						/* if we get here, the LCONST has been optimized */
						break;

normal_LCONST:
						/* the normal case of an unoptimized LCONST */
						NEW_OP0_1(TYPE_LNG);
						break;

	/************************ END OF LCONST OPTIMIZATIONS *********************/

					case ICMD_FCONST:
						COUNT(count_pcmd_load);
						NEW_OP0_1(TYPE_FLT);
						break;

					case ICMD_DCONST:
						COUNT(count_pcmd_load);
						NEW_OP0_1(TYPE_DBL);
						break;

	/************************** ACONST OPTIMIZATIONS **************************/

					case ICMD_ACONST:
						COUNT(count_pcmd_load);
#if SUPPORT_CONST_STORE
						IF_INTRP( goto normal_ACONST; )

						/* We can only optimize if the ACONST is resolved
						 * and there is an instruction after it. */

						if ((len == 0) || (iptr->flags.bits & INS_FLAG_UNRESOLVED))
							goto normal_ACONST;

						switch (iptr[1].opc) {
							case ICMD_AASTORE:
								/* We can only optimize for NULL values
								 * here because otherwise a checkcast is
								 * required. */
								if (iptr->sx.val.anyptr != NULL)
									goto normal_ACONST;

								/* copy the constant (NULL) to s3 */
								iptr->sx.s23.s3.constval = 0;
								iptr->opc = ICMD_AASTORECONST;
								NEW_OP2_0(TYPE_ADR, TYPE_INT);

								iptr[1].opc = ICMD_NOP;
								COUNT(count_pcmd_op);
								break;

							case ICMD_PUTSTATIC:
							case ICMD_PUTFIELD:
# if SUPPORT_CONST_STORE_ZERO_ONLY
								if (iptr->sx.val.anyptr != NULL)
									goto normal_ACONST;
# endif
								/* XXX check field type? */
								/* copy the constant to s2 */
								/* XXX constval -> fieldconstval? */
								iptr->sx.s23.s2.constval = (ptrint) iptr->sx.val.anyptr;

								goto putconst_tail;

							default:
								goto normal_ACONST;
						}

						/* if we get here the ACONST has been optimized */
						break;

normal_ACONST:
#endif /* SUPPORT_CONST_STORE */
						NEW_OP0_1(TYPE_ADR);
						break;


						/* pop 0 push 1 load */

					case ICMD_ILOAD:
					case ICMD_LLOAD:
					case ICMD_FLOAD:
					case ICMD_DLOAD:
					case ICMD_ALOAD:
						COUNT(count_load_instruction);
						i = opcode - ICMD_ILOAD;
						IF_NO_INTRP( rd->locals[iptr->s1.localindex][i].type = i; )
						NEW_LOAD(i, iptr->s1.localindex);
						break;

						/* pop 2 push 1 */

					case ICMD_LALOAD:
					case ICMD_FALOAD:
					case ICMD_DALOAD:
					case ICMD_AALOAD:
						COUNT(count_check_null);
						COUNT(count_check_bound);
						COUNT(count_pcmd_mem);
						NEW_OP2_1(TYPE_ADR, TYPE_INT, opcode - ICMD_IALOAD);
						break;

					case ICMD_IALOAD:
					case ICMD_BALOAD:
					case ICMD_CALOAD:
					case ICMD_SALOAD:
						COUNT(count_check_null);
						COUNT(count_check_bound);
						COUNT(count_pcmd_mem);
						NEW_OP2_1(TYPE_ADR, TYPE_INT, TYPE_INT);
						break;

						/* pop 0 push 0 iinc */

					case ICMD_IINC:
						STATISTICS_STACKDEPTH_DISTRIBUTION(count_store_depth);

						last_store[5 * iptr->s1.localindex + TYPE_INT] = bptr->icount - len - 1;

						copy = curstack;
						i = stackdepth - 1;
						while (copy) {
							if ((copy->varkind == LOCALVAR) &&
								(copy->varnum == iptr->s1.localindex))
							{
								copy->varkind = TEMPVAR;
								copy->varnum = i;
							}
							i--;
							copy = copy->prev;
						}

						iptr->dst.localindex = iptr->s1.localindex;
						break;

						/* pop 1 push 0 store */

					case ICMD_ISTORE:
					case ICMD_LSTORE:
					case ICMD_FSTORE:
					case ICMD_DSTORE:
					case ICMD_ASTORE:
						REQUIRE_1;

						i = opcode - ICMD_ISTORE; /* type */
						IF_NO_INTRP( rd->locals[iptr->dst.localindex][i].type = i; )

#if defined(ENABLE_STATISTICS)
						if (opt_stat) {
							count_pcmd_store++;
							i = new - curstack;
							if (i >= 20)
								count_store_length[20]++;
							else
								count_store_length[i]++;
							i = stackdepth - 1;
							if (i >= 10)
								count_store_depth[10]++;
							else
								count_store_depth[i]++;
						}
#endif
						/* check for conflicts as described in Figure 5.2 */
						copy = curstack->prev;
						i = stackdepth - 2;
						while (copy) {
							if ((copy->varkind == LOCALVAR) &&
								(copy->varnum == iptr->dst.localindex))
							{
								copy->varkind = TEMPVAR;
								copy->varnum = i;
							}
							i--;
							copy = copy->prev;
						}

						if ((curstack->varkind == LOCALVAR)
							&& (curstack->varnum == iptr->dst.localindex))
						{
							curstack->varkind = TEMPVAR;
							curstack->varnum = stackdepth-1;
						}

						last_store[5 * iptr->dst.localindex + (opcode - ICMD_ISTORE)] = bptr->icount - len - 1;

						NEW_STORE(opcode - ICMD_ISTORE, iptr->dst.localindex);
						break;

					/* pop 3 push 0 */

					case ICMD_AASTORE:
						COUNT(count_check_null);
						COUNT(count_check_bound);
						COUNT(count_pcmd_mem);

						bte = builtintable_get_internal(BUILTIN_canstore);
						md = bte->md;

						if (md->memuse > rd->memuse)
							rd->memuse = md->memuse;
						if (md->argintreguse > rd->argintreguse)
							rd->argintreguse = md->argintreguse;
						/* XXX non-leaf method? */

						/* make all stack variables saved */

						copy = curstack;
						while (copy) {
							copy->flags |= SAVEDVAR;
							copy = copy->prev;
						}

						NEW_OP3_0(TYPE_ADR, TYPE_INT, TYPE_ADR);
						break;


					case ICMD_LASTORE:
					case ICMD_FASTORE:
					case ICMD_DASTORE:
						COUNT(count_check_null);
						COUNT(count_check_bound);
						COUNT(count_pcmd_mem);
						NEW_OP3_0(TYPE_ADR, TYPE_INT, opcode - ICMD_IASTORE);
						break;

					case ICMD_IASTORE:
					case ICMD_BASTORE:
					case ICMD_CASTORE:
					case ICMD_SASTORE:
						COUNT(count_check_null);
						COUNT(count_check_bound);
						COUNT(count_pcmd_mem);
						NEW_OP3_0(TYPE_ADR, TYPE_INT, TYPE_INT);
						break;

						/* pop 1 push 0 */

					case ICMD_POP:
#ifdef ENABLE_VERIFIER
						if (opt_verify) {
							REQUIRE_1;
							if (IS_2_WORD_TYPE(curstack->type))
								goto throw_stack_category_error;
						}
#endif
						NEW_OP1_0_ANY;
						break;

					case ICMD_IRETURN:
					case ICMD_LRETURN:
					case ICMD_FRETURN:
					case ICMD_DRETURN:
					case ICMD_ARETURN:
						IF_JIT( md_return_alloc(jd, curstack); )
						COUNT(count_pcmd_return);
						NEW_OP1_0(opcode - ICMD_IRETURN);
						superblockend = true;
						break;

					case ICMD_ATHROW:
						COUNT(count_check_null);
						NEW_OP1_0(TYPE_ADR);
						STACKRESET;
						superblockend = true;
						break;

					case ICMD_PUTSTATIC:
						COUNT(count_pcmd_mem);
						NEW_INSTRUCTION_GET_FIELDREF(iptr, fmiref);
						NEW_OP1_0(fmiref->parseddesc.fd->type);
						break;

						/* pop 1 push 0 branch */

					case ICMD_IFNULL:
					case ICMD_IFNONNULL:
						COUNT(count_pcmd_bra);
						NEW_OP1_BRANCH(TYPE_ADR);
						BRANCH(tbptr, copy);
						break;

					case ICMD_IFEQ:
					case ICMD_IFNE:
					case ICMD_IFLT:
					case ICMD_IFGE:
					case ICMD_IFGT:
					case ICMD_IFLE:
						COUNT(count_pcmd_bra);
						/* iptr->sx.val.i is set implicitly in parse by
						   clearing the memory or from IF_ICMPxx
						   optimization. */

						NEW_OP1_BRANCH(TYPE_INT);
/* 						iptr->sx.val.i = 0; */
						BRANCH(tbptr, copy);
						break;

						/* pop 0 push 0 branch */

					case ICMD_GOTO:
						COUNT(count_pcmd_bra);
						NEW_OP0_BRANCH;
						BRANCH(tbptr, copy);
						superblockend = true;
						break;

						/* pop 1 push 0 table branch */

					case ICMD_TABLESWITCH:
						COUNT(count_pcmd_table);
						NEW_OP1_BRANCH(TYPE_INT);

						table = iptr->dst.table;
						BRANCH_TARGET(*table, tbptr, copy);
						table++;

						i = iptr->sx.s23.s3.tablehigh
						  - iptr->sx.s23.s2.tablelow + 1;

						while (--i >= 0) {
							BRANCH_TARGET(*table, tbptr, copy);
							table++;
						}
						superblockend = true;
						break;

						/* pop 1 push 0 table branch */

					case ICMD_LOOKUPSWITCH:
						COUNT(count_pcmd_table);
						NEW_OP1_BRANCH(TYPE_INT);

						BRANCH_TARGET(iptr->sx.s23.s3.lookupdefault, tbptr, copy);

						lookup = iptr->dst.lookup;

						i = iptr->sx.s23.s2.lookupcount;

						while (--i >= 0) {
							BRANCH_TARGET(lookup->target, tbptr, copy);
							lookup++;
						}
						superblockend = true;
						break;

					case ICMD_MONITORENTER:
					case ICMD_MONITOREXIT:
						COUNT(count_check_null);
						NEW_OP1_0(TYPE_ADR);
						break;

						/* pop 2 push 0 branch */

					case ICMD_IF_ICMPEQ:
					case ICMD_IF_ICMPNE:
					case ICMD_IF_ICMPLT:
					case ICMD_IF_ICMPGE:
					case ICMD_IF_ICMPGT:
					case ICMD_IF_ICMPLE:
						COUNT(count_pcmd_bra);
						NEW_OP2_BRANCH(TYPE_INT, TYPE_INT);
						BRANCH(tbptr, copy);
						break;

					case ICMD_IF_ACMPEQ:
					case ICMD_IF_ACMPNE:
						COUNT(count_pcmd_bra);
						NEW_OP2_BRANCH(TYPE_ADR, TYPE_ADR);
						BRANCH(tbptr, copy);
						break;

						/* pop 2 push 0 */

					case ICMD_PUTFIELD:
						COUNT(count_check_null);
						COUNT(count_pcmd_mem);
						NEW_INSTRUCTION_GET_FIELDREF(iptr, fmiref);
						NEW_OP2_0(TYPE_ADR, fmiref->parseddesc.fd->type);
						break;

					case ICMD_POP2:
						REQUIRE_1;
						if (!IS_2_WORD_TYPE(curstack->type)) {
							/* ..., cat1 */
#ifdef ENABLE_VERIFIER
							if (opt_verify) {
								REQUIRE_2;
								if (IS_2_WORD_TYPE(curstack->prev->type))
									goto throw_stack_category_error;
							}
#endif
							NEW_OP2_0_ANY_ANY; /* pop two slots */
						}
						else {
							iptr->opc = ICMD_POP;
							NEW_OP1_0_ANY; /* pop one (two-word) slot */
						}
						break;

						/* pop 0 push 1 dup */

					case ICMD_DUP:
#ifdef ENABLE_VERIFIER
						if (opt_verify) {
							REQUIRE_1;
							if (IS_2_WORD_TYPE(curstack->type))
								goto throw_stack_category_error;
						}
#endif
						last_dupx = bptr->icount - len - 1;
						COUNT(count_dup_instruction);

icmd_DUP:
						USE_S1_ANY; /* XXX live through */
						DUP_SLOT(iptr->s1.var);
						iptr->dst.var = curstack;
						stackdepth++;
						break;

					case ICMD_DUP2:
						last_dupx = bptr->icount - len - 1;
						REQUIRE_1;
						if (IS_2_WORD_TYPE(curstack->type)) {
							/* ..., cat2 */
							iptr->opc = ICMD_DUP;
							goto icmd_DUP;
						}
						else {
							REQUIRE_2;
							/* ..., ????, cat1 */
#ifdef ENABLE_VERIFIER
							if (opt_verify) {
								if (IS_2_WORD_TYPE(curstack->prev->type))
									goto throw_stack_category_error;
							}
#endif
							iptr->dst.dupslots = DMNEW(stackptr, 2 + 2);
							iptr->dst.dupslots[0] = curstack->prev; /* XXX live through */
							iptr->dst.dupslots[1] = curstack;       /* XXX live through */

							DUP_SLOT(iptr->dst.dupslots[0]);
							iptr->dst.dupslots[2+0] = curstack;
							DUP_SLOT(iptr->dst.dupslots[1]);
							iptr->dst.dupslots[2+1] = curstack;
							stackdepth += 2;
						}
						break;

						/* pop 2 push 3 dup */

					case ICMD_DUP_X1:
#ifdef ENABLE_VERIFIER
						if (opt_verify) {
							REQUIRE_2;
							if (IS_2_WORD_TYPE(curstack->type) ||
								IS_2_WORD_TYPE(curstack->prev->type))
									goto throw_stack_category_error;
						}
#endif
						last_dupx = bptr->icount - len - 1;

icmd_DUP_X1:
						iptr->dst.dupslots = DMNEW(stackptr, 2 + 3);
						iptr->dst.dupslots[0] = curstack->prev;
						iptr->dst.dupslots[1] = curstack;
						POPANY; POPANY;

						DUP_SLOT(iptr->dst.dupslots[1]);
						iptr->dst.dupslots[2+0] = curstack;
						DUP_SLOT(iptr->dst.dupslots[0]);
						iptr->dst.dupslots[2+1] = curstack;
						DUP_SLOT(iptr->dst.dupslots[1]);
						iptr->dst.dupslots[2+2] = curstack;
						stackdepth++;
						break;

					case ICMD_DUP2_X1:
						last_dupx = bptr->icount - len - 1;
						REQUIRE_2;
						if (IS_2_WORD_TYPE(curstack->type)) {
							/* ..., ????, cat2 */
#ifdef ENABLE_VERIFIER
							if (opt_verify) {
								if (IS_2_WORD_TYPE(curstack->prev->type))
									goto throw_stack_category_error;
							}
#endif
							iptr->opc = ICMD_DUP_X1;
							goto icmd_DUP_X1;
						}
						else {
							/* ..., ????, cat1 */
#ifdef ENABLE_VERIFIER
							if (opt_verify) {
								REQUIRE_3;
								if (IS_2_WORD_TYPE(curstack->prev->type)
									|| IS_2_WORD_TYPE(curstack->prev->prev->type))
										goto throw_stack_category_error;
							}
#endif

icmd_DUP2_X1:
							iptr->dst.dupslots = DMNEW(stackptr, 3 + 5);
							iptr->dst.dupslots[0] = curstack->prev->prev;
							iptr->dst.dupslots[1] = curstack->prev;
							iptr->dst.dupslots[2] = curstack;
							POPANY; POPANY; POPANY;

							DUP_SLOT(iptr->dst.dupslots[1]);
							iptr->dst.dupslots[3+0] = curstack;
							DUP_SLOT(iptr->dst.dupslots[2]);
							iptr->dst.dupslots[3+1] = curstack;
							DUP_SLOT(iptr->dst.dupslots[0]);
							iptr->dst.dupslots[3+2] = curstack;
							DUP_SLOT(iptr->dst.dupslots[1]);
							iptr->dst.dupslots[3+3] = curstack;
							DUP_SLOT(iptr->dst.dupslots[2]);
							iptr->dst.dupslots[3+4] = curstack;
							stackdepth += 2;
						}
						break;

						/* pop 3 push 4 dup */

					case ICMD_DUP_X2:
						last_dupx = bptr->icount - len - 1;
						REQUIRE_2;
						if (IS_2_WORD_TYPE(curstack->prev->type)) {
							/* ..., cat2, ???? */
#ifdef ENABLE_VERIFIER
							if (opt_verify) {
								if (IS_2_WORD_TYPE(curstack->type))
									goto throw_stack_category_error;
							}
#endif
							iptr->opc = ICMD_DUP_X1;
							goto icmd_DUP_X1;
						}
						else {
							/* ..., cat1, ???? */
#ifdef ENABLE_VERIFIER
							if (opt_verify) {
								REQUIRE_3;
								if (IS_2_WORD_TYPE(curstack->type)
									|| IS_2_WORD_TYPE(curstack->prev->prev->type))
											goto throw_stack_category_error;
							}
#endif
icmd_DUP_X2:
							iptr->dst.dupslots = DMNEW(stackptr, 3 + 4);
							iptr->dst.dupslots[0] = curstack->prev->prev;
							iptr->dst.dupslots[1] = curstack->prev;
							iptr->dst.dupslots[2] = curstack;
							POPANY; POPANY; POPANY;

							DUP_SLOT(iptr->dst.dupslots[2]);
							iptr->dst.dupslots[3+0] = curstack;
							DUP_SLOT(iptr->dst.dupslots[0]);
							iptr->dst.dupslots[3+1] = curstack;
							DUP_SLOT(iptr->dst.dupslots[1]);
							iptr->dst.dupslots[3+2] = curstack;
							DUP_SLOT(iptr->dst.dupslots[2]);
							iptr->dst.dupslots[3+3] = curstack;
							stackdepth++;
						}
						break;

					case ICMD_DUP2_X2:
						last_dupx = bptr->icount - len - 1;
						REQUIRE_2;
						if (IS_2_WORD_TYPE(curstack->type)) {
							/* ..., ????, cat2 */
							if (IS_2_WORD_TYPE(curstack->prev->type)) {
								/* ..., cat2, cat2 */
								iptr->opc = ICMD_DUP_X1;
								goto icmd_DUP_X1;
							}
							else {
								/* ..., cat1, cat2 */
#ifdef ENABLE_VERIFIER
								if (opt_verify) {
									REQUIRE_3;
									if (IS_2_WORD_TYPE(curstack->prev->prev->type))
											goto throw_stack_category_error;
								}
#endif
								iptr->opc = ICMD_DUP_X2;
								goto icmd_DUP_X2;
							}
						}

						REQUIRE_3;
						/* ..., ????, ????, cat1 */

						if (IS_2_WORD_TYPE(curstack->prev->prev->type)) {
							/* ..., cat2, ????, cat1 */
#ifdef ENABLE_VERIFIER
							if (opt_verify) {
								if (IS_2_WORD_TYPE(curstack->prev->type))
									goto throw_stack_category_error;
							}
#endif
							iptr->opc = ICMD_DUP2_X1;
							goto icmd_DUP2_X1;
						}
						else {
							/* ..., cat1, ????, cat1 */
#ifdef ENABLE_VERIFIER
							if (opt_verify) {
								REQUIRE_4;
								if (IS_2_WORD_TYPE(curstack->prev->type)
									|| IS_2_WORD_TYPE(curstack->prev->prev->prev->type))
									goto throw_stack_category_error;
							}
#endif
							iptr->dst.dupslots = DMNEW(stackptr, 4 + 6);
							iptr->dst.dupslots[0] = curstack->prev->prev->prev;
							iptr->dst.dupslots[1] = curstack->prev->prev;
							iptr->dst.dupslots[2] = curstack->prev;
							iptr->dst.dupslots[3] = curstack;
							POPANY; POPANY; POPANY; POPANY;

							DUP_SLOT(iptr->dst.dupslots[2]);
							iptr->dst.dupslots[4+0] = curstack;
							DUP_SLOT(iptr->dst.dupslots[3]);
							iptr->dst.dupslots[4+1] = curstack;
							DUP_SLOT(iptr->dst.dupslots[0]);
							iptr->dst.dupslots[4+2] = curstack;
							DUP_SLOT(iptr->dst.dupslots[1]);
							iptr->dst.dupslots[4+3] = curstack;
							DUP_SLOT(iptr->dst.dupslots[2]);
							iptr->dst.dupslots[4+4] = curstack;
							DUP_SLOT(iptr->dst.dupslots[3]);
							iptr->dst.dupslots[4+5] = curstack;
							stackdepth += 2;
						}
						break;

						/* pop 2 push 2 swap */

					case ICMD_SWAP:
						last_dupx = bptr->icount - len - 1;
#ifdef ENABLE_VERIFIER
						if (opt_verify) {
							REQUIRE_2;
							if (IS_2_WORD_TYPE(curstack->type)
								|| IS_2_WORD_TYPE(curstack->prev->type))
								goto throw_stack_category_error;
						}
#endif
						iptr->dst.dupslots = DMNEW(stackptr, 2 + 2);
						iptr->dst.dupslots[0] = curstack->prev;
						iptr->dst.dupslots[1] = curstack;
						POPANY; POPANY;

						DUP_SLOT(iptr->dst.dupslots[1]);
						iptr->dst.dupslots[2+0] = curstack;
						DUP_SLOT(iptr->dst.dupslots[0]);
						iptr->dst.dupslots[2+1] = curstack;
						break;

						/* pop 2 push 1 */

					case ICMD_IDIV:
					case ICMD_IREM:
#if !SUPPORT_DIVISION
						bte = iptr->sx.s23.s3.bte;
						md = bte->md;

						if (md->memuse > rd->memuse)
							rd->memuse = md->memuse;
						if (md->argintreguse > rd->argintreguse)
							rd->argintreguse = md->argintreguse;

						/* make all stack variables saved */

						copy = curstack;
						while (copy) {
							copy->flags |= SAVEDVAR;
							copy = copy->prev;
						}
						/* FALLTHROUGH */

#endif /* !SUPPORT_DIVISION */

					case ICMD_ISHL:
					case ICMD_ISHR:
					case ICMD_IUSHR:
					case ICMD_IADD:
					case ICMD_ISUB:
					case ICMD_IMUL:
					case ICMD_IAND:
					case ICMD_IOR:
					case ICMD_IXOR:
						COUNT(count_pcmd_op);
						NEW_OP2_1(TYPE_INT, TYPE_INT, TYPE_INT);
						break;

					case ICMD_LDIV:
					case ICMD_LREM:
#if !(SUPPORT_DIVISION && SUPPORT_LONG && SUPPORT_LONG_DIV)
						bte = iptr->sx.s23.s3.bte;
						md = bte->md;

						if (md->memuse > rd->memuse)
							rd->memuse = md->memuse;
						if (md->argintreguse > rd->argintreguse)
							rd->argintreguse = md->argintreguse;
						/* XXX non-leaf method? */

						/* make all stack variables saved */

						copy = curstack;
						while (copy) {
							copy->flags |= SAVEDVAR;
							copy = copy->prev;
						}
						/* FALLTHROUGH */

#endif /* !(SUPPORT_DIVISION && SUPPORT_LONG && SUPPORT_LONG_DIV) */

					case ICMD_LMUL:
					case ICMD_LADD:
					case ICMD_LSUB:
#if SUPPORT_LONG_LOGICAL
					case ICMD_LAND:
					case ICMD_LOR:
					case ICMD_LXOR:
#endif /* SUPPORT_LONG_LOGICAL */
						COUNT(count_pcmd_op);
						NEW_OP2_1(TYPE_LNG, TYPE_LNG, TYPE_LNG);
						break;

					case ICMD_LSHL:
					case ICMD_LSHR:
					case ICMD_LUSHR:
						COUNT(count_pcmd_op);
						NEW_OP2_1(TYPE_LNG, TYPE_INT, TYPE_LNG);
						break;

					case ICMD_FADD:
					case ICMD_FSUB:
					case ICMD_FMUL:
					case ICMD_FDIV:
					case ICMD_FREM:
						COUNT(count_pcmd_op);
						NEW_OP2_1(TYPE_FLT, TYPE_FLT, TYPE_FLT);
						break;

					case ICMD_DADD:
					case ICMD_DSUB:
					case ICMD_DMUL:
					case ICMD_DDIV:
					case ICMD_DREM:
						COUNT(count_pcmd_op);
						NEW_OP2_1(TYPE_DBL, TYPE_DBL, TYPE_DBL);
						break;

					case ICMD_LCMP:
						COUNT(count_pcmd_op);
#if SUPPORT_LONG_CMP_CONST
						if ((len == 0) || (iptr[1].sx.val.i != 0))
							goto normal_LCMP;

						switch (iptr[1].opc) {
						case ICMD_IFEQ:
							iptr->opc = ICMD_IF_LCMPEQ;
						icmd_lcmp_if_tail:
							iptr->dst.insindex = iptr[1].dst.insindex;
							iptr[1].opc = ICMD_NOP;

							NEW_OP2_BRANCH(TYPE_LNG, TYPE_LNG);
							BRANCH(tbptr, copy);

							COUNT(count_pcmd_bra);
							break;
						case ICMD_IFNE:
							iptr->opc = ICMD_IF_LCMPNE;
							goto icmd_lcmp_if_tail;
						case ICMD_IFLT:
							iptr->opc = ICMD_IF_LCMPLT;
							goto icmd_lcmp_if_tail;
						case ICMD_IFGT:
							iptr->opc = ICMD_IF_LCMPGT;
							goto icmd_lcmp_if_tail;
						case ICMD_IFLE:
							iptr->opc = ICMD_IF_LCMPLE;
							goto icmd_lcmp_if_tail;
						case ICMD_IFGE:
							iptr->opc = ICMD_IF_LCMPGE;
							goto icmd_lcmp_if_tail;
						default:
							goto normal_LCMP;
						}
						break;
normal_LCMP:
#endif /* SUPPORT_LONG_CMP_CONST */
							NEW_OP2_1(TYPE_LNG, TYPE_LNG, TYPE_INT);
						break;

						/* XXX why is this deactivated? */
#if 0
					case ICMD_FCMPL:
						COUNT(count_pcmd_op);
						if ((len == 0) || (iptr[1].sx.val.i != 0))
							goto normal_FCMPL;

						switch (iptr[1].opc) {
						case ICMD_IFEQ:
							iptr->opc = ICMD_IF_FCMPEQ;
						icmd_if_fcmpl_tail:
							iptr->dst.insindex = iptr[1].dst.insindex;
							iptr[1].opc = ICMD_NOP;

							NEW_OP2_BRANCH(TYPE_FLT, TYPE_FLT);
							BRANCH(tbptr, copy);

							COUNT(count_pcmd_bra);
							break;
						case ICMD_IFNE:
							iptr->opc = ICMD_IF_FCMPNE;
							goto icmd_if_fcmpl_tail;
						case ICMD_IFLT:
							iptr->opc = ICMD_IF_FCMPL_LT;
							goto icmd_if_fcmpl_tail;
						case ICMD_IFGT:
							iptr->opc = ICMD_IF_FCMPL_GT;
							goto icmd_if_fcmpl_tail;
						case ICMD_IFLE:
							iptr->opc = ICMD_IF_FCMPL_LE;
							goto icmd_if_fcmpl_tail;
						case ICMD_IFGE:
							iptr->opc = ICMD_IF_FCMPL_GE;
							goto icmd_if_fcmpl_tail;
						default:
							goto normal_FCMPL;
						}
						break;

normal_FCMPL:
						OPTT2_1(TYPE_FLT, TYPE_FLT, TYPE_INT);
						break;

					case ICMD_FCMPG:
						COUNT(count_pcmd_op);
						if ((len == 0) || (iptr[1].sx.val.i != 0))
							goto normal_FCMPG;

						switch (iptr[1].opc) {
						case ICMD_IFEQ:
							iptr->opc = ICMD_IF_FCMPEQ;
						icmd_if_fcmpg_tail:
							iptr->dst.insindex = iptr[1].dst.insindex;
							iptr[1].opc = ICMD_NOP;

							NEW_OP2_BRANCH(TYPE_FLT, TYPE_FLT);
							BRANCH(tbptr, copy);

							COUNT(count_pcmd_bra);
							break;
						case ICMD_IFNE:
							iptr->opc = ICMD_IF_FCMPNE;
							goto icmd_if_fcmpg_tail;
						case ICMD_IFLT:
							iptr->opc = ICMD_IF_FCMPG_LT;
							goto icmd_if_fcmpg_tail;
						case ICMD_IFGT:
							iptr->opc = ICMD_IF_FCMPG_GT;
							goto icmd_if_fcmpg_tail;
						case ICMD_IFLE:
							iptr->opc = ICMD_IF_FCMPG_LE;
							goto icmd_if_fcmpg_tail;
						case ICMD_IFGE:
							iptr->opc = ICMD_IF_FCMPG_GE;
							goto icmd_if_fcmpg_tail;
						default:
							goto normal_FCMPG;
						}
						break;

normal_FCMPG:
						NEW_OP2_1(TYPE_FLT, TYPE_FLT, TYPE_INT);
						break;

					case ICMD_DCMPL:
						COUNT(count_pcmd_op);
						if ((len == 0) || (iptr[1].sx.val.i != 0))
							goto normal_DCMPL;

						switch (iptr[1].opc) {
						case ICMD_IFEQ:
							iptr->opc = ICMD_IF_DCMPEQ;
						icmd_if_dcmpl_tail:
							iptr->dst.insindex = iptr[1].dst.insindex;
							iptr[1].opc = ICMD_NOP;

							NEW_OP2_BRANCH(TYPE_DBL, TYPE_DBL);
							BRANCH(tbptr, copy);

							COUNT(count_pcmd_bra);
							break;
						case ICMD_IFNE:
							iptr->opc = ICMD_IF_DCMPNE;
							goto icmd_if_dcmpl_tail;
						case ICMD_IFLT:
							iptr->opc = ICMD_IF_DCMPL_LT;
							goto icmd_if_dcmpl_tail;
						case ICMD_IFGT:
							iptr->opc = ICMD_IF_DCMPL_GT;
							goto icmd_if_dcmpl_tail;
						case ICMD_IFLE:
							iptr->opc = ICMD_IF_DCMPL_LE;
							goto icmd_if_dcmpl_tail;
						case ICMD_IFGE:
							iptr->opc = ICMD_IF_DCMPL_GE;
							goto icmd_if_dcmpl_tail;
						default:
							goto normal_DCMPL;
						}
						break;

normal_DCMPL:
						OPTT2_1(TYPE_DBL, TYPE_INT);
						break;

					case ICMD_DCMPG:
						COUNT(count_pcmd_op);
						if ((len == 0) || (iptr[1].sx.val.i != 0))
							goto normal_DCMPG;

						switch (iptr[1].opc) {
						case ICMD_IFEQ:
							iptr->opc = ICMD_IF_DCMPEQ;
						icmd_if_dcmpg_tail:
							iptr->dst.insindex = iptr[1].dst.insindex;
							iptr[1].opc = ICMD_NOP;

							NEW_OP2_BRANCH(TYPE_DBL, TYPE_DBL);
							BRANCH(tbptr, copy);

							COUNT(count_pcmd_bra);
							break;
						case ICMD_IFNE:
							iptr->opc = ICMD_IF_DCMPNE;
							goto icmd_if_dcmpg_tail;
						case ICMD_IFLT:
							iptr->opc = ICMD_IF_DCMPG_LT;
							goto icmd_if_dcmpg_tail;
						case ICMD_IFGT:
							iptr->opc = ICMD_IF_DCMPG_GT;
							goto icmd_if_dcmpg_tail;
						case ICMD_IFLE:
							iptr->opc = ICMD_IF_DCMPG_LE;
							goto icmd_if_dcmpg_tail;
						case ICMD_IFGE:
							iptr->opc = ICMD_IF_DCMPG_GE;
							goto icmd_if_dcmpg_tail;
						default:
							goto normal_DCMPG;
						}
						break;

normal_DCMPG:
						NEW_OP2_1(TYPE_DBL, TYPE_DBL, TYPE_INT);
						break;
#else
					case ICMD_FCMPL:
					case ICMD_FCMPG:
						COUNT(count_pcmd_op);
						NEW_OP2_1(TYPE_FLT, TYPE_FLT, TYPE_INT);
						break;

					case ICMD_DCMPL:
					case ICMD_DCMPG:
						COUNT(count_pcmd_op);
						NEW_OP2_1(TYPE_DBL, TYPE_DBL, TYPE_INT);
						break;
#endif

						/* pop 1 push 1 */

					case ICMD_INEG:
					case ICMD_INT2BYTE:
					case ICMD_INT2CHAR:
					case ICMD_INT2SHORT:
						COUNT(count_pcmd_op);
						NEW_OP1_1(TYPE_INT, TYPE_INT);
						break;
					case ICMD_LNEG:
						COUNT(count_pcmd_op);
						NEW_OP1_1(TYPE_LNG, TYPE_LNG);
						break;
					case ICMD_FNEG:
						COUNT(count_pcmd_op);
						NEW_OP1_1(TYPE_FLT, TYPE_FLT);
						break;
					case ICMD_DNEG:
						COUNT(count_pcmd_op);
						NEW_OP1_1(TYPE_DBL, TYPE_DBL);
						break;

					case ICMD_I2L:
						COUNT(count_pcmd_op);
						NEW_OP1_1(TYPE_INT, TYPE_LNG);
						break;
					case ICMD_I2F:
						COUNT(count_pcmd_op);
						NEW_OP1_1(TYPE_INT, TYPE_FLT);
						break;
					case ICMD_I2D:
						COUNT(count_pcmd_op);
						NEW_OP1_1(TYPE_INT, TYPE_DBL);
						break;
					case ICMD_L2I:
						COUNT(count_pcmd_op);
						NEW_OP1_1(TYPE_LNG, TYPE_INT);
						break;
					case ICMD_L2F:
						COUNT(count_pcmd_op);
						NEW_OP1_1(TYPE_LNG, TYPE_FLT);
						break;
					case ICMD_L2D:
						COUNT(count_pcmd_op);
						NEW_OP1_1(TYPE_LNG, TYPE_DBL);
						break;
					case ICMD_F2I:
						COUNT(count_pcmd_op);
						NEW_OP1_1(TYPE_FLT, TYPE_INT);
						break;
					case ICMD_F2L:
						COUNT(count_pcmd_op);
						NEW_OP1_1(TYPE_FLT, TYPE_LNG);
						break;
					case ICMD_F2D:
						COUNT(count_pcmd_op);
						NEW_OP1_1(TYPE_FLT, TYPE_DBL);
						break;
					case ICMD_D2I:
						COUNT(count_pcmd_op);
						NEW_OP1_1(TYPE_DBL, TYPE_INT);
						break;
					case ICMD_D2L:
						COUNT(count_pcmd_op);
						NEW_OP1_1(TYPE_DBL, TYPE_LNG);
						break;
					case ICMD_D2F:
						COUNT(count_pcmd_op);
						NEW_OP1_1(TYPE_DBL, TYPE_FLT);
						break;

					case ICMD_CHECKCAST:
						if (iptr->flags.bits & INS_FLAG_ARRAY) {
							/* array type cast-check */

							bte = builtintable_get_internal(BUILTIN_arraycheckcast);
							md = bte->md;

							if (md->memuse > rd->memuse)
								rd->memuse = md->memuse;
							if (md->argintreguse > rd->argintreguse)
								rd->argintreguse = md->argintreguse;

							/* make all stack variables saved */

							copy = curstack;
							while (copy) {
								copy->flags |= SAVEDVAR;
								copy = copy->prev;
							}
						}
						NEW_OP1_1(TYPE_ADR, TYPE_ADR);
						break;

					case ICMD_INSTANCEOF:
					case ICMD_ARRAYLENGTH:
						NEW_OP1_1(TYPE_ADR, TYPE_INT);
						break;

					case ICMD_NEWARRAY:
					case ICMD_ANEWARRAY:
						NEW_OP1_1(TYPE_INT, TYPE_ADR);
						break;

					case ICMD_GETFIELD:
						COUNT(count_check_null);
						COUNT(count_pcmd_mem);
						NEW_INSTRUCTION_GET_FIELDREF(iptr, fmiref);
						NEW_OP1_1(TYPE_ADR, fmiref->parseddesc.fd->type);
						break;

						/* pop 0 push 1 */

					case ICMD_GETSTATIC:
						COUNT(count_pcmd_mem);
						NEW_INSTRUCTION_GET_FIELDREF(iptr, fmiref);
						NEW_OP0_1(fmiref->parseddesc.fd->type);
						break;

					case ICMD_NEW:
						NEW_OP0_1(TYPE_ADR);
						break;

					case ICMD_JSR:
						NEW_OP0_1(TYPE_ADR);

						BRANCH_TARGET(iptr->sx.s23.s3.jsrtarget, tbptr, copy);

						tbptr->type = BBTYPE_SBR;

						/* We need to check for overflow right here because
						 * the pushed value is poped afterwards */
						NEW_CHECKOVERFLOW;

						/* calculate stack after return */
						POPANY;
						stackdepth--;
						break;

					/* pop many push any */

					case ICMD_BUILTIN:
#if defined(USEBUILTINTABLE)
icmd_BUILTIN:
#endif
						bte = iptr->sx.s23.s3.bte;
						md = bte->md;
						goto _callhandling;

					case ICMD_INVOKESTATIC:
					case ICMD_INVOKESPECIAL:
					case ICMD_INVOKEVIRTUAL:
					case ICMD_INVOKEINTERFACE:
						COUNT(count_pcmd_met);
						NEW_INSTRUCTION_GET_METHODDESC(iptr, md);
						/* XXX resurrect this COUNT? */
/*                          if (lm->flags & ACC_STATIC) */
/*                              {COUNT(count_check_null);} */

					_callhandling:

						last_pei = bptr->icount - len - 1;

						i = md->paramcount;

						if (md->memuse > rd->memuse)
							rd->memuse = md->memuse;
						if (md->argintreguse > rd->argintreguse)
							rd->argintreguse = md->argintreguse;
						if (md->argfltreguse > rd->argfltreguse)
							rd->argfltreguse = md->argfltreguse;

						REQUIRE(i);

						/* XXX optimize for <= 2 args */
						iptr->s1.argcount = i;
						iptr->sx.s23.s2.args = DMNEW(stackptr, i);

						copy = curstack;
						for (i-- ; i >= 0; i--) {
							iptr->sx.s23.s2.args[i] = copy;

#if defined(SUPPORT_PASS_FLOATARGS_IN_INTREGS)
							/* If we pass float arguments in integer argument registers, we
							 * are not allowed to precolor them here. Floats have to be moved
							 * to this regs explicitly in codegen().
							 * Only arguments that are passed by stack anyway can be precolored
							 * (michi 2005/07/24) */
							if (!(copy->flags & SAVEDVAR) &&
							   (!IS_FLT_DBL_TYPE(copy->type) || md->params[i].inmemory)) {
#else
							if (!(copy->flags & SAVEDVAR)) {
#endif
								copy->varkind = ARGVAR;
								copy->varnum = i;

#if defined(ENABLE_INTRP)
								if (!opt_intrp) {
#endif
									if (md->params[i].inmemory) {
										copy->flags = INMEMORY;
										copy->regoff = md->params[i].regoff;
									}
									else {
										copy->flags = 0;
										if (IS_FLT_DBL_TYPE(copy->type)) {
#if defined(SUPPORT_PASS_FLOATARGS_IN_INTREGS)
											assert(0); /* XXX is this assert ok? */
#else
											copy->regoff =
												rd->argfltregs[md->params[i].regoff];
#endif /* SUPPORT_PASS_FLOATARGS_IN_INTREGS */
										}
										else {
#if defined(SUPPORT_COMBINE_INTEGER_REGISTERS)
											if (IS_2_WORD_TYPE(copy->type))
												copy->regoff = PACK_REGS(
													rd->argintregs[GET_LOW_REG(md->params[i].regoff)],
													rd->argintregs[GET_HIGH_REG(md->params[i].regoff)]);
											else
#endif /* SUPPORT_COMBINE_INTEGER_REGISTERS */
												copy->regoff =
													rd->argintregs[md->params[i].regoff];
										}
									}
#if defined(ENABLE_INTRP)
								} /* end if (!opt_intrp) */
#endif
							}
							copy = copy->prev;
						}

						while (copy) {
							copy->flags |= SAVEDVAR;
							copy = copy->prev;
						}

						i = md->paramcount;

						stackdepth -= i;
						while (--i >= 0) {
							POPANY;
						}

						if (md->returntype.type != TYPE_VOID) {
							NEW_DST(md->returntype.type, stackdepth);
							stackdepth++;
						}
						break;

					case ICMD_INLINE_START:
					case ICMD_INLINE_END:
						CLR_S1;
						CLR_DST;
						break;

					case ICMD_MULTIANEWARRAY:
						if (rd->argintreguse < 3)
							rd->argintreguse = 3;

						i = iptr->s1.argcount;

						REQUIRE(i);

						iptr->sx.s23.s2.args = DMNEW(stackptr, i);

#if defined(SPECIALMEMUSE)
# if defined(__DARWIN__)
						if (rd->memuse < (i + INT_ARG_CNT + LA_WORD_SIZE))
							rd->memuse = i + LA_WORD_SIZE + INT_ARG_CNT;
# else
						if (rd->memuse < (i + LA_WORD_SIZE + 3))
							rd->memuse = i + LA_WORD_SIZE + 3;
# endif
#else
# if defined(__I386__)
						if (rd->memuse < i + 3)
							rd->memuse = i + 3; /* n integer args spilled on stack */
# elif defined(__MIPS__) && SIZEOF_VOID_P == 4
						if (rd->memuse < i + 2)
							rd->memuse = i + 2; /* 4*4 bytes callee save space */
# else
						if (rd->memuse < i)
							rd->memuse = i; /* n integer args spilled on stack */
# endif /* defined(__I386__) */
#endif
						copy = curstack;
						while (--i >= 0) {
							/* check INT type here? Currently typecheck does this. */
							iptr->sx.s23.s2.args[i] = copy;
							if (!(copy->flags & SAVEDVAR)) {
								copy->varkind = ARGVAR;
								copy->varnum = i + INT_ARG_CNT;
								copy->flags |= INMEMORY;
#if defined(SPECIALMEMUSE)
# if defined(__DARWIN__)
								copy->regoff = i + LA_WORD_SIZE + INT_ARG_CNT;
# else
								copy->regoff = i + LA_WORD_SIZE + 3;
# endif
#else
# if defined(__I386__)
								copy->regoff = i + 3;
# elif defined(__MIPS__) && SIZEOF_VOID_P == 4
								copy->regoff = i + 2;
# else
								copy->regoff = i;
# endif /* defined(__I386__) */
#endif /* defined(SPECIALMEMUSE) */
							}
							copy = copy->prev;
						}
						while (copy) {
							copy->flags |= SAVEDVAR;
							copy = copy->prev;
						}

						i = iptr->s1.argcount;
						stackdepth -= i;
						while (--i >= 0) {
							POPANY;
						}
						NEW_DST(TYPE_ADR, stackdepth);
						stackdepth++;
						break;

					default:
						*exceptionptr =
							new_internalerror("Unknown ICMD %d", opcode);
						return false;
					} /* switch */

					NEW_CHECKOVERFLOW;
					iptr++;
				} /* while instructions */

				/* set out-stack of block */

				bptr->outstack = curstack;
				bptr->outdepth = stackdepth;

				/* stack slots at basic block end become interfaces */

				i = stackdepth - 1;
				for (copy = curstack; copy; i--, copy = copy->prev) {
					if ((copy->varkind == STACKVAR) && (copy->varnum > i))
						copy->varkind = TEMPVAR;
					else {
						copy->varkind = STACKVAR;
						copy->varnum = i;
					}
					IF_NO_INTRP(
							rd->interfaces[i][copy->type].type = copy->type;
							rd->interfaces[i][copy->type].flags |= copy->flags;
					);
				}

				/* check if interface slots at basic block begin must be saved */

				IF_NO_INTRP(
					i = bptr->indepth - 1;
					for (copy = bptr->instack; copy; i--, copy = copy->prev) {
						rd->interfaces[i][copy->type].type = copy->type;
						if (copy->varkind == STACKVAR) {
							if (copy->flags & SAVEDVAR)
								rd->interfaces[i][copy->type].flags |= SAVEDVAR;
						}
					}
				);

			} /* if */
			else
				superblockend = true;

			bptr++;
		} /* while blocks */
	} while (repeat && !deadcode);

	/* gather statistics *****************************************************/

#if defined(ENABLE_STATISTICS)
	if (opt_stat) {
		if (jd->new_basicblockcount > count_max_basic_blocks)
			count_max_basic_blocks = jd->new_basicblockcount;
		count_basic_blocks += jd->new_basicblockcount;
		if (jd->new_instructioncount > count_max_javainstr)
			count_max_javainstr = jd->new_instructioncount;
		count_javainstr += jd->new_instructioncount;
		if (jd->new_stackcount > count_upper_bound_new_stack)
			count_upper_bound_new_stack = jd->new_stackcount;
		if ((new - jd->new_stack) > count_max_new_stack)
			count_max_new_stack = (new - jd->new_stack);

		b_count = jd->new_basicblockcount;
		bptr = jd->new_basicblocks;
		while (--b_count >= 0) {
			if (bptr->flags > BBREACHED) {
				if (bptr->indepth >= 10)
					count_block_stack[10]++;
				else
					count_block_stack[bptr->indepth]++;
				len = bptr->icount;
				if (len < 10)
					count_block_size_distribution[len]++;
				else if (len <= 12)
					count_block_size_distribution[10]++;
				else if (len <= 14)
					count_block_size_distribution[11]++;
				else if (len <= 16)
					count_block_size_distribution[12]++;
				else if (len <= 18)
					count_block_size_distribution[13]++;
				else if (len <= 20)
					count_block_size_distribution[14]++;
				else if (len <= 25)
					count_block_size_distribution[15]++;
				else if (len <= 30)
					count_block_size_distribution[16]++;
				else
					count_block_size_distribution[17]++;
			}
			bptr++;
		}

		if (iteration_count == 1)
			count_analyse_iterations[0]++;
		else if (iteration_count == 2)
			count_analyse_iterations[1]++;
		else if (iteration_count == 3)
			count_analyse_iterations[2]++;
		else if (iteration_count == 4)
			count_analyse_iterations[3]++;
		else
			count_analyse_iterations[4]++;

		if (jd->new_basicblockcount <= 5)
			count_method_bb_distribution[0]++;
		else if (jd->new_basicblockcount <= 10)
			count_method_bb_distribution[1]++;
		else if (jd->new_basicblockcount <= 15)
			count_method_bb_distribution[2]++;
		else if (jd->new_basicblockcount <= 20)
			count_method_bb_distribution[3]++;
		else if (jd->new_basicblockcount <= 30)
			count_method_bb_distribution[4]++;
		else if (jd->new_basicblockcount <= 40)
			count_method_bb_distribution[5]++;
		else if (jd->new_basicblockcount <= 50)
			count_method_bb_distribution[6]++;
		else if (jd->new_basicblockcount <= 75)
			count_method_bb_distribution[7]++;
		else
			count_method_bb_distribution[8]++;
	}
#endif /* defined(ENABLE_STATISTICS) */

	/* everything's ok *******************************************************/

	return true;

	/* goto labels for throwing verifier exceptions **************************/

#if defined(ENABLE_VERIFIER)

throw_stack_underflow:
	*exceptionptr =
		new_verifyerror(m, "Unable to pop operand off an empty stack");
	return false;

throw_stack_overflow:
	*exceptionptr = new_verifyerror(m, "Stack size too large");
	return false;

throw_stack_depth_error:
	*exceptionptr = new_verifyerror(m,"Stack depth mismatch");
	return false;

throw_stack_type_error:
	exceptions_throw_verifyerror_for_stack(m, expectedtype);
	return false;

throw_stack_category_error:
	*exceptionptr =
		new_verifyerror(m, "Attempt to split long or double on the stack");
	return false;

#endif
}

bool stack_analyse(jitdata *jd)
{
	methodinfo   *m;
	codeinfo     *code;
	codegendata  *cd;
	registerdata *rd;
	int           b_count;
	int           b_index;
	int           stackdepth;
	stackptr      curstack;
	stackptr      new;
	stackptr      copy;
	int           opcode, i, j, len, loops;
	int           superblockend, repeat, deadcode;
	instruction  *iptr;
	basicblock   *bptr;
	basicblock   *tbptr;
	s4           *s4ptr;
	void        **tptr;
	s4           *last_store;/* instruction index of last XSTORE */
	                         /* [ local_index * 5 + type ] */
	s4            last_pei;  /* instruction index of last possible exception */
	                         /* used for conflict resolution for copy        */
                             /* elimination (XLOAD, IINC, XSTORE) */
	s4            last_dupx;
#if defined(ENABLE_VERIFIER)
	int           expectedtype; /* used by CHECK_BASIC_TYPE */
#endif

	builtintable_entry *bte;
	methoddesc         *md;

	/* get required compiler data */

	m    = jd->m;
	code = jd->code;
	cd   = jd->cd;
	rd   = jd->rd;

#if defined(ENABLE_LSRA)
	m->maxlifetimes = 0;
#endif

	last_store = DMNEW(s4 , cd->maxlocals * 5);
	
	new = m->stack;
	loops = 0;
	m->basicblocks[0].flags = BBREACHED;
	m->basicblocks[0].instack = 0;
	m->basicblocks[0].indepth = 0;

	for (i = 0; i < cd->exceptiontablelength; i++) {
		bptr = &m->basicblocks[m->basicblockindex[cd->exceptiontable[i].handlerpc]];
		bptr->flags = BBREACHED;
		bptr->type = BBTYPE_EXH;
		bptr->instack = new;
		bptr->indepth = 1;
		bptr->pre_count = 10000;
		STACKRESET;
		NEWXSTACK;
	}

#if CONDITIONAL_LOADCONST
	b_count = m->basicblockcount;
	bptr = m->basicblocks;
	while (--b_count >= 0) {
		if (bptr->icount != 0) {
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
				bptr[1].pre_count++;
			case ICMD_GOTO:
				m->basicblocks[m->basicblockindex[iptr->op1]].pre_count++;
				break;

			case ICMD_TABLESWITCH:
				s4ptr = iptr->val.a;
				m->basicblocks[m->basicblockindex[*s4ptr++]].pre_count++;
				i = *s4ptr++;                               /* low     */
				i = *s4ptr++ - i + 1;                       /* high    */
				while (--i >= 0) {
					m->basicblocks[m->basicblockindex[*s4ptr++]].pre_count++;
				}
				break;
					
			case ICMD_LOOKUPSWITCH:
				s4ptr = iptr->val.a;
				m->basicblocks[m->basicblockindex[*s4ptr++]].pre_count++;
				i = *s4ptr++;                               /* count   */
				while (--i >= 0) {
					m->basicblocks[m->basicblockindex[s4ptr[1]]].pre_count++;
					s4ptr += 2;
				}
				break;
			default:
				bptr[1].pre_count++;
				break;
			}
		}
		bptr++;
	}
#endif /* CONDITIONAL_LOADCONST */


	do {
		loops++;
		b_count = m->basicblockcount;
		bptr = m->basicblocks;
		superblockend = true;
		repeat = false;
		STACKRESET;
		deadcode = true;

		while (--b_count >= 0) {
			if (bptr->flags == BBDELETED) {
				/* do nothing */
			} 
			else if (superblockend && (bptr->flags < BBREACHED)) {
				repeat = true;
			} 
			else if (bptr->flags <= BBREACHED) {
				if (superblockend) {
					stackdepth = bptr->indepth;
				} 
				else if (bptr->flags < BBREACHED) {
					COPYCURSTACK(copy);
					bptr->instack = copy;
					bptr->indepth = stackdepth;
				} 
				else {
					CHECK_STACK_DEPTH(bptr->indepth, stackdepth);
				}

				curstack = bptr->instack;
				deadcode = false;
				superblockend = false;
				bptr->flags = BBFINISHED;
				len = bptr->icount;
				iptr = bptr->iinstr;
				b_index = bptr - m->basicblocks;

				last_pei = -1;
				last_dupx = -1;
				for( i = 0; i < cd->maxlocals; i++)
					for( j = 0; j < 5; j++)
						last_store[5 * i + j] = -1;

				bptr->stack = new;

				while (--len >= 0)  {
					opcode = iptr->opc;

#if defined(USEBUILTINTABLE)
# if defined(ENABLE_INTRP)
					if (!opt_intrp) {
# endif
						bte = builtintable_get_automatic(opcode);

						if (bte && bte->opcode == opcode) {
							iptr->opc   = ICMD_BUILTIN;
							iptr->op1   = false; /* don't check for exception */
							iptr->val.a = bte;
							jd->isleafmethod = false;
							goto builtin;
						}
# if defined(ENABLE_INTRP)
					}
# endif
#endif /* defined(USEBUILTINTABLE) */

					/* this is the main switch */

					switch (opcode) {

						/* pop 0 push 0 */

					case ICMD_CHECKNULL:
						COUNT(count_check_null);
					case ICMD_NOP:

					case ICMD_IFEQ_ICONST:
					case ICMD_IFNE_ICONST:
					case ICMD_IFLT_ICONST:
					case ICMD_IFGE_ICONST:
					case ICMD_IFGT_ICONST:
					case ICMD_IFLE_ICONST:
					case ICMD_ELSE_ICONST:
						SETDST;
						break;

					case ICMD_RET:
#if defined(ENABLE_INTRP)
						if (!opt_intrp)
#endif
							rd->locals[iptr->op1][TYPE_ADR].type = TYPE_ADR;
					case ICMD_RETURN:
						COUNT(count_pcmd_return);
						SETDST;
						superblockend = true;
						break;

						/* pop 0 push 1 const */
						
					case ICMD_ICONST:
						COUNT(count_pcmd_load);
						if (len > 0) {
							switch (iptr[1].opc) {
							case ICMD_IADD:
								iptr[0].opc = ICMD_IADDCONST;
							icmd_iconst_tail:
								iptr[1].opc = ICMD_NOP;
								OP1_1(TYPE_INT, TYPE_INT);
								COUNT(count_pcmd_op);
								break;
							case ICMD_ISUB:
								iptr[0].opc = ICMD_ISUBCONST;
								goto icmd_iconst_tail;
#if SUPPORT_CONST_MUL
							case ICMD_IMUL:
								iptr[0].opc = ICMD_IMULCONST;
								goto icmd_iconst_tail;
#else /* SUPPORT_CONST_MUL */
							case ICMD_IMUL:
								if (iptr[0].val.i == 0x00000002)
									iptr[0].val.i = 1;
								else if (iptr[0].val.i == 0x00000004)
									iptr[0].val.i = 2;
								else if (iptr[0].val.i == 0x00000008)
									iptr[0].val.i = 3;
								else if (iptr[0].val.i == 0x00000010)
									iptr[0].val.i = 4;
								else if (iptr[0].val.i == 0x00000020)
									iptr[0].val.i = 5;
								else if (iptr[0].val.i == 0x00000040)
									iptr[0].val.i = 6;
								else if (iptr[0].val.i == 0x00000080)
									iptr[0].val.i = 7;
								else if (iptr[0].val.i == 0x00000100)
									iptr[0].val.i = 8;
								else if (iptr[0].val.i == 0x00000200)
									iptr[0].val.i = 9;
								else if (iptr[0].val.i == 0x00000400)
									iptr[0].val.i = 10;
								else if (iptr[0].val.i == 0x00000800)
									iptr[0].val.i = 11;
								else if (iptr[0].val.i == 0x00001000)
									iptr[0].val.i = 12;
								else if (iptr[0].val.i == 0x00002000)
									iptr[0].val.i = 13;
								else if (iptr[0].val.i == 0x00004000)
									iptr[0].val.i = 14;
								else if (iptr[0].val.i == 0x00008000)
									iptr[0].val.i = 15;
								else if (iptr[0].val.i == 0x00010000)
									iptr[0].val.i = 16;
								else if (iptr[0].val.i == 0x00020000)
									iptr[0].val.i = 17;
								else if (iptr[0].val.i == 0x00040000)
									iptr[0].val.i = 18;
								else if (iptr[0].val.i == 0x00080000)
									iptr[0].val.i = 19;
								else if (iptr[0].val.i == 0x00100000)
									iptr[0].val.i = 20;
								else if (iptr[0].val.i == 0x00200000)
									iptr[0].val.i = 21;
								else if (iptr[0].val.i == 0x00400000)
									iptr[0].val.i = 22;
								else if (iptr[0].val.i == 0x00800000)
									iptr[0].val.i = 23;
								else if (iptr[0].val.i == 0x01000000)
									iptr[0].val.i = 24;
								else if (iptr[0].val.i == 0x02000000)
									iptr[0].val.i = 25;
								else if (iptr[0].val.i == 0x04000000)
									iptr[0].val.i = 26;
								else if (iptr[0].val.i == 0x08000000)
									iptr[0].val.i = 27;
								else if (iptr[0].val.i == 0x10000000)
									iptr[0].val.i = 28;
								else if (iptr[0].val.i == 0x20000000)
									iptr[0].val.i = 29;
								else if (iptr[0].val.i == 0x40000000)
									iptr[0].val.i = 30;
								else if (iptr[0].val.i == 0x80000000)
									iptr[0].val.i = 31;
								else {
									PUSHCONST(TYPE_INT);
									break;
								}
								iptr[0].opc = ICMD_IMULPOW2;
								goto icmd_iconst_tail;
#endif /* SUPPORT_CONST_MUL */
							case ICMD_IDIV:
								if (iptr[0].val.i == 0x00000002)
									iptr[0].val.i = 1;
								else if (iptr[0].val.i == 0x00000004)
									iptr[0].val.i = 2;
								else if (iptr[0].val.i == 0x00000008)
									iptr[0].val.i = 3;
								else if (iptr[0].val.i == 0x00000010)
									iptr[0].val.i = 4;
								else if (iptr[0].val.i == 0x00000020)
									iptr[0].val.i = 5;
								else if (iptr[0].val.i == 0x00000040)
									iptr[0].val.i = 6;
								else if (iptr[0].val.i == 0x00000080)
									iptr[0].val.i = 7;
								else if (iptr[0].val.i == 0x00000100)
									iptr[0].val.i = 8;
								else if (iptr[0].val.i == 0x00000200)
									iptr[0].val.i = 9;
								else if (iptr[0].val.i == 0x00000400)
									iptr[0].val.i = 10;
								else if (iptr[0].val.i == 0x00000800)
									iptr[0].val.i = 11;
								else if (iptr[0].val.i == 0x00001000)
									iptr[0].val.i = 12;
								else if (iptr[0].val.i == 0x00002000)
									iptr[0].val.i = 13;
								else if (iptr[0].val.i == 0x00004000)
									iptr[0].val.i = 14;
								else if (iptr[0].val.i == 0x00008000)
									iptr[0].val.i = 15;
								else if (iptr[0].val.i == 0x00010000)
									iptr[0].val.i = 16;
								else if (iptr[0].val.i == 0x00020000)
									iptr[0].val.i = 17;
								else if (iptr[0].val.i == 0x00040000)
									iptr[0].val.i = 18;
								else if (iptr[0].val.i == 0x00080000)
									iptr[0].val.i = 19;
								else if (iptr[0].val.i == 0x00100000)
									iptr[0].val.i = 20;
								else if (iptr[0].val.i == 0x00200000)
									iptr[0].val.i = 21;
								else if (iptr[0].val.i == 0x00400000)
									iptr[0].val.i = 22;
								else if (iptr[0].val.i == 0x00800000)
									iptr[0].val.i = 23;
								else if (iptr[0].val.i == 0x01000000)
									iptr[0].val.i = 24;
								else if (iptr[0].val.i == 0x02000000)
									iptr[0].val.i = 25;
								else if (iptr[0].val.i == 0x04000000)
									iptr[0].val.i = 26;
								else if (iptr[0].val.i == 0x08000000)
									iptr[0].val.i = 27;
								else if (iptr[0].val.i == 0x10000000)
									iptr[0].val.i = 28;
								else if (iptr[0].val.i == 0x20000000)
									iptr[0].val.i = 29;
								else if (iptr[0].val.i == 0x40000000)
									iptr[0].val.i = 30;
								else if (iptr[0].val.i == 0x80000000)
									iptr[0].val.i = 31;
								else {
									PUSHCONST(TYPE_INT);
									break;
								}
								iptr[0].opc = ICMD_IDIVPOW2;
								goto icmd_iconst_tail;
							case ICMD_IREM:
								/*log_text("stack.c: ICMD_ICONST/ICMD_IREM");*/
								if ((iptr[0].val.i == 0x00000002) ||
									(iptr[0].val.i == 0x00000004) ||
									(iptr[0].val.i == 0x00000008) ||
									(iptr[0].val.i == 0x00000010) ||
									(iptr[0].val.i == 0x00000020) ||
									(iptr[0].val.i == 0x00000040) ||
									(iptr[0].val.i == 0x00000080) ||
									(iptr[0].val.i == 0x00000100) ||
									(iptr[0].val.i == 0x00000200) ||
									(iptr[0].val.i == 0x00000400) ||
									(iptr[0].val.i == 0x00000800) ||
									(iptr[0].val.i == 0x00001000) ||
									(iptr[0].val.i == 0x00002000) ||
									(iptr[0].val.i == 0x00004000) ||
									(iptr[0].val.i == 0x00008000) ||
									(iptr[0].val.i == 0x00010000) ||
									(iptr[0].val.i == 0x00020000) ||
									(iptr[0].val.i == 0x00040000) ||
									(iptr[0].val.i == 0x00080000) ||
									(iptr[0].val.i == 0x00100000) ||
									(iptr[0].val.i == 0x00200000) ||
									(iptr[0].val.i == 0x00400000) ||
									(iptr[0].val.i == 0x00800000) ||
									(iptr[0].val.i == 0x01000000) ||
									(iptr[0].val.i == 0x02000000) ||
									(iptr[0].val.i == 0x04000000) ||
									(iptr[0].val.i == 0x08000000) ||
									(iptr[0].val.i == 0x10000000) ||
									(iptr[0].val.i == 0x20000000) ||
									(iptr[0].val.i == 0x40000000) ||
									(iptr[0].val.i == 0x80000000)) {
									iptr[0].opc = ICMD_IREMPOW2;
									iptr[0].val.i -= 1;
									goto icmd_iconst_tail;
								}
								PUSHCONST(TYPE_INT);
								break;
#if SUPPORT_CONST_LOGICAL
							case ICMD_IAND:
								iptr[0].opc = ICMD_IANDCONST;
								goto icmd_iconst_tail;
							case ICMD_IOR:
								iptr[0].opc = ICMD_IORCONST;
								goto icmd_iconst_tail;
							case ICMD_IXOR:
								iptr[0].opc = ICMD_IXORCONST;
								goto icmd_iconst_tail;
#endif /* SUPPORT_CONST_LOGICAL */
							case ICMD_ISHL:
								iptr[0].opc = ICMD_ISHLCONST;
								goto icmd_iconst_tail;
							case ICMD_ISHR:
								iptr[0].opc = ICMD_ISHRCONST;
								goto icmd_iconst_tail;
							case ICMD_IUSHR:
								iptr[0].opc = ICMD_IUSHRCONST;
								goto icmd_iconst_tail;
#if SUPPORT_LONG_SHIFT
							case ICMD_LSHL:
								iptr[0].opc = ICMD_LSHLCONST;
								goto icmd_lconst_tail;
							case ICMD_LSHR:
								iptr[0].opc = ICMD_LSHRCONST;
								goto icmd_lconst_tail;
							case ICMD_LUSHR:
								iptr[0].opc = ICMD_LUSHRCONST;
								goto icmd_lconst_tail;
#endif /* SUPPORT_LONG_SHIFT */
							case ICMD_IF_ICMPEQ:
								iptr[1].opc = ICMD_IFEQ;
							icmd_if_icmp_tail:
/* 								iptr[0].op1 = iptr[1].op1; */
								/* IF_ICMPxx is the last instruction in the  
								   basic block, just remove it. */
								iptr[0].opc = ICMD_NOP;
								iptr[1].val.i = iptr[0].val.i;
								SETDST;
/* 								bptr->icount--; */
/* 								len--; */
#if 0
								OP1_0(TYPE_INT);
								tbptr = m->basicblocks +
									m->basicblockindex[iptr[1].op1];

								iptr[1].target = (void *) tbptr;

								MARKREACHED(tbptr, copy);
								COUNT(count_pcmd_bra);
#endif
								break;
							case ICMD_IF_ICMPLT:
								iptr[1].opc = ICMD_IFLT;
								goto icmd_if_icmp_tail;
							case ICMD_IF_ICMPLE:
								iptr[1].opc = ICMD_IFLE;
								goto icmd_if_icmp_tail;
							case ICMD_IF_ICMPNE:
								iptr[1].opc = ICMD_IFNE;
								goto icmd_if_icmp_tail;
							case ICMD_IF_ICMPGT:
								iptr[1].opc = ICMD_IFGT;
								goto icmd_if_icmp_tail;
							case ICMD_IF_ICMPGE:
								iptr[1].opc = ICMD_IFGE;
								goto icmd_if_icmp_tail;

#if SUPPORT_CONST_STORE
							case ICMD_IASTORE:
							case ICMD_BASTORE:
							case ICMD_CASTORE:
							case ICMD_SASTORE:
# if defined(ENABLE_INTRP)
								if (!opt_intrp) {
# endif
# if SUPPORT_CONST_STORE_ZERO_ONLY
									if (iptr[0].val.i == 0) {
# endif
										switch (iptr[1].opc) {
										case ICMD_IASTORE:
											iptr[0].opc = ICMD_IASTORECONST;
											break;
										case ICMD_BASTORE:
											iptr[0].opc = ICMD_BASTORECONST;
											break;
										case ICMD_CASTORE:
											iptr[0].opc = ICMD_CASTORECONST;
											break;
										case ICMD_SASTORE:
											iptr[0].opc = ICMD_SASTORECONST;
											break;
										}

										iptr[1].opc = ICMD_NOP;
										OPTT2_0(TYPE_INT, TYPE_ADR);
										COUNT(count_pcmd_op);
# if SUPPORT_CONST_STORE_ZERO_ONLY
									} 
									else
										PUSHCONST(TYPE_INT);
# endif
# if defined(ENABLE_INTRP)
								} 
								else
									PUSHCONST(TYPE_INT);
# endif
								break;

							case ICMD_PUTSTATIC:
							case ICMD_PUTFIELD:
# if defined(ENABLE_INTRP)
								if (!opt_intrp) {
# endif
# if SUPPORT_CONST_STORE_ZERO_ONLY
									if (iptr[0].val.i == 0) {
# endif
										switch (iptr[1].opc) {
										case ICMD_PUTSTATIC:
											iptr[0].opc = ICMD_PUTSTATICCONST;
											SETDST;
											break;
										case ICMD_PUTFIELD:
											iptr[0].opc = ICMD_PUTFIELDCONST;
											OP1_0(TYPE_ADR);
											break;
										}

										iptr[1].opc = ICMD_NOP;
										iptr[0].op1 = TYPE_INT;
										COUNT(count_pcmd_op);
# if SUPPORT_CONST_STORE_ZERO_ONLY
									} 
									else
										PUSHCONST(TYPE_INT);
# endif
# if defined(ENABLE_INTRP)
								} 
								else
									PUSHCONST(TYPE_INT);
# endif
								break;
#endif /* SUPPORT_CONST_STORE */
							default:
								PUSHCONST(TYPE_INT);
							}
						}
						else
							PUSHCONST(TYPE_INT);
						break;

					case ICMD_LCONST:
						COUNT(count_pcmd_load);
						if (len > 0) {
							switch (iptr[1].opc) {
#if SUPPORT_LONG_ADD
							case ICMD_LADD:
								iptr[0].opc = ICMD_LADDCONST;
							icmd_lconst_tail:
								iptr[1].opc = ICMD_NOP;
								OP1_1(TYPE_LNG,TYPE_LNG);
								COUNT(count_pcmd_op);
								break;
							case ICMD_LSUB:
								iptr[0].opc = ICMD_LSUBCONST;
								goto icmd_lconst_tail;
#endif /* SUPPORT_LONG_ADD */
#if SUPPORT_LONG_MUL && SUPPORT_CONST_MUL
							case ICMD_LMUL:
								iptr[0].opc = ICMD_LMULCONST;
								goto icmd_lconst_tail;
#else /* SUPPORT_LONG_MUL && SUPPORT_CONST_MUL */
# if SUPPORT_LONG_SHIFT
							case ICMD_LMUL:
								if (iptr[0].val.l == 0x00000002)
									iptr[0].val.i = 1;
								else if (iptr[0].val.l == 0x00000004)
									iptr[0].val.i = 2;
								else if (iptr[0].val.l == 0x00000008)
									iptr[0].val.i = 3;
								else if (iptr[0].val.l == 0x00000010)
									iptr[0].val.i = 4;
								else if (iptr[0].val.l == 0x00000020)
									iptr[0].val.i = 5;
								else if (iptr[0].val.l == 0x00000040)
									iptr[0].val.i = 6;
								else if (iptr[0].val.l == 0x00000080)
									iptr[0].val.i = 7;
								else if (iptr[0].val.l == 0x00000100)
									iptr[0].val.i = 8;
								else if (iptr[0].val.l == 0x00000200)
									iptr[0].val.i = 9;
								else if (iptr[0].val.l == 0x00000400)
									iptr[0].val.i = 10;
								else if (iptr[0].val.l == 0x00000800)
									iptr[0].val.i = 11;
								else if (iptr[0].val.l == 0x00001000)
									iptr[0].val.i = 12;
								else if (iptr[0].val.l == 0x00002000)
									iptr[0].val.i = 13;
								else if (iptr[0].val.l == 0x00004000)
									iptr[0].val.i = 14;
								else if (iptr[0].val.l == 0x00008000)
									iptr[0].val.i = 15;
								else if (iptr[0].val.l == 0x00010000)
									iptr[0].val.i = 16;
								else if (iptr[0].val.l == 0x00020000)
									iptr[0].val.i = 17;
								else if (iptr[0].val.l == 0x00040000)
									iptr[0].val.i = 18;
								else if (iptr[0].val.l == 0x00080000)
									iptr[0].val.i = 19;
								else if (iptr[0].val.l == 0x00100000)
									iptr[0].val.i = 20;
								else if (iptr[0].val.l == 0x00200000)
									iptr[0].val.i = 21;
								else if (iptr[0].val.l == 0x00400000)
									iptr[0].val.i = 22;
								else if (iptr[0].val.l == 0x00800000)
									iptr[0].val.i = 23;
								else if (iptr[0].val.l == 0x01000000)
									iptr[0].val.i = 24;
								else if (iptr[0].val.l == 0x02000000)
									iptr[0].val.i = 25;
								else if (iptr[0].val.l == 0x04000000)
									iptr[0].val.i = 26;
								else if (iptr[0].val.l == 0x08000000)
									iptr[0].val.i = 27;
								else if (iptr[0].val.l == 0x10000000)
									iptr[0].val.i = 28;
								else if (iptr[0].val.l == 0x20000000)
									iptr[0].val.i = 29;
								else if (iptr[0].val.l == 0x40000000)
									iptr[0].val.i = 30;
								else if (iptr[0].val.l == 0x80000000)
									iptr[0].val.i = 31;
								else {
									PUSHCONST(TYPE_LNG);
									break;
								}
								iptr[0].opc = ICMD_LMULPOW2;
								goto icmd_lconst_tail;
# endif /* SUPPORT_LONG_SHIFT */
#endif /* SUPPORT_LONG_MUL && SUPPORT_CONST_MUL */

#if SUPPORT_LONG_DIV_POW2
							case ICMD_LDIV:
								if (iptr[0].val.l == 0x00000002)
									iptr[0].val.i = 1;
								else if (iptr[0].val.l == 0x00000004)
									iptr[0].val.i = 2;
								else if (iptr[0].val.l == 0x00000008)
									iptr[0].val.i = 3;
								else if (iptr[0].val.l == 0x00000010)
									iptr[0].val.i = 4;
								else if (iptr[0].val.l == 0x00000020)
									iptr[0].val.i = 5;
								else if (iptr[0].val.l == 0x00000040)
									iptr[0].val.i = 6;
								else if (iptr[0].val.l == 0x00000080)
									iptr[0].val.i = 7;
								else if (iptr[0].val.l == 0x00000100)
									iptr[0].val.i = 8;
								else if (iptr[0].val.l == 0x00000200)
									iptr[0].val.i = 9;
								else if (iptr[0].val.l == 0x00000400)
									iptr[0].val.i = 10;
								else if (iptr[0].val.l == 0x00000800)
									iptr[0].val.i = 11;
								else if (iptr[0].val.l == 0x00001000)
									iptr[0].val.i = 12;
								else if (iptr[0].val.l == 0x00002000)
									iptr[0].val.i = 13;
								else if (iptr[0].val.l == 0x00004000)
									iptr[0].val.i = 14;
								else if (iptr[0].val.l == 0x00008000)
									iptr[0].val.i = 15;
								else if (iptr[0].val.l == 0x00010000)
									iptr[0].val.i = 16;
								else if (iptr[0].val.l == 0x00020000)
									iptr[0].val.i = 17;
								else if (iptr[0].val.l == 0x00040000)
									iptr[0].val.i = 18;
								else if (iptr[0].val.l == 0x00080000)
									iptr[0].val.i = 19;
								else if (iptr[0].val.l == 0x00100000)
									iptr[0].val.i = 20;
								else if (iptr[0].val.l == 0x00200000)
									iptr[0].val.i = 21;
								else if (iptr[0].val.l == 0x00400000)
									iptr[0].val.i = 22;
								else if (iptr[0].val.l == 0x00800000)
									iptr[0].val.i = 23;
								else if (iptr[0].val.l == 0x01000000)
									iptr[0].val.i = 24;
								else if (iptr[0].val.l == 0x02000000)
									iptr[0].val.i = 25;
								else if (iptr[0].val.l == 0x04000000)
									iptr[0].val.i = 26;
								else if (iptr[0].val.l == 0x08000000)
									iptr[0].val.i = 27;
								else if (iptr[0].val.l == 0x10000000)
									iptr[0].val.i = 28;
								else if (iptr[0].val.l == 0x20000000)
									iptr[0].val.i = 29;
								else if (iptr[0].val.l == 0x40000000)
									iptr[0].val.i = 30;
								else if (iptr[0].val.l == 0x80000000)
									iptr[0].val.i = 31;
								else {
									PUSHCONST(TYPE_LNG);
									break;
								}
								iptr[0].opc = ICMD_LDIVPOW2;
								goto icmd_lconst_tail;
#endif /* SUPPORT_LONG_DIV_POW2 */

#if SUPPORT_LONG_REM_POW2
							case ICMD_LREM:
								if ((iptr[0].val.l == 0x00000002) ||
									(iptr[0].val.l == 0x00000004) ||
									(iptr[0].val.l == 0x00000008) ||
									(iptr[0].val.l == 0x00000010) ||
									(iptr[0].val.l == 0x00000020) ||
									(iptr[0].val.l == 0x00000040) ||
									(iptr[0].val.l == 0x00000080) ||
									(iptr[0].val.l == 0x00000100) ||
									(iptr[0].val.l == 0x00000200) ||
									(iptr[0].val.l == 0x00000400) ||
									(iptr[0].val.l == 0x00000800) ||
									(iptr[0].val.l == 0x00001000) ||
									(iptr[0].val.l == 0x00002000) ||
									(iptr[0].val.l == 0x00004000) ||
									(iptr[0].val.l == 0x00008000) ||
									(iptr[0].val.l == 0x00010000) ||
									(iptr[0].val.l == 0x00020000) ||
									(iptr[0].val.l == 0x00040000) ||
									(iptr[0].val.l == 0x00080000) ||
									(iptr[0].val.l == 0x00100000) ||
									(iptr[0].val.l == 0x00200000) ||
									(iptr[0].val.l == 0x00400000) ||
									(iptr[0].val.l == 0x00800000) ||
									(iptr[0].val.l == 0x01000000) ||
									(iptr[0].val.l == 0x02000000) ||
									(iptr[0].val.l == 0x04000000) ||
									(iptr[0].val.l == 0x08000000) ||
									(iptr[0].val.l == 0x10000000) ||
									(iptr[0].val.l == 0x20000000) ||
									(iptr[0].val.l == 0x40000000) ||
									(iptr[0].val.l == 0x80000000)) {
									iptr[0].opc = ICMD_LREMPOW2;
									iptr[0].val.l -= 1;
									goto icmd_lconst_tail;
								}
								PUSHCONST(TYPE_LNG);
								break;
#endif /* SUPPORT_LONG_REM_POW2 */

#if SUPPORT_LONG_LOGICAL && SUPPORT_CONST_LOGICAL

							case ICMD_LAND:
								iptr[0].opc = ICMD_LANDCONST;
								goto icmd_lconst_tail;
							case ICMD_LOR:
								iptr[0].opc = ICMD_LORCONST;
								goto icmd_lconst_tail;
							case ICMD_LXOR:
								iptr[0].opc = ICMD_LXORCONST;
								goto icmd_lconst_tail;
#endif /* SUPPORT_LONG_LOGICAL && SUPPORT_CONST_LOGICAL */

#if SUPPORT_LONG_CMP_CONST
							case ICMD_LCMP:
								if ((len > 1) && (iptr[2].val.i == 0)) {
									switch (iptr[2].opc) {
									case ICMD_IFEQ:
										iptr[0].opc = ICMD_IF_LEQ;
									icmd_lconst_lcmp_tail:
										iptr[0].op1 = iptr[2].op1;
										iptr[1].opc = ICMD_NOP;
										iptr[2].opc = ICMD_NOP;

/* 										bptr->icount -= 2; */
/* 										len -= 2; */

										OP1_0(TYPE_LNG);
										tbptr = m->basicblocks +
											m->basicblockindex[iptr[0].op1];

										iptr[0].target = (void *) tbptr;

										MARKREACHED(tbptr, copy);
										COUNT(count_pcmd_bra);
										COUNT(count_pcmd_op);
										break;
									case ICMD_IFNE:
										iptr[0].opc = ICMD_IF_LNE;
										goto icmd_lconst_lcmp_tail;
									case ICMD_IFLT:
										iptr[0].opc = ICMD_IF_LLT;
										goto icmd_lconst_lcmp_tail;
									case ICMD_IFGT:
										iptr[0].opc = ICMD_IF_LGT;
										goto icmd_lconst_lcmp_tail;
									case ICMD_IFLE:
										iptr[0].opc = ICMD_IF_LLE;
										goto icmd_lconst_lcmp_tail;
									case ICMD_IFGE:
										iptr[0].opc = ICMD_IF_LGE;
										goto icmd_lconst_lcmp_tail;
									default:
										PUSHCONST(TYPE_LNG);
									} /* switch (iptr[2].opc) */
								} /* if (iptr[2].val.i == 0) */
								else
									PUSHCONST(TYPE_LNG);
								break;
#endif /* SUPPORT_LONG_CMP_CONST */

#if SUPPORT_CONST_STORE
							case ICMD_LASTORE:
# if defined(ENABLE_INTRP)
								if (!opt_intrp) {
# endif
# if SUPPORT_CONST_STORE_ZERO_ONLY
									if (iptr[0].val.l == 0) {
# endif
										iptr[0].opc = ICMD_LASTORECONST;
										iptr[1].opc = ICMD_NOP;
										OPTT2_0(TYPE_INT, TYPE_ADR);
										COUNT(count_pcmd_op);
# if SUPPORT_CONST_STORE_ZERO_ONLY
									} 
									else
										PUSHCONST(TYPE_LNG);
# endif
# if defined(ENABLE_INTRP)
								} 
								else
									PUSHCONST(TYPE_LNG);
# endif
								break;

							case ICMD_PUTSTATIC:
							case ICMD_PUTFIELD:
# if defined(ENABLE_INTRP)
								if (!opt_intrp) {
# endif
# if SUPPORT_CONST_STORE_ZERO_ONLY
									if (iptr[0].val.l == 0) {
# endif
										switch (iptr[1].opc) {
										case ICMD_PUTSTATIC:
											iptr[0].opc = ICMD_PUTSTATICCONST;
											SETDST;
											break;
										case ICMD_PUTFIELD:
											iptr[0].opc = ICMD_PUTFIELDCONST;
											OP1_0(TYPE_ADR);
											break;
										}

										iptr[1].opc = ICMD_NOP;
										iptr[0].op1 = TYPE_LNG;
										COUNT(count_pcmd_op);
# if SUPPORT_CONST_STORE_ZERO_ONLY
									} 
									else
										PUSHCONST(TYPE_LNG);
# endif
# if defined(ENABLE_INTRP)
								} 
								else
									PUSHCONST(TYPE_LNG);
# endif
								break;
#endif /* SUPPORT_CONST_STORE */
							default:
								PUSHCONST(TYPE_LNG);
							}
						}
						else
							PUSHCONST(TYPE_LNG);
						break;

					case ICMD_FCONST:
						COUNT(count_pcmd_load);
						PUSHCONST(TYPE_FLT);
						break;

					case ICMD_DCONST:
						COUNT(count_pcmd_load);
						PUSHCONST(TYPE_DBL);
						break;

					case ICMD_ACONST:
						COUNT(count_pcmd_load);
#if SUPPORT_CONST_STORE
# if defined(ENABLE_INTRP)
						if (!opt_intrp) {
# endif
							/* We can only optimize if the ACONST is resolved
							 * and there is an instruction after it. */

							if ((len > 0) && INSTRUCTION_IS_RESOLVED(iptr))
							{
								switch (iptr[1].opc) {
								case ICMD_AASTORE:
									/* We can only optimize for NULL values
									 * here because otherwise a checkcast is
									 * required. */
									if (iptr->val.a != NULL)
										goto aconst_no_transform;

									iptr[0].opc = ICMD_AASTORECONST;
									OPTT2_0(TYPE_INT, TYPE_ADR);

									iptr[1].opc = ICMD_NOP;
									COUNT(count_pcmd_op);
									break;

								case ICMD_PUTSTATIC:
								case ICMD_PUTFIELD:
# if SUPPORT_CONST_STORE_ZERO_ONLY
									if (iptr->val.a == 0) {
# endif

										switch (iptr[1].opc) {
										case ICMD_PUTSTATIC:
											iptr[0].opc = ICMD_PUTSTATICCONST;
											iptr[0].op1 = TYPE_ADR;
											SETDST;
											break;
										case ICMD_PUTFIELD:
											iptr[0].opc = ICMD_PUTFIELDCONST;
											iptr[0].op1 = TYPE_ADR;
											OP1_0(TYPE_ADR);
											break;
										}

										iptr[1].opc = ICMD_NOP;
										COUNT(count_pcmd_op);

# if SUPPORT_CONST_STORE_ZERO_ONLY
									}
									else
										/* no transformation */
										PUSHCONST(TYPE_ADR);
# endif
									break;

								default:
								aconst_no_transform:
									/* no transformation */
									PUSHCONST(TYPE_ADR);
								}
							}
							else {
								/* no transformation */
								PUSHCONST(TYPE_ADR);
							}
# if defined(ENABLE_INTRP)
						}
						else
							PUSHCONST(TYPE_ADR);
# endif
#else /* SUPPORT_CONST_STORE */
						PUSHCONST(TYPE_ADR);
#endif /* SUPPORT_CONST_STORE */
						break;

						/* pop 0 push 1 load */
						
					case ICMD_ILOAD:
					case ICMD_LLOAD:
					case ICMD_FLOAD:
					case ICMD_DLOAD:
					case ICMD_ALOAD:
						COUNT(count_load_instruction);
						i = opcode - ICMD_ILOAD;
#if defined(ENABLE_INTRP)
						if (!opt_intrp)
#endif
							rd->locals[iptr->op1][i].type = i;
						LOAD(i, LOCALVAR, iptr->op1);
						break;

						/* pop 2 push 1 */

					case ICMD_LALOAD:
					case ICMD_IALOAD:
					case ICMD_FALOAD:
					case ICMD_DALOAD:
					case ICMD_AALOAD:
						COUNT(count_check_null);
						COUNT(count_check_bound);
						COUNT(count_pcmd_mem);
						OP2IAT_1(opcode - ICMD_IALOAD);
						break;

					case ICMD_BALOAD:
					case ICMD_CALOAD:
					case ICMD_SALOAD:
						COUNT(count_check_null);
						COUNT(count_check_bound);
						COUNT(count_pcmd_mem);
						OP2IAT_1(TYPE_INT);
						break;

						/* pop 0 push 0 iinc */

					case ICMD_IINC:
#if defined(ENABLE_STATISTICS)
						if (opt_stat) {
							i = stackdepth;
							if (i >= 10)
								count_store_depth[10]++;
							else
								count_store_depth[i]++;
						}
#endif
						last_store[5 * iptr->op1 + TYPE_INT] = bptr->icount - len - 1;

						copy = curstack;
						i = stackdepth - 1;
						while (copy) {
							if ((copy->varkind == LOCALVAR) &&
								(copy->varnum == iptr->op1)) {
								copy->varkind = TEMPVAR;
								copy->varnum = i;
							}
							i--;
							copy = copy->prev;
						}
						
						SETDST;
						break;

						/* pop 1 push 0 store */

					case ICMD_ISTORE:
					case ICMD_LSTORE:
					case ICMD_FSTORE:
					case ICMD_DSTORE:
					case ICMD_ASTORE:
						REQUIRE_1;

					i = opcode - ICMD_ISTORE;
#if defined(ENABLE_INTRP)
						if (!opt_intrp)
#endif
							rd->locals[iptr->op1][i].type = i;
#if defined(ENABLE_STATISTICS)
					if (opt_stat) {
						count_pcmd_store++;
						i = new - curstack;
						if (i >= 20)
							count_store_length[20]++;
						else
							count_store_length[i]++;
						i = stackdepth - 1;
						if (i >= 10)
							count_store_depth[10]++;
						else
							count_store_depth[i]++;
					}
#endif
					/* check for conflicts as described in Figure 5.2 */
					copy = curstack->prev;
					i = stackdepth - 2;
					while (copy) {
						if ((copy->varkind == LOCALVAR) &&
							(copy->varnum == iptr->op1)) {
							copy->varkind = TEMPVAR;
							copy->varnum = i;
						}
						i--;
						copy = copy->prev;
					}

					/* do not change instack Stackslots */
					/* it won't improve performance if we copy the interface */
					/* at the BB begin or here, and lsra relies that no      */
					/* instack stackslot is marked LOCALVAR */
					if (curstack->varkind == STACKVAR)
						goto _possible_conflict;

					/* check for a DUPX,SWAP while the lifetime of curstack */
					/* and as creator curstack */
					if (last_dupx != -1) { 
						/* we have to look at the dst stack of DUPX */
						/* == src Stack of PEI */
						copy = bptr->iinstr[last_dupx].dst;
						/*
						if (last_pei == 0)
							copy = bptr->instack;
						else
							copy = bptr->iinstr[last_pei-1].dst;
						*/
						if ((copy != NULL) && (curstack <= copy)) {
							/* curstack alive at or created by DUPX */

							/* TODO:.... */
							/* now look, if there is a LOCALVAR at anyone of */
							/* the src stacklots used by DUPX */

							goto _possible_conflict;
						}
					}

					/* check for a PEI while the lifetime of curstack */
					if (last_pei != -1) { 
						/* && there are exception handler in this method */
						/* when this is checked prevent ARGVAR from      */
						/* overwriting LOCALVAR!!! */

						/* we have to look at the stack _before_ the PEI! */
						/* == src Stack of PEI */
						if (last_pei == 0)
							copy = bptr->instack;
						else
							copy = bptr->iinstr[last_pei-1].dst;
						if ((copy != NULL) && (curstack <= copy)) {
							/* curstack alive at PEI */
							goto _possible_conflict;
						}
					}
					
					/* check if there is a possible conflicting XSTORE */
					if (last_store[5 * iptr->op1 + opcode - ICMD_ISTORE] != -1) {
						/* we have to look at the stack _before_ the XSTORE! */
						/* == src Stack of XSTORE */
						if (last_store[5 * iptr->op1 + opcode - ICMD_ISTORE] == 0)
							copy = bptr->instack;
						else
							copy = bptr->iinstr[last_store[5 * iptr->op1 + opcode - ICMD_ISTORE] - 1].dst;
						if ((copy != NULL) && (curstack <= copy)) {
							/* curstack alive at Last Store */
							goto _possible_conflict;
						}
					}

					/* check if there is a conflict with a XLOAD */
					/* this is done indirectly by looking if a Stackslot is */
					/* marked LOCALVAR and is live while curstack is live   */
					/* see figure 5.3 */

					/* First check "above" stackslots of the instack */
					copy = curstack + 1;
					for(;(copy <= bptr->instack); copy++)
						if ((copy->varkind == LOCALVAR) && (copy->varnum == iptr->op1)) {
							goto _possible_conflict;
						}
					
					/* "intra" Basic Block Stackslots are allocated above    */
					/* bptr->stack (see doc/stack.txt), so if curstack + 1   */
					/* is an instack, copy could point now to the stackslots */
					/* of an inbetween analysed Basic Block */
					if (copy < bptr->stack)
						copy = bptr->stack;
					while (copy < new) {
						if ((copy->varkind == LOCALVAR) && (copy->varnum == iptr->op1)) {
							goto _possible_conflict;
						}
						copy++;
					}
					/* If Stackslot is already marked as LOCALVAR, do not    */
					/* change it! Conflict resolution works only, if xLOAD   */
					/* has priority! */
					if (curstack->varkind == LOCALVAR)
						goto _possible_conflict;
					/* no conflict - mark the Stackslot as LOCALVAR */
					curstack->varkind = LOCALVAR;
					curstack->varnum = iptr->op1;
					
					goto _local_join;
				_possible_conflict:
					if ((curstack->varkind == LOCALVAR) 
						&& (curstack->varnum == iptr->op1)) {
						curstack->varkind = TEMPVAR;
						curstack->varnum = stackdepth-1;
					}
				_local_join:
					last_store[5 * iptr->op1 + opcode - ICMD_ISTORE] = bptr->icount - len - 1;

					STORE(opcode - ICMD_ISTORE);
					break;

					/* pop 3 push 0 */

					case ICMD_AASTORE:
						COUNT(count_check_null);
						COUNT(count_check_bound);
						COUNT(count_pcmd_mem);

						bte = builtintable_get_internal(BUILTIN_canstore);
						md = bte->md;

						if (md->memuse > rd->memuse)
							rd->memuse = md->memuse;
						if (md->argintreguse > rd->argintreguse)
							rd->argintreguse = md->argintreguse;

						/* make all stack variables saved */

						copy = curstack;
						while (copy) {
							copy->flags |= SAVEDVAR;
							copy = copy->prev;
						}

						OP3TIA_0(TYPE_ADR);
						break;

					case ICMD_IASTORE:
					case ICMD_LASTORE:
					case ICMD_FASTORE:
					case ICMD_DASTORE:
						COUNT(count_check_null);
						COUNT(count_check_bound);
						COUNT(count_pcmd_mem);
						OP3TIA_0(opcode - ICMD_IASTORE);
						break;

					case ICMD_BASTORE:
					case ICMD_CASTORE:
					case ICMD_SASTORE:
						COUNT(count_check_null);
						COUNT(count_check_bound);
						COUNT(count_pcmd_mem);
						OP3TIA_0(TYPE_INT);
						break;

						/* pop 1 push 0 */

					case ICMD_POP:
#ifdef ENABLE_VERIFIER
						if (opt_verify) {
							REQUIRE_1;
							if (IS_2_WORD_TYPE(curstack->type))
								goto throw_stack_category_error;
						}
#endif
						OP1_0ANY;
						break;

					case ICMD_IRETURN:
					case ICMD_LRETURN:
					case ICMD_FRETURN:
					case ICMD_DRETURN:
					case ICMD_ARETURN:
#if defined(ENABLE_JIT)
# if defined(ENABLE_INTRP)
						if (!opt_intrp)
# endif
							md_return_alloc(jd, curstack);
#endif
						COUNT(count_pcmd_return);
						OP1_0(opcode - ICMD_IRETURN);
						superblockend = true;
						break;

					case ICMD_ATHROW:
						COUNT(count_check_null);
						OP1_0(TYPE_ADR);
						STACKRESET;
						SETDST;
						superblockend = true;
						break;

					case ICMD_PUTSTATIC:
						COUNT(count_pcmd_mem);
						OP1_0(iptr->op1);
						break;

						/* pop 1 push 0 branch */

					case ICMD_IFNULL:
					case ICMD_IFNONNULL:
						COUNT(count_pcmd_bra);
						OP1_0(TYPE_ADR);
						tbptr = m->basicblocks + m->basicblockindex[iptr->op1];

						iptr[0].target = (void *) tbptr;

						MARKREACHED(tbptr, copy);
						break;

					case ICMD_IFEQ:
					case ICMD_IFNE:
					case ICMD_IFLT:
					case ICMD_IFGE:
					case ICMD_IFGT:
					case ICMD_IFLE:
						COUNT(count_pcmd_bra);
#if CONDITIONAL_LOADCONST && 0
# if defined(ENABLE_INTRP)
						if (!opt_intrp) {
# endif
							tbptr = m->basicblocks + b_index;

							if ((b_count >= 3) &&
								((b_index + 2) == m->basicblockindex[iptr[0].op1]) &&
								(tbptr[1].pre_count == 1) &&
								(tbptr[1].iinstr[0].opc == ICMD_ICONST) &&
								(tbptr[1].iinstr[1].opc == ICMD_GOTO)   &&
								((b_index + 3) == m->basicblockindex[tbptr[1].iinstr[1].op1]) &&
								(tbptr[2].pre_count == 1) &&
								(tbptr[2].iinstr[0].opc == ICMD_ICONST)  &&
								(tbptr[2].icount==1)) {
								/*printf("tbptr[2].icount=%d\n",tbptr[2].icount);*/
								OP1_1(TYPE_INT, TYPE_INT);
								switch (iptr[0].opc) {
								case ICMD_IFEQ:
									iptr[0].opc = ICMD_IFNE_ICONST;
									break;
								case ICMD_IFNE:
									iptr[0].opc = ICMD_IFEQ_ICONST;
									break;
								case ICMD_IFLT:
									iptr[0].opc = ICMD_IFGE_ICONST;
									break;
								case ICMD_IFGE:
									iptr[0].opc = ICMD_IFLT_ICONST;
									break;
								case ICMD_IFGT:
									iptr[0].opc = ICMD_IFLE_ICONST;
									break;
								case ICMD_IFLE:
									iptr[0].opc = ICMD_IFGT_ICONST;
									break;
								}
#if 1
								iptr[0].val.i = iptr[1].val.i;
								iptr[1].opc = ICMD_ELSE_ICONST;
								iptr[1].val.i = iptr[3].val.i;
								iptr[2].opc = ICMD_NOP;
								iptr[3].opc = ICMD_NOP;
#else
								/* HACK: save compare value in iptr[1].op1 */ 	 
								iptr[1].op1 = iptr[0].val.i; 	 
								iptr[0].val.i = tbptr[1].iinstr[0].val.i; 	 
								iptr[1].opc = ICMD_ELSE_ICONST; 	 
								iptr[1].val.i = tbptr[2].iinstr[0].val.i; 	 
								tbptr[1].iinstr[0].opc = ICMD_NOP; 	 
								tbptr[1].iinstr[1].opc = ICMD_NOP; 	 
								tbptr[2].iinstr[0].opc = ICMD_NOP; 	 
#endif
								tbptr[1].flags = BBDELETED;
								tbptr[2].flags = BBDELETED;
								tbptr[1].icount = 0;
								tbptr[2].icount = 0;
								if (tbptr[3].pre_count == 2) {
									len += tbptr[3].icount + 3;
									bptr->icount += tbptr[3].icount + 3;
									tbptr[3].flags = BBDELETED;
									tbptr[3].icount = 0;
									b_index++;
								}
								else {
									bptr->icount++;
									len ++;
								}
								b_index += 2;
								break;
							}
# if defined(ENABLE_INTRP)
						}
# endif

#endif /* CONDITIONAL_LOADCONST */

						/* iptr->val.i is set implicitly in parse by
						   clearing the memory or from IF_ICMPxx
						   optimization. */

						OP1_0(TYPE_INT);
/* 						iptr->val.i = 0; */
						tbptr = m->basicblocks + m->basicblockindex[iptr->op1];

						iptr[0].target = (void *) tbptr;

						MARKREACHED(tbptr, copy);
						break;

						/* pop 0 push 0 branch */

					case ICMD_GOTO:
						COUNT(count_pcmd_bra);
						tbptr = m->basicblocks + m->basicblockindex[iptr->op1];

						iptr[0].target = (void *) tbptr;

						MARKREACHED(tbptr, copy);
						SETDST;
						superblockend = true;
						break;

						/* pop 1 push 0 table branch */

					case ICMD_TABLESWITCH:
						COUNT(count_pcmd_table);
						OP1_0(TYPE_INT);
						s4ptr = iptr->val.a;
						tbptr = m->basicblocks + m->basicblockindex[*s4ptr++];
						MARKREACHED(tbptr, copy);
						i = *s4ptr++;                          /* low     */
						i = *s4ptr++ - i + 1;                  /* high    */

						tptr = DMNEW(void*, i+1);
						iptr->target = (void *) tptr;

						tptr[0] = (void *) tbptr;
						tptr++;

						while (--i >= 0) {
							tbptr = m->basicblocks + m->basicblockindex[*s4ptr++];

							tptr[0] = (void *) tbptr;
							tptr++;

							MARKREACHED(tbptr, copy);
						}
						SETDST;
						superblockend = true;
						break;
							
						/* pop 1 push 0 table branch */

					case ICMD_LOOKUPSWITCH:
						COUNT(count_pcmd_table);
						OP1_0(TYPE_INT);
						s4ptr = iptr->val.a;
						tbptr = m->basicblocks + m->basicblockindex[*s4ptr++];
						MARKREACHED(tbptr, copy);
						i = *s4ptr++;                          /* count   */

						tptr = DMNEW(void*, i+1);
						iptr->target = (void *) tptr;

						tptr[0] = (void *) tbptr;
						tptr++;

						while (--i >= 0) {
							tbptr = m->basicblocks + m->basicblockindex[s4ptr[1]];

							tptr[0] = (void *) tbptr;
							tptr++;
								
							MARKREACHED(tbptr, copy);
							s4ptr += 2;
						}
						SETDST;
						superblockend = true;
						break;

					case ICMD_MONITORENTER:
						COUNT(count_check_null);
					case ICMD_MONITOREXIT:
						OP1_0(TYPE_ADR);
						break;

						/* pop 2 push 0 branch */

					case ICMD_IF_ICMPEQ:
					case ICMD_IF_ICMPNE:
					case ICMD_IF_ICMPLT:
					case ICMD_IF_ICMPGE:
					case ICMD_IF_ICMPGT:
					case ICMD_IF_ICMPLE:
						COUNT(count_pcmd_bra);
						OP2_0(TYPE_INT);
						tbptr = m->basicblocks + m->basicblockindex[iptr->op1];
							
						iptr[0].target = (void *) tbptr;

						MARKREACHED(tbptr, copy);
						break;

					case ICMD_IF_ACMPEQ:
					case ICMD_IF_ACMPNE:
						COUNT(count_pcmd_bra);
						OP2_0(TYPE_ADR);
						tbptr = m->basicblocks + m->basicblockindex[iptr->op1];

						iptr[0].target = (void *) tbptr;

						MARKREACHED(tbptr, copy);
						break;

						/* pop 2 push 0 */

					case ICMD_PUTFIELD:
						COUNT(count_check_null);
						COUNT(count_pcmd_mem);
						OPTT2_0(iptr->op1,TYPE_ADR);
						break;

					case ICMD_POP2:
						REQUIRE_1;
						if (!IS_2_WORD_TYPE(curstack->type)) {
							/* ..., cat1 */
#ifdef ENABLE_VERIFIER
							if (opt_verify) {
								REQUIRE_2;
								if (IS_2_WORD_TYPE(curstack->prev->type))
									goto throw_stack_category_error;
							}
#endif
							OP1_0ANY;                /* second pop */
						}
						else
							iptr->opc = ICMD_POP;
						OP1_0ANY;
						break;

						/* pop 0 push 1 dup */
						
					case ICMD_DUP:
#ifdef ENABLE_VERIFIER
						if (opt_verify) {
							REQUIRE_1;
							if (IS_2_WORD_TYPE(curstack->type))
						   		goto throw_stack_category_error;
						}
#endif
						last_dupx = bptr->icount - len - 1;
						COUNT(count_dup_instruction);
						DUP;
						break;

					case ICMD_DUP2:
						last_dupx = bptr->icount - len - 1;
						REQUIRE_1;
						if (IS_2_WORD_TYPE(curstack->type)) {
							/* ..., cat2 */
							iptr->opc = ICMD_DUP;
							DUP;
						}
						else {
							REQUIRE_2;
							/* ..., ????, cat1 */
#ifdef ENABLE_VERIFIER
							if (opt_verify) {
								if (IS_2_WORD_TYPE(curstack->prev->type))
							   		goto throw_stack_category_error;
							}
#endif
							copy = curstack;
							NEWSTACK(copy->prev->type, copy->prev->varkind,
									 copy->prev->varnum);
							NEWSTACK(copy->type, copy->varkind,
									 copy->varnum);
							SETDST;
							stackdepth += 2;
						}
						break;

						/* pop 2 push 3 dup */
						
					case ICMD_DUP_X1:
#ifdef ENABLE_VERIFIER
						if (opt_verify) {
							REQUIRE_2;
							if (IS_2_WORD_TYPE(curstack->type) ||
								IS_2_WORD_TYPE(curstack->prev->type))
						   			goto throw_stack_category_error;
						}
#endif
						last_dupx = bptr->icount - len - 1;
						DUP_X1;
						break;

					case ICMD_DUP2_X1:
						last_dupx = bptr->icount - len - 1;
						REQUIRE_2;
						if (IS_2_WORD_TYPE(curstack->type)) {
							/* ..., ????, cat2 */
#ifdef ENABLE_VERIFIER
							if (opt_verify) {
								if (IS_2_WORD_TYPE(curstack->prev->type))
							   		goto throw_stack_category_error;
							}
#endif
							iptr->opc = ICMD_DUP_X1;
							DUP_X1;
						}
						else {
							/* ..., ????, cat1 */
#ifdef ENABLE_VERIFIER
							if (opt_verify) {
								REQUIRE_3;
								if (IS_2_WORD_TYPE(curstack->prev->type)
									|| IS_2_WORD_TYPE(curstack->prev->prev->type))
							   			goto throw_stack_category_error;
							}
#endif
							DUP2_X1;
						}
						break;

						/* pop 3 push 4 dup */
						
					case ICMD_DUP_X2:
						last_dupx = bptr->icount - len - 1;
						REQUIRE_2;
						if (IS_2_WORD_TYPE(curstack->prev->type)) {
							/* ..., cat2, ???? */
#ifdef ENABLE_VERIFIER
							if (opt_verify) {
								if (IS_2_WORD_TYPE(curstack->type))
							   		goto throw_stack_category_error;
							}
#endif
							iptr->opc = ICMD_DUP_X1;
							DUP_X1;
						}
						else {
							/* ..., cat1, ???? */
#ifdef ENABLE_VERIFIER
							if (opt_verify) {
								REQUIRE_3;
								if (IS_2_WORD_TYPE(curstack->type)
									|| IS_2_WORD_TYPE(curstack->prev->prev->type))
							   				goto throw_stack_category_error;
							}
#endif
							DUP_X2;
						}
						break;

					case ICMD_DUP2_X2:
						last_dupx = bptr->icount - len - 1;
						REQUIRE_2;
						if (IS_2_WORD_TYPE(curstack->type)) {
							/* ..., ????, cat2 */
							if (IS_2_WORD_TYPE(curstack->prev->type)) {
								/* ..., cat2, cat2 */
								iptr->opc = ICMD_DUP_X1;
								DUP_X1;
							}
							else {
								/* ..., cat1, cat2 */
#ifdef ENABLE_VERIFIER
								if (opt_verify) {
									REQUIRE_3;
									if (IS_2_WORD_TYPE(curstack->prev->prev->type))
								   			goto throw_stack_category_error;
								}
#endif
								iptr->opc = ICMD_DUP_X2;
								DUP_X2;
							}
						}
						else {
							REQUIRE_3;
							/* ..., ????, ????, cat1 */
							if (IS_2_WORD_TYPE(curstack->prev->prev->type)) {
								/* ..., cat2, ????, cat1 */
#ifdef ENABLE_VERIFIER
								if (opt_verify) {
									if (IS_2_WORD_TYPE(curstack->prev->type))
								   		goto throw_stack_category_error;
								}
#endif
								iptr->opc = ICMD_DUP2_X1;
								DUP2_X1;
							}
							else {
								/* ..., cat1, ????, cat1 */
#ifdef ENABLE_VERIFIER
								if (opt_verify) {
									REQUIRE_4;
									if (IS_2_WORD_TYPE(curstack->prev->type)
										|| IS_2_WORD_TYPE(curstack->prev->prev->prev->type))
										goto throw_stack_category_error;
								}
#endif
								DUP2_X2;
							}
						}
						break;

						/* pop 2 push 2 swap */
						
					case ICMD_SWAP:
						last_dupx = bptr->icount - len - 1;
#ifdef ENABLE_VERIFIER
						if (opt_verify) {
							REQUIRE_2;
							if (IS_2_WORD_TYPE(curstack->type)
								|| IS_2_WORD_TYPE(curstack->prev->type))
								goto throw_stack_category_error;
						}
#endif
						SWAP;
						break;

						/* pop 2 push 1 */

					case ICMD_IDIV:
					case ICMD_IREM:
#if !SUPPORT_DIVISION
						bte = (builtintable_entry *) iptr->val.a;
						md = bte->md;
						i = iptr->op1;

						if (md->memuse > rd->memuse)
							rd->memuse = md->memuse;
						if (md->argintreguse > rd->argintreguse)
							rd->argintreguse = md->argintreguse;

						/* make all stack variables saved */

						copy = curstack;
						while (copy) {
							copy->flags |= SAVEDVAR;
							copy = copy->prev;
						}

						/* fall through */
#endif /* !SUPPORT_DIVISION */

					case ICMD_ISHL:
					case ICMD_ISHR:
					case ICMD_IUSHR:
					case ICMD_IADD:
					case ICMD_ISUB:
					case ICMD_IMUL:
					case ICMD_IAND:
					case ICMD_IOR:
					case ICMD_IXOR:
						COUNT(count_pcmd_op);
						OP2_1(TYPE_INT);
						break;

					case ICMD_LDIV:
					case ICMD_LREM:
#if !(SUPPORT_DIVISION && SUPPORT_LONG && SUPPORT_LONG_DIV)
						bte = (builtintable_entry *) iptr->val.a;
						md = bte->md;
						i = iptr->op1;

						if (md->memuse > rd->memuse)
							rd->memuse = md->memuse;
						if (md->argintreguse > rd->argintreguse)
							rd->argintreguse = md->argintreguse;

						/* make all stack variables saved */

						copy = curstack;
						while (copy) {
							copy->flags |= SAVEDVAR;
							copy = copy->prev;
						}

						/* fall through */
#endif /* !(SUPPORT_DIVISION && SUPPORT_LONG && SUPPORT_LONG_DIV) */

					case ICMD_LMUL:
					case ICMD_LADD:
					case ICMD_LSUB:
#if SUPPORT_LONG_LOGICAL
					case ICMD_LAND:
					case ICMD_LOR:
					case ICMD_LXOR:
#endif /* SUPPORT_LONG_LOGICAL */
						COUNT(count_pcmd_op);
						OP2_1(TYPE_LNG);
						break;

					case ICMD_LSHL:
					case ICMD_LSHR:
					case ICMD_LUSHR:
						COUNT(count_pcmd_op);
						OP2IT_1(TYPE_LNG);
						break;

					case ICMD_FADD:
					case ICMD_FSUB:
					case ICMD_FMUL:
					case ICMD_FDIV:
					case ICMD_FREM:
						COUNT(count_pcmd_op);
						OP2_1(TYPE_FLT);
						break;

					case ICMD_DADD:
					case ICMD_DSUB:
					case ICMD_DMUL:
					case ICMD_DDIV:
					case ICMD_DREM:
						COUNT(count_pcmd_op);
						OP2_1(TYPE_DBL);
						break;

					case ICMD_LCMP:
						COUNT(count_pcmd_op);
#if SUPPORT_LONG_CMP_CONST
						if ((len > 0) && (iptr[1].val.i == 0)) {
							switch (iptr[1].opc) {
							case ICMD_IFEQ:
								iptr[0].opc = ICMD_IF_LCMPEQ;
							icmd_lcmp_if_tail:
								iptr[0].op1 = iptr[1].op1;
								iptr[1].opc = ICMD_NOP;
/* 								len--; */
/* 								bptr->icount--; */

								OP2_0(TYPE_LNG);
								tbptr = m->basicblocks +
									m->basicblockindex[iptr[0].op1];
			
								iptr[0].target = (void *) tbptr;

								MARKREACHED(tbptr, copy);
								COUNT(count_pcmd_bra);
								break;
							case ICMD_IFNE:
								iptr[0].opc = ICMD_IF_LCMPNE;
								goto icmd_lcmp_if_tail;
							case ICMD_IFLT:
								iptr[0].opc = ICMD_IF_LCMPLT;
								goto icmd_lcmp_if_tail;
							case ICMD_IFGT:
								iptr[0].opc = ICMD_IF_LCMPGT;
								goto icmd_lcmp_if_tail;
							case ICMD_IFLE:
								iptr[0].opc = ICMD_IF_LCMPLE;
								goto icmd_lcmp_if_tail;
							case ICMD_IFGE:
								iptr[0].opc = ICMD_IF_LCMPGE;
								goto icmd_lcmp_if_tail;
							default:
								OPTT2_1(TYPE_LNG, TYPE_INT);
							}
						}
						else
#endif /* SUPPORT_LONG_CMP_CONST */
							OPTT2_1(TYPE_LNG, TYPE_INT);
						break;

#if 0
					case ICMD_FCMPL:
						COUNT(count_pcmd_op);
						if ((len > 0) && (iptr[1].val.i == 0)) {
							switch (iptr[1].opc) {
							case ICMD_IFEQ:
								iptr[0].opc = ICMD_IF_FCMPEQ;
							icmd_if_fcmpl_tail:
								iptr[0].op1 = iptr[1].op1;
								iptr[1].opc = ICMD_NOP;

								OP2_0(TYPE_FLT);
								tbptr = m->basicblocks +
									m->basicblockindex[iptr[0].op1];
			
								iptr[0].target = (void *) tbptr;

								MARKREACHED(tbptr, copy);
								COUNT(count_pcmd_bra);
								break;
							case ICMD_IFNE:
								iptr[0].opc = ICMD_IF_FCMPNE;
								goto icmd_if_fcmpl_tail;
							case ICMD_IFLT:
								iptr[0].opc = ICMD_IF_FCMPL_LT;
								goto icmd_if_fcmpl_tail;
							case ICMD_IFGT:
								iptr[0].opc = ICMD_IF_FCMPL_GT;
								goto icmd_if_fcmpl_tail;
							case ICMD_IFLE:
								iptr[0].opc = ICMD_IF_FCMPL_LE;
								goto icmd_if_fcmpl_tail;
							case ICMD_IFGE:
								iptr[0].opc = ICMD_IF_FCMPL_GE;
								goto icmd_if_fcmpl_tail;
							default:
								OPTT2_1(TYPE_FLT, TYPE_INT);
							}
						}
						else
							OPTT2_1(TYPE_FLT, TYPE_INT);
						break;

					case ICMD_FCMPG:
						COUNT(count_pcmd_op);
						if ((len > 0) && (iptr[1].val.i == 0)) {
							switch (iptr[1].opc) {
							case ICMD_IFEQ:
								iptr[0].opc = ICMD_IF_FCMPEQ;
							icmd_if_fcmpg_tail:
								iptr[0].op1 = iptr[1].op1;
								iptr[1].opc = ICMD_NOP;

								OP2_0(TYPE_FLT);
								tbptr = m->basicblocks +
									m->basicblockindex[iptr[0].op1];
			
								iptr[0].target = (void *) tbptr;

								MARKREACHED(tbptr, copy);
								COUNT(count_pcmd_bra);
								break;
							case ICMD_IFNE:
								iptr[0].opc = ICMD_IF_FCMPNE;
								goto icmd_if_fcmpg_tail;
							case ICMD_IFLT:
								iptr[0].opc = ICMD_IF_FCMPG_LT;
								goto icmd_if_fcmpg_tail;
							case ICMD_IFGT:
								iptr[0].opc = ICMD_IF_FCMPG_GT;
								goto icmd_if_fcmpg_tail;
							case ICMD_IFLE:
								iptr[0].opc = ICMD_IF_FCMPG_LE;
								goto icmd_if_fcmpg_tail;
							case ICMD_IFGE:
								iptr[0].opc = ICMD_IF_FCMPG_GE;
								goto icmd_if_fcmpg_tail;
							default:
								OPTT2_1(TYPE_FLT, TYPE_INT);
							}
						}
						else
							OPTT2_1(TYPE_FLT, TYPE_INT);
						break;

					case ICMD_DCMPL:
						COUNT(count_pcmd_op);
						if ((len > 0) && (iptr[1].val.i == 0)) {
							switch (iptr[1].opc) {
							case ICMD_IFEQ:
								iptr[0].opc = ICMD_IF_DCMPEQ;
							icmd_if_dcmpl_tail:
								iptr[0].op1 = iptr[1].op1;
								iptr[1].opc = ICMD_NOP;

								OP2_0(TYPE_DBL);
								tbptr = m->basicblocks +
									m->basicblockindex[iptr[0].op1];
			
								iptr[0].target = (void *) tbptr;

								MARKREACHED(tbptr, copy);
								COUNT(count_pcmd_bra);
								break;
							case ICMD_IFNE:
								iptr[0].opc = ICMD_IF_DCMPNE;
								goto icmd_if_dcmpl_tail;
							case ICMD_IFLT:
								iptr[0].opc = ICMD_IF_DCMPL_LT;
								goto icmd_if_dcmpl_tail;
							case ICMD_IFGT:
								iptr[0].opc = ICMD_IF_DCMPL_GT;
								goto icmd_if_dcmpl_tail;
							case ICMD_IFLE:
								iptr[0].opc = ICMD_IF_DCMPL_LE;
								goto icmd_if_dcmpl_tail;
							case ICMD_IFGE:
								iptr[0].opc = ICMD_IF_DCMPL_GE;
								goto icmd_if_dcmpl_tail;
							default:
								OPTT2_1(TYPE_DBL, TYPE_INT);
							}
						}
						else
							OPTT2_1(TYPE_DBL, TYPE_INT);
						break;

					case ICMD_DCMPG:
						COUNT(count_pcmd_op);
						if ((len > 0) && (iptr[1].val.i == 0)) {
							switch (iptr[1].opc) {
							case ICMD_IFEQ:
								iptr[0].opc = ICMD_IF_DCMPEQ;
							icmd_if_dcmpg_tail:
								iptr[0].op1 = iptr[1].op1;
								iptr[1].opc = ICMD_NOP;

								OP2_0(TYPE_DBL);
								tbptr = m->basicblocks +
									m->basicblockindex[iptr[0].op1];
			
								iptr[0].target = (void *) tbptr;

								MARKREACHED(tbptr, copy);
								COUNT(count_pcmd_bra);
								break;
							case ICMD_IFNE:
								iptr[0].opc = ICMD_IF_DCMPNE;
								goto icmd_if_dcmpg_tail;
							case ICMD_IFLT:
								iptr[0].opc = ICMD_IF_DCMPG_LT;
								goto icmd_if_dcmpg_tail;
							case ICMD_IFGT:
								iptr[0].opc = ICMD_IF_DCMPG_GT;
								goto icmd_if_dcmpg_tail;
							case ICMD_IFLE:
								iptr[0].opc = ICMD_IF_DCMPG_LE;
								goto icmd_if_dcmpg_tail;
							case ICMD_IFGE:
								iptr[0].opc = ICMD_IF_DCMPG_GE;
								goto icmd_if_dcmpg_tail;
							default:
								OPTT2_1(TYPE_DBL, TYPE_INT);
							}
						}
						else
							OPTT2_1(TYPE_DBL, TYPE_INT);
						break;
#else
					case ICMD_FCMPL:
					case ICMD_FCMPG:
						COUNT(count_pcmd_op);
						OPTT2_1(TYPE_FLT, TYPE_INT);
						break;

					case ICMD_DCMPL:
					case ICMD_DCMPG:
						COUNT(count_pcmd_op);
						OPTT2_1(TYPE_DBL, TYPE_INT);
						break;
#endif

						/* pop 1 push 1 */
						
					case ICMD_INEG:
					case ICMD_INT2BYTE:
					case ICMD_INT2CHAR:
					case ICMD_INT2SHORT:
						COUNT(count_pcmd_op);
						OP1_1(TYPE_INT, TYPE_INT);
						break;
					case ICMD_LNEG:
						COUNT(count_pcmd_op);
						OP1_1(TYPE_LNG, TYPE_LNG);
						break;
					case ICMD_FNEG:
						COUNT(count_pcmd_op);
						OP1_1(TYPE_FLT, TYPE_FLT);
						break;
					case ICMD_DNEG:
						COUNT(count_pcmd_op);
						OP1_1(TYPE_DBL, TYPE_DBL);
						break;

					case ICMD_I2L:
						COUNT(count_pcmd_op);
						OP1_1(TYPE_INT, TYPE_LNG);
						break;
					case ICMD_I2F:
						COUNT(count_pcmd_op);
						OP1_1(TYPE_INT, TYPE_FLT);
						break;
					case ICMD_I2D:
						COUNT(count_pcmd_op);
						OP1_1(TYPE_INT, TYPE_DBL);
						break;
					case ICMD_L2I:
						COUNT(count_pcmd_op);
						OP1_1(TYPE_LNG, TYPE_INT);
						break;
					case ICMD_L2F:
						COUNT(count_pcmd_op);
						OP1_1(TYPE_LNG, TYPE_FLT);
						break;
					case ICMD_L2D:
						COUNT(count_pcmd_op);
						OP1_1(TYPE_LNG, TYPE_DBL);
						break;
					case ICMD_F2I:
						COUNT(count_pcmd_op);
						OP1_1(TYPE_FLT, TYPE_INT);
						break;
					case ICMD_F2L:
						COUNT(count_pcmd_op);
						OP1_1(TYPE_FLT, TYPE_LNG);
						break;
					case ICMD_F2D:
						COUNT(count_pcmd_op);
						OP1_1(TYPE_FLT, TYPE_DBL);
						break;
					case ICMD_D2I:
						COUNT(count_pcmd_op);
						OP1_1(TYPE_DBL, TYPE_INT);
						break;
					case ICMD_D2L:
						COUNT(count_pcmd_op);
						OP1_1(TYPE_DBL, TYPE_LNG);
						break;
					case ICMD_D2F:
						COUNT(count_pcmd_op);
						OP1_1(TYPE_DBL, TYPE_FLT);
						break;

					case ICMD_CHECKCAST:
						if (iptr->op1 == 0) {
							/* array type cast-check */

							bte = builtintable_get_internal(BUILTIN_arraycheckcast);
							md = bte->md;

							if (md->memuse > rd->memuse)
								rd->memuse = md->memuse;
							if (md->argintreguse > rd->argintreguse)
								rd->argintreguse = md->argintreguse;

							/* make all stack variables saved */

							copy = curstack;
							while (copy) {
								copy->flags |= SAVEDVAR;
								copy = copy->prev;
							}
						}
						OP1_1(TYPE_ADR, TYPE_ADR);
						break;

					case ICMD_INSTANCEOF:
					case ICMD_ARRAYLENGTH:
						OP1_1(TYPE_ADR, TYPE_INT);
						break;

					case ICMD_NEWARRAY:
					case ICMD_ANEWARRAY:
						OP1_1(TYPE_INT, TYPE_ADR);
						break;

					case ICMD_GETFIELD:
						COUNT(count_check_null);
						COUNT(count_pcmd_mem);
						OP1_1(TYPE_ADR, iptr->op1);
						break;

						/* pop 0 push 1 */
						
					case ICMD_GETSTATIC:
						COUNT(count_pcmd_mem);
						OP0_1(iptr->op1);
						break;

					case ICMD_NEW:
						OP0_1(TYPE_ADR);
						break;

					case ICMD_JSR:
						OP0_1(TYPE_ADR);
						tbptr = m->basicblocks + m->basicblockindex[iptr->op1];

						iptr[0].target = (void *) tbptr;

						/* This is a dirty hack. The typechecker
						 * needs it because the OP1_0ANY below
						 * overwrites iptr->dst.
						 */
						iptr->val.a = (void *) iptr->dst;

						tbptr->type = BBTYPE_SBR;

						/* We need to check for overflow right here because
						 * the pushed value is poped after MARKREACHED. */
						CHECKOVERFLOW;
						MARKREACHED(tbptr, copy);
						OP1_0ANY;
						break;

					/* pop many push any */

					case ICMD_BUILTIN:
#if defined(USEBUILTINTABLE)
					builtin:
#endif
						bte = (builtintable_entry *) iptr->val.a;
						md = bte->md;
						goto _callhandling;

					case ICMD_INVOKESTATIC:
					case ICMD_INVOKESPECIAL:
					case ICMD_INVOKEVIRTUAL:
					case ICMD_INVOKEINTERFACE:
						COUNT(count_pcmd_met);
						INSTRUCTION_GET_METHODDESC(iptr,md);
/*                          if (lm->flags & ACC_STATIC) */
/*                              {COUNT(count_check_null);} */ 	 

					_callhandling:

						last_pei = bptr->icount - len - 1;

						i = md->paramcount;

						if (md->memuse > rd->memuse)
							rd->memuse = md->memuse;
						if (md->argintreguse > rd->argintreguse)
							rd->argintreguse = md->argintreguse;
						if (md->argfltreguse > rd->argfltreguse)
							rd->argfltreguse = md->argfltreguse;

						REQUIRE(i);

						copy = curstack;
						for (i-- ; i >= 0; i--) {
#if defined(SUPPORT_PASS_FLOATARGS_IN_INTREGS)
						/* If we pass float arguments in integer argument registers, we
						 * are not allowed to precolor them here. Floats have to be moved
						 * to this regs explicitly in codegen().
						 * Only arguments that are passed by stack anyway can be precolored
						 * (michi 2005/07/24) */
							if (!(copy->flags & SAVEDVAR) &&
							   (!IS_FLT_DBL_TYPE(copy->type) || md->params[i].inmemory)) {
#else
							if (!(copy->flags & SAVEDVAR)) {
#endif
								copy->varkind = ARGVAR;
								copy->varnum = i;

#if defined(ENABLE_INTRP)
								if (!opt_intrp) {
#endif
									if (md->params[i].inmemory) {
										copy->flags = INMEMORY;
										copy->regoff = md->params[i].regoff;
									} 
									else {
										copy->flags = 0;
										if (IS_FLT_DBL_TYPE(copy->type))
#if defined(SUPPORT_PASS_FLOATARGS_IN_INTREGS)
											assert(0); /* XXX is this assert ok? */
#else
										copy->regoff =
											rd->argfltregs[md->params[i].regoff];
#endif
										else {
#if defined(SUPPORT_COMBINE_INTEGER_REGISTERS)
											if (IS_2_WORD_TYPE(copy->type))
												copy->regoff = PACK_REGS(
																		 rd->argintregs[GET_LOW_REG(md->params[i].regoff)],
																		 rd->argintregs[GET_HIGH_REG(md->params[i].regoff)]);
											else
#endif
												copy->regoff =
													rd->argintregs[md->params[i].regoff];
										}
									}
#if defined(ENABLE_INTRP)
								}
#endif
							}
							copy = copy->prev;
						}

						while (copy) {
							copy->flags |= SAVEDVAR;
							copy = copy->prev;
						}

						i = md->paramcount;
						POPMANY(i);
						if (md->returntype.type != TYPE_VOID)
							OP0_1(md->returntype.type);
						break;

					case ICMD_INLINE_START:
					case ICMD_INLINE_END:
						SETDST;
						break;

					case ICMD_MULTIANEWARRAY:
						if (rd->argintreguse < 3)
							rd->argintreguse = 3;

						i = iptr->op1;

						REQUIRE(i);
#if defined(SPECIALMEMUSE)
# if defined(__DARWIN__)
						if (rd->memuse < (i + INT_ARG_CNT + LA_WORD_SIZE))
							rd->memuse = i + LA_WORD_SIZE + INT_ARG_CNT;
# else
						if (rd->memuse < (i + LA_WORD_SIZE + 3))
							rd->memuse = i + LA_WORD_SIZE + 3;
# endif
#else
# if defined(__I386__)
						if (rd->memuse < i + 3)
							rd->memuse = i + 3; /* n integer args spilled on stack */
# elif defined(__MIPS__) && SIZEOF_VOID_P == 4
						if (rd->memuse < i + 2)
							rd->memuse = i + 2; /* 4*4 bytes callee save space */
# else
						if (rd->memuse < i)
							rd->memuse = i; /* n integer args spilled on stack */
# endif /* defined(__I386__) */
#endif
						copy = curstack;
						while (--i >= 0) {
							/* check INT type here? Currently typecheck does this. */
							if (!(copy->flags & SAVEDVAR)) {
								copy->varkind = ARGVAR;
								copy->varnum = i + INT_ARG_CNT;
								copy->flags |= INMEMORY;
#if defined(SPECIALMEMUSE)
# if defined(__DARWIN__)
								copy->regoff = i + LA_WORD_SIZE + INT_ARG_CNT;
# else
								copy->regoff = i + LA_WORD_SIZE + 3;
# endif
#else
# if defined(__I386__)
								copy->regoff = i + 3;
# elif defined(__MIPS__) && SIZEOF_VOID_P == 4
								copy->regoff = i + 2;
# else
								copy->regoff = i;
# endif /* defined(__I386__) */
#endif /* defined(SPECIALMEMUSE) */
							}
							copy = copy->prev;
						}
						while (copy) {
							copy->flags |= SAVEDVAR;
							copy = copy->prev;
						}
						i = iptr->op1;
						POPMANY(i);
						OP0_1(TYPE_ADR);
						break;

					default:
						*exceptionptr =
							new_internalerror("Unknown ICMD %d", opcode);
						return false;
					} /* switch */

					CHECKOVERFLOW;
					iptr++;
				} /* while instructions */

				/* set out-stack of block */

				bptr->outstack = curstack;
				bptr->outdepth = stackdepth;

				/* stack slots at basic block end become interfaces */

				i = stackdepth - 1;
				for (copy = curstack; copy; i--, copy = copy->prev) {
					if ((copy->varkind == STACKVAR) && (copy->varnum > i))
						copy->varkind = TEMPVAR;
					else {
						copy->varkind = STACKVAR;
						copy->varnum = i;
					}
					IF_NO_INTRP(
							rd->interfaces[i][copy->type].type = copy->type;
							rd->interfaces[i][copy->type].flags |= copy->flags;
					);
				}

				/* check if interface slots at basic block begin must be saved */

				IF_NO_INTRP(
					i = bptr->indepth - 1;
					for (copy = bptr->instack; copy; i--, copy = copy->prev) {
						rd->interfaces[i][copy->type].type = copy->type;
						if (copy->varkind == STACKVAR) {
							if (copy->flags & SAVEDVAR)
								rd->interfaces[i][copy->type].flags |= SAVEDVAR;
						}
					}
				);

			} /* if */
			else
				superblockend = true;

			bptr++;
		} /* while blocks */
	} while (repeat && !deadcode);

#if defined(ENABLE_STATISTICS)
	if (opt_stat) {
		if (m->basicblockcount > count_max_basic_blocks)
			count_max_basic_blocks = m->basicblockcount;
		count_basic_blocks += m->basicblockcount;
		if (m->instructioncount > count_max_javainstr)			count_max_javainstr = m->instructioncount;
		count_javainstr += m->instructioncount;
		if (m->stackcount > count_upper_bound_new_stack)
			count_upper_bound_new_stack = m->stackcount;
		if ((new - m->stack) > count_max_new_stack)
			count_max_new_stack = (new - m->stack);

		b_count = m->basicblockcount;
		bptr = m->basicblocks;
		while (--b_count >= 0) {
			if (bptr->flags > BBREACHED) {
				if (bptr->indepth >= 10)
					count_block_stack[10]++;
				else
					count_block_stack[bptr->indepth]++;
				len = bptr->icount;
				if (len < 10) 
					count_block_size_distribution[len]++;
				else if (len <= 12)
					count_block_size_distribution[10]++;
				else if (len <= 14)
					count_block_size_distribution[11]++;
				else if (len <= 16)
					count_block_size_distribution[12]++;
				else if (len <= 18)
					count_block_size_distribution[13]++;
				else if (len <= 20)
					count_block_size_distribution[14]++;
				else if (len <= 25)
					count_block_size_distribution[15]++;
				else if (len <= 30)
					count_block_size_distribution[16]++;
				else
					count_block_size_distribution[17]++;
			}
			bptr++;
		}

		if (loops == 1)
			count_analyse_iterations[0]++;
		else if (loops == 2)
			count_analyse_iterations[1]++;
		else if (loops == 3)
			count_analyse_iterations[2]++;
		else if (loops == 4)
			count_analyse_iterations[3]++;
		else
			count_analyse_iterations[4]++;

		if (m->basicblockcount <= 5)
			count_method_bb_distribution[0]++;
		else if (m->basicblockcount <= 10)
			count_method_bb_distribution[1]++;
		else if (m->basicblockcount <= 15)
			count_method_bb_distribution[2]++;
		else if (m->basicblockcount <= 20)
			count_method_bb_distribution[3]++;
		else if (m->basicblockcount <= 30)
			count_method_bb_distribution[4]++;
		else if (m->basicblockcount <= 40)
			count_method_bb_distribution[5]++;
		else if (m->basicblockcount <= 50)
			count_method_bb_distribution[6]++;
		else if (m->basicblockcount <= 75)
			count_method_bb_distribution[7]++;
		else
			count_method_bb_distribution[8]++;
	}
#endif /* defined(ENABLE_STATISTICS) */

	/* everything's ok */

	return true;

#if defined(ENABLE_VERIFIER)

throw_stack_underflow:
	*exceptionptr =
		new_verifyerror(m, "Unable to pop operand off an empty stack");
	return false;

throw_stack_overflow:
	*exceptionptr = new_verifyerror(m, "Stack size too large");
	return false;

throw_stack_depth_error:
	*exceptionptr = new_verifyerror(m,"Stack depth mismatch");
	return false;

throw_stack_type_error:
	exceptions_throw_verifyerror_for_stack(m, expectedtype);
	return false;

throw_stack_category_error:
	*exceptionptr =
		new_verifyerror(m, "Attempt to split long or double on the stack");
	return false;

#endif
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
