/* builtin.h - prototypes of builtin functions

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

   Authors: Reinhard Grafl

   $Id: builtin.h 727 2003-12-11 10:52:40Z edwin $

*/


#ifndef _BUILTIN_H
#define _BUILTIN_H


/* define infinity for floating point numbers */

#define FLT_NAN     0x7fc00000
#define FLT_POSINF  0x7f800000
#define FLT_NEGINF  0xff800000

/* define infinity for double floating point numbers */

#define DBL_NAN     0x7ff8000000000000LL
#define DBL_POSINF  0x7ff0000000000000LL
#define DBL_NEGINF  0xfff0000000000000LL

/* XXX delete */
#if 0
typedef struct builtin_descriptor {
	functionptr bptr;
	char        *name;
	} builtin_descriptor;
#endif

typedef struct builtin_descriptor builtin_descriptor;

struct builtin_descriptor {
	int         opcode;
	functionptr builtin;
	int         icmd;
	u1          type_s1;
	u1          type_s2;
	u1          type_s3;
	u1          type_d;
	bool        supported;
	bool        isfloat;
	char        *name;
};

extern builtin_descriptor builtin_desc[];
extern int builtintablelen;

extern java_objectheader* exceptionptr;


/**********************************************************************/
/* BUILTIN FUNCTIONS                                                  */
/**********************************************************************/

/* NOTE: Builtin functions which are used in the BUILTIN* opcodes must
 * have a BUILTIN_... macro defined as seen below. In code dealing
 * with the BUILTIN* opcodes the functions may only be addressed by
 * these macros, never by their actual name! (This helps to make this
 * code more portable.)
 *
 * C and assembler code which does not deal with the BUILTIN* opcodes,
 * can use the builtin functions normally (like all other functions).
 *
 * IMPORTANT:
 * For each builtin function which is used in a BUILTIN* opcode there
 * must be an entry in the builtin_desc table in builtin.c!
 *
 * Below each prototype is either the BUILTIN_ macro definition or a
 * comment specifiying that this function is not used in BUILTIN*
 * opcodes.
 *
 * (The BUILTIN* opcodes are ICMD_BUILTIN1, ICMD_BUILTIN2 and
 * ICMD_BUILTIN3.)
 */

s4 builtin_instanceof(java_objectheader *obj, classinfo *class);
#define BUILTIN_instanceof (functionptr) builtin_instanceof
s4 builtin_isanysubclass (classinfo *sub, classinfo *super);
/* NOT AN OP */
s4 builtin_isanysubclass_vftbl (vftbl *sub, vftbl *super);
/* NOT AN OP */
s4 builtin_checkcast(java_objectheader *obj, classinfo *class);
/* NOT AN OP */
s4 builtin_arrayinstanceof(java_objectheader *obj, vftbl *target);
#define BUILTIN_arrayinstanceof (functionptr) builtin_arrayinstanceof

#if defined(__I386__)
s4 asm_builtin_arrayinstanceof(java_objectheader *obj, classinfo *class); /* XXX ? */
#undef  BUILTIN_arrayinstanceof
#define BUILTIN_arrayinstanceof (functionptr) asm_builtin_arrayinstanceof
#endif

s4 builtin_checkarraycast(java_objectheader *obj, vftbl *target);
/* NOT AN OP */
s4 asm_builtin_checkarraycast(java_objectheader *obj, vftbl *target);
#define BUILTIN_checkarraycast (functionptr) asm_builtin_checkarraycast

java_objectheader *builtin_throw_exception(java_objectheader *exception);
/* NOT AN OP */
java_objectheader *builtin_trace_exception(java_objectheader *exceptionptr,
										   methodinfo *method, 
										   int *pos, int noindent);
/* NOT AN OP */

java_objectheader *builtin_new(classinfo *c);
#define BUILTIN_new (functionptr) builtin_new

java_arrayheader *builtin_newarray(s4 size, vftbl *arrayvftbl);
#define BUILTIN_newarray (functionptr) builtin_newarray
java_objectarray *builtin_anewarray(s4 size, classinfo *component);
/* NOT AN OP */

