/* src/vm/jit/x86_64/patcher.c - x86_64 code patching functions

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

   Changes: Peter Molnar

*/


#include "config.h"

#include <assert.h>
#include <stdint.h>

#include "mm/memory.h"
#include "native/native.h"
#include "vm/builtin.h"
#include "vm/exceptions.h"
#include "vm/initialize.h"
#include "vm/jit/patcher-common.h"
#include "vm/jit/s390/codegen.h"
#include "vm/jit/s390/md-abi.h"
#include "vm/jit/stacktrace.h"
#include "vm/resolve.h"
#include "vm/types.h"
#include "vmcore/class.h"
#include "vmcore/field.h"
#include "vmcore/options.h"
#include "vmcore/references.h"

#define PATCH_BACK_ORIGINAL_MCODE \
	*((u2 *) pr->mpc) = (u2) pr->mcode;

#define PATCHER_TRACE 

/* patcher_get_putstatic *******************************************************

   Machine code:

*******************************************************************************/

bool patcher_get_putstatic(patchref_t *pr)
{
	unresolved_field *uf;
	u1               *datap;
	fieldinfo        *fi;

	PATCHER_TRACE;

	/* get stuff from the stack */

	uf    = (unresolved_field *) pr->ref;
	datap = (u1 *)               pr->datap;

	/* get the fieldinfo */

	if (!(fi = resolve_field_eager(uf)))
		return false;

	/* check if the field's class is initialized */

	if (!(fi->class->state & CLASS_INITIALIZED))
		if (!initialize_class(fi->class))
			return false;

	PATCH_BACK_ORIGINAL_MCODE;

	/* patch the field value's address */

	*((intptr_t *) datap) = (intptr_t) fi->value;

	return true;
}


/* patcher_get_putfield ********************************************************

   Machine code:

*******************************************************************************/

bool patcher_get_putfield(patchref_t *pr)
{
	u1               *ra;
	unresolved_field *uf;
	fieldinfo        *fi;
	s4                disp;

	PATCHER_TRACE;

	/* get stuff from the stack */

	ra    = (u1 *)               pr->mpc;
	uf    = (unresolved_field *) pr->ref;
	disp  =                      pr->disp;

	/* get the fieldinfo */

	if (!(fi = resolve_field_eager(uf)))
		return false;

	PATCH_BACK_ORIGINAL_MCODE;

	/* If NOPs are generated, skip them */

	if (opt_shownops)
		ra += PATCHER_NOPS_SKIP;

	/* If there is an operand load before, skip the load size passed in disp (see ICMD_PUTFIELD) */

	ra += disp;

	/* patch correct offset */

	if (fi->type == TYPE_LNG) {
		assert(N_VALID_DISP(fi->offset + 4));
		/* 2 RX operations, for 2 words; each already contains a 0 or 4 offset. */
		*((u4 *) ra ) |= (fi->offset + (*((u4 *) ra) & 0xF));
		ra += 4;
		*((u4 *) ra ) |= (fi->offset + (*((u4 *) ra) & 0xF));
	} else {
		assert(N_VALID_DISP(fi->offset));
		/* 1 RX operation */
		*((u4 *) ra) |= fi->offset;
	}

	return true;
}

/* patcher_invokestatic_special ************************************************

   Machine code:

*******************************************************************************/

bool patcher_invokestatic_special(patchref_t *pr)
{
	unresolved_method *um;
	u1                *datap;
	methodinfo        *m;

	PATCHER_TRACE;

	/* get stuff from the stack */

	um    = (unresolved_method *) pr->ref;
	datap = (u1 *)                pr->datap;

	/* get the fieldinfo */

	if (!(m = resolve_method_eager(um)))
		return false;

	PATCH_BACK_ORIGINAL_MCODE;

	/* patch stubroutine */

	*((ptrint *) datap) = (ptrint) m->stubroutine;

	return true;
}

/* patcher_invokevirtual *******************************************************

   Machine code:

*******************************************************************************/

