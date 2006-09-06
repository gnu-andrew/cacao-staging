/* src/vm/jit/parse.h - parser header

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

   Author:  Christian Thalinger

   Changes: Edwin Steiner

   $Id: parse.h 5363 2006-09-06 10:20:07Z christian $

*/


#ifndef _PARSE_H
#define _PARSE_H

#include "config.h"
#include "vm/types.h"

#include "vm/global.h"
#include "vm/jit/codegen-common.h"


/* macros for verifier checks during parsing **********************************/

#if defined(ENABLE_VERIFIER)

/* We have to check local variables indices here because they are             */
/* used in stack.c to index the locals array.                                 */

#define INDEX_ONEWORD(num) \
    do { \
        if (((num) < 0) || ((num) >= m->maxlocals)) \
            goto throw_illegal_local_variable_number; \
    } while (0)

#define INDEX_TWOWORD(num) \
    do { \
        if (((num) < 0) || (((num) + 1) >= m->maxlocals)) \
            goto throw_illegal_local_variable_number; \
    } while (0)

/* CHECK_BYTECODE_INDEX(i) checks whether i is a valid bytecode index.        */
/* The end of the bytecode (i == m->jcodelength) is considered valid.         */

#define CHECK_BYTECODE_INDEX(i) \
    do { \
        if (((i) < 0) || ((i) >= m->jcodelength)) \
			goto throw_invalid_bytecode_index; \
    } while (0)

/* CHECK_BYTECODE_INDEX_EXCLUSIVE is used for the exclusive ends               */
/* of exception handler ranges.                                                */
#define CHECK_BYTECODE_INDEX_EXCLUSIVE(i) \
    do { \
        if ((i) < 0 || (i) > m->jcodelength) \
			goto throw_invalid_bytecode_index; \
    } while (0)

#else /* !define(ENABLE_VERIFIER) */

#define INDEX_ONEWORD(num)
#define INDEX_TWOWORD(num)
#define CHECK_BYTECODE_INDEX(i)
#define CHECK_BYTECODE_INDEX_EXCLUSIVE(i)

#endif /* define(ENABLE_VERIFIER) */


/* basic block generating macro ***********************************************/

#define NEW_MARK_BASICBLOCK(i) \
    do { \
        if (!(jd->new_basicblockindex[(i)] & 1)) { \
            b_count++; \
            jd->new_basicblockindex[(i)] |= 1; \
        } \
    } while (0)


/* intermediate code generating macros ****************************************/

/* These macros ALWAYS set the following fields of *iptr to valid values:     */
/*     iptr->opc                                                              */
/*     iptr->flags                                                            */
/*     iptr->line                                                             */

/* These macros do NOT touch the following fields of *iptr, unless a value is */
/* given for them:                                                            */
/*     iptr->s1                                                               */
/*     iptr->sx                                                               */
/*     iptr->dst                                                              */

/* The _PREPARE macros omit the PINC, so you can set additional fields        */
/* afterwards.                                                                */
/* CAUTION: Some of the _PREPARE macros don't set iptr->flags!                */

#define PINC                                                           \
    iptr++; ipc++

/* CAUTION: You must set iptr->flags yourself when using this!                */
#define NEW_OP_PREPARE(o)                                              \
    iptr->opc                = (o);                                    \
    iptr->line               = currentline;

#define NEW_OP_PREPARE_ZEROFLAGS(o)                                    \
    iptr->opc                = (o);                                    \
    iptr->line               = currentline;                            \
    iptr->flags.bits         = 0;

#define NEW_OP(o)                                                      \
	NEW_OP_PREPARE_ZEROFLAGS(o);                                       \
    PINC

#define NEW_OP_LOADCONST_I(v)                                          \
	NEW_OP_PREPARE_ZEROFLAGS(ICMD_ICONST);                             \
    iptr->sx.val.i           = (v);                                    \
    PINC

#define NEW_OP_LOADCONST_L(v)                                          \
	NEW_OP_PREPARE_ZEROFLAGS(ICMD_LCONST);                             \
    iptr->sx.val.l           = (v);                                    \
    PINC

#define NEW_OP_LOADCONST_F(v)                                          \
	NEW_OP_PREPARE_ZEROFLAGS(ICMD_FCONST);                             \
    iptr->sx.val.f           = (v);                                    \
    PINC

#define NEW_OP_LOADCONST_D(v)                                          \
	NEW_OP_PREPARE_ZEROFLAGS(ICMD_DCONST);                             \
    iptr->sx.val.d           = (v);                                    \
    PINC

#define NEW_OP_LOADCONST_NULL()                                        \
	NEW_OP_PREPARE_ZEROFLAGS(ICMD_ACONST);                             \
    iptr->sx.val.anyptr      = NULL;                                   \
    PINC

#define NEW_OP_LOADCONST_STRING(v)                                     \
	NEW_OP_PREPARE_ZEROFLAGS(ICMD_ACONST);                             \
    iptr->sx.val.stringconst = (v);                                    \
    PINC