#if defined(__I386__)
void asm_builtin_newarray(s4 size, vftbl *arrayvftbl);
#undef  BUILTIN_newarray
#define BUILTIN_newarray (functionptr) asm_builtin_newarray
#endif

java_booleanarray *builtin_newarray_boolean(s4 size);
#define BUILTIN_newarray_boolean (functionptr) builtin_newarray_boolean
java_chararray *builtin_newarray_char(s4 size);
#define BUILTIN_newarray_char (functionptr) builtin_newarray_char
java_floatarray *builtin_newarray_float(s4 size);
#define BUILTIN_newarray_float (functionptr) builtin_newarray_float
java_doublearray *builtin_newarray_double(s4 size);
#define BUILTIN_newarray_double (functionptr) builtin_newarray_double
java_bytearray *builtin_newarray_byte(s4 size);
#define BUILTIN_newarray_byte (functionptr) builtin_newarray_byte
java_shortarray *builtin_newarray_short(s4 size);
#define BUILTIN_newarray_short (functionptr) builtin_newarray_short
java_intarray *builtin_newarray_int(s4 size);
#define BUILTIN_newarray_int (functionptr) builtin_newarray_int
java_longarray *builtin_newarray_long(s4 size);
#define BUILTIN_newarray_long (functionptr) builtin_newarray_long
java_arrayheader *builtin_nmultianewarray(int n,
                                          vftbl *arrayvftbl, long *dims);
/* NOT AN OP */

s4 builtin_canstore(java_objectarray *a, java_objectheader *o);
/* NOT AN OP */
void asm_builtin_aastore(java_objectarray *a, s4 index, java_objectheader *o);
#define BUILTIN_aastore (functionptr) asm_builtin_aastore

#ifdef TRACE_ARGS_NUM
#if TRACE_ARGS_NUM == 6
void builtin_trace_args(s8 a0, s8 a1, s8 a2, s8 a3, s8 a4, s8 a5, methodinfo *method);
/* NOT AN OP */
#else
void builtin_trace_args(s8 a0, s8 a1, s8 a2, s8 a3, s8 a4, s8 a5, s8 a6, s8 a7, methodinfo *method);
/* NOT AN OP */
#endif
#endif
void builtin_displaymethodstart(methodinfo *method); /* XXX? */
/* NOT AN OP */
void builtin_displaymethodstop(methodinfo *method, s8 l, double d, float f);
/* NOT AN OP */
/* void builtin_displaymethodstop(methodinfo *method); */ /* XXX? */
void builtin_displaymethodexception(methodinfo *method); /* XXX? */
/* NOT AN OP */

void builtin_monitorenter(java_objectheader *o);
/* NOT AN OP */
void asm_builtin_monitorenter(java_objectheader *o);
#define BUILTIN_monitorenter (functionptr) asm_builtin_monitorenter
void builtin_monitorexit(java_objectheader *o);
/* NOT AN OP */
void asm_builtin_monitorexit(java_objectheader *o);
#define BUILTIN_monitorexit (functionptr) asm_builtin_monitorexit

s4 builtin_idiv(s4 a, s4 b);
/* NOT AN OP */
s4 asm_builtin_idiv(s4 a, s4 b);
#define BUILTIN_idiv (functionptr) asm_builtin_idiv
s4 builtin_irem(s4 a, s4 b);
/* NOT AN OP */
s4 asm_builtin_irem(s4 a, s4 b);
#define BUILTIN_irem (functionptr) asm_builtin_irem

