/* vm/jit/alpha/arch.h - architecture defines for Alpha

   Copyright (C) 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003
   R. Grafl, A. Krall, C. Kruegel, C. Oates, R. Obermaisser,
   M. Probst, S. Ring, E. Steiner, C. Thalinger, D. Thuernbeck,
   P. Tomsich, J. Wenninger

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

   $Id: arch.h 1624 2004-11-30 14:49:45Z twisti $

*/


#ifndef _ARCH_H
#define _ARCH_H


/* preallocated registers *****************************************************/

/* integer registers */
  
#define REG_RESULT      0    /* to deliver method results                     */

#define REG_RA          26   /* return address                                */
#define REG_PV          27   /* procedure vector, must be provided by caller  */
#define REG_METHODPTR   28   /* pointer to the place from where the procedure */
                             /* vector has been fetched                       */
#define REG_ITMP1       25   /* temporary register                            */
#define REG_ITMP2       28   /* temporary register and method pointer         */
#define REG_ITMP3       29   /* temporary register                            */

#define REG_ITMP1_XPTR  25   /* exception pointer = temporary register 1      */
#define REG_ITMP2_XPC   28   /* exception pc = temporary register 2           */

#define REG_SP          30   /* stack pointer                                 */
#define REG_ZERO        31   /* always zero                                   */

/* floating point registers */

#define REG_FRESULT     0    /* to deliver floating point method results      */

#define REG_FTMP1       28   /* temporary floating point register             */
#define REG_FTMP2       29   /* temporary floating point register             */
#define REG_FTMP3       30   /* temporary floating point register             */

#define REG_IFTMP       28   /* temporary integer and floating point register */


#define INT_SAV_CNT      7   /* number of int callee saved registers          */
#define INT_ARG_CNT      6   /* number of int argument registers              */

#define FLT_SAV_CNT      8   /* number of flt callee saved registers          */
#define FLT_ARG_CNT      6   /* number of flt argument registers              */


/* define architecture features ***********************************************/

#define POINTERSIZE              8
#define WORDS_BIGENDIAN          0

#define U8_AVAILABLE             1

#define USE_CODEMMAP             1

#define SUPPORT_DIVISION         0
#define SUPPORT_LONG             1
#define SUPPORT_FLOAT            1
#define SUPPORT_DOUBLE           1

/*  #define SUPPORT_IFCVT            1 */
/*  #define SUPPORT_FICVT            1 */

#define SUPPORT_LONG_ADD         1
#define SUPPORT_LONG_CMP         1
#define SUPPORT_LONG_LOG         1
#define SUPPORT_LONG_SHIFT       1
#define SUPPORT_LONG_MUL         1
#define SUPPORT_LONG_DIV         0
#define SUPPORT_LONG_ICVT        1
#define SUPPORT_LONG_FCVT        1

#define SUPPORT_CONST_ASTORE     1      /* do we support const astores        */
#define SUPPORT_ONLY_ZERO_ASTORE 1      /* on risc machines we can only store */
                                        /* REG_ZERO                           */

/*  #define USEBUILTINTABLE */

#define CONDITIONAL_LOADCONST

/*  #define CONSECUTIVE_INTARGS */
/*  #define CONSECUTIVE_FLOATARGS */

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