#define NEW_OP_LOADCONST_CLASSINFO_OR_CLASSREF(c, cr, extraflags)      \
	NEW_OP_PREPARE(ICMD_ACONST);                                       \
    if (c) {                                                           \
        iptr->sx.val.c.cls   = (c);                                    \
        iptr->flags.bits     = INS_FLAG_CLASS | (extraflags);          \
    }                                                                  \
    else {                                                             \
        iptr->sx.val.c.ref   = (cr);                                   \
        iptr->flags.bits     = INS_FLAG_CLASS | INS_FLAG_UNRESOLVED    \
                             | (extraflags);                           \
    }                                                                  \
    PINC

#define NEW_OP_S3_CLASSINFO_OR_CLASSREF(o, c, cr, extraflags)          \
	NEW_OP_PREPARE(o);                                                 \
    if (c) {                                                           \
        iptr->sx.s23.s3.c.cls= (c);                                    \
        iptr->flags.bits     = (extraflags);                           \
    }                                                                  \
    else {                                                             \
        iptr->sx.s23.s3.c.ref= (cr);                                   \
        iptr->flags.bits     = INS_FLAG_UNRESOLVED | (extraflags);     \
    }                                                                  \
    PINC

#define NEW_OP_INSINDEX(o, iindex)                                     \
	NEW_OP_PREPARE_ZEROFLAGS(o);                                       \
    iptr->dst.insindex       = (iindex);                               \
    PINC

#define NEW_OP_LOCALINDEX(o,index)                                     \
	NEW_OP_PREPARE_ZEROFLAGS(o);                                       \
    iptr->s1.localindex      = (index);                                \
    PINC

#define NEW_OP_LOCALINDEX_I(o,index,v)                                 \
	NEW_OP_PREPARE_ZEROFLAGS(o);                                       \
    iptr->s1.localindex      = (index);                                \
    iptr->sx.val.i           = (v);                                    \
    PINC

#if defined(NEW_VAR)
#define LOCALTYPE_USED(index,type)									   \
	do {															   \
		local_map[index * 5 +type] = 1;								   \
	} while (0)
#else
#define LOCALTYPE_USED(index,type)
#endif

#define NEW_OP_LOAD_ONEWORD(o,index,type)							   \
    do {                                                               \
        INDEX_ONEWORD(index);                                          \
        NEW_OP_LOCALINDEX(o,index);                                    \
		LOCALTYPE_USED(index,type);								   \
    } while (0)

#define NEW_OP_LOAD_TWOWORD(o,index,type)							   \
    do {                                                               \
        INDEX_TWOWORD(index);                                          \
        NEW_OP_LOCALINDEX(o,index);                                    \
		LOCALTYPE_USED(index,type);									   \
	} while (0)

#define NEW_OP_STORE_ONEWORD(o,index,type)							   \
    do {                                                               \
        INDEX_ONEWORD(index);                                          \
        NEW_OP_PREPARE_ZEROFLAGS(o);                                   \
        iptr->dst.localindex = (index);                                \
		LOCALTYPE_USED(index,type);									   \
        PINC;                                                          \
    } while (0)

#define NEW_OP_STORE_TWOWORD(o,index,type)							   \
    do {                                                               \
        INDEX_TWOWORD(index);                                          \
        NEW_OP_PREPARE_ZEROFLAGS(o);                                   \
        iptr->dst.localindex = (index);                                \
		LOCALTYPE_USED(index,type);									   \
        PINC;                                                          \
    } while (0)

#define NEW_OP_BUILTIN_CHECK_EXCEPTION(bte)                            \
    jd->isleafmethod         = false;                                  \
	NEW_OP_PREPARE_ZEROFLAGS(ICMD_BUILTIN);                            \
    iptr->sx.s23.s3.bte      = (bte);                                  \
    PINC

#define NEW_OP_BUILTIN_NO_EXCEPTION(bte)                               \
    jd->isleafmethod         = false;                                  \
	NEW_OP_PREPARE(ICMD_BUILTIN);                                      \
    iptr->sx.s23.s3.bte      = (bte);                                  \
    iptr->flags.bits         = INS_FLAG_NOCHECK;                       \
    PINC

#define NEW_OP_BUILTIN_ARITHMETIC(opcode, bte)                         \
    jd->isleafmethod         = false;                                  \
	NEW_OP_PREPARE_ZEROFLAGS(opcode);                                  \
    iptr->sx.s23.s3.bte      = (bte);                                  \
    PINC

/* CAUTION: You must set iptr->flags yourself when using this!                */
#define NEW_OP_FMIREF_PREPARE(o, fmiref)                               \
	NEW_OP_PREPARE(o);                                                 \
    iptr->sx.s23.s3.fmiref   = (fmiref);


/* function prototypes ********************************************************/

bool new_parse(jitdata *jd);

#endif /* _PARSE_H */


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

