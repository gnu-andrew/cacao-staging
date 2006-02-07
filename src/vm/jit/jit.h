/* src/vm/jit/jit.h - code generation header

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

   Authors: Andreas Krall
            Reinhard Grafl

   Changes: Christian Thalinger
   			Edwin Steiner

   $Id: jit.h 4476 2006-02-07 16:47:44Z edwin $

*/


#ifndef _JIT_H
#define _JIT_H

/* resolve typedef cycles *****************************************************/

typedef struct stackelement stackelement;
typedef stackelement *stackptr;
typedef struct basicblock basicblock;
typedef struct instruction instruction;
typedef struct subroutineinfo subroutineinfo;


#include "config.h"
#include "vm/types.h"

#include "toolbox/chain.h"
#include "vm/global.h"
#include "vm/method.h"
#include "vm/references.h"
#include "vm/statistics.h"
#include "vm/jit/codegen-common.h"
#include "vm/jit/verify/typeinfo.h"


/* common jit/codegen macros **************************************************/

#if defined(ENABLE_STATISTICS)
# define COUNT(x)        (x)++
# define COUNT_SPILLS    count_spills++
#else
# define COUNT(x)        /* nothing */
# define COUNT_SPILLS    /* nothing */
#endif


/************************** stack element structure ***************************/

/* slot types */

/* Unified these with longer names. Maybe someday use only
 * one set of names? -Edwin
 */
/*#define TYPE_INT   0*/               /* the stack slot types must numbered in the  */
#define TYPE_LNG   TYPE_LONG    /*1*/  /* same order as the ICMD_Ixxx to ICMD_Axxx   */
#define TYPE_FLT   TYPE_FLOAT   /*2*/  /* instructions (LOAD and STORE)              */
#define TYPE_DBL   TYPE_DOUBLE  /*3*/  /* integer, long, float, double, address      */
#define TYPE_ADR   TYPE_ADDRESS /*4*/

#define IS_INT_LNG_TYPE(a)      (!((a) & TYPE_FLT))
#define IS_FLT_DBL_TYPE(a)      ((a) & TYPE_FLT)
#define IS_2_WORD_TYPE(a)       ((a) & TYPE_LNG)
#define IS_ADR_TYPE(a)          ((a) & TYPE_ADR)


/* flags */

#define SAVEDVAR   1            /* variable has to survive method invocations */
#define INMEMORY   2            /* variable stored in memory                  */
#define SAVEDTMP   4            /* temporary variable using a saved register  */
#define TMPARG     8            /* temporary variable using a argument register  */

/* variable kinds */

#define UNDEFVAR   0            /* stack slot will become temp during regalloc*/
#define TEMPVAR    1            /* stack slot is temp register                */
#define STACKVAR   2            /* stack slot is numbered stack slot          */
#define LOCALVAR   3            /* stack slot is local variable               */
#define ARGVAR     4            /* stack slot is argument variable            */


struct stackelement {
	stackptr prev;              /* pointer to next element towards bottom     */
	s4       type;              /* slot type of stack element                 */
#ifdef ENABLE_VERIFIER
	typeinfo typeinfo;          /* info on reference types                    */
#endif
	s4       flags;             /* flags (SAVED, INMEMORY)                    */
	s4       varkind;           /* kind of variable or register               */
	s4       varnum;            /* number of variable                         */
	s4       regoff;            /* register number or memory offset           */
};


/**************************** instruction structure ***************************/

struct instruction {
	stackptr    dst;            /* stack index of destination operand stack   */
	u2          opc;            /* opcode of intermediate code command        */
	s4          op1;            /* first operand, usually variable number     */
	imm_union   val;            /* immediate constant                         */
	void       *target;         /* used for targets of branches and jumps     */
	                            /* and as address for list of targets for     */
	                            /* statements                                 */
	u2          line;           /* line number in source file                 */
	methodinfo *method;         /* needed for inlining. can't be done on      */
	                            /* basic block level, since an inlined        */
	                            /* function doesn't necessarily start         */
	                            /* a new block                                */
};

