/* src/vm/jit/dseg.c - data segment handling stuff

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

   Authors: Reinhard Grafl
            Andreas  Krall

   Changes: Christian Thalinger
            Joseph Wenninger

   $Id: dseg.c 4445 2006-02-05 15:26:34Z edwin $

*/


#include "config.h"

#include "vm/types.h"

#include <assert.h>
#include "mm/memory.h"
#include "vm/jit/codegen-common.h"


/* desg_increase ***************************************************************

   Doubles data area.

*******************************************************************************/

void dseg_increase(codegendata *cd)
{
	u1 *newstorage;

	newstorage = DMNEW(u1, cd->dsegsize * 2);

	MCOPY(newstorage + cd->dsegsize, cd->dsegtop - cd->dsegsize, u1,
		  cd->dsegsize);

	cd->dsegtop   = newstorage;
	cd->dsegsize *= 2;
	cd->dsegtop  += cd->dsegsize;
}


s4 dseg_adds4_increase(codegendata *cd, s4 value)
{
	dseg_increase(cd);

	*((s4 *) (cd->dsegtop - cd->dseglen)) = value;

	return -(cd->dseglen);
}


s4 dseg_adds4(codegendata *cd, s4 value)
{
	s4 *dataptr;

	cd->dseglen += 4;
	dataptr = (s4 *) (cd->dsegtop - cd->dseglen);

	if (cd->dseglen > cd->dsegsize)
		return dseg_adds4_increase(cd, value);

	*dataptr = value;

	return -(cd->dseglen);
}


s4 dseg_adds8_increase(codegendata *cd, s8 value)
{
	dseg_increase(cd);

	*((s8 *) (cd->dsegtop - cd->dseglen)) = value;

	return -(cd->dseglen);
}


s4 dseg_adds8(codegendata *cd, s8 value)
{
	s8 *dataptr;

	cd->dseglen = ALIGN(cd->dseglen + 8, 8);
	dataptr = (s8 *) (cd->dsegtop - cd->dseglen);

	if (cd->dseglen > cd->dsegsize)
		return dseg_adds8_increase(cd, value);

	*dataptr = value;

	return -(cd->dseglen);
}


s4 dseg_addfloat_increase(codegendata *cd, float value)
{
	dseg_increase(cd);

	*((float *) (cd->dsegtop - cd->dseglen)) = value;

	return -(cd->dseglen);
}


s4 dseg_addfloat(codegendata *cd, float value)
{
	float *dataptr;

	cd->dseglen += 4;
	dataptr = (float *) (cd->dsegtop - cd->dseglen);

	if (cd->dseglen > cd->dsegsize)
		return dseg_addfloat_increase(cd, value);

	*dataptr = value;

	return -(cd->dseglen);
}


s4 dseg_adddouble_increase(codegendata *cd, double value)
{
	dseg_increase(cd);

	*((double *) (cd->dsegtop - cd->dseglen)) = value;

	return -(cd->dseglen);
}


s4 dseg_adddouble(codegendata *cd, double value)
{
	double *dataptr;

	cd->dseglen = ALIGN(cd->dseglen + 8, 8);
	dataptr = (double *) (cd->dsegtop - cd->dseglen);

	if (cd->dseglen > cd->dsegsize)
		return dseg_adddouble_increase(cd, value);

	*dataptr = value;

	return -(cd->dseglen);
}


void dseg_addtarget(codegendata *cd, basicblock *target)
{
	jumpref *jr;

	jr = DNEW(jumpref);

	jr->tablepos = dseg_addaddress(cd, NULL);
	jr->target   = target;
	jr->next     = cd->jumpreferences;

	cd->jumpreferences = jr;
}


/* dseg_addlinenumbertablesize *************************************************

   XXX

*******************************************************************************/

void dseg_addlinenumbertablesize(codegendata *cd)
{
#if SIZEOF_VOID_P == 8
	/* 4-byte ALIGNMENT PADDING */

	dseg_adds4(cd, 0);
#endif

	cd->linenumbertablesizepos  = dseg_addaddress(cd, NULL);
	cd->linenumbertablestartpos = dseg_addaddress(cd, NULL);

#if SIZEOF_VOID_P == 8
	/* 4-byte ALIGNMENT PADDING */

	dseg_adds4(cd, 0);
#endif
}


/* dseg_addlinenumber **********************************************************

   Add a line number reference.

   IN:
      cd.............current codegen data
      linenumber.....number of line that starts with the given mcodeptr
      mcodeptr.......start mcodeptr of line

*******************************************************************************/

void dseg_addlinenumber(codegendata *cd, u2 linenumber, u1 *mcodeptr)
{
	linenumberref *lr;

	lr = DNEW(linenumberref);

	lr->linenumber = linenumber;
	lr->tablepos   = 0;
	lr->targetmpc  = mcodeptr - cd->mcodebase;
	lr->next       = cd->linenumberreferences;

	cd->linenumberreferences = lr;
}


