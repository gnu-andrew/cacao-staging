/* src/vm/jit/x86_64/arch.h - architecture defines for x86_64

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

   $Id: arch.h 7356 2007-02-14 11:00:28Z twisti $

*/


#ifndef _ARCH_H
#define _ARCH_H

#include "config.h"


/* define architecture features ***********************************************/

#define U8_AVAILABLE                     1

/* #define USEBUILTINTABLE */

#define SUPPORT_DIVISION                 1
#define SUPPORT_LONG                     1

#define SUPPORT_I2F                      1
#define SUPPORT_I2D                      1
#define SUPPORT_L2F                      1
#define SUPPORT_L2D                      1

/* ATTENTION: x86_64 architectures support these conversions, but we
   need the builtin functions in corner cases */
#define SUPPORT_F2I                      0
#define SUPPORT_F2L                      0
#define SUPPORT_D2I                      0
#define SUPPORT_D2L                      0

#define SUPPORT_LONG_ADD                 1
#define SUPPORT_LONG_CMP                 1
#define SUPPORT_LONG_CMP_CONST           1
#define SUPPORT_LONG_LOGICAL             1
#define SUPPORT_LONG_SHIFT               1
#define SUPPORT_LONG_MUL                 1
#define SUPPORT_LONG_DIV                 1

#define SUPPORT_LONG_DIV_POW2            1
#define SUPPORT_LONG_REM_POW2            1

#define SUPPORT_CONST_LOGICAL            1  /* AND, OR, XOR with immediates   */
#define SUPPORT_CONST_MUL                1  /* mutiply with immediate         */

#define SUPPORT_CONST_STORE              0  /* do we support const stores     */
#define SUPPORT_CONST_STORE_ZERO_ONLY    0  /* on some risc machines we can   */
                                            /* only store REG_ZERO            */


/* float **********************************************************************/

#define SUPPORT_FLOAT                    1

#if defined(ENABLE_SOFT_FLOAT_CMP)
# define SUPPORT_FLOAT_CMP               0
#else
# define SUPPORT_FLOAT_CMP               1
#endif


/* double *********************************************************************/

#define SUPPORT_DOUBLE                   1

#if defined(ENABLE_SOFT_FLOAT_CMP)
# define SUPPORT_DOUBLE_CMP              0
#else
# define SUPPORT_DOUBLE_CMP              1
#endif


#define CONSECUTIVE_INTEGER_ARGS
#define CONSECUTIVE_FLOAT_ARGS

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