bool patcher_invokevirtual(patchref_t *pr)
{
	u1                *ra;
	unresolved_method *um;
	methodinfo        *m;
	s4                 off;

	PATCHER_TRACE;

	/* get stuff from the stack */

	ra    = (u1 *)                pr->mpc;
	um    = (unresolved_method *) pr->ref;

	/* get the fieldinfo */

	if (!(m = resolve_method_eager(um)))
		return false;

	/* patch back original code */

	PATCH_BACK_ORIGINAL_MCODE;

	/* If NOPs are generated, skip them */

	if (opt_shownops)
		ra += PATCHER_NOPS_SKIP;

	/* patch vftbl index */


	off = (s4) (OFFSET(vftbl_t, table[0]) +
								   sizeof(methodptr) * m->vftblindex);

	assert(N_VALID_DISP(off));

	*((s4 *)(ra + 4)) |= off;

	return true;
}


/* patcher_invokeinterface *****************************************************

   Machine code:

*******************************************************************************/

bool patcher_invokeinterface(patchref_t *pr)
{
	u1                *ra;
	unresolved_method *um;
	methodinfo        *m;
	s4                 idx, off;

	PATCHER_TRACE;

	/* get stuff from the stack */

	ra    = (u1 *)                pr->mpc;
	um    = (unresolved_method *) pr->ref;

	/* get the fieldinfo */

	if (!(m = resolve_method_eager(um)))
		return false;

	/* patch back original code */

	PATCH_BACK_ORIGINAL_MCODE;

	/* If NOPs are generated, skip them */

	if (opt_shownops)
		ra += PATCHER_NOPS_SKIP;

	/* get interfacetable index */

	idx = (s4) (OFFSET(vftbl_t, interfacetable[0]) -
		sizeof(methodptr) * m->class->index);

	ASSERT_VALID_IMM(idx);

	/* get method offset */

	off =
		(s4) (sizeof(methodptr) * (m - m->class->methods));
	ASSERT_VALID_DISP(off);

	/* patch them */

	*((s4 *)(ra + 4)) |= (u2)idx;
	*((s4 *)(ra + 4 + 4 + 4)) |= off;

	return true;
}


/* patcher_resolve_classref_to_flags *******************************************

   CHECKCAST/INSTANCEOF:

   <patched call position>

*******************************************************************************/

bool patcher_resolve_classref_to_flags(patchref_t *pr)
{
	constant_classref *cr;
	u1                *datap;
	classinfo         *c;

	PATCHER_TRACE;

	/* get stuff from the stack */

	cr    = (constant_classref *) pr->ref;
	datap = (u1 *)                pr->datap;

	/* get the fieldinfo */

	if (!(c = resolve_classref_eager(cr)))
		return false;

	PATCH_BACK_ORIGINAL_MCODE;

	/* patch class flags */

	*((s4 *) datap) = (s4) c->flags;

	return true;
}

/* patcher_resolve_classref_to_classinfo ***************************************

   ACONST:
   MULTIANEWARRAY:
   ARRAYCHECKCAST:

*******************************************************************************/

bool patcher_resolve_classref_to_classinfo(patchref_t *pr)
{
	constant_classref *cr;
	u1                *datap;
	classinfo         *c;

	PATCHER_TRACE;

	/* get stuff from the stack */

	cr    = (constant_classref *) pr->ref;
	datap = (u1 *)                pr->datap;

	/* get the classinfo */

	if (!(c = resolve_classref_eager(cr)))
		return false;

	PATCH_BACK_ORIGINAL_MCODE;

	/* patch the classinfo pointer */

	*((ptrint *) datap) = (ptrint) c;

	return true;
}

/* patcher_resolve_classref_to_vftbl *******************************************

   CHECKCAST (class):
   INSTANCEOF (class):

*******************************************************************************/

bool patcher_resolve_classref_to_vftbl(patchref_t *pr)
{
	constant_classref *cr;
	u1                *datap;
	classinfo         *c;

	PATCHER_TRACE;

	/* get stuff from the stack */

	cr    = (constant_classref *) pr->ref;
	datap = (u1 *)                pr->datap;

	/* get the fieldinfo */

	if (!(c = resolve_classref_eager(cr)))
		return false;

	PATCH_BACK_ORIGINAL_MCODE;

	/* patch super class' vftbl */

	*((ptrint *) datap) = (ptrint) c->vftbl;

	return true;
}

