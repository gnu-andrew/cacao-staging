/* alpha/ngen.h ****************************************************************

	Copyright (c) 1997 A. Krall, R. Grafl, M. Gschwind, M. Probst

	See file COPYRIGHT for information on usage and disclaimer of warranties

	Contains the machine dependent code generator definitions and macros for an
	Alpha processor.

	Authors: Andreas  Krall      EMAIL: cacao@complang.tuwien.ac.at
	         Reinhard Grafl      EMAIL: cacao@complang.tuwien.ac.at

	Last Change: 1998/08/10

*******************************************************************************/

/* preallocated registers *****************************************************/

/* integer registers */
  
#define REG_RESULT      0    /* to deliver method results                     */ 
#define REG_EXCEPTION  	1    /* to throw an exception across method bounds    */

#define REG_RA          26   /* return address                                */
#define REG_PV          27   /* procedure vector, must be provided by caller  */
#define REG_METHODPTR   28   /* pointer to the place from where the procedure */
                             /* vector has been fetched                       */
#define REG_ITMP1       25   /* temporary register                            */
#define REG_ITMP2       28   /* temporary register                            */
#define REG_ITMP3       29   /* temporary register                            */

#define REG_ITMP1_XPTR  25   /* exception pointer = temporary register 1      */
#define REG_ITMP2_XPC   28   /* exception pc = temporary register 2           */

#define REG_SP          30   /* stack pointer                                 */
#define REG_ZERO        31   /* allways zero                                  */

/* floating point registers */

#define REG_FRESULT     0    /* to deliver floating point method results      */ 
#define REG_FTMP1       28   /* temporary floating point register             */
#define REG_FTMP2       29   /* temporary floating point register             */
#define REG_FTMP3       30   /* temporary floating point register             */

#define REG_IFTMP       28   /* temporary integer and floating point register */

/* register descripton - array ************************************************/

/* #define REG_RES   0         reserved register for OS or code generator */
/* #define REG_RET   1         return value register */
/* #define REG_EXC   2         exception value register */
/* #define REG_SAV   3         (callee) saved register */
/* #define REG_TMP   4         scratch temporary register (caller saved) */
/* #define REG_ARG   5         argument register (caller saved) */

/* #define REG_END   -1        last entry in tables */
 
int nregdescint[] = {
	REG_RET, REG_TMP, REG_TMP, REG_TMP, REG_TMP, REG_TMP, REG_TMP, REG_TMP, 
	REG_TMP, REG_SAV, REG_SAV, REG_SAV, REG_SAV, REG_SAV, REG_SAV, REG_SAV, 
	REG_ARG, REG_ARG, REG_ARG, REG_ARG, REG_ARG, REG_ARG, REG_TMP, REG_TMP,
	REG_TMP, REG_RES, REG_RES, REG_RES, REG_RES, REG_RES, REG_RES, REG_RES,
	REG_END };

#define INT_SAV_CNT      7   /* number of int callee saved registers          */
#define INT_ARG_CNT      6   /* number of int argument registers              */

/* for use of reserved registers, see comment above */
	
int nregdescfloat[] = {
	REG_RET, REG_TMP, REG_SAV, REG_SAV, REG_SAV, REG_SAV, REG_SAV, REG_SAV,
	REG_SAV, REG_SAV, REG_TMP, REG_TMP, REG_TMP, REG_TMP, REG_TMP, REG_TMP, 
	REG_ARG, REG_ARG, REG_ARG, REG_ARG, REG_ARG, REG_ARG, REG_TMP, REG_TMP,
	REG_TMP, REG_TMP, REG_TMP, REG_TMP, REG_RES, REG_RES, REG_RES, REG_RES,
	REG_END };

#define FLT_SAV_CNT      8   /* number of flt callee saved registers          */
#define FLT_ARG_CNT      6   /* number of flt argument registers              */

/* for use of reserved registers, see comment above */


/* parameter allocation mode */

int nreg_parammode = PARAMMODE_NUMBERED;  

   /* parameter-registers will be allocated by assigning the
      1. parameter:   int/float-reg 16
      2. parameter:   int/float-reg 17  
      3. parameter:   int/float-reg 18 ....
   */


/* stackframe-infos ***********************************************************/

int parentargs_base; /* offset in stackframe for the parameter from the caller*/

/* -> see file 'calling.doc' */


/* macros to create code ******************************************************/