#define INSTRUCTION_PUTCONST_TYPE(iptr) \
	((iptr)[0].op1)

#define INSTRUCTION_PUTCONST_VALUE_ADR(iptr) \
	((iptr)[0].val.a)

#define INSTRUCTION_PUTCONST_FIELDINFO(iptr) \
	((fieldinfo *)((iptr)[1].val.a))

#define INSTRUCTION_PUTCONST_FIELDINFO_PTR(iptr) \
	((fieldinfo **) &((iptr)[1].val.a))

#define INSTRUCTION_PUTCONST_FIELDREF(iptr) \
	((unresolved_field *)((iptr)[1].target))


/* basicblock *****************************************************************/
 
/* flags */

#define BBDELETED            -2
#define BBUNDEF              -1
#define BBREACHED            0
#define BBFINISHED           1

#define BBTYPECHECK_UNDEF    2
#define BBTYPECHECK_REACHED  3

#define BBTYPE_STD           0  /* standard basic block type                  */
#define BBTYPE_EXH           1  /* exception handler basic block type         */
#define BBTYPE_SBR           2  /* subroutine basic block type                */


struct basicblock {
	s4           debug_nr;      /* basic block number                         */
	s4           flags;         /* used during stack analysis, init with -1   */
	s4           type;          /* basic block type (std, xhandler, subroutine*/
	instruction *iinstr;        /* pointer to intermediate code instructions  */
	s4           icount;        /* number of intermediate code instructions   */
	s4           mpc;           /* machine code pc at start of block          */
	stackptr     instack;       /* stack at begin of basic block              */
	stackptr     outstack;      /* stack at end of basic block                */
	s4           indepth;       /* stack depth at begin of basic block        */
	s4           outdepth;      /* stack depth end of basic block             */
	s4           pre_count;     /* count of predecessor basic blocks          */
	branchref   *branchrefs;    /* list of branches to be patched             */

	basicblock  *next;          /* used to build a BB list (instead of array) */
	s4           lflags;        /* used during loop copying, init with 0	  */
	basicblock  *copied_to;     /* points to the copy of this basic block	  */
                                /* when loop nodes are copied                 */
	stackptr     stack;         /* start of stack array for this block        */
};


/********** op1 values for ACONST instructions ********************************/

#define ACONST_LOAD     0  /* ACONST_NULL or LDC instruction                  */
#define ACONST_BUILTIN  1  /* constant argument for a builtin function call   */

/********** JavaVM operation codes (sorted) and instruction lengths ***********/

extern char *icmd_names[256];
extern char *opcode_names[256];
extern int jcommandsize[256];

#define JAVA_NOP               0
#define ICMD_NOP               0

#define JAVA_ACONST_NULL       1
#define ICMD_ACONST            1        /* val.a = constant                   */

#define JAVA_ICONST_M1         2
#define ICMD_CHECKNULL         2

#define JAVA_ICONST_0          3
#define ICMD_ICONST            3        /* val.i = constant                   */

#define JAVA_ICONST_1          4

#define JAVA_ICONST_2          5
#define ICMD_IDIVPOW2          5        /* val.i = constant                   */

#define JAVA_ICONST_3          6
#define ICMD_LDIVPOW2          6        /* val.l = constant                   */

#define JAVA_ICONST_4          7

#define JAVA_ICONST_5          8

#define JAVA_LCONST_0          9
#define ICMD_LCONST            9        /* val.l = constant                   */

#define JAVA_LCONST_1         10
#define ICMD_LCMPCONST        10        /* val.l = constant                   */

#define JAVA_FCONST_0         11
#define ICMD_FCONST           11        /* val.f = constant                   */

#define JAVA_FCONST_1         12

#define JAVA_FCONST_2         13
#define ICMD_ELSE_ICONST      13

#define JAVA_DCONST_0         14
#define ICMD_DCONST           14        /* val.d = constant                   */

