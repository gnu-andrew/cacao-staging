/*	src/vm/jit/m68k/md.h

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


#ifndef _VM_JIT_M68K_MD_H
#define _VM_JIT_M68K_MD_H

#include "config.h"

#include <assert.h>
#include <stdint.h>

#include "vm/jit/codegen-common.h"


/* md_stacktrace_get_returnaddress *********************************************

   Returns the return address of the current stackframe, specified by
   the passed stack pointer and the stack frame size.

*******************************************************************************/

void *md_stacktrace_get_returnaddress(void *sp, int32_t stackframesize);


/* md_codegen_get_pv_from_pc ***************************************************

   On this architecture just a wrapper function to
   codegen_get_pv_from_pc.

*******************************************************************************/

inline static void *md_codegen_get_pv_from_pc(void *ra)
{
	void *pv;

	pv = codegen_get_pv_from_pc(ra);

	return pv;
}


/* XXX i can't find a definition of cacheflush in any installed header files but i can find the symbol in libc */
/* lets extract the signature from the assembler code*/
/*
    000e7158 <cacheflush>:
    e7158:       707b            moveq #123,%d0
    e715a:       2f04            movel %d4,%sp@-
    e715c:       282f 0014       movel %sp@(20),%d4			arg 
    e7160:       2243            moveal %d3,%a1
    e7162:       262f 0010       movel %sp@(16),%d3			arg 
    e7166:       2042            moveal %d2,%a0
    e7168:       242f 000c       movel %sp@(12),%d2			arg 
    e716c:       222f 0008       movel %sp@(8),%d1			arg 
    e7170:       4e40            trap #0				traps into system i guess
    e7172:       2408            movel %a0,%d2
    e7174:       2609            movel %a1,%d3
    e7176:       281f            movel %sp@+,%d4
    e7178:       223c ffff f001  movel #-4095,%d1
    e717e:       b081            cmpl %d1,%d0
    e7180:       6402            bccs e7184 <cacheflush+0x2c>
    e7182:       4e75            rts
    e7184:       4480            negl %d0
    e7186:       2f00            movel %d0,%sp@-
    e7188:       61ff fff3 82e2  bsrl 1f46c <D_MAX_EXP+0x1ec6d>
    e718e:       209f            movel %sp@+,%a0@
    e7190:       70ff            moveq #-1,%d0
    e7192:       2040            moveal %d0,%a0
    e7194:       4e75            rts
    e7196:       4e75            rts
									*/

/* seems to have 4 arguments */
/* best guess: it is this syscall */
/* asmlinkage int sys_cacheflush (unsigned long addr, int scope, int cache, unsigned long len) */
/* kernel 2.6.10 with freescale patches (the one I develop against) needs a patch of */
/* arch/m68k/kernel/sys_m68k.c(sys_cacheflush) */
/* evil hack: */
/*
void DcacheFlushInvalidateCacheBlock(void *start, unsigned long size);
void IcacheInvalidateCacheBlock(void *start, unsigned long size);

asmlinkage int
sys_cacheflush (unsigned long addr, int scope, int cache, unsigned long len)
{
	lock_kernel();
	DcacheFlushInvalidateCacheBlock(addr, len);
	IcacheInvalidateCacheBlock(addr, len);
	unlock_kernel();
	return 0;
}
*/

extern int cacheflush(unsigned long addr, int scope, int cache, unsigned long len);

#include "asm/cachectl.h"	/* found more traces of the cacheflush function */
#include "errno.h"


/* md_cacheflush ***************************************************************

   Calls the system's function to flush the instruction and data
   cache.

*******************************************************************************/

inline static void md_cacheflush(void *addr, int nbytes)
{
	cacheflush((unsigned long)addr, FLUSH_SCOPE_PAGE, FLUSH_CACHE_BOTH, nbytes);
}


/* md_icacheflush **************************************************************

   Calls the system's function to flush the instruction cache.

*******************************************************************************/

inline static void md_icacheflush(void *addr, int nbytes)
{
	cacheflush((unsigned long)addr, FLUSH_SCOPE_LINE, FLUSH_CACHE_INSN, nbytes);
}


/* md_dcacheflush **************************************************************

   Calls the system's function to flush the data cache.

*******************************************************************************/

inline static void md_dcacheflush(void *addr, int nbytes)
{
	cacheflush((unsigned long)addr, FLUSH_SCOPE_PAGE, FLUSH_CACHE_DATA, nbytes);
}

#endif /* _VM_JIT_M68K_MD_H */


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