/* 3-address-operations: M_OP3
      op ..... opcode
      fu ..... function-number
      a  ..... register number source 1
      b  ..... register number or constant integer source 2
      c  ..... register number destination
      const .. switch to use b as constant integer 
                 (0 means: use b as register number)
                 (1 means: use b as constant 8-bit-integer)
*/      
#define M_OP3(op,fu,a,b,c,const) \
	*(mcodeptr++) = ( (((s4)(op))<<26)|((a)<<21)|((b)<<(16-3*(const)))| \
	((const)<<12)|((fu)<<5)|((c)) )

/* 3-address-floating-point-operation: M_FOP3 
     op .... opcode
     fu .... function-number
     a,b ... source floating-point registers
     c ..... destination register
*/ 
#define M_FOP3(op,fu,a,b,c) \
	*(mcodeptr++) = ( (((s4)(op))<<26)|((a)<<21)|((b)<<16)|((fu)<<5)|(c) )

/* branch instructions: M_BRA 
      op ..... opcode
      a ...... register to be tested
      disp ... relative address to be jumped to (divided by 4)
*/
#define M_BRA(op,a,disp) \
	*(mcodeptr++) = ( (((s4)(op))<<26)|((a)<<21)|((disp)&0x1fffff) )


/* memory operations: M_MEM
      op ..... opcode
      a ...... source/target register for memory access
      b ...... base register
      disp ... displacement (16 bit signed) to be added to b
*/ 
#define M_MEM(op,a,b,disp) \
	*(mcodeptr++) = ( (((s4)(op))<<26)|((a)<<21)|((b)<<16)|((disp)&0xffff) )


/* macros for all used commands (see an Alpha-manual for description) *********/ 

#define M_LDA(a,b,disp)         M_MEM (0x08,a,b,disp)           /* low const  */
#define M_LDAH(a,b,disp)        M_MEM (0x09,a,b,disp)           /* high const */
#define M_BLDU(a,b,disp)        M_MEM (0x0a,a,b,disp)           /*  8 load    */
#define M_SLDU(a,b,disp)        M_MEM (0x0c,a,b,disp)           /* 16 load    */
#define M_ILD(a,b,disp)         M_MEM (0x28,a,b,disp)           /* 32 load    */
#define M_LLD(a,b,disp)         M_MEM (0x29,a,b,disp)           /* 64 load    */
#define M_ALD(a,b,disp)         M_MEM (0x29,a,b,disp)           /* addr load  */
#define M_BST(a,b,disp)         M_MEM (0x0e,a,b,disp)           /*  8 store   */
#define M_SST(a,b,disp)         M_MEM (0x0d,a,b,disp)           /* 16 store   */
#define M_IST(a,b,disp)         M_MEM (0x2c,a,b,disp)           /* 32 store   */
#define M_LST(a,b,disp)         M_MEM (0x2d,a,b,disp)           /* 64 store   */
#define M_AST(a,b,disp)         M_MEM (0x2d,a,b,disp)           /* addr store */

#define M_BSEXT(b,c)            M_OP3 (0x1c,0x0,REG_ZERO,b,c,0) /*  8 signext */
#define M_SSEXT(b,c)            M_OP3 (0x1c,0x1,REG_ZERO,b,c,0) /* 16 signext */

#define M_BR(disp)              M_BRA (0x30,REG_ZERO,disp)      /* branch     */
#define M_BSR(ra,disp)          M_BRA (0x34,ra,disp)            /* branch sbr */
#define M_BEQZ(a,disp)          M_BRA (0x39,a,disp)             /* br a == 0  */
#define M_BLTZ(a,disp)          M_BRA (0x3a,a,disp)             /* br a <  0  */
#define M_BLEZ(a,disp)          M_BRA (0x3b,a,disp)             /* br a <= 0  */
#define M_BNEZ(a,disp)          M_BRA (0x3d,a,disp)             /* br a != 0  */
#define M_BGEZ(a,disp)          M_BRA (0x3e,a,disp)             /* br a >= 0  */
#define M_BGTZ(a,disp)          M_BRA (0x3f,a,disp)             /* br a >  0  */

#define M_JMP(a,b)              M_MEM (0x1a,a,b,0x0000)         /* jump       */
#define M_JSR(a,b)              M_MEM (0x1a,a,b,0x4000)         /* call sbr   */
#define M_RET(a,b)              M_MEM (0x1a,a,b,0x8000)         /* return     */

#define M_IADD(a,b,c,const)     M_OP3 (0x10,0x0,  a,b,c,const)  /* 32 add     */
#define M_LADD(a,b,c,const)     M_OP3 (0x10,0x20, a,b,c,const)  /* 64 add     */
#define M_ISUB(a,b,c,const)     M_OP3 (0x10,0x09, a,b,c,const)  /* 32 sub     */
#define M_LSUB(a,b,c,const)     M_OP3 (0x10,0x29, a,b,c,const)  /* 64 sub     */
#define M_IMUL(a,b,c,const)     M_OP3 (0x13,0x00, a,b,c,const)  /* 32 mul     */
#define M_LMUL(a,b,c,const)     M_OP3 (0x13,0x20, a,b,c,const)  /* 64 mul     */