#define JAVA_DCONST_1         15
#define ICMD_IFEQ_ICONST      15

#define JAVA_BIPUSH           16
#define ICMD_IFNE_ICONST      16

#define JAVA_SIPUSH           17
#define ICMD_IFLT_ICONST      17

#define JAVA_LDC1             18
#define ICMD_IFGE_ICONST      18

#define JAVA_LDC2             19
#define ICMD_IFGT_ICONST      19

#define JAVA_LDC2W            20
#define ICMD_IFLE_ICONST      20

	                                    /* order of LOAD instructions must be */
	                                    /* equal to order of TYPE_XXX defines */
#define JAVA_ILOAD            21
#define ICMD_ILOAD            21        /* op1 = local variable               */

#define JAVA_LLOAD            22
#define ICMD_LLOAD            22        /* op1 = local variable               */

#define JAVA_FLOAD            23
#define ICMD_FLOAD            23        /* op1 = local variable               */

#define JAVA_DLOAD            24
#define ICMD_DLOAD            24        /* op1 = local variable               */

#define JAVA_ALOAD            25
#define ICMD_ALOAD            25        /* op1 = local variable               */

#define JAVA_ILOAD_0          26
#define ICMD_IADDCONST        26        /* val.i = constant                   */

#define JAVA_ILOAD_1          27
#define ICMD_ISUBCONST        27        /* val.i = constant                   */

#define JAVA_ILOAD_2          28
#define ICMD_IMULCONST        28        /* val.i = constant                   */

#define JAVA_ILOAD_3          29
#define ICMD_IANDCONST        29        /* val.i = constant                   */

#define JAVA_LLOAD_0          30
#define ICMD_IORCONST         30        /* val.i = constant                   */

#define JAVA_LLOAD_1          31
#define ICMD_IXORCONST        31        /* val.i = constant                   */

#define JAVA_LLOAD_2          32
#define ICMD_ISHLCONST        32        /* val.i = constant                   */

#define JAVA_LLOAD_3          33
#define ICMD_ISHRCONST        33        /* val.i = constant                   */

#define JAVA_FLOAD_0          34
#define ICMD_IUSHRCONST       34        /* val.i = constant                   */

#define JAVA_FLOAD_1          35
#define ICMD_IREMPOW2         35        /* val.i = constant                   */

#define JAVA_FLOAD_2          36
#define ICMD_LADDCONST        36        /* val.l = constant                   */

#define JAVA_FLOAD_3          37
#define ICMD_LSUBCONST        37        /* val.l = constant                   */

#define JAVA_DLOAD_0          38
#define ICMD_LMULCONST        38        /* val.l = constant                   */

#define JAVA_DLOAD_1          39
#define ICMD_LANDCONST        39        /* val.l = constant                   */

#define JAVA_DLOAD_2          40
#define ICMD_LORCONST         40        /* val.l = constant                   */

#define JAVA_DLOAD_3          41
#define ICMD_LXORCONST        41        /* val.l = constant                   */

#define JAVA_ALOAD_0          42
#define ICMD_LSHLCONST        42        /* val.l = constant                   */

#define JAVA_ALOAD_1          43
#define ICMD_LSHRCONST        43        /* val.l = constant                   */

#define JAVA_ALOAD_2          44
#define ICMD_LUSHRCONST       44        /* val.l = constant                   */

#define JAVA_ALOAD_3          45
#define ICMD_LREMPOW2         45        /* val.l = constant                   */

#define JAVA_IALOAD           46
#define ICMD_IALOAD           46

#define JAVA_LALOAD           47
#define ICMD_LALOAD           47

#define JAVA_FALOAD           48
#define ICMD_FALOAD           48

#define JAVA_DALOAD           49
#define ICMD_DALOAD           49

#define JAVA_AALOAD           50
#define ICMD_AALOAD           50

#define JAVA_BALOAD           51
#define ICMD_BALOAD           51

#define JAVA_CALOAD           52
#define ICMD_CALOAD           52