s8 builtin_ladd(s8 a, s8 b);
#define BUILTIN_ladd (functionptr) builtin_ladd
s8 builtin_lsub(s8 a, s8 b);
#define BUILTIN_lsub (functionptr) builtin_lsub
s8 builtin_lmul(s8 a, s8 b);
#define BUILTIN_lmul (functionptr) builtin_lmul
s8 builtin_ldiv(s8 a, s8 b);
/* NOT AN OP */
s8 asm_builtin_ldiv(s8 a, s8 b);
#define BUILTIN_ldiv (functionptr) asm_builtin_ldiv
s8 builtin_lrem(s8 a, s8 b);
/* NOT AN OP */
s8 asm_builtin_lrem(s8 a, s8 b);
#define BUILTIN_lrem (functionptr) asm_builtin_lrem
s8 builtin_lshl(s8 a, s4 b);
#define BUILTIN_lshl (functionptr) builtin_lshl
s8 builtin_lshr(s8 a, s4 b);
#define BUILTIN_lshr (functionptr) builtin_lshr
s8 builtin_lushr(s8 a, s4 b);
#define BUILTIN_lushr (functionptr) builtin_lushr
s8 builtin_land(s8 a, s8 b);
#define BUILTIN_land (functionptr) builtin_land
s8 builtin_lor(s8 a, s8 b);
#define BUILTIN_lor (functionptr) builtin_lor
s8 builtin_lxor(s8 a, s8 b);
#define BUILTIN_lxor (functionptr) builtin_lxor
s8 builtin_lneg(s8 a);
#define BUILTIN_lneg (functionptr) builtin_lneg
s4 builtin_lcmp(s8 a, s8 b);
#define BUILTIN_lcmp (functionptr) builtin_lcmp

float builtin_fadd(float a, float b); /* XXX? */
/* NOT AN OP */
float builtin_fsub(float a, float b); /* XXX? */
/* NOT AN OP */
float builtin_fmul(float a, float b); /* XXX? */
/* NOT AN OP */
float builtin_fdiv(float a, float b); /* XXX? */
/* NOT AN OP */
float builtin_fneg(float a);          /* XXX? */
/* NOT AN OP */
s4 builtin_fcmpl(float a, float b);   /* XXX? */
/* NOT AN OP */
s4 builtin_fcmpg(float a, float b);   /* XXX? */
/* NOT AN OP */
float builtin_frem(float a, float b);
#define BUILTIN_frem (functionptr) builtin_frem

double builtin_dadd(double a, double b); /* XXX? */
/* NOT AN OP */
double builtin_dsub(double a, double b); /* XXX? */
/* NOT AN OP */
double builtin_dmul(double a, double b); /* XXX? */
/* NOT AN OP */
double builtin_ddiv(double a, double b); /* XXX? */
/* NOT AN OP */
double builtin_dneg(double a);           /* XXX? */ 
/* NOT AN OP */
s4 builtin_dcmpl(double a, double b);    /* XXX? */
/* NOT AN OP */
s4 builtin_dcmpg(double a, double b);    /* XXX? */
/* NOT AN OP */
double builtin_drem(double a, double b);
#define BUILTIN_drem (functionptr) builtin_drem

s8       builtin_i2l(s4 i);
/* NOT AN OP */
float    builtin_i2f(s4 i);
#define BUILTIN_i2f (functionptr) builtin_i2f
double   builtin_i2d(s4 i);
#define BUILTIN_i2d (functionptr) builtin_i2d
s4       builtin_l2i(s8 l);
/* NOT AN OP */
float    builtin_l2f(s8 l);
#define BUILTIN_l2f (functionptr) builtin_l2f
double   builtin_l2d(s8 l);
#define BUILTIN_l2d (functionptr) builtin_l2d

s4       builtin_f2i(float a);
#define BUILTIN_f2i (functionptr) builtin_f2i
s4       asm_builtin_f2i(float a);
/* NOT AN OP */
s8       builtin_f2l(float a);
#define BUILTIN_f2l (functionptr) builtin_f2l
s8       asm_builtin_f2l(float a);
/* NOT AN OP */

double   builtin_f2d(float a);     /* XXX? */
/* NOT AN OP */

s4       builtin_d2i(double a);
#define BUILTIN_d2i (functionptr) builtin_d2i
s4       asm_builtin_d2i(double a);
/* NOT AN OP */
s8       builtin_d2l(double a);
#define BUILTIN_d2l (functionptr) builtin_d2l
s8       asm_builtin_d2l(double a);
/* NOT AN OP */

float    builtin_d2f(double a);    /* XXX? */
/* NOT AN OP */

java_arrayheader *builtin_clone_array(void *env, java_arrayheader *o);
/* NOT AN OP */

/* conversion helper functions */

inline float intBitsToFloat(s4 i);
inline float longBitsToDouble(s8 l);

#endif /* _BUILTIN_H */


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