#define M_CMPEQ(a,b,c,const)    M_OP3 (0x10,0x2d, a,b,c,const)  /* c = a == b */
#define M_CMPLT(a,b,c,const)    M_OP3 (0x10,0x4d, a,b,c,const)  /* c = a <  b */
#define M_CMPLE(a,b,c,const)    M_OP3 (0x10,0x6d, a,b,c,const)  /* c = a <= b */

#define M_CMPULE(a,b,c,const)   M_OP3 (0x10,0x3d, a,b,c,const)  /* c = a <= b */
#define M_CMPULT(a,b,c,const)   M_OP3 (0x10,0x1d, a,b,c,const)  /* c = a <= b */

#define M_AND(a,b,c,const)      M_OP3 (0x11,0x00, a,b,c,const)  /* c = a &  b */
#define M_OR(a,b,c,const)       M_OP3 (0x11,0x20, a,b,c,const)  /* c = a |  b */
#define M_XOR(a,b,c,const)      M_OP3 (0x11,0x40, a,b,c,const)  /* c = a ^  b */

#define M_MOV(a,c)              M_OR (a,a,c,0)                  /* c = a      */
#define M_CLR(c)                M_OR (31,31,c,0)                /* c = 0      */
#define M_NOP                   M_OR (31,31,31,0)               /* ;          */

#define M_SLL(a,b,c,const)      M_OP3 (0x12,0x39, a,b,c,const)  /* c = a << b */
#define M_SRA(a,b,c,const)      M_OP3 (0x12,0x3c, a,b,c,const)  /* c = a >> b */
#define M_SRL(a,b,c,const)      M_OP3 (0x12,0x34, a,b,c,const)  /* c = a >>>b */

#define M_FLD(a,b,disp)         M_MEM (0x22,a,b,disp)           /* load flt   */
#define M_DLD(a,b,disp)         M_MEM (0x23,a,b,disp)           /* load dbl   */
#define M_FST(a,b,disp)         M_MEM (0x26,a,b,disp)           /* store flt  */
#define M_DST(a,b,disp)         M_MEM (0x27,a,b,disp)           /* store dbl  */

#define M_FADD(a,b,c)           M_FOP3 (0x16, 0x080, a,b,c)     /* flt add    */
#define M_DADD(a,b,c)           M_FOP3 (0x16, 0x0a0, a,b,c)     /* dbl add    */
#define M_FSUB(a,b,c)           M_FOP3 (0x16, 0x081, a,b,c)     /* flt sub    */
#define M_DSUB(a,b,c)           M_FOP3 (0x16, 0x0a1, a,b,c)     /* dbl sub    */
#define M_FMUL(a,b,c)           M_FOP3 (0x16, 0x082, a,b,c)     /* flt mul    */
#define M_DMUL(a,b,c)           M_FOP3 (0x16, 0x0a2, a,b,c)     /* dbl mul    */
#define M_FDIV(a,b,c)           M_FOP3 (0x16, 0x083, a,b,c)     /* flt div    */
#define M_DDIV(a,b,c)           M_FOP3 (0x16, 0x0a3, a,b,c)     /* dbl div    */

#define M_FADDS(a,b,c)          M_FOP3 (0x16, 0x580, a,b,c)     /* flt add    */
#define M_DADDS(a,b,c)          M_FOP3 (0x16, 0x5a0, a,b,c)     /* dbl add    */
#define M_FSUBS(a,b,c)          M_FOP3 (0x16, 0x581, a,b,c)     /* flt sub    */
#define M_DSUBS(a,b,c)          M_FOP3 (0x16, 0x5a1, a,b,c)     /* dbl sub    */
#define M_FMULS(a,b,c)          M_FOP3 (0x16, 0x582, a,b,c)     /* flt mul    */
#define M_DMULS(a,b,c)          M_FOP3 (0x16, 0x5a2, a,b,c)     /* dbl mul    */
#define M_FDIVS(a,b,c)          M_FOP3 (0x16, 0x583, a,b,c)     /* flt div    */
#define M_DDIVS(a,b,c)          M_FOP3 (0x16, 0x5a3, a,b,c)     /* dbl div    */