#define JAVA_SALOAD           53
#define ICMD_SALOAD           53

	                                    /* order of STORE instructions must be*/
	                                    /* equal to order of TYPE_XXX defines */
#define JAVA_ISTORE           54
#define ICMD_ISTORE           54        /* op1 = local variable               */

#define JAVA_LSTORE           55
#define ICMD_LSTORE           55        /* op1 = local variable               */

#define JAVA_FSTORE           56
#define ICMD_FSTORE           56        /* op1 = local variable               */

#define JAVA_DSTORE           57
#define ICMD_DSTORE           57        /* op1 = local variable               */

#define JAVA_ASTORE           58
#define ICMD_ASTORE           58        /* op1 = local variable               */

#define JAVA_ISTORE_0         59
#define ICMD_IF_LEQ           59        /* op1 = target JavaVM pc, val.l      */

#define JAVA_ISTORE_1         60
#define ICMD_IF_LNE           60        /* op1 = target JavaVM pc, val.l      */

#define JAVA_ISTORE_2         61
#define ICMD_IF_LLT           61        /* op1 = target JavaVM pc, val.l      */

#define JAVA_ISTORE_3         62
#define ICMD_IF_LGE           62        /* op1 = target JavaVM pc, val.l      */

#define JAVA_LSTORE_0         63
#define ICMD_IF_LGT           63        /* op1 = target JavaVM pc, val.l      */

#define JAVA_LSTORE_1         64
#define ICMD_IF_LLE           64        /* op1 = target JavaVM pc, val.l      */

#define JAVA_LSTORE_2         65
#define ICMD_IF_LCMPEQ        65        /* op1 = target JavaVM pc             */

#define JAVA_LSTORE_3         66
#define ICMD_IF_LCMPNE        66        /* op1 = target JavaVM pc             */

#define JAVA_FSTORE_0         67
#define ICMD_IF_LCMPLT        67        /* op1 = target JavaVM pc             */

#define JAVA_FSTORE_1         68
#define ICMD_IF_LCMPGE        68        /* op1 = target JavaVM pc             */

#define JAVA_FSTORE_2         69
#define ICMD_IF_LCMPGT        69        /* op1 = target JavaVM pc             */

#define JAVA_FSTORE_3         70
#define ICMD_IF_LCMPLE        70        /* op1 = target JavaVM pc             */

#define JAVA_DSTORE_0         71

#define JAVA_DSTORE_1         72

#define JAVA_DSTORE_2         73

#define JAVA_DSTORE_3         74

#define JAVA_ASTORE_0         75

#define JAVA_ASTORE_1         76

#define JAVA_ASTORE_2         77

#define JAVA_ASTORE_3         78

#define JAVA_IASTORE          79
#define ICMD_IASTORE          79

#define JAVA_LASTORE          80
#define ICMD_LASTORE          80

#define JAVA_FASTORE          81
#define ICMD_FASTORE          81

#define JAVA_DASTORE          82
#define ICMD_DASTORE          82

#define JAVA_AASTORE          83
#define ICMD_AASTORE          83

#define JAVA_BASTORE          84
#define ICMD_BASTORE          84

#define JAVA_CASTORE          85
#define ICMD_CASTORE          85

#define JAVA_SASTORE          86
#define ICMD_SASTORE          86

#define JAVA_POP              87
#define ICMD_POP              87

#define JAVA_POP2             88
#define ICMD_POP2             88

#define JAVA_DUP              89
#define ICMD_DUP              89

#define JAVA_DUP_X1           90
#define ICMD_DUP_X1           90

#define JAVA_DUP_X2           91
#define ICMD_DUP_X2           91

#define JAVA_DUP2             92
#define ICMD_DUP2             92

#define JAVA_DUP2_X1          93
#define ICMD_DUP2_X1          93

#define JAVA_DUP2_X2          94
#define ICMD_DUP2_X2          94

#define JAVA_SWAP             95
#define ICMD_SWAP             95

#define JAVA_IADD             96
#define ICMD_IADD             96

