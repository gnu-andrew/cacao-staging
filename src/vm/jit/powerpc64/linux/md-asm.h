/* src/vm/jit/powerpc64/linux/md-asm.h - assembler defines for PowerPC Linux ABI

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

   Changes:

*/


#ifndef _MD_ASM_H
#define _MD_ASM_H

#include <asm/ppc_asm.h>


/* register defines ***********************************************************/

#define zero  r0
#define sp    r1

/* #define XXX   r2  -  system reserved register */

#define a0    r3
#define a1    r4
#define a2    r5
#define a3    r6
#define a4    r7
#define a5    r8
#define a6    r9
#define a7    r10

#define itmp1 r11
#define itmp2 r12
#define pv    r14

#define s0    r15

#define itmp3 r16
#define t0    r17
#define t1    r18
#define t2    r19
#define t3    r20
#define t4    r21
#define t5    r22
#define t6    r23

#define s1    r24
#define s2    r25
#define s3    r26
#define s4    r27
#define s5    r28
#define s6    r29
#define s7    r30
#define s8    r31

#define v0    a0
#define v1    a1

#define xptr  itmp1
#define xpc   itmp2

#define mptr  r12
#define mptrn 12


#define ftmp3 fr0

#define fa0   fr1
#define fa1   fr2
#define fa2   fr3
#define fa3   fr4
#define fa4   fr5
#define fa5   fr6
#define fa6   fr7
#define fa7   fr8
#define fa8   fr9
#define fa9   fr10
#define fa10  fr11
#define fa11  fr12
#define fa12  fr13

#define ftmp1 fr14
#define ftmp2 fr15

#define fs0   fr16
#define fs1   fr17
#define fs2   fr18
#define fs3   fr19
#define fs4   fr20
#define fs5   fr21
#define fs6   fr22
#define fs7   fr23
#define fs8   fr24
#define fs9   fr25
#define fs10  fr26
#define fs11  fr27
#define fs12  fr28
#define fs13  fr29
#define fs14  fr30
#define fs15  fr31

#define fv0   fa0


/* save and restore macros ****************************************************/

#define SAVE_ARGUMENT_REGISTERS(off) \
	std     a0,(0+(off))*8(sp); \
	std     a1,(1+(off))*8(sp); \
	std     a2,(2+(off))*8(sp); \
	std     a3,(3+(off))*8(sp); \
	std     a4,(4+(off))*8(sp); \
	std     a5,(5+(off))*8(sp); \
	std     a6,(6+(off))*8(sp); \
	std     a7,(7+(off))*8(sp); \
	\
	stfd    fa0,(8+(off))*8(sp); \
	stfd    fa1,(9+(off))*8(sp); \
	stfd    fa2,(10+(off))*8(sp); \
	stfd    fa3,(11+(off))*8(sp); \
	stfd    fa4,(12+(off))*8(sp); \
	stfd    fa5,(13+(off))*8(sp); \
	stfd    fa6,(14+(off))*8(sp); \
	stfd    fa7,(15+(off))*8(sp); \
	stfd    fa8,(16+(off))*8(sp); \
	stfd    fa9,(17+(off))*8(sp); \
	stfd    fa10,(18+(off))*8(sp);\
	stfd    fa11,(19+(off))*8(sp);\
	stfd    fa12,(20+(off))*8(sp);\

#define RESTORE_ARGUMENT_REGISTERS(off) \
	ld     a0,(0+(off))*8(sp); \
	ld     a1,(1+(off))*8(sp); \
	ld     a2,(2+(off))*8(sp); \
	ld     a3,(3+(off))*8(sp); \
	ld     a4,(4+(off))*8(sp); \
	ld     a5,(5+(off))*8(sp); \
	ld     a6,(6+(off))*8(sp); \
	ld     a7,(7+(off))*8(sp); \
	\
	lfd     fa0,(8+(off))*8(sp); \
	lfd     fa1,(9+(off))*8(sp); \
	lfd     fa2,(10+(off))*8(sp); \
	lfd     fa3,(11+(off))*8(sp); \
	lfd     fa4,(12+(off))*8(sp); \
	lfd     fa5,(13+(off))*8(sp); \
	lfd     fa6,(14+(off))*8(sp); \
	lfd     fa7,(15+(off))*8(sp); \
	lfd     fa8,(16+(off))*8(sp); \
	lfd     fa9,(17+(off))*8(sp); \
	lfd     fa10,(18+(off))*8(sp); \
	lfd     fa11,(19+(off))*8(sp); \
	lfd     fa12,(20+(off))*8(sp);


#define SAVE_TEMPORARY_REGISTERS(off) \
	std     t0,(0+(off))*8(sp); \
	std     t1,(1+(off))*8(sp); \
	std     t2,(2+(off))*8(sp); \
	std     t3,(3+(off))*8(sp); \
	std     t4,(4+(off))*8(sp); \
	std     t5,(5+(off))*8(sp); \
	std     t6,(6+(off))*8(sp); 
#if 0	
	\
	\
	stfd    ft0,(7+(off))*8(sp); \
	stfd    ft1,(8+(off))*8(sp); \
	stfd    ft2,(9+(off))*8(sp); \
	stfd    ft3,(10+(off))*8(sp); \
	stfd    ft4,(11+(off))*8(sp); \
	stfd    ft5,(12+(off))*8(sp);
#endif
#define RESTORE_TEMPORARY_REGISTERS(off) \
	ld     t0,(0+(off))*8(sp); \
	ld     t1,(1+(off))*8(sp); \
	ld     t2,(2+(off))*8(sp); \
	ld     t3,(3+(off))*8(sp); \
	ld     t4,(4+(off))*8(sp); \
	ld     t5,(5+(off))*8(sp); \
	ld     t6,(6+(off))*8(sp); 
#if 0	
	\
	\
	lfd     ft0,(7+(off))*8(sp); \
	lfd     ft1,(8+(off))*8(sp); \
	lfd     ft2,(9+(off))*8(sp); \
	lfd     ft3,(10+(off))*8(sp); \
	lfd     ft4,(11+(off))*8(sp); \
	lfd     ft5,(12+(off))*8(sp);
#endif
#endif /* _MD_ASM_H */


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