#define M_CVTDF(a,b,c)          M_FOP3 (0x16, 0x0ac, a,b,c)     /* dbl2long   */
#define M_CVTLF(a,b,c)          M_FOP3 (0x16, 0x0bc, a,b,c)     /* long2flt   */
#define M_CVTLD(a,b,c)          M_FOP3 (0x16, 0x0be, a,b,c)     /* long2dbl   */
#define M_CVTDL(a,b,c)          M_FOP3 (0x16, 0x1af, a,b,c)     /* dbl2long   */
#define M_CVTDL_C(a,b,c)        M_FOP3 (0x16, 0x12f, a,b,c)     /* dbl2long   */
#define M_CVTLI(a,b)            M_FOP3 (0x17, 0x130, 31,a,b)    /* long2int   */

#define M_CVTDFS(a,b,c)         M_FOP3 (0x16, 0x5ac, a,b,c)     /* dbl2long   */
#define M_CVTDLS(a,b,c)         M_FOP3 (0x16, 0x5af, a,b,c)     /* dbl2long   */
#define M_CVTDL_CS(a,b,c)       M_FOP3 (0x16, 0x52f, a,b,c)     /* dbl2long   */
#define M_CVTLIS(a,b)           M_FOP3 (0x17, 0x530, 31,a,b)    /* long2int   */

#define M_FCMPEQ(a,b,c)         M_FOP3 (0x16, 0x0a5, a,b,c)     /* c = a==b   */
#define M_FCMPLT(a,b,c)         M_FOP3 (0x16, 0x0a6, a,b,c)     /* c = a<b    */

#define M_FCMPEQS(a,b,c)        M_FOP3 (0x16, 0x5a5, a,b,c)     /* c = a==b   */
#define M_FCMPLTS(a,b,c)        M_FOP3 (0x16, 0x5a6, a,b,c)     /* c = a<b    */

#define M_FMOV(fa,fb)           M_FOP3 (0x17, 0x020, fa,fa,fb)  /* b = a      */
#define M_FMOVN(fa,fb)          M_FOP3 (0x17, 0x021, fa,fa,fb)  /* b = -a     */

#define M_FNOP                  M_FMOV (31,31)

#define M_FBEQZ(fa,disp)        M_BRA (0x31,fa,disp)            /* br a == 0.0*/

/* macros for special commands (see an Alpha-manual for description) **********/ 

#define M_TRAPB                 M_MEM (0x18,0,0,0x0000)        /* trap barrier*/

#define M_S4ADDL(a,b,c,const)   M_OP3 (0x10,0x02, a,b,c,const) /* c = a*4 + b */
#define M_S4ADDQ(a,b,c,const)   M_OP3 (0x10,0x22, a,b,c,const) /* c = a*4 + b */
#define M_S4SUBL(a,b,c,const)   M_OP3 (0x10,0x0b, a,b,c,const) /* c = a*4 - b */
#define M_S4SUBQ(a,b,c,const)   M_OP3 (0x10,0x2b, a,b,c,const) /* c = a*4 - b */
#define M_S8ADDL(a,b,c,const)   M_OP3 (0x10,0x12, a,b,c,const) /* c = a*8 + b */
#define M_S8ADDQ(a,b,c,const)   M_OP3 (0x10,0x32, a,b,c,const) /* c = a*8 + b */
#define M_S8SUBL(a,b,c,const)   M_OP3 (0x10,0x1b, a,b,c,const) /* c = a*8 - b */
#define M_S8SUBQ(a,b,c,const)   M_OP3 (0x10,0x3b, a,b,c,const) /* c = a*8 - b */

#define M_LLD_U(a,b,disp)       M_MEM (0x0b,a,b,disp)          /* unalign ld  */
#define M_LST_U(a,b,disp)       M_MEM (0x0f,a,b,disp)          /* unalign st  */