#define JAVA_LADD             97
#define ICMD_LADD             97

#define JAVA_FADD             98
#define ICMD_FADD             98

#define JAVA_DADD             99
#define ICMD_DADD             99

#define JAVA_ISUB             100
#define ICMD_ISUB             100

#define JAVA_LSUB             101
#define ICMD_LSUB             101

#define JAVA_FSUB             102
#define ICMD_FSUB             102

#define JAVA_DSUB             103
#define ICMD_DSUB             103

#define JAVA_IMUL             104
#define ICMD_IMUL             104

#define JAVA_LMUL             105
#define ICMD_LMUL             105

#define JAVA_FMUL             106
#define ICMD_FMUL             106

#define JAVA_DMUL             107
#define ICMD_DMUL             107

#define JAVA_IDIV             108
#define ICMD_IDIV             108

#define JAVA_LDIV             109
#define ICMD_LDIV             109

#define JAVA_FDIV             110
#define ICMD_FDIV             110

#define JAVA_DDIV             111
#define ICMD_DDIV             111

#define JAVA_IREM             112
#define ICMD_IREM             112

#define JAVA_LREM             113
#define ICMD_LREM             113

#define JAVA_FREM             114
#define ICMD_FREM             114

#define JAVA_DREM             115
#define ICMD_DREM             115

#define JAVA_INEG             116
#define ICMD_INEG             116

#define JAVA_LNEG             117
#define ICMD_LNEG             117

#define JAVA_FNEG             118
#define ICMD_FNEG             118

#define JAVA_DNEG             119
#define ICMD_DNEG             119

#define JAVA_ISHL             120
#define ICMD_ISHL             120

#define JAVA_LSHL             121
#define ICMD_LSHL             121

#define JAVA_ISHR             122
#define ICMD_ISHR             122

#define JAVA_LSHR             123
#define ICMD_LSHR             123

#define JAVA_IUSHR            124
#define ICMD_IUSHR            124

#define JAVA_LUSHR            125
#define ICMD_LUSHR            125

#define JAVA_IAND             126
#define ICMD_IAND             126

#define JAVA_LAND             127
#define ICMD_LAND             127

#define JAVA_IOR              128
#define ICMD_IOR              128

#define JAVA_LOR              129
#define ICMD_LOR              129

#define JAVA_IXOR             130
#define ICMD_IXOR             130

#define JAVA_LXOR             131
#define ICMD_LXOR             131

#define JAVA_IINC             132
#define ICMD_IINC             132   /* op1 = local variable, val.i = constant */

#define JAVA_I2L              133
#define ICMD_I2L              133

#define JAVA_I2F              134
#define ICMD_I2F              134

#define JAVA_I2D              135
#define ICMD_I2D              135

#define JAVA_L2I              136
#define ICMD_L2I              136

#define JAVA_L2F              137
#define ICMD_L2F              137

#define JAVA_L2D              138
#define ICMD_L2D              138

#define JAVA_F2I              139
#define ICMD_F2I              139

#define JAVA_F2L              140
#define ICMD_F2L              140

#define JAVA_F2D              141
#define ICMD_F2D              141

#define JAVA_D2I              142
#define ICMD_D2I              142

#define JAVA_D2L              143
#define ICMD_D2L              143

#define JAVA_D2F              144
#define ICMD_D2F              144

#define JAVA_INT2BYTE         145
#define ICMD_INT2BYTE         145

#define JAVA_INT2CHAR         146
#define ICMD_INT2CHAR         146

#define JAVA_INT2SHORT        147
#define ICMD_INT2SHORT        147

#define JAVA_LCMP             148
#define ICMD_LCMP             148

#define JAVA_FCMPL            149
#define ICMD_FCMPL            149

#define JAVA_FCMPG            150
#define ICMD_FCMPG            150

#define JAVA_DCMPL            151
#define ICMD_DCMPL            151

#define JAVA_DCMPG            152
#define ICMD_DCMPG            152