/* patcher_checkcast_instanceof_interface **************************************

   Machine code:

*******************************************************************************/

bool patcher_checkcast_instanceof_interface(patchref_t *pr)
{

	u1                *ra;
	constant_classref *cr;
	classinfo         *c;

	PATCHER_TRACE;

	/* get stuff from the stack */

	ra    = (u1 *)                pr->mpc;
	cr    = (constant_classref *) pr->ref;

	/* get the fieldinfo */

	if (!(c = resolve_classref_eager(cr)))
		return false;

	/* patch back original code */

	PATCH_BACK_ORIGINAL_MCODE;

	/* If NOPs are generated, skip them */

	if (opt_shownops)
		ra += PATCHER_NOPS_SKIP;

	/* patch super class index */

	/* From here, split your editor and open codegen.c */

	switch (*(ra + 1) >> 4) {
		case REG_ITMP1: 
			/* First M_ALD is into ITMP1 */
			/* INSTANCEOF code */

			*(u4 *)(ra + SZ_L + SZ_L) |= (u2)(s2)(- c->index);
			*(u4 *)(ra + SZ_L + SZ_L + SZ_AHI + SZ_BRC) |=
				(u2)(s2)(OFFSET(vftbl_t, interfacetable[0]) -
					c->index * sizeof(methodptr*));

			break;

		case REG_ITMP2:
			/* First M_ALD is into ITMP2 */
			/* CHECKCAST code */

			*(u4 *)(ra + SZ_L + SZ_L) |= (u2)(s2)(- c->index);
			*(u4 *)(ra + SZ_L + SZ_L + SZ_AHI + SZ_BRC + SZ_ILL) |=
				(u2)(s2)(OFFSET(vftbl_t, interfacetable[0]) -
					c->index * sizeof(methodptr*));

			break;

		default:
			assert(0);
			break;
	}

	return true;
}

/* patcher_clinit **************************************************************

   May be used for GET/PUTSTATIC and in native stub.

   Machine code:

*******************************************************************************/

bool patcher_clinit(patchref_t *pr)
{
	classinfo *c;

	PATCHER_TRACE;

	/* get stuff from the stack */

	c     = (classinfo *)pr->ref;

	/* check if the class is initialized */

	if (!(c->state & CLASS_INITIALIZED))
		if (!initialize_class(c))
			return false;

	/* patch back original code */

	PATCH_BACK_ORIGINAL_MCODE;

	return true;
}


/* patcher_athrow_areturn ******************************************************

   Machine code:

   <patched call position>

*******************************************************************************/

#ifdef ENABLE_VERIFIER
bool patcher_athrow_areturn(patchref_t *pr)
{
	unresolved_class *uc;

	PATCHER_TRACE;

	/* get stuff from the stack */

	uc    = (unresolved_class *) pr->ref;

	/* resolve the class and check subtype constraints */

	if (!resolve_class_eager_no_access_check(uc))
		return false;

	/* patch back original code */

	PATCH_BACK_ORIGINAL_MCODE;

	return true;
}
#endif /* ENABLE_VERIFIER */


/* patcher_resolve_native ******************************************************

*******************************************************************************/

#if !defined(WITH_STATIC_CLASSPATH)
bool patcher_resolve_native_function(patchref_t *pr)
{
	methodinfo  *m;
	u1          *datap;
	functionptr  f;

	PATCHER_TRACE;

	/* get stuff from the stack */

	m     = (methodinfo *) pr->ref;
	datap = (u1 *)         pr->datap;

	/* resolve native function */

	if (!(f = native_resolve_function(m)))
		return false;

	PATCH_BACK_ORIGINAL_MCODE;

	/* patch native function pointer */

	*((ptrint *) datap) = (ptrint) f;

	return true;
}
#endif /* !defined(WITH_STATIC_CLASSPATH) */

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
