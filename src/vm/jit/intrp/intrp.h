/* src/vm/jit/intrp/intrp.h - definitions for Interpreter

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
            Anton Ertl

   Changes:

   $Id: intrp.h 3895 2005-12-07 16:03:37Z anton $

*/


#ifndef _INTRP_H
#define _INTRP_H

#include <stdio.h>

/* #define VM_PROFILING */

#include "config.h"
#include "vm/types.h"

#include "arch.h"

#include "vm/class.h"
#include "vm/global.h"
#include "vm/method.h"
#include "vm/references.h"
#include "vm/resolve.h"

#include "libffi/include/ffi.h"


typedef void *Label;
typedef void *Inst;

#if SIZEOF_VOID_P == 8
typedef s8 Cell;
#else
typedef s4 Cell;
#endif

#if 1
#define MAYBE_UNUSED __attribute__((unused))
#else
#define MAYBE_UNUSED
#endif

#if SIZEOF_VOID_P == 4

typedef union {
    struct {
		u4 low;
		s4 high;
    } cells;
    s8 l;
    double d;
} Double_Store;

#define FETCH_DCELL_T(d_,lo,hi,t_)	({ \
				     Double_Store _d; \
				     _d.cells.low = (lo); \
				     _d.cells.high = (hi); \
				     (d_) = _d.t_; \
				 })

#define STORE_DCELL_T(d_,lo,hi,t_)	({ \
				     Double_Store _d; \
				     _d.t_ = (d_); \
				     (lo) = _d.cells.low; \
				     (hi) = _d.cells.high; \
				 })

#else /* SIZEOF_VOID_P == 4 */

typedef union {
	s8 low;
	s8 l;
	double d;
} Double_Store;


#define FETCH_DCELL_T(d_,lo,hi,t_)	({ (d_) = ((Double_Store)(lo)).t_; })
#define STORE_DCELL_T(d_,lo,hi,t_)	({ (lo) = ((Double_Store)(d_)).low; })

#endif /* SIZEOF_VOID_P == 4 */


#if defined(USE_THREADS) && defined(NATIVE_THREADS)

#define global_sp    (*(Cell **)&(THREADINFO->_global_sp))

#else /* defined(USE_THREADS) && defined(NATIVE_THREADS) */

#define MAX_STACK_SIZE 128*1024
static char stack[MAX_STACK_SIZE];

static Cell *_global_sp = (Cell *)(stack+MAX_STACK_SIZE);
#define global_sp    _global_sp

#endif /* defined(USE_THREADS) && defined(NATIVE_THREADS) */

#define CLEAR_global_sp (global_sp=NULL)


#define vm_twoCell2l(hi,lo,d_)  FETCH_DCELL_T(d_,lo,hi,l);
#define vm_twoCell2d(hi,lo,d_)  FETCH_DCELL_T(d_,lo,hi,d);
					   							 
#define vm_l2twoCell(d_,hi,lo)  STORE_DCELL_T(d_,lo,hi,l);
#define vm_d2twoCell(d_,hi,lo)  STORE_DCELL_T(d_,lo,hi,d);

#define vm_Cell2v(cell, v) ((v)=(Cell)(cell))
#define vm_Cell2b(cell, b) ((b)=(u1)(Cell)(cell))
#define vm_Cell2i(cell, i) ((i)=(s4)(Cell)(cell))

#define vm_Cell2aRef(x1,x2)       ((x2) = (java_objectheader *)(x1))
#define vm_Cell2aArray(x1,x2)     ((x2) = (java_arrayheader * )(x1))
#define vm_Cell2aaTarget(x1,x2)   ((x2) = (Inst **            )(x1))
#define vm_Cell2aClass(x1,x2)     ((x2) = (classinfo *        )(x1))
#define vm_Cell2acr(x1,x2)        ((x2) = (constant_classref *)(x1))
#define vm_Cell2addr(x1,x2)       ((x2) = (u1 *               )(x1))
#define vm_Cell2af(x1,x2)         ((x2) = (functionptr        )(x1))
#define vm_Cell2am(x1,x2)         ((x2) = (methodinfo *       )(x1))
#define vm_Cell2acell(x1,x2)      ((x2) = (Cell *             )(x1))
#define vm_Cell2ainst(x1,x2)      ((x2) = (Inst *             )(x1))
#define vm_Cell2auf(x1,x2)        ((x2) = (unresolved_field * )(x1))
#define vm_Cell2aum(x1,x2)        ((x2) = (unresolved_method *)(x1))
#define vm_Cell2avftbl(x1,x2)     ((x2) = (vftbl_t *          )(x1))