#define JAVA_IFEQ             153
#define ICMD_IFEQ             153       /* op1 = target JavaVM pc, val.i      */

#define JAVA_IFNE             154
#define ICMD_IFNE             154       /* op1 = target JavaVM pc, val.i      */

#define JAVA_IFLT             155
#define ICMD_IFLT             155       /* op1 = target JavaVM pc, val.i      */

#define JAVA_IFGE             156
#define ICMD_IFGE             156       /* op1 = target JavaVM pc, val.i      */

#define JAVA_IFGT             157
#define ICMD_IFGT             157       /* op1 = target JavaVM pc, val.i      */

#define JAVA_IFLE             158
#define ICMD_IFLE             158       /* op1 = target JavaVM pc, val.i      */

#define JAVA_IF_ICMPEQ        159
#define ICMD_IF_ICMPEQ        159       /* op1 = target JavaVM pc             */

#define JAVA_IF_ICMPNE        160
#define ICMD_IF_ICMPNE        160       /* op1 = target JavaVM pc             */

#define JAVA_IF_ICMPLT        161
#define ICMD_IF_ICMPLT        161       /* op1 = target JavaVM pc             */

#define JAVA_IF_ICMPGE        162
#define ICMD_IF_ICMPGE        162       /* op1 = target JavaVM pc             */

#define JAVA_IF_ICMPGT        163
#define ICMD_IF_ICMPGT        163       /* op1 = target JavaVM pc             */

#define JAVA_IF_ICMPLE        164
#define ICMD_IF_ICMPLE        164       /* op1 = target JavaVM pc             */

#define JAVA_IF_ACMPEQ        165
#define ICMD_IF_ACMPEQ        165       /* op1 = target JavaVM pc             */

#define JAVA_IF_ACMPNE        166
#define ICMD_IF_ACMPNE        166       /* op1 = target JavaVM pc             */

#define JAVA_GOTO             167
#define ICMD_GOTO             167       /* op1 = target JavaVM pc             */

#define JAVA_JSR              168
#define ICMD_JSR              168       /* op1 = target JavaVM pc             */

#define JAVA_RET              169
#define ICMD_RET              169       /* op1 = local variable               */

#define JAVA_TABLESWITCH      170
#define ICMD_TABLESWITCH      170       /* val.a = pointer to s4 table        */
                                        /* length must be computed            */
#define JAVA_LOOKUPSWITCH     171
#define ICMD_LOOKUPSWITCH     171       /* val.a = pointer to s4 table        */
                                        /* length must be computed            */
#define JAVA_IRETURN          172
#define ICMD_IRETURN          172

#define JAVA_LRETURN          173
#define ICMD_LRETURN          173

#define JAVA_FRETURN          174
#define ICMD_FRETURN          174

#define JAVA_DRETURN          175
#define ICMD_DRETURN          175

#define JAVA_ARETURN          176
#define ICMD_ARETURN          176

#define JAVA_RETURN           177
#define ICMD_RETURN           177

#define JAVA_GETSTATIC        178
#define ICMD_GETSTATIC        178       /* op1 = type, val.a = field address  */

#define JAVA_PUTSTATIC        179
#define ICMD_PUTSTATIC        179       /* op1 = type, val.a = field address  */

#define JAVA_GETFIELD         180
#define ICMD_GETFIELD         180       /* op1 = type, val.i = field offset   */

#define JAVA_PUTFIELD         181
#define ICMD_PUTFIELD         181       /* op1 = type, val.i = field offset   */

#define JAVA_INVOKEVIRTUAL    182
#define ICMD_INVOKEVIRTUAL    182       /* val.a = method info pointer        */

#define JAVA_INVOKESPECIAL    183
#define ICMD_INVOKESPECIAL    183       /* val.a = method info pointer        */

#define JAVA_INVOKESTATIC     184
#define ICMD_INVOKESTATIC     184       /* val.a = method info pointer        */

#define JAVA_INVOKEINTERFACE  185
#define ICMD_INVOKEINTERFACE  185       /* val.a = method info pointer        */