#define M_ZAP(a,b,c,const)      M_OP3 (0x12,0x30, a,b,c,const)
#define M_ZAPNOT(a,b,c,const)   M_OP3 (0x12,0x31, a,b,c,const)
#define M_EXTBL(a,b,c,const)    M_OP3 (0x12,0x06, a,b,c,const)
#define M_EXTWL(a,b,c,const)    M_OP3 (0x12,0x16, a,b,c,const)
#define M_EXTLL(a,b,c,const)    M_OP3 (0x12,0x26, a,b,c,const)
#define M_EXTQL(a,b,c,const)    M_OP3 (0x12,0x36, a,b,c,const)
#define M_EXTWH(a,b,c,const)    M_OP3 (0x12,0x5a, a,b,c,const)
#define M_EXTLH(a,b,c,const)    M_OP3 (0x12,0x6a, a,b,c,const)
#define M_EXTQH(a,b,c,const)    M_OP3 (0x12,0x7a, a,b,c,const)
#define M_INSBL(a,b,c,const)    M_OP3 (0x12,0x0b, a,b,c,const)
#define M_INSWL(a,b,c,const)    M_OP3 (0x12,0x1b, a,b,c,const)
#define M_INSLL(a,b,c,const)    M_OP3 (0x12,0x2b, a,b,c,const)
#define M_INSQL(a,b,c,const)    M_OP3 (0x12,0x3b, a,b,c,const)
#define M_INSWH(a,b,c,const)    M_OP3 (0x12,0x57, a,b,c,const)
#define M_INSLH(a,b,c,const)    M_OP3 (0x12,0x67, a,b,c,const)
#define M_INSQH(a,b,c,const)    M_OP3 (0x12,0x77, a,b,c,const)
#define M_MSKBL(a,b,c,const)    M_OP3 (0x12,0x02, a,b,c,const)
#define M_MSKWL(a,b,c,const)    M_OP3 (0x12,0x12, a,b,c,const)
#define M_MSKLL(a,b,c,const)    M_OP3 (0x12,0x22, a,b,c,const)
#define M_MSKQL(a,b,c,const)    M_OP3 (0x12,0x32, a,b,c,const)
#define M_MSKWH(a,b,c,const)    M_OP3 (0x12,0x52, a,b,c,const)
#define M_MSKLH(a,b,c,const)    M_OP3 (0x12,0x62, a,b,c,const)
#define M_MSKQH(a,b,c,const)    M_OP3 (0x12,0x72, a,b,c,const)

#define M_UMULH(a,b,c,const)    M_OP3 (0x13,0x30, a,b,c,const)  /* 64 umulh   */

/* macros for unused commands (see an Alpha-manual for description) ***********/ 

#define M_ANDNOT(a,b,c,const)   M_OP3 (0x11,0x08, a,b,c,const) /* c = a &~ b  */
#define M_ORNOT(a,b,c,const)    M_OP3 (0x11,0x28, a,b,c,const) /* c = a |~ b  */
#define M_XORNOT(a,b,c,const)   M_OP3 (0x11,0x48, a,b,c,const) /* c = a ^~ b  */

#define M_CMOVEQ(a,b,c,const)   M_OP3 (0x11,0x24, a,b,c,const) /* a==0 ? c=b  */
#define M_CMOVNE(a,b,c,const)   M_OP3 (0x11,0x26, a,b,c,const) /* a!=0 ? c=b  */
#define M_CMOVLT(a,b,c,const)   M_OP3 (0x11,0x44, a,b,c,const) /* a< 0 ? c=b  */
#define M_CMOVGE(a,b,c,const)   M_OP3 (0x11,0x46, a,b,c,const) /* a>=0 ? c=b  */
#define M_CMOVLE(a,b,c,const)   M_OP3 (0x11,0x64, a,b,c,const) /* a<=0 ? c=b  */
#define M_CMOVGT(a,b,c,const)   M_OP3 (0x11,0x66, a,b,c,const) /* a> 0 ? c=b  */

#define M_CMPBGE(a,b,c,const)   M_OP3 (0x10,0x0f, a,b,c,const)

#define M_FCMPUN(a,b,c)         M_FOP3 (0x16, 0x0a4, a,b,c)    /* unordered   */
#define M_FCMPLE(a,b,c)         M_FOP3 (0x16, 0x0a7, a,b,c)    /* c = a<=b    */

#define M_FCMPUNS(a,b,c)        M_FOP3 (0x16, 0x5a4, a,b,c)    /* unordered   */
#define M_FCMPLES(a,b,c)        M_FOP3 (0x16, 0x5a7, a,b,c)    /* c = a<=b    */

#define M_FBNEZ(fa,disp)        M_BRA (0x35,fa,disp)
#define M_FBLEZ(fa,disp)        M_BRA (0x33,fa,disp)

#define M_JMP_CO(a,b)           M_MEM (0x1a,a,b,0xc000)        /* call cosub  */


/* function gen_resolvebranch **************************************************

	backpatches a branch instruction; Alpha branch instructions are very
	regular, so it is only necessary to overwrite some fixed bits in the
	instruction.

	parameters: ip ... pointer to instruction after branch (void*)
	            so ... offset of instruction after branch  (s4)
	            to ... offset of branch target             (s4)

*******************************************************************************/

#define gen_resolvebranch(ip,so,to) ((s4*)(ip))[-1]|=((s4)(to)-(so))>>2&0x1fffff

#define SOFTNULLPTRCHECK
