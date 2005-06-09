/* src/vm/jit/mips/arch.h - architecture defines for MIPS

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

   Changes:

   $Id: arch.h 2626 2005-06-09 20:36:48Z twisti $

*/


#ifndef _ARCH_H
#define _ARCH_H

/* define architecture features ***********************************************/

#define U8_AVAILABLE                     1

/* #define USE_CODEMMAP */

#define USEBUILTINTABLE

#define SUPPORT_DIVISION                 0
#define SUPPORT_LONG                     1
#define SUPPORT_FLOAT                    1
#define SUPPORT_DOUBLE                   1

#define SUPPORT_FMOD                     1
#define SUPPORT_IFCVT                    1
#define SUPPORT_FICVT                    0

#define SUPPORT_LONG_ADD                 1
#define SUPPORT_LONG_CMP                 1
#define SUPPORT_LONG_LOGICAL             1
#define SUPPORT_LONG_SHIFT               1
#define SUPPORT_LONG_MUL                 1
#define SUPPORT_LONG_DIV                 0
#define SUPPORT_LONG_ICVT                0
#define SUPPORT_LONG_FCVT                1

#define SUPPORT_CONST_LOGICAL            1  /* AND, OR, XOR with immediates   */
#define SUPPORT_CONST_MUL                1  /* mutiply with immediate         */

#define SUPPORT_CONST_STORE              1  /* do we support const stores     */
#define SUPPORT_CONST_STORE_ZERO_ONLY    1  /* on some risc machines we can   */
                                            /* only store REG_ZERO            */

/* #define CONDITIONAL_LOADCONST           1 */

#endif /* _ARCH_H */


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