/* UNDEF186 */

#define JAVA_NEW              187
#define ICMD_NEW              187       /* op1 = 1, val.a = class pointer     */

#define JAVA_NEWARRAY         188
#define ICMD_NEWARRAY         188       /* op1 = basic type                   */

#define JAVA_ANEWARRAY        189
#define ICMD_ANEWARRAY        189       /* op1 = 0, val.a = array pointer     */
                                        /* op1 = 1, val.a = class pointer     */
#define JAVA_ARRAYLENGTH      190
#define ICMD_ARRAYLENGTH      190

#define JAVA_ATHROW           191
#define ICMD_ATHROW           191

#define JAVA_CHECKCAST        192
#define ICMD_CHECKCAST        192       /* op1 = 0, val.a = array pointer     */
                                        /* op1 = 1, val.a = class pointer     */
#define JAVA_INSTANCEOF       193
#define ICMD_INSTANCEOF       193       /* op1 = 0, val.a = array pointer     */
                                        /* op1 = 1, val.a = class pointer     */
#define JAVA_MONITORENTER     194
#define ICMD_MONITORENTER     194

#define JAVA_MONITOREXIT      195
#define ICMD_MONITOREXIT      195

#define JAVA_WIDE             196

#define JAVA_MULTIANEWARRAY   197
#define ICMD_MULTIANEWARRAY   197       /* op1 = dimension, val.a = array     */
                                        /* pointer                            */
#define JAVA_IFNULL           198
#define ICMD_IFNULL           198       /* op1 = target JavaVM pc             */

#define JAVA_IFNONNULL        199
#define ICMD_IFNONNULL        199       /* op1 = target JavaVM pc             */

#define JAVA_GOTO_W           200

#define JAVA_JSR_W            201

#define JAVA_BREAKPOINT       202

/* UNDEF203 */

#define ICMD_IASTORECONST     204

#define ICMD_LASTORECONST     205

#define ICMD_FASTORECONST     206

#define ICMD_DASTORECONST     207

#define ICMD_AASTORECONST     208

#define ICMD_BASTORECONST     209

#define ICMD_CASTORECONST     210

#define ICMD_SASTORECONST     211

#define ICMD_PUTSTATICCONST   212

#define ICMD_PUTFIELDCONST    213

#define ICMD_IMULPOW2         214

#define ICMD_LMULPOW2         215

#define ICMD_INLINE_START     251       /* instruction before inlined method  */
#define ICMD_INLINE_END       252       /* instruction after inlined method   */
#define ICMD_INLINE_GOTO      253       /* jump to caller of inlined method   */

#define ICMD_BUILTIN          255       /* internal opcode                    */

#define ICMD_READONLY_ARG     1024      /* used for inlining, opcodes 1024-1028 are used */
#define ICMD_CLEAR_ARGREN     1029      /* indicates the start of a new inlined method argument renaming must be reset */


/******************* description of JavaVM instructions ***********************/



/***************************** register types *********************************/

#define REG_RES   0         /* reserved register for OS or code generator     */
#define REG_RET   1         /* return value register                          */
#define REG_EXC   2         /* exception value register                       */
#define REG_SAV   3         /* (callee) saved register                        */
#define REG_TMP   4         /* scratch temporary register (caller saved)      */
#define REG_ARG   5         /* argument register (caller saved)               */

#define REG_END   -1        /* last entry in tables                           */
 
#define PARAMMODE_NUMBERED  0 
#define PARAMMODE_STUFFED   1


/***************************** register info block ****************************/

extern int stackreq[256];


/* function prototypes ********************************************************/

/* compiler initialisation */
void jit_init(void);

/* compiler finalisation */
void jit_close(void);

/* compile a method with jit compiler */
u1 *jit_compile(methodinfo *m);

/* machine dependent initialization */
#if defined(ENABLE_JIT)
void md_init(void);
#endif

#if defined(ENABLE_INTRP)
void intrp_md_init(void);
#endif

#endif /* _JIT_H */


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