/* dseg_addlinenumber_inline_start *********************************************

   Add a marker to the line number table indicating the start of an inlined
   method body. (see doc/inlining_stacktrace.txt)

   IN:
      cd.............current codegen data
      iptr...........the ICMD_INLINE_START instruction
      mcodeptr.......start mcodeptr of inlined body

*******************************************************************************/

void dseg_addlinenumber_inline_start(codegendata *cd, 
									 instruction *iptr, 
									 u1 *mcodeptr)
{
	linenumberref *lr;

	lr = DNEW(linenumberref);

	lr->linenumber = (-2); /* marks start of inlined method */
	lr->tablepos   = 0;
	lr->targetmpc  = mcodeptr - cd->mcodebase;
	lr->next       = cd->linenumberreferences;

	cd->linenumberreferences = lr;

	iptr->target = mcodeptr; /* store for corresponding INLINE_END */
}


/* dseg_addlinenumber_inline_end ***********************************************

   Add a marker to the line number table indicating the end of an inlined
   method body. (see doc/inlining_stacktrace.txt)

   IN:
      cd.............current codegen data
      iptr...........the ICMD_INLINE_END instruction

   Note:
      iptr->method must point to the inlined callee.

*******************************************************************************/

void dseg_addlinenumber_inline_end(codegendata *cd, instruction *iptr)
{
	linenumberref *lr;
	linenumberref *prev;
	instruction *inlinestart;

	/* get the pointer to the corresponding ICMD_INLINE_START */
	inlinestart = (instruction *)iptr->target;

	assert(inlinestart);
	assert(iptr->method);

	lr = DNEW(linenumberref);

	/* special entry containing the methodinfo * */
	lr->linenumber = (-3) - iptr->line;
	lr->tablepos   = 0;
	lr->targetmpc  = (ptrint) iptr->method;
	lr->next       = cd->linenumberreferences;

	prev = lr;
	lr = DNEW(linenumberref);

	/* end marker with PC of start of body */
	lr->linenumber = (-1);
	lr->tablepos   = 0;
	lr->targetmpc  = (u1*)inlinestart->target - cd->mcodebase;
	lr->next       = prev;

	cd->linenumberreferences = lr;
}


/* dseg_createlinenumbertable **************************************************

   Creates a line number table in the data segment from the created
   entries in linenumberreferences.

*******************************************************************************/

void dseg_createlinenumbertable(codegendata *cd)
{
	linenumberref *lr;

	for (lr = cd->linenumberreferences; lr != NULL; lr = lr->next) {
		lr->tablepos = dseg_addaddress(cd, NULL);

		if (cd->linenumbertab == 0)
			cd->linenumbertab = lr->tablepos;

		dseg_addaddress(cd, lr->linenumber);
	}
}


/* dseg_adddata ****************************************************************

   Adds a data segment reference to the codegendata.

*******************************************************************************/

#if defined(__I386__) || defined(__X86_64__) || defined(__XDSPCORE__) || defined(ENABLE_INTRP)
void dseg_adddata(codegendata *cd, u1 *mcodeptr)
{
	dataref *dr;

	dr = DNEW(dataref);

	dr->datapos = mcodeptr - cd->mcodebase;
	dr->next    = cd->datareferences;

	cd->datareferences = dr;
}
#endif


/* dseg_resolve_datareferences *************************************************

   Resolve data segment references.

*******************************************************************************/

#if defined(__I386__) || defined(__X86_64__) || defined(__XDSPCORE__) || defined(ENABLE_INTRP)
void dseg_resolve_datareferences(codegendata *cd, methodinfo *m)
{
	dataref *dr;

	/* data segment references resolving */

	for (dr = cd->datareferences; dr != NULL; dr = dr->next)
		*((u1 **) (m->entrypoint + dr->datapos - SIZEOF_VOID_P)) = m->entrypoint;
}
#endif


/* dseg_display ****************************************************************

   Displays the content of the methods' data segment.

*******************************************************************************/

#if !defined(NDEBUG)
void dseg_display(methodinfo *m, codegendata *cd)
{
	s4 *s4ptr;
	s4 i;
	
	s4ptr = (s4 *) (ptrint) m->mcode;

	printf("  --- dump of datasegment\n");

	for (i = cd->dseglen; i > 0 ; i -= 4) {
#if SIZEOF_VOID_P == 8
		printf("0x%016lx:   -%6x (%6d): %8x\n",
			   (ptrint) s4ptr, i, i, (s4) *s4ptr);
#else
		printf("0x%08x:   -%6x (%6d): %8x\n",
			   (ptrint) s4ptr, i, i, (s4) *s4ptr);
#endif
		s4ptr++;
	}

	printf("  --- begin of data segment: %p\n", (void *) s4ptr);
}
#endif /* !defined(NDEBUG) */


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