#define vm_ui2Cell(x1,x2) ((x2) = (Cell)(x1))
#define vm_v2Cell(x1,x2) ((x2) = (Cell)(x1))
#define vm_b2Cell(x1,x2) ((x2) = (Cell)(x1))
#define vm_s2Cell(x1,x2) ((x2) = (Cell)(x1))
#define vm_i2Cell(x1,x2) ((x2) = (Cell)(x1))
#define vm_aRef2Cell(x1,x2) ((x2) = (Cell)(x1))
#define vm_aArray2Cell(x1,x2) ((x2) = (Cell)(x1))
#define vm_aaTarget2Cell(x1,x2) ((x2) = (Cell)(x1))
#define vm_aClass2Cell(x1,x2) ((x2) = (Cell)(x1))
#define vm_acr2Cell(x1,x2) ((x2) = (Cell)(x1))
#define vm_addr2Cell(x1,x2) ((x2) = (Cell)(x1))
#define vm_af2Cell(x1,x2) ((x2) = (Cell)(x1))
#define vm_am2Cell(x1,x2) ((x2) = (Cell)(x1))
#define vm_acell2Cell(x1,x2) ((x2) = (Cell)(x1))
#define vm_ainst2Cell(x1,x2) ((x2) = (Cell)(x1))
#define vm_auf2Cell(x1,x2) ((x2) = (Cell)(x1))
#define vm_aum2Cell(x1,x2) ((x2) = (Cell)(x1))
#define vm_avftbl2Cell(x1,x2) ((x2) = (Cell)(x1))

#define vm_Cell2Cell(x1,x2) ((x2)=(Cell)(x1))

#define IMM_ARG(access,value)		(access)

/* for disassembler and tracer */
#define VM_IS_INST(inst, n) ((inst) == vm_prim[n])


#define gen_BBSTART (cd->lastmcodeptr = NULL, append_dispatch(cd))


union Cell_float {
    Cell cell;
    float f;
};


#define access_local_int(_offset) \
        ( *(Cell*)(((u1 *)fp) + (_offset)) )

#define access_local_ref(_offset) \
        ( *(void **)(((u1 *)fp) + (_offset)) )

#define access_local_cell(_offset) \
        ( *(Cell *)(((u1 *)fp) + (_offset)) )


typedef struct block_count block_count;

#define vm_f2Cell(x1,x2)	((x2) =(((union Cell_float)(x1)).cell))
#define vm_Cell2f(x1,x2)	((x2) =(((union Cell_float)(x1)).f))

extern Inst *vm_prim;
extern Cell peeptable;
extern Inst *last_compiled;
extern FILE *vm_out;

void init_peeptable(void);
Inst peephole_opt(Inst inst1, Inst inst2, Cell peeptable);
void check_prims(Label symbols1[]);
void gen_inst(codegendata *cd, u4 instr);

void vm_disassemble(Inst *ip, Inst *endp, Inst vm_prim[]);
Inst *vm_disassemble_inst(Inst *ip, Inst vm_prim[]);

java_objectheader *engine(Inst *ip0, Cell * sp, Cell * fp);
ffi_type *cacaotype2ffitype(s4 cacaotype);

/* print types for disassembler and tracer */
void printarg_ui      (u4                 ui      );
void printarg_v       (Cell               v       );
void printarg_b       (s4                 b       );
void printarg_s       (s4                 s       );
void printarg_i       (s4                 i       );
void printarg_l       (s8                 l       );
void printarg_f       (float              f       );
void printarg_d       (double             d       );
void printarg_aRef    (java_objectheader *aRef    );
void printarg_aArray  (java_arrayheader * aArray  );
void printarg_aaTarget(Inst **            aaTarget);
void printarg_aClass  (classinfo *        aClass  );
void printarg_acr     (constant_classref *acr     );
void printarg_addr    (u1 *               addr    );
void printarg_af      (functionptr        af      );
void printarg_am      (methodinfo *       am      );
void printarg_acell   (Cell *             acell   );
void printarg_ainst   (Inst *             ainst   );
void printarg_auf     (unresolved_field * auf     );
void printarg_aum     (unresolved_method *aum     );
void printarg_avftbl  (vftbl_t *          avftbl  );
void printarg_Cell    (Cell               x       );

void vm_uncount_block(Inst *ip);
block_count *vm_block_insert(Inst *ip);

Cell *nativecall(functionptr f, methodinfo *m, Cell *sp, Inst *ra, Cell *fp, u1 *addrcif);
u1 *createcalljavafunction(methodinfo *m);

Inst *asm_handle_exception(Inst *ip, java_objectheader *o, Cell *fp, Cell **new_spp, Cell **new_fpp);

#endif /* _INTRP_H */


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
