/* i386/ngen.c *****************************************************************

	Copyright (c) 1997 A. Krall, R. Grafl, M. Gschwind, M. Probst

	See file COPYRIGHT for information on usage and disclaimer of warranties

	Contains the codegenerator for an i386 processor.
	This module generates i386 machine code for a sequence of
	pseudo commands (ICMDs).

	Authors: Andreas  Krall      EMAIL: cacao@complang.tuwien.ac.at
	         Reinhard Grafl      EMAIL: cacao@complang.tuwien.ac.at

	Last Change: $Id: ngen.c 264 2003-03-30 01:07:33Z twisti $

*******************************************************************************/

#include "jitdef.h"   /* phil */

/* additional functions and macros to generate code ***************************/

/* #define BlockPtrOfPC(pc)        block+block_index[pc] */
#define BlockPtrOfPC(pc)  ((basicblock *) iptr->target)


#ifdef STATISTICS
#define COUNT_SPILLS count_spills++
#else
#define COUNT_SPILLS
#endif


#define CALCOFFSETBYTES(val) \
    do { \
        if ((s4) (val) < -128 || (s4) (val) > 127) offset += 4; \
        else if ((s4) (val) != 0) offset += 1; \
    } while (0)


#define CALCIMMEDIATEBYTES(val) \
    do { \
        if ((s4) (val) < -128 || (s4) (val) > 127) offset += 4; \
        else offset += 1; \
    } while (0)


/* gen_nullptr_check(objreg) */

#ifdef SOFTNULLPTRCHECK

#warning ################################
#warning ################################
#warning SOFTNULLPTRCHECK
#warning ################################
#warning ################################

#define gen_nullptr_check(objreg) \
	if (checknull) { \
        i386_alu_imm_reg(I386_CMP, 0, (objreg)); \
        i386_jcc(I386_CC_E, 0); \
 	    mcode_addxnullrefs(mcodeptr); \
	}
#else
#define gen_nullptr_check(objreg)
#endif


/* MCODECHECK(icnt) */

#define MCODECHECK(icnt) \
	if((mcodeptr+(icnt))>mcodeend)mcodeptr=mcode_increase((u1*)mcodeptr)

/* M_INTMOVE:
     generates an integer-move from register a to b.
     if a and b are the same int-register, no code will be generated.
*/ 

#define M_INTMOVE(reg,dreg) if ((reg) != (dreg)) { i386_mov_reg_reg((reg),(dreg)); }


/* M_FLTMOVE:
    generates a floating-point-move from register a to b.
    if a and b are the same float-register, no code will be generated
*/ 

#define M_FLTMOVE(a,b) if(a!=b){M_FMOV(a,b);}


/* var_to_reg_xxx:
    this function generates code to fetch data from a pseudo-register
    into a real register. 
    If the pseudo-register has actually been assigned to a real 
    register, no code will be emitted, since following operations
    can use this register directly.
    
    v: pseudoregister to be fetched from
    tempregnum: temporary register to be used if v is actually spilled to ram

    return: the register number, where the operand can be found after 
            fetching (this wil be either tempregnum or the register
            number allready given to v)
*/

#define var_to_reg_int(regnr,v,tempnr) \
    do { \
        if ((v)->flags & INMEMORY) { \
            COUNT_SPILLS; \
            i386_mov_membase_reg(REG_SP, (v)->regoff * 8, tempnr); \
            regnr = tempnr; \
        } else { \
            regnr = (v)->regoff; \
        } \
    } while (0)



#define var_to_reg_flt(regnr,v,tempnr) \
    do { \
        if ((v)->type == TYPE_FLT) { \
            if ((v)->flags & INMEMORY) { \
                COUNT_SPILLS; \
                i386_flds_membase(REG_SP, (v)->regoff * 8); \
                regnr = tempnr; \
            } else { \
                panic("var_to_reg_flt: floats have to be in memory"); \
                regnr = (v)->regoff; \
            } \
        } else { \
            if ((v)->flags & INMEMORY) { \
                COUNT_SPILLS; \
                i386_fldl_membase(REG_SP, (v)->regoff * 8); \
                regnr = tempnr; \
            } else { \
                panic("var_to_reg_flt: doubles have to be in memory"); \
                regnr = (v)->regoff; \
            } \
        } \
    } while (0)


/* reg_of_var:
    This function determines a register, to which the result of an operation
    should go, when it is ultimatively intended to store the result in
    pseudoregister v.
    If v is assigned to an actual register, this register will be returned.
    Otherwise (when v is spilled) this function returns tempregnum.
    If not already done, regoff and flags are set in the stack location.
*/        

static int reg_of_var(stackptr v, int tempregnum)
{
	varinfo      *var;

	switch (v->varkind) {
		case TEMPVAR:
			if (!(v->flags & INMEMORY))
				return(v->regoff);
			break;
		case STACKVAR:
			var = &(interfaces[v->varnum][v->type]);
			v->regoff = var->regoff;
			if (!(var->flags & INMEMORY))
				return(var->regoff);
			break;
		case LOCALVAR:
			var = &(locals[v->varnum][v->type]);
			v->regoff = var->regoff;
			if (!(var->flags & INMEMORY))
				return(var->regoff);
			break;
		case ARGVAR:
			v->regoff = v->varnum;
			if (IS_FLT_DBL_TYPE(v->type)) {
				if (v->varnum < fltreg_argnum) {
					v->regoff = argfltregs[v->varnum];
					return(argfltregs[v->varnum]);
					}
				}
			else
				if (v->varnum < intreg_argnum) {
					v->regoff = argintregs[v->varnum];
					return(argintregs[v->varnum]);
					}
			v->regoff -= intreg_argnum;
			break;
		}
	v->flags |= INMEMORY;
	return tempregnum;
}


/* store_reg_to_var_xxx:
    This function generates the code to store the result of an operation
    back into a spilled pseudo-variable.
    If the pseudo-variable has not been spilled in the first place, this 
    function will generate nothing.
    
    v ............ Pseudovariable
    tempregnum ... Number of the temporary registers as returned by
                   reg_of_var.
*/	

#define store_reg_to_var_int(sptr, tempregnum) \
    do { \
        if ((sptr)->flags & INMEMORY) { \
            COUNT_SPILLS; \
            i386_mov_reg_membase(tempregnum, REG_SP, (sptr)->regoff * 8); \
        } \
    } while (0)


#define store_reg_to_var_flt(sptr, tempregnum) \
    do { \
        if ((sptr)->type == TYPE_FLT) { \
            if ((sptr)->flags & INMEMORY) { \
/*                if ((sptr)->varkind == ARGVAR) {*/ \
                    COUNT_SPILLS; \
                    /* a little fpu optimization */ \
                    /*if (iptr[1].opc != ICMD_FSTORE) {*/ \
                        i386_fstps_membase(REG_SP, (sptr)->regoff * 8); \
                    /*}*/ \
/*                }*/ \
            } else { \
                panic("store_reg_to_var_flt: floats have to be in memory"); \
            } \
        } else { \
            if ((sptr)->flags & INMEMORY) { \
/*                if ((sptr)->varkind == ARGVAR) {*/ \
                    COUNT_SPILLS; \
                    /* a little fpu optimization */ \
                    /*if (iptr[1].opc != ICMD_FSTORE) {*/ \
                        i386_fstpl_membase(REG_SP, (sptr)->regoff * 8); \
                    /*}*/ \
/*                }*/ \
            } else { \
                panic("store_reg_to_var_flt: doubles have to be in memory"); \
            } \
        } \
    } while (0)


/* NullPointerException handlers and exception handling initialisation        */

typedef struct sigctx_struct {

	long          sc_onstack;           /* sigstack state to restore          */
	long          sc_mask;              /* signal mask to restore             */
	long          sc_pc;                /* pc at time of signal               */
	long          sc_ps;                /* psl to retore                      */
	long          sc_regs[32];          /* processor regs 0 to 31             */
	long          sc_ownedfp;           /* fp has been used                   */
	long          sc_fpregs[32];        /* fp regs 0 to 31                    */
	unsigned long sc_fpcr;              /* floating point control register    */
	unsigned long sc_fp_control;        /* software fpcr                      */
	                                    /* rest is unused                     */
	unsigned long sc_reserved1, sc_reserved2;
	unsigned long sc_ssize;
	char          *sc_sbase;
	unsigned long sc_traparg_a0;
	unsigned long sc_traparg_a1;
	unsigned long sc_traparg_a2;
	unsigned long sc_fp_trap_pc;
	unsigned long sc_fp_trigger_sum;
	unsigned long sc_fp_trigger_inst;
	unsigned long sc_retcode[2];
} sigctx_struct;


/* NullPointerException signal handler for hardware null pointer check */

void catch_NullPointerException(int sig, int code, sigctx_struct *sigctx)
{
	sigset_t nsig;
	int      instr;
	long     faultaddr;

	/* Reset signal handler - necessary for SysV, does no harm for BSD */

	instr = *((int*)(sigctx->sc_pc));
	faultaddr = sigctx->sc_regs[(instr >> 16) & 0x1f];

	if (faultaddr == 0) {
		signal(sig, (void*) catch_NullPointerException); /* reinstall handler */
		sigemptyset(&nsig);
		sigaddset(&nsig, sig);
		sigprocmask(SIG_UNBLOCK, &nsig, NULL);           /* unblock signal    */
		sigctx->sc_regs[REG_ITMP1_XPTR] =
		                            (long) proto_java_lang_NullPointerException;
		sigctx->sc_regs[REG_ITMP2_XPC] = sigctx->sc_pc;
		sigctx->sc_pc = (long) asm_handle_nat_exception;
		return;
		}
	else {
		faultaddr += (long) ((instr << 16) >> 16);
		fprintf(stderr, "faulting address: 0x%16lx\n", faultaddr);
		panic("Stack overflow");
		}
}

void init_exceptions(void)
{
	/* install signal handlers we need to convert to exceptions */

	if (!checknull) {

#if defined(SIGSEGV)
		signal(SIGSEGV, (void*) catch_NullPointerException);
#endif

#if defined(SIGBUS)
		signal(SIGBUS, (void*) catch_NullPointerException);
#endif
		}
}


/* function gen_mcode **********************************************************

	generates machine code

*******************************************************************************/

static void gen_mcode()
{
	int  len, s1, s2, s3, d, bbs;
	s4   a;
	s4          *mcodeptr;
	stackptr    src;
	varinfo     *var;
	varinfo     *dst;
	basicblock  *bptr;
	instruction *iptr;

	xtable *ex;

	{
	int p, pa, t, l, r;

	/* TWISTI */
/*  	savedregs_num = (isleafmethod) ? 0 : 1;           /* space to save the RA */

	/* space to save used callee saved registers */

	savedregs_num += (savintregcnt - maxsavintreguse);
	savedregs_num += (savfltregcnt - maxsavfltreguse);

	parentargs_base = maxmemuse + savedregs_num;

#ifdef USE_THREADS                 /* space to save argument of monitor_enter */

	if (checksync && (method->flags & ACC_SYNCHRONIZED))
		parentargs_base++;

#endif

	/* create method header */

	(void) dseg_addaddress(method);                         /* MethodPointer  */
	(void) dseg_adds4(parentargs_base * 8);                 /* FrameSize      */

#ifdef USE_THREADS

	/* IsSync contains the offset relative to the stack pointer for the
	   argument of monitor_exit used in the exception handler. Since the
	   offset could be zero and give a wrong meaning of the flag it is
	   offset by one.
	*/

	if (checksync && (method->flags & ACC_SYNCHRONIZED))
		(void) dseg_adds4((maxmemuse + 1) * 8);             /* IsSync         */
	else

#endif

	(void) dseg_adds4(0);                                   /* IsSync         */
	                                       
	(void) dseg_adds4(isleafmethod);                        /* IsLeaf         */
	(void) dseg_adds4(savintregcnt - maxsavintreguse);      /* IntSave        */
	(void) dseg_adds4(savfltregcnt - maxsavfltreguse);      /* FltSave        */
	(void) dseg_adds4(exceptiontablelength);                /* ExTableSize    */

	/* create exception table */

	for (ex = extable; ex != NULL; ex = ex->down) {

#ifdef LOOP_DEBUG	
		if (ex->start != NULL)
			printf("adding start - %d - ", ex->start->debug_nr);
		else {
			printf("PANIC - start is NULL");
			exit(-1);
		}
#endif

		dseg_addtarget(ex->start);

#ifdef LOOP_DEBUG			
		if (ex->end != NULL)
			printf("adding end - %d - ", ex->end->debug_nr);
		else {
			printf("PANIC - end is NULL");
			exit(-1);
		}
#endif

   		dseg_addtarget(ex->end);

#ifdef LOOP_DEBUG		
		if (ex->handler != NULL)
			printf("adding handler - %d\n", ex->handler->debug_nr);
		else {
			printf("PANIC - handler is NULL");
			exit(-1);
		}
#endif

		dseg_addtarget(ex->handler);
	   
		(void) dseg_addaddress(ex->catchtype);
		}
	
	/* initialize mcode variables */
	
	mcodeptr = (s4*) mcodebase;
	mcodeend = (s4*) (mcodebase + mcodesize);
	MCODECHECK(128 + mparamcount);

	/* create stack frame (if necessary) */

	if (parentargs_base) {
		i386_alu_imm_reg(I386_SUB, parentargs_base * 8, REG_SP);
	}

	/* save return address and used callee saved registers */

	p = parentargs_base;
	if (!isleafmethod) {
		/* p--; M_AST (REG_RA, REG_SP, 8*p); -- do we really need this on i386 */
	}
	for (r = savintregcnt - 1; r >= maxsavintreguse; r--) {
 		p--; i386_mov_reg_membase(savintregs[r], REG_SP, p * 8);
	}
	for (r = savfltregcnt - 1; r >= maxsavfltreguse; r--) {
		p--; M_DST (savfltregs[r], REG_SP, 8 * p);
	}

	/* save monitorenter argument */

#ifdef USE_THREADS
	if (checksync && (method->flags & ACC_SYNCHRONIZED)) {
		if (method->flags & ACC_STATIC) {
			p = dseg_addaddress (class);
			M_ALD(REG_ITMP1, REG_PV, p);
			M_AST(REG_ITMP1, REG_SP, 8 * maxmemuse);

		} else {
			i386_mov_membase_reg(REG_SP, parentargs_base * 8 + 4, REG_ITMP1);
			i386_mov_reg_membase(REG_ITMP1, REG_SP, 8 * maxmemuse);
		}
	}			
#endif

	/* copy argument registers to stack and call trace function with pointer
	   to arguments on stack. ToDo: save floating point registers !!!!!!!!!
	*/

	if (runverbose && isleafmethod) {
		M_LDA (REG_SP, REG_SP, -(14*8));
		M_AST(REG_RA, REG_SP, 1*8);

		M_LST(argintregs[0], REG_SP,  2*8);
		M_LST(argintregs[1], REG_SP,  3*8);
		M_LST(argintregs[2], REG_SP,  4*8);
		M_LST(argintregs[3], REG_SP,  5*8);
		M_LST(argintregs[4], REG_SP,  6*8);
		M_LST(argintregs[5], REG_SP,  7*8);

		M_DST(argfltregs[0], REG_SP,  8*8);
		M_DST(argfltregs[1], REG_SP,  9*8);
		M_DST(argfltregs[2], REG_SP, 10*8);
		M_DST(argfltregs[3], REG_SP, 11*8);
		M_DST(argfltregs[4], REG_SP, 12*8);
		M_DST(argfltregs[5], REG_SP, 13*8);

		p = dseg_addaddress (method);
		M_ALD(REG_ITMP1, REG_PV, p);
		M_AST(REG_ITMP1, REG_SP, 0);
/*  		p = dseg_addaddress ((void*) (builtin_trace_args)); */
		M_ALD(REG_PV, REG_PV, p);
		M_JSR(REG_RA, REG_PV);
		M_LDA(REG_PV, REG_RA, -(int)((u1*) mcodeptr - mcodebase));
		M_ALD(REG_RA, REG_SP, 1*8);

		M_LLD(argintregs[0], REG_SP,  2*8);
		M_LLD(argintregs[1], REG_SP,  3*8);
		M_LLD(argintregs[2], REG_SP,  4*8);
		M_LLD(argintregs[3], REG_SP,  5*8);
		M_LLD(argintregs[4], REG_SP,  6*8);
		M_LLD(argintregs[5], REG_SP,  7*8);

		M_DLD(argfltregs[0], REG_SP,  8*8);
		M_DLD(argfltregs[1], REG_SP,  9*8);
		M_DLD(argfltregs[2], REG_SP, 10*8);
		M_DLD(argfltregs[3], REG_SP, 11*8);
		M_DLD(argfltregs[4], REG_SP, 12*8);
		M_DLD(argfltregs[5], REG_SP, 13*8);

		M_LDA (REG_SP, REG_SP, 14*8);
		}

	/* take arguments out of register or stack frame */

 	for (p = 0, l = 0; p < mparamcount; p++) {
 		t = mparamtypes[p];
 		var = &(locals[l][t]);
 		l++;
 		if (IS_2_WORD_TYPE(t))    /* increment local counter for 2 word types */
 			l++;
 		if (var->type < 0)
 			continue;
 		r = var->regoff; 
		if (IS_INT_LNG_TYPE(t)) {                    /* integer args          */
 			if (p < intreg_argnum) {                 /* register arguments    */
 				if (!(var->flags & INMEMORY)) {      /* reg arg -> register   */
 					M_INTMOVE (argintregs[p], r);
				} else {                             /* reg arg -> spilled    */
 					M_LST (argintregs[p], REG_SP, 8 * r);
 				}
			} else {                                 /* stack arguments       */
 				pa = p - intreg_argnum;
 				if (!(var->flags & INMEMORY)) {      /* stack arg -> register */ 
 					i386_mov_membase_reg(REG_SP, (parentargs_base + pa) * 8 + 4, r);            /* + 4 for return address */
				} else {                             /* stack arg -> spilled  */
					if (IS_2_WORD_TYPE(t)) {
						i386_mov_membase_reg(REG_SP, (parentargs_base + pa) * 8 + 4, REG_ITMP1);    /* + 4 for return address */
						i386_mov_membase_reg(REG_SP, (parentargs_base + pa) * 8 + 4 + 4, REG_ITMP2);    /* + 4 for return address */
						i386_mov_reg_membase(REG_ITMP1, REG_SP, r * 8);
						i386_mov_reg_membase(REG_ITMP2, REG_SP, r * 8 + 4);

					} else {
						i386_mov_membase_reg(REG_SP, (parentargs_base + pa) * 8 + 4, REG_ITMP1);    /* + 4 for return address */
						i386_mov_reg_membase(REG_ITMP1, REG_SP, r * 8);
					}
				}
			}
		
		} else {                                     /* floating args         */   
 			if (p < fltreg_argnum) {                 /* register arguments    */
 				if (!(var->flags & INMEMORY)) {      /* reg arg -> register   */
					panic("There are no float argument registers!");

 				} else {			                 /* reg arg -> spilled    */
					panic("There are no float argument registers!");
 				}

 			} else {                                 /* stack arguments       */
 				pa = p - fltreg_argnum;
 				if (!(var->flags & INMEMORY)) {      /* stack-arg -> register */
					panic("floats have to be in memory!");

 				} else {                              /* stack-arg -> spilled  */
/*   					i386_mov_membase_reg(REG_SP, (parentargs_base + pa) * 8 + 4, REG_ITMP1); */
/*   					i386_mov_reg_membase(REG_ITMP1, REG_SP, r * 8); */
					if (t == TYPE_FLT) {
						i386_flds_membase(REG_SP, (parentargs_base + pa) * 8 + 4);
						i386_fstps_membase(REG_SP, r * 8);

					} else {
						i386_fldl_membase(REG_SP, (parentargs_base + pa) * 8 + 4);
						i386_fstpl_membase(REG_SP, r * 8);
					}
				}
			}
		}
	}  /* end for */

	/* call trace function */

	if (runverbose && !isleafmethod) {
		M_LDA (REG_SP, REG_SP, -8);
		p = dseg_addaddress (method);
		M_ALD(REG_ITMP1, REG_PV, p);
		M_AST(REG_ITMP1, REG_SP, 0);
/*  		p = dseg_addaddress ((void*) (builtin_trace_args)); */
		M_ALD(REG_PV, REG_PV, p);
		M_JSR(REG_RA, REG_PV);
		M_LDA(REG_PV, REG_RA, -(int)((u1*) mcodeptr - mcodebase));
		M_LDA(REG_SP, REG_SP, 8);
		}

	/* call monitorenter function */

#ifdef USE_THREADS
	if (checksync && (method->flags & ACC_SYNCHRONIZED)) {
		i386_mov_membase_reg(REG_SP, 8 * maxmemuse, REG_ITMP1);
		i386_alu_imm_reg(I386_SUB, 4, REG_SP);
		i386_mov_reg_membase(REG_ITMP1, REG_SP, 0);
		i386_mov_imm_reg(builtin_monitorenter, REG_ITMP1);
		i386_call_reg(REG_ITMP1);
		i386_alu_imm_reg(I386_ADD, 4, REG_SP);
	}			
#endif
	}

	/* end of header generation */

	/* walk through all basic blocks */
	for (/* bbs = block_count, */ bptr = block; /* --bbs >= 0 */ bptr != NULL; bptr = bptr->next) {

		bptr -> mpc = (int)((u1*) mcodeptr - mcodebase);

		if (bptr->flags >= BBREACHED) {

		/* branch resolving */

		branchref *brefs;
		for (brefs = bptr->branchrefs; brefs != NULL; brefs = brefs->next) {
			gen_resolvebranch((u1*) mcodebase + brefs->branchpos, 
			                  brefs->branchpos, bptr->mpc);
			}

		/* copy interface registers to their destination */

		src = bptr->instack;
		len = bptr->indepth;
		MCODECHECK(64+len);
		while (src != NULL) {
			len--;
			if ((len == 0) && (bptr->type != BBTYPE_STD)) {
				d = reg_of_var(src, REG_ITMP1);
				M_INTMOVE(REG_ITMP1, d);
				store_reg_to_var_int(src, d);

			} else {
				d = reg_of_var(src, REG_ITMP1);
				if ((src->varkind != STACKVAR)) {
					s2 = src->type;
					if (IS_FLT_DBL_TYPE(s2)) {
						if (!(interfaces[len][s2].flags & INMEMORY)) {
							s1 = interfaces[len][s2].regoff;
							M_FLTMOVE(s1, d);

						} else {
							M_DLD(d, REG_SP, 8 * interfaces[len][s2].regoff);
						}
						store_reg_to_var_flt(src, d);

					} else {
						if (!(interfaces[len][s2].flags & INMEMORY)) {
							s1 = interfaces[len][s2].regoff;
							M_INTMOVE(s1, d);

						} else {
							i386_mov_membase_reg(REG_SP, interfaces[len][s2].regoff * 8, d);
						}
						store_reg_to_var_int(src, d);
					}
				}
			}
			src = src->prev;
		}

		/* walk through all instructions */
		
		src = bptr->instack;
		len = bptr->icount;
		for (iptr = bptr->iinstr;
		    len > 0;
		    src = iptr->dst, len--, iptr++) {

	MCODECHECK(64);           /* an instruction usually needs < 64 words      */
	switch (iptr->opc) {

		case ICMD_NOP:        /* ...  ==> ...                                 */
			break;

		case ICMD_NULLCHECKPOP: /* ..., objectref  ==> ...                    */
			if (src->flags & INMEMORY) {
				i386_alu_imm_membase(I386_CMP, 0, REG_SP, src->regoff * 8);

			} else {
				i386_test_reg_reg(src->regoff, src->regoff);
			}
			i386_jcc(I386_CC_E, 0);
			mcode_addxnullrefs(mcodeptr);
			break;

		/* constant operations ************************************************/

		case ICMD_ICONST:     /* ...  ==> ..., constant                       */
		                      /* op1 = 0, val.i = constant                    */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if (iptr->dst->flags & INMEMORY) {
				i386_mov_imm_membase(iptr->val.i, REG_SP, iptr->dst->regoff * 8);
				i386_mov_imm_membase(0, REG_SP, iptr->dst->regoff * 8 + 4);

			} else {
				i386_mov_imm_reg(iptr->val.i, d);
			}
			break;

		case ICMD_LCONST:     /* ...  ==> ..., constant                       */
		                      /* op1 = 0, val.l = constant                    */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if (iptr->dst->flags & INMEMORY) {
				i386_mov_imm_membase(iptr->val.l, REG_SP, iptr->dst->regoff * 8);
				i386_mov_imm_membase(iptr->val.l >> 32, REG_SP, iptr->dst->regoff * 8 + 4);
				
			} else {
				panic("LCONST: longs have to be in memory");
			}
			break;

		case ICMD_FCONST:     /* ...  ==> ..., constant                       */
		                      /* op1 = 0, val.f = constant                    */

			d = reg_of_var(iptr->dst, REG_FTMP1);
			if (iptr->val.f == 0) {
				i386_fldz();

			} else if (iptr->val.f == 1) {
				i386_fld1();

			} else {
  				a = dseg_addfloat(iptr->val.f);
				i386_mov_imm_reg(0, REG_ITMP1);
				dseg_adddata(mcodeptr);
				i386_flds_membase(REG_ITMP1, a);
			}
			store_reg_to_var_flt(iptr->dst, d);
			break;
		
		case ICMD_DCONST:     /* ...  ==> ..., constant                       */
		                      /* op1 = 0, val.d = constant                    */

			d = reg_of_var(iptr->dst, REG_FTMP1);
			if (iptr->val.d == 0) {
				i386_fldz();

			} else if (iptr->val.d == 1) {
				i386_fld1();

			} else {
				a = dseg_adddouble(iptr->val.d);
				i386_mov_imm_reg(0, REG_ITMP1);
				dseg_adddata(mcodeptr);
				i386_fldl_membase(REG_ITMP1, a);
/*  				i386_mov_double_low_membase(iptr->val.d, REG_SP, iptr->dst->regoff * 8); */
/*  				i386_mov_double_high_membase(iptr->val.d, REG_SP, iptr->dst->regoff * 8 + 4); */
			}
			store_reg_to_var_flt(iptr->dst, d);
			break;

		case ICMD_ACONST:     /* ...  ==> ..., constant                       */
		                      /* op1 = 0, val.a = constant                    */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if (iptr->dst->flags & INMEMORY) {
				i386_mov_imm_membase(iptr->val.a, REG_SP, iptr->dst->regoff * 8);
				i386_mov_imm_membase(0, REG_SP, iptr->dst->regoff * 8 + 4);

			} else {

				i386_mov_imm_reg(iptr->val.a, iptr->dst->regoff);
			}
			break;


		/* load/store operations **********************************************/

		case ICMD_ILOAD:      /* ...  ==> ..., content of local variable      */
                              /* op1 = local variable                         */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if ((iptr->dst->varkind == LOCALVAR) &&
			    (iptr->dst->varnum == iptr->op1)) {
				break;
			}
			var = &(locals[iptr->op1][iptr->opc - ICMD_ILOAD]);
			if (iptr->dst->flags & INMEMORY) {
				if (var->flags & INMEMORY) {
					i386_mov_membase_reg(REG_SP, var->regoff * 8, REG_ITMP1);
					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);

				} else {
					i386_mov_reg_membase(var->regoff, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if (var->flags & INMEMORY) {
					i386_mov_membase_reg(REG_SP, var->regoff * 8, iptr->dst->regoff);

				} else {
					M_INTMOVE(var->regoff, iptr->dst->regoff);
				}
			}
			break;

		case ICMD_ALOAD:      /* ...  ==> ..., content of local variable      */
                              /* op1 = local variable                         */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if ((iptr->dst->varkind == LOCALVAR) &&
			    (iptr->dst->varnum == iptr->op1)) {
				break;
			}
			var = &(locals[iptr->op1][iptr->opc - ICMD_ILOAD]);
			if (iptr->dst->flags & INMEMORY) {
				if (var->flags & INMEMORY) {
					i386_mov_membase_reg(REG_SP, var->regoff * 8, REG_ITMP1);
					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);

				} else {
					i386_mov_reg_membase(var->regoff, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if (var->flags & INMEMORY) {
					i386_mov_membase_reg(REG_SP, var->regoff * 8, iptr->dst->regoff);

				} else {
					M_INTMOVE(var->regoff, iptr->dst->regoff);
				}
			}
			break;

		case ICMD_LLOAD:      /* ...  ==> ..., content of local variable      */
		                      /* op1 = local variable                         */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if ((iptr->dst->varkind == LOCALVAR) &&
			    (iptr->dst->varnum == iptr->op1)) {
				break;
			}
			var = &(locals[iptr->op1][iptr->opc - ICMD_ILOAD]);
			if (iptr->dst->flags & INMEMORY) {
				if (var->flags & INMEMORY) {
					i386_mov_membase_reg(REG_SP, var->regoff * 8, REG_ITMP1);
					i386_mov_membase_reg(REG_SP, var->regoff * 8 + 4, REG_ITMP2);
					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
					i386_mov_reg_membase(REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);

				} else {
					i386_mov_reg_membase(var->regoff, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				panic("LLOAD: longs have to be in memory");
			}
			break;

		case ICMD_FLOAD:      /* ...  ==> ..., content of local variable      */
		                      /* op1 = local variable                         */

			d = reg_of_var(iptr->dst, REG_FTMP1);
  			if ((iptr->dst->varkind == LOCALVAR) &&
  			    (iptr->dst->varnum == iptr->op1)) {
    				break;
  			}
			var = &(locals[iptr->op1][iptr->opc - ICMD_ILOAD]);
  			i386_flds_membase(REG_SP, var->regoff * 8);
  			store_reg_to_var_flt(iptr->dst, d);
			break;

		case ICMD_DLOAD:      /* ...  ==> ..., content of local variable      */
		                      /* op1 = local variable                         */

			d = reg_of_var(iptr->dst, REG_FTMP1);
  			if ((iptr->dst->varkind == LOCALVAR) &&
  			    (iptr->dst->varnum == iptr->op1)) {
    				break;
  			}
			var = &(locals[iptr->op1][iptr->opc - ICMD_ILOAD]);
  			i386_fldl_membase(REG_SP, var->regoff * 8);
  			store_reg_to_var_flt(iptr->dst, d);
			break;

		case ICMD_ISTORE:     /* ..., value  ==> ...                          */
		                      /* op1 = local variable                         */

			if ((src->varkind == LOCALVAR) &&
			    (src->varnum == iptr->op1)) {
				break;
			}
			var = &(locals[iptr->op1][iptr->opc - ICMD_ISTORE]);
			if (var->flags & INMEMORY) {
				if (src->flags & INMEMORY) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
					i386_mov_reg_membase(REG_ITMP1, REG_SP, var->regoff * 8);
					
				} else {
					i386_mov_reg_membase(src->regoff, REG_SP, var->regoff * 8);
				}

			} else {
				var_to_reg_int(s1, src, var->regoff);
				M_INTMOVE(s1, var->regoff);
			}
			break;

		case ICMD_ASTORE:     /* ..., value  ==> ...                          */
		                      /* op1 = local variable                         */

			if ((src->varkind == LOCALVAR) &&
			    (src->varnum == iptr->op1)) {
				break;
			}
			var = &(locals[iptr->op1][iptr->opc - ICMD_ISTORE]);
			if (var->flags & INMEMORY) {
				if (src->flags & INMEMORY) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
					i386_mov_reg_membase(REG_ITMP1, REG_SP, var->regoff * 8);
					
				} else {
					i386_mov_reg_membase(src->regoff, REG_SP, var->regoff * 8);
				}

			} else {
				var_to_reg_int(s1, src, var->regoff);
				M_INTMOVE(s1, var->regoff);
			}
			break;

		case ICMD_LSTORE:     /* ..., value  ==> ...                          */
		                      /* op1 = local variable                         */

			if ((src->varkind == LOCALVAR) &&
			    (src->varnum == iptr->op1)) {
				break;
			}
			var = &(locals[iptr->op1][iptr->opc - ICMD_ISTORE]);
			panic("LSTORE");
/*  			if (var->flags & INMEMORY) { */
/*  				var_to_reg_int(s1, src, REG_ITMP1); */
/*  				i386_mov_reg_membase(s1, REG_SP, var->regoff * 8); */

/*  			} else { */
/*  				var_to_reg_int(s1, src, var->regoff); */
/*  				M_INTMOVE(s1, var->regoff); */
/*  			} */
			break;

		case ICMD_FSTORE:     /* ..., value  ==> ...                          */
		                      /* op1 = local variable                         */

			var = &(locals[iptr->op1][iptr->opc - ICMD_ISTORE]);
/*     			i386_fstps_membase(REG_SP, var->regoff * 8); */
			break;

		case ICMD_DSTORE:     /* ..., value  ==> ...                          */
		                      /* op1 = local variable                         */

			var = &(locals[iptr->op1][iptr->opc - ICMD_ISTORE]);
/*     			i386_fstpl_membase(REG_SP, var->regoff * 8); */
			break;


		/* pop/dup/swap operations ********************************************/

		/* attention: double and longs are only one entry in CACAO ICMDs      */

		case ICMD_POP:        /* ..., value  ==> ...                          */
		case ICMD_POP2:       /* ..., value, value  ==> ...                   */
			break;

			/* TWISTI */
/*  #define M_COPY(from,to) \ */
/*  			d = reg_of_var(to, REG_IFTMP); \ */
/*  			if ((from->regoff != to->regoff) || \ */
/*  			    ((from->flags ^ to->flags) & INMEMORY)) { \ */
/*  				if (IS_FLT_DBL_TYPE(from->type)) { \ */
/*  					var_to_reg_flt(s1, from, d); \ */
/*  					M_FLTMOVE(s1,d); \ */
/*  					store_reg_to_var_flt(to, d); \ */
/*  					}\ */
/*  				else { \ */
/*  					var_to_reg_int(s1, from, d); \ */
/*  					M_INTMOVE(s1,d); \ */
/*  					store_reg_to_var_int(to, d); \ */
/*  					}\ */
/*  				} */
#define M_COPY(from,to) \
			if ((from->regoff != to->regoff) || \
			    ((from->flags ^ to->flags) & INMEMORY)) { \
				if (IS_FLT_DBL_TYPE(from->type)) { \
			        d = reg_of_var(to, REG_IFTMP); \
					var_to_reg_flt(s1, from, d); \
					M_FLTMOVE(s1, d); \
					store_reg_to_var_flt(to, d); \
				} else { \
			        d = reg_of_var(to, REG_ITMP1); \
					var_to_reg_int(s1, from, d); \
  					M_INTMOVE(s1, d); \
					store_reg_to_var_int(to, s1); \
				}\
			}

		case ICMD_DUP:        /* ..., a ==> ..., a, a                         */
			M_COPY(src, iptr->dst);
			break;

		case ICMD_DUP_X1:     /* ..., a, b ==> ..., b, a, b                   */

			M_COPY(src,       iptr->dst->prev->prev);

		case ICMD_DUP2:       /* ..., a, b ==> ..., a, b, a, b                */

			M_COPY(src,       iptr->dst);
			M_COPY(src->prev, iptr->dst->prev);
			break;

		case ICMD_DUP2_X1:    /* ..., a, b, c ==> ..., b, c, a, b, c          */

			M_COPY(src->prev,       iptr->dst->prev->prev->prev);

		case ICMD_DUP_X2:     /* ..., a, b, c ==> ..., c, a, b, c             */

			M_COPY(src,             iptr->dst);
			M_COPY(src->prev,       iptr->dst->prev);
			M_COPY(src->prev->prev, iptr->dst->prev->prev);
			M_COPY(src, iptr->dst->prev->prev->prev);
			break;

		case ICMD_DUP2_X2:    /* ..., a, b, c, d ==> ..., c, d, a, b, c, d    */

			M_COPY(src,                   iptr->dst);
			M_COPY(src->prev,             iptr->dst->prev);
			M_COPY(src->prev->prev,       iptr->dst->prev->prev);
			M_COPY(src->prev->prev->prev, iptr->dst->prev->prev->prev);
			M_COPY(src,       iptr->dst->prev->prev->prev->prev);
			M_COPY(src->prev, iptr->dst->prev->prev->prev->prev->prev);
			break;

		case ICMD_SWAP:       /* ..., a, b ==> ..., b, a                      */

			M_COPY(src, iptr->dst->prev);
			M_COPY(src->prev, iptr->dst);
			break;


		/* integer operations *************************************************/

		case ICMD_INEG:       /* ..., value  ==> ..., - value                 */

			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->dst->flags & INMEMORY) {
				if (src->flags & INMEMORY) {
					if (src->regoff == iptr->dst->regoff) {
						i386_neg_membase(REG_SP, iptr->dst->regoff * 8);

					} else {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
						i386_neg_reg(REG_ITMP1);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
					}

				} else {
					i386_mov_reg_membase(src->regoff, REG_SP, iptr->dst->regoff * 8);
					i386_neg_membase(REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if (src->flags & INMEMORY) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, iptr->dst->regoff);
					i386_neg_reg(iptr->dst->regoff);

				} else {
					M_INTMOVE(src->regoff, iptr->dst->regoff);
					i386_neg_reg(iptr->dst->regoff);
				}
			}
			break;

		case ICMD_LNEG:       /* ..., value  ==> ..., - value                 */

			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->dst->flags & INMEMORY) {
				if (src->flags & INMEMORY) {
					if (src->regoff == iptr->dst->regoff) {
						i386_neg_membase(REG_SP, iptr->dst->regoff * 8);
						i386_alu_imm_membase(I386_ADC, 0, REG_SP, iptr->dst->regoff * 8 + 4);
						i386_neg_membase(REG_SP, iptr->dst->regoff * 8 + 4);

					} else {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->regoff * 8 + 4, REG_ITMP2);
						i386_neg_reg(REG_ITMP1);
						i386_alu_imm_reg(I386_ADC, 0, REG_ITMP2);
						i386_neg_reg(REG_ITMP2);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
						i386_mov_reg_membase(REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);
					}
				}
			}
			break;

		case ICMD_I2L:        /* ..., value  ==> ..., value                   */

			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->dst->flags & INMEMORY) {
				if (src->flags & INMEMORY) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, I386_EAX);
					i386_cltd();
					i386_mov_reg_membase(I386_EAX, REG_SP, iptr->dst->regoff * 8);
					i386_mov_reg_membase(I386_EDX, REG_SP, iptr->dst->regoff * 8 + 4);

				} else {
					M_INTMOVE(src->regoff, I386_EAX);
					i386_cltd();
					i386_mov_reg_membase(I386_EAX, REG_SP, iptr->dst->regoff * 8);
					i386_mov_reg_membase(I386_EDX, REG_SP, iptr->dst->regoff * 8 + 4);
				}
			}
			break;

		case ICMD_L2I:        /* ..., value  ==> ..., value                   */

			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->dst->flags & INMEMORY) {
				if (src->flags & INMEMORY) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if (src->flags & INMEMORY) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, iptr->dst->regoff);
				}
			}
			break;

		case ICMD_INT2BYTE:   /* ..., value  ==> ..., value                   */

			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->dst->flags & INMEMORY) {
				if (src->flags & INMEMORY) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
					i386_shift_imm_reg(I386_SHL, 24, REG_ITMP1);
					i386_shift_imm_reg(I386_SAR, 24, REG_ITMP1);
					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);

				} else {
					i386_mov_reg_membase(src->regoff, REG_SP, iptr->dst->regoff * 8);
					i386_shift_imm_membase(I386_SHL, 24, REG_SP, iptr->dst->regoff * 8);
					i386_shift_imm_membase(I386_SAR, 24, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if (src->flags & INMEMORY) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, iptr->dst->regoff);
					i386_shift_imm_reg(I386_SHL, 24, iptr->dst->regoff);
					i386_shift_imm_reg(I386_SAR, 24, iptr->dst->regoff);

				} else {
					M_INTMOVE(src->regoff, iptr->dst->regoff);
					i386_shift_imm_reg(I386_SHL, 24, iptr->dst->regoff);
					i386_shift_imm_reg(I386_SAR, 24, iptr->dst->regoff);
				}
			}
			break;

		case ICMD_INT2CHAR:   /* ..., value  ==> ..., value                   */

			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->dst->flags & INMEMORY) {
				if (src->flags & INMEMORY) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
					i386_alu_imm_reg(I386_AND, 0x0000ffff, REG_ITMP1);
					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);

				} else {
					i386_mov_reg_membase(src->regoff, REG_SP, iptr->dst->regoff * 8);
					i386_alu_imm_membase(I386_AND, 0x0000ffff, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if (src->flags & INMEMORY) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, iptr->dst->regoff);
					i386_alu_imm_reg(I386_AND, 0x0000ffff, iptr->dst->regoff);

				} else {
					M_INTMOVE(src->regoff, iptr->dst->regoff);
					i386_alu_imm_reg(I386_AND, 0x0000ffff, iptr->dst->regoff);
				}
			}
			break;

		case ICMD_INT2SHORT:  /* ..., value  ==> ..., value                   */

			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->dst->flags & INMEMORY) {
				if (src->flags & INMEMORY) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
					i386_shift_imm_reg(I386_SHL, 16, REG_ITMP1);
					i386_shift_imm_reg(I386_SAR, 16, REG_ITMP1);
					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);

				} else {
					i386_mov_reg_membase(src->regoff, REG_SP, iptr->dst->regoff * 8);
					i386_shift_imm_membase(I386_SHL, 16, REG_SP, iptr->dst->regoff * 8);
					i386_shift_imm_membase(I386_SAR, 16, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if (src->flags & INMEMORY) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, iptr->dst->regoff);
					i386_shift_imm_reg(I386_SHL, 16, iptr->dst->regoff);
					i386_shift_imm_reg(I386_SAR, 16, iptr->dst->regoff);

				} else {
					M_INTMOVE(src->regoff, iptr->dst->regoff);
					i386_shift_imm_reg(I386_SHL, 16, iptr->dst->regoff);
					i386_shift_imm_reg(I386_SAR, 16, iptr->dst->regoff);
				}
			}
			break;


		case ICMD_IADD:       /* ..., val1, val2  ==> ..., val1 + val2        */

			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->dst->flags & INMEMORY) {
				if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					if (src->regoff == iptr->dst->regoff) {
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_alu_reg_membase(I386_ADD, REG_ITMP1, REG_SP, iptr->dst->regoff * 8);

					} else if (src->prev->regoff == iptr->dst->regoff) {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
						i386_alu_reg_membase(I386_ADD, REG_ITMP1, REG_SP, iptr->dst->regoff * 8);

					} else {
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_alu_membase_reg(I386_ADD, REG_SP, src->regoff * 8, REG_ITMP1);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
					}

				} else if ((src->flags & INMEMORY) && !(src->prev->flags & INMEMORY)) {
					if (src->regoff == iptr->dst->regoff) {
						i386_alu_reg_membase(I386_ADD, src->prev->regoff, REG_SP, iptr->dst->regoff * 8);

					} else {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
						i386_alu_reg_reg(I386_ADD, src->prev->regoff, REG_ITMP1);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
					}

				} else if (!(src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					if (src->prev->regoff == iptr->dst->regoff) {
						i386_alu_reg_membase(I386_ADD, src->regoff, REG_SP, iptr->dst->regoff * 8);
						
					} else {
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_alu_reg_reg(I386_ADD, src->regoff, REG_ITMP1);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
					}

				} else {
					i386_mov_reg_membase(src->prev->regoff, REG_SP, iptr->dst->regoff * 8);
					i386_alu_reg_membase(I386_ADD, src->regoff, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, iptr->dst->regoff);
					i386_alu_membase_reg(I386_ADD, REG_SP, src->regoff * 8, iptr->dst->regoff);

				} else if ((src->flags & INMEMORY) && !(src->prev->flags & INMEMORY)) {
					M_INTMOVE(src->prev->regoff, iptr->dst->regoff);
					i386_alu_membase_reg(I386_ADD, REG_SP, src->regoff * 8, iptr->dst->regoff);

				} else if (!(src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					M_INTMOVE(src->regoff, iptr->dst->regoff);
					i386_alu_membase_reg(I386_ADD, REG_SP, src->prev->regoff * 8, iptr->dst->regoff);

				} else {
					if (src->regoff == iptr->dst->regoff) {
						i386_alu_reg_reg(I386_ADD, src->prev->regoff, iptr->dst->regoff);

					} else {
						M_INTMOVE(src->prev->regoff, iptr->dst->regoff);
						i386_alu_reg_reg(I386_ADD, src->regoff, iptr->dst->regoff);
					}
				}
			}
			break;

		case ICMD_IADDCONST:  /* ..., value  ==> ..., value + constant        */
		                      /* val.i = constant                             */

			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->dst->flags & INMEMORY) {
				if (src->flags & INMEMORY) {
					/*
					 * do not use here inc optimization, because it's slower (???) 
					 */
					if (src->regoff == iptr->dst->regoff) {
						i386_alu_imm_membase(I386_ADD, iptr->val.i, REG_SP, iptr->dst->regoff * 8);

					} else {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
						i386_alu_imm_reg(I386_ADD, iptr->val.i, REG_ITMP1);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
					}

				} else {
					i386_mov_reg_membase(src->regoff, REG_SP, iptr->dst->regoff * 8);
					i386_alu_imm_membase(I386_ADD, iptr->val.i, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if (src->flags & INMEMORY) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, iptr->dst->regoff);

					if (iptr->val.i == 1) {
						i386_inc_reg(iptr->dst->regoff);

					} else {
						i386_alu_imm_reg(I386_ADD, iptr->val.i, iptr->dst->regoff);
					}

				} else {
					M_INTMOVE(src->regoff, iptr->dst->regoff);

					if (iptr->val.i == 1) {
						i386_inc_reg(iptr->dst->regoff);

					} else {
						i386_alu_imm_reg(I386_ADD, iptr->val.i, iptr->dst->regoff);
					}
				}
			}
			break;

		case ICMD_LADD:       /* ..., val1, val2  ==> ..., val1 + val2        */

			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->dst->flags & INMEMORY) {
				if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					if (src->regoff == iptr->dst->regoff) {
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8 + 4, REG_ITMP2);
						i386_alu_reg_membase(I386_ADD, REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
						i386_alu_reg_membase(I386_ADC, REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);

					} else if (src->prev->regoff == iptr->dst->regoff) {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->regoff * 8 + 4, REG_ITMP2);
						i386_alu_reg_membase(I386_ADD, REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
						i386_alu_reg_membase(I386_ADC, REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);

					} else {
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8 + 4, REG_ITMP2);
						i386_alu_membase_reg(I386_ADD, REG_SP, src->regoff * 8, REG_ITMP1);
						i386_alu_membase_reg(I386_ADC, REG_SP, src->regoff * 8 + 4, REG_ITMP2);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
						i386_mov_reg_membase(REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);
					}

				}
			}
			break;

		case ICMD_LADDCONST:  /* ..., value  ==> ..., value + constant        */
		                      /* val.l = constant                             */

			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->dst->flags & INMEMORY) {
				if (src->flags & INMEMORY) {
					if (src->regoff == iptr->dst->regoff) {
						i386_alu_imm_membase(I386_ADD, iptr->val.l, REG_SP, iptr->dst->regoff * 8);
						i386_alu_imm_membase(I386_ADC, iptr->val.l >> 32, REG_SP, iptr->dst->regoff * 8 + 4);

					} else {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->regoff * 8 + 4, REG_ITMP2);
						i386_alu_imm_reg(I386_ADD, iptr->val.l, REG_ITMP1);
						i386_alu_imm_reg(I386_ADC, iptr->val.l >> 32, REG_ITMP2);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
						i386_mov_reg_membase(REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);
					}
				}
			}
			break;

		case ICMD_ISUB:       /* ..., val1, val2  ==> ..., val1 - val2        */

			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->dst->flags & INMEMORY) {
				if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					if (src->prev->regoff == iptr->dst->regoff) {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
						i386_alu_reg_membase(I386_SUB, REG_ITMP1, REG_SP, iptr->dst->regoff * 8);

					} else {
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_alu_membase_reg(I386_SUB, REG_SP, src->regoff * 8, REG_ITMP1);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
					}

				} else if ((src->flags & INMEMORY) && !(src->prev->flags & INMEMORY)) {
					M_INTMOVE(src->prev->regoff, REG_ITMP1);
					i386_alu_membase_reg(I386_SUB, REG_SP, src->regoff * 8, REG_ITMP1);
					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);

				} else if (!(src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					if (src->prev->regoff == iptr->dst->regoff) {
						i386_alu_reg_membase(I386_SUB, src->regoff, REG_SP, iptr->dst->regoff * 8);

					} else {
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_alu_reg_reg(I386_SUB, src->regoff, REG_ITMP1);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
					}

				} else {
					i386_mov_reg_membase(src->prev->regoff, REG_SP, iptr->dst->regoff * 8);
					i386_alu_reg_membase(I386_SUB, src->regoff, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, iptr->dst->regoff);
					i386_alu_membase_reg(I386_SUB, REG_SP, src->regoff * 8, iptr->dst->regoff);

				} else if ((src->flags & INMEMORY) && !(src->prev->flags & INMEMORY)) {
					M_INTMOVE(src->prev->regoff, iptr->dst->regoff);
					i386_alu_membase_reg(I386_SUB, REG_SP, src->regoff * 8, iptr->dst->regoff);

				} else if (!(src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, iptr->dst->regoff);
					i386_alu_reg_reg(I386_SUB, src->regoff, iptr->dst->regoff);

				} else {
					M_INTMOVE(src->prev->regoff, iptr->dst->regoff);
					i386_alu_reg_reg(I386_SUB, src->regoff, iptr->dst->regoff);
				}
			}
			break;

		case ICMD_ISUBCONST:  /* ..., value  ==> ..., value + constant        */
		                      /* val.i = constant                             */

			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->dst->flags & INMEMORY) {
				if (src->flags & INMEMORY) {
					if (src->regoff == iptr->dst->regoff) {
						i386_alu_imm_membase(I386_SUB, iptr->val.i, REG_SP, iptr->dst->regoff * 8);

					} else {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
						i386_alu_imm_reg(I386_SUB, iptr->val.i, REG_ITMP1);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
					}

				} else {
					i386_mov_reg_membase(src->regoff, REG_SP, iptr->dst->regoff * 8);
					i386_alu_imm_membase(I386_SUB, iptr->val.i, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if (src->flags & INMEMORY) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, iptr->dst->regoff);
					i386_alu_imm_reg(I386_SUB, iptr->val.i, iptr->dst->regoff);

				} else {
					M_INTMOVE(src->regoff, iptr->dst->regoff);
					i386_alu_imm_reg(I386_SUB, iptr->val.i, iptr->dst->regoff);
				}
			}
			break;

		case ICMD_LSUB:       /* ..., val1, val2  ==> ..., val1 - val2        */

			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->dst->flags & INMEMORY) {
				if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					if (src->prev->regoff == iptr->dst->regoff) {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->regoff * 8 + 4, REG_ITMP2);
						i386_alu_reg_membase(I386_SUB, REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
						i386_alu_reg_membase(I386_SBB, REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);

					} else {
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8 + 4, REG_ITMP2);
						i386_alu_membase_reg(I386_SUB, REG_SP, src->regoff * 8, REG_ITMP1);
						i386_alu_membase_reg(I386_SBB, REG_SP, src->regoff * 8 + 4, REG_ITMP2);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
						i386_mov_reg_membase(REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);
					}
				}
			}
			break;

		case ICMD_LSUBCONST:  /* ..., value  ==> ..., value - constant        */
		                      /* val.l = constant                             */

			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->dst->flags & INMEMORY) {
				if (src->flags & INMEMORY) {
					if (src->regoff == iptr->dst->regoff) {
						i386_alu_imm_membase(I386_SUB, iptr->val.l, REG_SP, iptr->dst->regoff * 8);
						i386_alu_imm_membase(I386_SBB, iptr->val.l >> 32, REG_SP, iptr->dst->regoff * 8 + 4);

					} else {
						/* TODO: could be size optimized with lea -- see gcc output */
						i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->regoff * 8 + 4, REG_ITMP2);
						i386_alu_imm_reg(I386_SUB, iptr->val.l, REG_ITMP1);
						i386_alu_imm_reg(I386_SBB, iptr->val.l >> 32, REG_ITMP2);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
						i386_mov_reg_membase(REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);
					}
				}
			}
			break;

		case ICMD_IMUL:       /* ..., val1, val2  ==> ..., val1 * val2        */

			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->dst->flags & INMEMORY) {
				if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
					i386_imul_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);

				} else if ((src->flags & INMEMORY) && !(src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
					i386_imul_reg_reg(src->prev->regoff, REG_ITMP1);
					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);

				} else if (!(src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
					i386_imul_reg_reg(src->regoff, REG_ITMP1);
					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);

				} else {
					i386_mov_reg_reg(src->prev->regoff, REG_ITMP1);
					i386_imul_reg_reg(src->regoff, REG_ITMP1);
					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, iptr->dst->regoff);
					i386_imul_membase_reg(REG_SP, src->regoff * 8, iptr->dst->regoff);

				} else if ((src->flags & INMEMORY) && !(src->prev->flags & INMEMORY)) {
					M_INTMOVE(src->prev->regoff, iptr->dst->regoff);
					i386_imul_membase_reg(REG_SP, src->regoff * 8, iptr->dst->regoff);

				} else if (!(src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					M_INTMOVE(src->regoff, iptr->dst->regoff);
					i386_imul_membase_reg(REG_SP, src->prev->regoff * 8, iptr->dst->regoff);

				} else {
					if (src->regoff == iptr->dst->regoff) {
						i386_imul_reg_reg(src->prev->regoff, iptr->dst->regoff);

					} else {
						M_INTMOVE(src->prev->regoff, iptr->dst->regoff);
						i386_imul_reg_reg(src->regoff, iptr->dst->regoff);
					}
				}
			}
			break;

		case ICMD_IMULCONST:  /* ..., value  ==> ..., value * constant        */
		                      /* val.i = constant                             */

			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->dst->flags & INMEMORY) {
				if (src->flags & INMEMORY) {
					i386_imul_imm_membase_reg(iptr->val.i, REG_SP, src->regoff * 8, REG_ITMP1);
					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);

				} else {
					i386_imul_imm_reg_reg(iptr->val.i, src->regoff, REG_ITMP1);
					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if (src->flags & INMEMORY) {
					i386_imul_imm_membase_reg(iptr->val.i, REG_SP, src->regoff * 8, iptr->dst->regoff);

				} else {
					i386_imul_imm_reg_reg(iptr->val.i, src->regoff, iptr->dst->regoff);
				}
			}
			break;

		case ICMD_LMUL:       /* ..., val1, val2  ==> ..., val1 * val2        */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if (iptr->dst->flags & INMEMORY) {
				if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, I386_EAX);        /* mem -> EAX             */
					/* optimize move EAX -> REG_ITMP3 is slower??? */
/*    					i386_mov_reg_reg(I386_EAX, REG_ITMP3); */
					i386_mul_membase(REG_SP, src->regoff * 8);                            /* mem * EAX -> EDX:EAX   */

					/* TODO: optimize move EAX -> REG_ITMP3 */
  					i386_mov_membase_reg(REG_SP, src->prev->regoff * 8 + 4, REG_ITMP3);   /* mem -> ITMP3           */
					i386_imul_membase_reg(REG_SP, src->regoff * 8, REG_ITMP3);            /* mem * ITMP3 -> ITMP3   */
					i386_alu_reg_reg(I386_ADD, REG_ITMP3, I386_EDX);                      /* ITMP3 + EDX -> EDX     */

					i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP3);       /* mem -> ITMP3           */
					i386_imul_membase_reg(REG_SP, src->regoff * 8 + 4, REG_ITMP3);        /* mem * ITMP3 -> ITMP3   */

					i386_alu_reg_reg(I386_ADD, REG_ITMP3, I386_EDX);                      /* ITMP3 + EDX -> EDX     */
					i386_mov_reg_membase(I386_EAX, REG_SP, iptr->dst->regoff * 8);
					i386_mov_reg_membase(I386_EDX, REG_SP, iptr->dst->regoff * 8 + 4);
				}
			}
			break;

		case ICMD_LMULCONST:  /* ..., value  ==> ..., value * constant        */
		                      /* val.l = constant                             */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if (iptr->dst->flags & INMEMORY) {
				if (src->flags & INMEMORY) {
					i386_mov_imm_reg(iptr->val.l, I386_EAX);                              /* imm -> EAX             */
					i386_mul_membase(REG_SP, src->regoff * 8);                            /* mem * EAX -> EDX:EAX   */
					/* TODO: optimize move EAX -> REG_ITMP3 */
					i386_mov_imm_reg(iptr->val.l >> 32, REG_ITMP3);                       /* imm -> ITMP3           */
					i386_imul_membase_reg(REG_SP, src->regoff * 8, REG_ITMP3);            /* mem * ITMP3 -> ITMP3   */

					i386_alu_reg_reg(I386_ADD, REG_ITMP3, I386_EDX);                      /* ITMP3 + EDX -> EDX     */
					i386_mov_imm_reg(iptr->val.l, REG_ITMP3);                             /* imm -> ITMP3           */
					i386_imul_membase_reg(REG_SP, src->regoff * 8 + 4, REG_ITMP3);        /* mem * ITMP3 -> ITMP3   */

					i386_alu_reg_reg(I386_ADD, REG_ITMP3, I386_EDX);                      /* ITMP3 + EDX -> EDX     */
					i386_mov_reg_membase(I386_EAX, REG_SP, iptr->dst->regoff * 8);
					i386_mov_reg_membase(I386_EDX, REG_SP, iptr->dst->regoff * 8 + 4);
				}
			}
			break;

#define gen_div_check(v) \
    do { \
        if ((v)->flags & INMEMORY) { \
            i386_alu_imm_membase(I386_CMP, 0, REG_SP, src->regoff * 8); \
        } else { \
            i386_test_reg_reg(src->regoff, src->regoff); \
        } \
        i386_jcc(I386_CC_E, 0); \
        mcode_addxdivrefs(mcodeptr); \
    } while (0)

		case ICMD_IDIV:       /* ..., val1, val2  ==> ..., val1 / val2        */

			d = reg_of_var(iptr->dst, REG_ITMP3);
  			gen_div_check(src);
	        if (src->prev->flags & INMEMORY) {
				i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, I386_EAX);

			} else {
				M_INTMOVE(src->prev->regoff, I386_EAX);
			}
			
			i386_cltd();

			if (src->flags & INMEMORY) {
				i386_idiv_membase(REG_SP, src->regoff * 8);

			} else {
				i386_idiv_reg(src->regoff);
			}

			if (iptr->dst->flags & INMEMORY) {
				i386_mov_reg_membase(I386_EAX, REG_SP, iptr->dst->regoff * 8);

			} else {
				M_INTMOVE(I386_EAX, iptr->dst->regoff);
			}
			break;

		case ICMD_IREM:       /* ..., val1, val2  ==> ..., val1 % val2        */

			d = reg_of_var(iptr->dst, REG_ITMP3);
  			gen_div_check(src);
			if (src->prev->flags & INMEMORY) {
				i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, I386_EAX);

			} else {
				M_INTMOVE(src->prev->regoff, I386_EAX);
			}
			
			i386_cltd();

			if (src->flags & INMEMORY) {
				i386_idiv_membase(REG_SP, src->regoff * 8);

			} else {
				i386_idiv_reg(src->regoff);
			}

			if (iptr->dst->flags & INMEMORY) {
				i386_mov_reg_membase(I386_EDX, REG_SP, iptr->dst->regoff * 8);

			} else {
				M_INTMOVE(I386_EDX, iptr->dst->regoff);
			}
			break;

		case ICMD_IDIVPOW2:   /* ..., value  ==> ..., value >> constant       */
		                      /* val.i = constant                             */

			/* TODO: optimize for `/ 2' */
			{
			int offset = 0;
			var_to_reg_int(s1, src, REG_ITMP1);
			d = reg_of_var(iptr->dst, REG_ITMP1);

			M_INTMOVE(s1, d);
			i386_test_reg_reg(d, d);
			offset += 2;
			CALCIMMEDIATEBYTES((1 << iptr->val.i) - 1);
			i386_jcc(I386_CC_NS, offset);
			i386_alu_imm_reg(I386_ADD, (1 << iptr->val.i) - 1, d);
				
			i386_shift_imm_reg(I386_SAR, iptr->val.i, d);
			store_reg_to_var_int(iptr->dst, d);
			}
			break;

		case ICMD_LDIVPOW2:   /* ..., value  ==> ..., value >> constant       */
		                      /* val.i = constant                             */

			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->dst->flags & INMEMORY) {
				if (src->flags & INMEMORY) {
					int offset = 0;
					offset += 2;
					CALCIMMEDIATEBYTES((1 << iptr->val.i) - 1);
					offset += 3;
					i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
					i386_mov_membase_reg(REG_SP, src->regoff * 8 + 4, REG_ITMP2);

					i386_test_reg_reg(REG_ITMP2, REG_ITMP2);
					i386_jcc(I386_CC_NS, offset);
					i386_alu_imm_reg(I386_ADD, (1 << iptr->val.i) - 1, REG_ITMP1);
					i386_alu_imm_reg(I386_ADC, 0, REG_ITMP2);
					i386_shrd_imm_reg_reg(iptr->val.i, REG_ITMP2, REG_ITMP1);
					i386_shift_imm_reg(I386_SAR, iptr->val.i, REG_ITMP2);

					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
					i386_mov_reg_membase(REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);
				}
			}
			break;

		case ICMD_ISHL:       /* ..., val1, val2  ==> ..., val1 << val2       */

			d = reg_of_var(iptr->dst, REG_ITMP2);
			if (iptr->dst->flags & INMEMORY) {
				if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					if (src->prev->regoff == iptr->dst->regoff) {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, I386_ECX);
						i386_shift_membase(I386_SHL, REG_SP, iptr->dst->regoff * 8);

					} else {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, I386_ECX);
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_shift_reg(I386_SHL, REG_ITMP1);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
					}

				} else if ((src->flags & INMEMORY) && !(src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, I386_ECX);
					i386_mov_reg_membase(src->prev->regoff, REG_SP, iptr->dst->regoff * 8);
					i386_shift_membase(I386_SHL, REG_SP, iptr->dst->regoff * 8);

				} else if (!(src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					if (src->prev->regoff == iptr->dst->regoff) {
						M_INTMOVE(src->regoff, I386_ECX);
						i386_shift_membase(I386_SHL, REG_SP, iptr->dst->regoff * 8);

					} else {
						M_INTMOVE(src->regoff, I386_ECX);
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_shift_reg(I386_SHL, REG_ITMP1);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
					}

				} else {
					M_INTMOVE(src->regoff, I386_ECX);
					i386_mov_reg_membase(src->prev->regoff, REG_SP, iptr->dst->regoff * 8);
					i386_shift_membase(I386_SHL, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, I386_ECX);
					i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, iptr->dst->regoff);
					i386_shift_reg(I386_SHL, iptr->dst->regoff);

				} else if ((src->flags & INMEMORY) && !(src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, I386_ECX);
					M_INTMOVE(src->prev->regoff, iptr->dst->regoff);
					i386_shift_reg(I386_SHL, iptr->dst->regoff);

				} else if (!(src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					M_INTMOVE(src->regoff, I386_ECX);
					i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, iptr->dst->regoff);
					i386_shift_reg(I386_SHL, iptr->dst->regoff);

				} else {
					M_INTMOVE(src->regoff, I386_ECX);
					M_INTMOVE(src->prev->regoff, iptr->dst->regoff);
					i386_shift_reg(I386_SHL, iptr->dst->regoff);
				}
			}
			break;

		case ICMD_ISHLCONST:  /* ..., value  ==> ..., value << constant       */
		                      /* val.i = constant                             */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if ((src->flags & INMEMORY) && (iptr->dst->flags & INMEMORY)) {
				if (src->regoff == iptr->dst->regoff) {
					i386_shift_imm_membase(I386_SHL, iptr->val.i, REG_SP, iptr->dst->regoff * 8);

				} else {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
					i386_shift_imm_reg(I386_SHL, iptr->val.i, REG_ITMP1);
					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
				}

			} else if ((src->flags & INMEMORY) && !(iptr->dst->flags & INMEMORY)) {
				i386_mov_membase_reg(REG_SP, src->regoff * 8, iptr->dst->regoff);
				i386_shift_imm_reg(I386_SHL, iptr->val.i, iptr->dst->regoff);
				
			} else if (!(src->flags & INMEMORY) && (iptr->dst->flags & INMEMORY)) {
				i386_mov_reg_membase(src->regoff, REG_SP, iptr->dst->regoff * 8);
				i386_shift_imm_membase(I386_SHL, iptr->val.i, REG_SP, iptr->dst->regoff * 8);

			} else {
				M_INTMOVE(src->regoff, iptr->dst->regoff);
				i386_shift_imm_reg(I386_SHL, iptr->val.i, iptr->dst->regoff);
			}
			break;

		case ICMD_ISHR:       /* ..., val1, val2  ==> ..., val1 >> val2       */

			d = reg_of_var(iptr->dst, REG_ITMP2);
			if (iptr->dst->flags & INMEMORY) {
				if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					if (src->prev->regoff == iptr->dst->regoff) {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, I386_ECX);
						i386_shift_membase(I386_SAR, REG_SP, iptr->dst->regoff * 8);

					} else {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, I386_ECX);
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_shift_reg(I386_SAR, REG_ITMP1);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
					}

				} else if ((src->flags & INMEMORY) && !(src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, I386_ECX);
					i386_mov_reg_membase(src->prev->regoff, REG_SP, iptr->dst->regoff * 8);
					i386_shift_membase(I386_SAR, REG_SP, iptr->dst->regoff * 8);

				} else if (!(src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					if (src->prev->regoff == iptr->dst->regoff) {
						M_INTMOVE(src->regoff, I386_ECX);
						i386_shift_membase(I386_SAR, REG_SP, iptr->dst->regoff * 8);

					} else {
						M_INTMOVE(src->regoff, I386_ECX);
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_shift_reg(I386_SAR, REG_ITMP1);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
					}

				} else {
					M_INTMOVE(src->regoff, I386_ECX);
					i386_mov_reg_membase(src->prev->regoff, REG_SP, iptr->dst->regoff * 8);
					i386_shift_membase(I386_SAR, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, I386_ECX);
					i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, iptr->dst->regoff);
					i386_shift_reg(I386_SAR, iptr->dst->regoff);

				} else if ((src->flags & INMEMORY) && !(src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, I386_ECX);
					M_INTMOVE(src->prev->regoff, iptr->dst->regoff);
					i386_shift_reg(I386_SAR, iptr->dst->regoff);

				} else if (!(src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					M_INTMOVE(src->regoff, I386_ECX);
					i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, iptr->dst->regoff);
					i386_shift_reg(I386_SAR, iptr->dst->regoff);

				} else {
					M_INTMOVE(src->regoff, I386_ECX);
					M_INTMOVE(src->prev->regoff, iptr->dst->regoff);
					i386_shift_reg(I386_SAR, iptr->dst->regoff);
				}
			}
			break;

		case ICMD_ISHRCONST:  /* ..., value  ==> ..., value >> constant       */
		                      /* val.i = constant                             */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if ((src->flags & INMEMORY) && (iptr->dst->flags & INMEMORY)) {
				if (src->regoff == iptr->dst->regoff) {
					i386_shift_imm_membase(I386_SAR, iptr->val.i, REG_SP, iptr->dst->regoff * 8);

				} else {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
					i386_shift_imm_reg(I386_SAR, iptr->val.i, REG_ITMP1);
					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
				}

			} else if ((src->flags & INMEMORY) && !(iptr->dst->flags & INMEMORY)) {
				i386_mov_membase_reg(REG_SP, src->regoff * 8, iptr->dst->regoff);
				i386_shift_imm_reg(I386_SAR, iptr->val.i, iptr->dst->regoff);
				
			} else if (!(src->flags & INMEMORY) && (iptr->dst->flags & INMEMORY)) {
				i386_mov_reg_membase(src->regoff, REG_SP, iptr->dst->regoff * 8);
				i386_shift_imm_membase(I386_SAR, iptr->val.i, REG_SP, iptr->dst->regoff * 8);

			} else {
				M_INTMOVE(src->regoff, iptr->dst->regoff);
				i386_shift_imm_reg(I386_SAR, iptr->val.i, iptr->dst->regoff);
			}
			break;

		case ICMD_IUSHR:      /* ..., val1, val2  ==> ..., val1 >>> val2      */

			d = reg_of_var(iptr->dst, REG_ITMP2);
			if (iptr->dst->flags & INMEMORY) {
				if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					if (src->prev->regoff == iptr->dst->regoff) {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, I386_ECX);
						i386_shift_membase(I386_SHR, REG_SP, iptr->dst->regoff * 8);

					} else {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, I386_ECX);
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_shift_reg(I386_SHR, REG_ITMP1);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
					}

				} else if ((src->flags & INMEMORY) && !(src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, I386_ECX);
					i386_mov_reg_membase(src->prev->regoff, REG_SP, iptr->dst->regoff * 8);
					i386_shift_membase(I386_SHR, REG_SP, iptr->dst->regoff * 8);

				} else if (!(src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					if (src->prev->regoff == iptr->dst->regoff) {
						M_INTMOVE(src->regoff, I386_ECX);
						i386_shift_membase(I386_SHR, REG_SP, iptr->dst->regoff * 8);

					} else {
						M_INTMOVE(src->regoff, I386_ECX);
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_shift_reg(I386_SHR, REG_ITMP1);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
					}

				} else {
					M_INTMOVE(src->regoff, I386_ECX);
					i386_mov_reg_membase(src->prev->regoff, REG_SP, iptr->dst->regoff * 8);
					i386_shift_membase(I386_SHR, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, I386_ECX);
					i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, iptr->dst->regoff);
					i386_shift_reg(I386_SHR, iptr->dst->regoff);

				} else if ((src->flags & INMEMORY) && !(src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, I386_ECX);
					M_INTMOVE(src->prev->regoff, iptr->dst->regoff);
					i386_shift_reg(I386_SHR, iptr->dst->regoff);

				} else if (!(src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					M_INTMOVE(src->regoff, I386_ECX);
					i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, iptr->dst->regoff);
					i386_shift_reg(I386_SHR, iptr->dst->regoff);

				} else {
					M_INTMOVE(src->regoff, I386_ECX);
					M_INTMOVE(src->prev->regoff, iptr->dst->regoff);
					i386_shift_reg(I386_SHR, iptr->dst->regoff);
				}
			}
			break;

		case ICMD_IUSHRCONST: /* ..., value  ==> ..., value >>> constant      */
		                      /* val.i = constant                             */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if ((src->flags & INMEMORY) && (iptr->dst->flags & INMEMORY)) {
				if (src->regoff == iptr->dst->regoff) {
					i386_shift_imm_membase(I386_SHR, iptr->val.i, REG_SP, iptr->dst->regoff * 8);

				} else {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
					i386_shift_imm_reg(I386_SHR, iptr->val.i, REG_ITMP1);
					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
				}

			} else if ((src->flags & INMEMORY) && !(iptr->dst->flags & INMEMORY)) {
				i386_mov_membase_reg(REG_SP, src->regoff * 8, iptr->dst->regoff);
				i386_shift_imm_reg(I386_SHR, iptr->val.i, iptr->dst->regoff);
				
			} else if (!(src->flags & INMEMORY) && (iptr->dst->flags & INMEMORY)) {
				i386_mov_reg_membase(src->regoff, REG_SP, iptr->dst->regoff * 8);
				i386_shift_imm_membase(I386_SHR, iptr->val.i, REG_SP, iptr->dst->regoff * 8);

			} else {
				M_INTMOVE(src->regoff, iptr->dst->regoff);
				i386_shift_imm_reg(I386_SHR, iptr->val.i, iptr->dst->regoff);
			}
			break;

		case ICMD_LSHL:       /* ..., val1, val2  ==> ..., val1 << val2       */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if (iptr->dst->flags & INMEMORY ){
				if (src->prev->flags & INMEMORY) {
					if (src->prev->regoff == iptr->dst->regoff) {
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);

						if (src->flags & INMEMORY) {
							i386_mov_membase_reg(REG_SP, src->regoff * 8, I386_ECX);
						} else {
							M_INTMOVE(src->regoff, I386_ECX);
						}

						i386_test_imm_reg(32, I386_ECX);
						i386_jcc(I386_CC_E, 2 + 2);
						i386_mov_reg_reg(REG_ITMP1, REG_ITMP2);
						i386_alu_reg_reg(I386_XOR, REG_ITMP1, REG_ITMP1);
						
						i386_shld_reg_membase(REG_ITMP1, REG_SP, src->prev->regoff * 8 + 4);
						i386_shift_membase(I386_SHL, REG_SP, iptr->dst->regoff * 8);

					} else {
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8 + 4, REG_ITMP2);
						
						if (src->flags & INMEMORY) {
							i386_mov_membase_reg(REG_SP, src->regoff * 8, I386_ECX);
						} else {
							M_INTMOVE(src->regoff, I386_ECX);
						}
						
						i386_test_imm_reg(32, I386_ECX);
						i386_jcc(I386_CC_E, 2 + 2);
						i386_mov_reg_reg(REG_ITMP1, REG_ITMP2);
						i386_alu_reg_reg(I386_XOR, REG_ITMP1, REG_ITMP1);
						
						i386_shld_reg_reg(REG_ITMP1, REG_ITMP2);
						i386_shift_reg(I386_SHL, REG_ITMP1);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
						i386_mov_reg_membase(REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);
					}
				}
			}
			break;

/*  		case ICMD_LSHLCONST:  /* ..., value  ==> ..., value << constant       */
/*  		                      /* val.l = constant                             */

/*  			var_to_reg_int(s1, src, REG_ITMP1); */
/*  			d = reg_of_var(iptr->dst, REG_ITMP3); */
/*  			M_SLL_IMM(s1, iptr->val.l & 0x3f, d); */
/*  			store_reg_to_var_int(iptr->dst, d); */
/*  			break; */

		case ICMD_LSHR:       /* ..., val1, val2  ==> ..., val1 >> val2       */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if (iptr->dst->flags & INMEMORY ){
				if (src->prev->flags & INMEMORY) {
					if (src->prev->regoff == iptr->dst->regoff) {
						/* TODO: optimize */
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8 + 4, REG_ITMP2);

						if (src->flags & INMEMORY) {
							i386_mov_membase_reg(REG_SP, src->regoff * 8, I386_ECX);
						} else {
							M_INTMOVE(src->regoff, I386_ECX);
						}

						i386_test_imm_reg(32, I386_ECX);
						i386_jcc(I386_CC_E, 2 + 3);
						i386_mov_reg_reg(REG_ITMP2, REG_ITMP1);
						i386_shift_imm_reg(I386_SAR, 31, REG_ITMP2);
						
						i386_shrd_reg_reg(REG_ITMP2, REG_ITMP1);
						i386_shift_reg(I386_SAR, REG_ITMP2);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
						i386_mov_reg_membase(REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);

					} else {
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8 + 4, REG_ITMP2);

						if (src->flags & INMEMORY) {
							i386_mov_membase_reg(REG_SP, src->regoff * 8, I386_ECX);
						} else {
							M_INTMOVE(src->regoff, I386_ECX);
						}

						i386_test_imm_reg(32, I386_ECX);
						i386_jcc(I386_CC_E, 2 + 3);
						i386_mov_reg_reg(REG_ITMP2, REG_ITMP1);
						i386_shift_imm_reg(I386_SAR, 31, REG_ITMP2);
						
						i386_shrd_reg_reg(REG_ITMP2, REG_ITMP1);
						i386_shift_reg(I386_SAR, REG_ITMP2);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
						i386_mov_reg_membase(REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);
					}
				}
			}
			break;

/*  		case ICMD_LSHRCONST:  /* ..., value  ==> ..., value >> constant       */
/*  		                      /* val.l = constant                             */

/*  			var_to_reg_int(s1, src, REG_ITMP1); */
/*  			d = reg_of_var(iptr->dst, REG_ITMP3); */
/*  			M_SRA_IMM(s1, iptr->val.l & 0x3f, d); */
/*  			store_reg_to_var_int(iptr->dst, d); */
/*  			break; */

		case ICMD_LUSHR:      /* ..., val1, val2  ==> ..., val1 >>> val2      */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if (iptr->dst->flags & INMEMORY ){
				if (src->prev->flags & INMEMORY) {
					if (src->prev->regoff == iptr->dst->regoff) {
						/* TODO: optimize */
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8 + 4, REG_ITMP2);

						if (src->flags & INMEMORY) {
							i386_mov_membase_reg(REG_SP, src->regoff * 8, I386_ECX);
						} else {
							M_INTMOVE(src->regoff, I386_ECX);
						}

						i386_test_imm_reg(32, I386_ECX);
						i386_jcc(I386_CC_E, 2 + 2);
						i386_mov_reg_reg(REG_ITMP2, REG_ITMP1);
						i386_alu_reg_reg(I386_XOR, REG_ITMP2, REG_ITMP2);
						
						i386_shrd_reg_reg(REG_ITMP2, REG_ITMP1);
						i386_shift_reg(I386_SHR, REG_ITMP2);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
						i386_mov_reg_membase(REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);

					} else {
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8 + 4, REG_ITMP2);

						if (src->flags & INMEMORY) {
							i386_mov_membase_reg(REG_SP, src->regoff * 8, I386_ECX);
						} else {
							M_INTMOVE(src->regoff, I386_ECX);
						}

						i386_test_imm_reg(32, I386_ECX);
						i386_jcc(I386_CC_E, 2 + 2);
						i386_mov_reg_reg(REG_ITMP2, REG_ITMP1);
						i386_alu_reg_reg(I386_XOR, REG_ITMP2, REG_ITMP2);
						
						i386_shrd_reg_reg(REG_ITMP2, REG_ITMP1);
						i386_shift_reg(I386_SHR, REG_ITMP2);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
						i386_mov_reg_membase(REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);
					}
				}
			}
			break;

/*  		case ICMD_LUSHRCONST: /* ..., value  ==> ..., value >>> constant      */
/*  		                      /* val.l = constant                             */

/*  			var_to_reg_int(s1, src, REG_ITMP1); */
/*  			d = reg_of_var(iptr->dst, REG_ITMP3); */
/*  			M_SRL_IMM(s1, iptr->val.l & 0x3f, d); */
/*  			store_reg_to_var_int(iptr->dst, d); */
/*  			break; */

		case ICMD_IAND:       /* ..., val1, val2  ==> ..., val1 & val2        */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if (iptr->dst->flags & INMEMORY) {
				if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					if (src->regoff == iptr->dst->regoff) {
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_alu_reg_membase(I386_AND, REG_ITMP1, REG_SP, iptr->dst->regoff * 8);

					} else if (src->prev->regoff == iptr->dst->regoff) {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
						i386_alu_reg_membase(I386_AND, REG_ITMP1, REG_SP, iptr->dst->regoff * 8);

					} else {
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_alu_membase_reg(I386_AND, REG_SP, src->regoff * 8, REG_ITMP1);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
					}

				} else if ((src->flags & INMEMORY) && !(src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
					i386_alu_reg_reg(I386_AND, src->prev->regoff, REG_ITMP1);
					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);

				} else if (!(src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
					i386_alu_reg_reg(I386_AND, src->regoff, REG_ITMP1);
					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);

				} else {
					i386_mov_reg_membase(src->prev->regoff, REG_SP, iptr->dst->regoff * 8);
					i386_alu_reg_membase(I386_AND, src->regoff, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, iptr->dst->regoff);
					i386_alu_membase_reg(I386_AND, REG_SP, src->regoff * 8, iptr->dst->regoff);

				} else if ((src->flags & INMEMORY) && !(src->prev->flags & INMEMORY)) {
					M_INTMOVE(src->prev->regoff, iptr->dst->regoff);
					i386_alu_membase_reg(I386_AND, REG_SP, src->regoff * 8, iptr->dst->regoff);

				} else if (!(src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					M_INTMOVE(src->regoff, iptr->dst->regoff);
					i386_alu_membase_reg(I386_AND, REG_SP, src->prev->regoff * 8, iptr->dst->regoff);

				} else {
					if (src->regoff == iptr->dst->regoff) {
						i386_alu_reg_reg(I386_AND, src->prev->regoff, iptr->dst->regoff);

					} else {
						M_INTMOVE(src->prev->regoff, iptr->dst->regoff);
						i386_alu_reg_reg(I386_AND, src->regoff, iptr->dst->regoff);
					}
				}
			}
			break;

		case ICMD_IANDCONST:  /* ..., value  ==> ..., value & constant        */
		                      /* val.i = constant                             */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if (iptr->dst->flags & INMEMORY) {
				if (src->flags & INMEMORY) {
					if (src->regoff == iptr->dst->regoff) {
						i386_alu_imm_membase(I386_AND, iptr->val.i, REG_SP, iptr->dst->regoff * 8);

					} else {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
						i386_alu_imm_reg(I386_AND, iptr->val.i, REG_ITMP1);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
					}

				} else {
					i386_mov_reg_membase(src->regoff, REG_SP, iptr->dst->regoff * 8);
					i386_alu_imm_membase(I386_AND, iptr->val.i, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if (src->flags & INMEMORY) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, iptr->dst->regoff);
					i386_alu_imm_reg(I386_AND, iptr->val.i, iptr->dst->regoff);

				} else {
					M_INTMOVE(src->regoff, iptr->dst->regoff);
					i386_alu_imm_reg(I386_AND, iptr->val.i, iptr->dst->regoff);
				}
			}
			break;

		case ICMD_LAND:       /* ..., val1, val2  ==> ..., val1 & val2        */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if (iptr->dst->flags & INMEMORY) {
				if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					if (src->regoff == iptr->dst->regoff) {
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8 + 4, REG_ITMP2);
						i386_alu_reg_membase(I386_AND, REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
						i386_alu_reg_membase(I386_AND, REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);

					} else if (src->prev->regoff == iptr->dst->regoff) {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->regoff * 8 + 4, REG_ITMP2);
						i386_alu_reg_membase(I386_AND, REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
						i386_alu_reg_membase(I386_AND, REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);

					} else {
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8 + 4, REG_ITMP2);
						i386_alu_membase_reg(I386_AND, REG_SP, src->regoff * 8, REG_ITMP1);
						i386_alu_membase_reg(I386_AND, REG_SP, src->regoff * 8 + 4, REG_ITMP2);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
						i386_mov_reg_membase(REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);
					}
				}
			}
			break;

		case ICMD_LANDCONST:  /* ..., value  ==> ..., value & constant        */
		                      /* val.l = constant                             */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if (iptr->dst->flags & INMEMORY) {
				if (src->flags & INMEMORY) {
					if (src->regoff == iptr->dst->regoff) {
						i386_alu_imm_membase(I386_AND, iptr->val.l, REG_SP, iptr->dst->regoff * 8);
						i386_alu_imm_membase(I386_AND, iptr->val.l >> 32, REG_SP, iptr->dst->regoff * 8 + 4);

					} else {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->regoff * 8 + 4, REG_ITMP2);
						i386_alu_imm_reg(I386_AND, iptr->val.l, REG_ITMP1);
						i386_alu_imm_reg(I386_AND, iptr->val.l >> 32, REG_ITMP2);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
						i386_mov_reg_membase(REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);
					}
				}
			}
			break;

		case ICMD_IREMPOW2:   /* ..., value  ==> ..., value % constant        */
		                      /* val.i = constant                             */

			var_to_reg_int(s1, src, REG_ITMP1);
			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (s1 == d) {
				M_INTMOVE(s1, REG_ITMP1);
				s1 = REG_ITMP1;
			} 

			{
			int offset = 0;

			offset += 2;
			offset += 2;
			offset += 2;
			CALCIMMEDIATEBYTES(iptr->val.i);
			offset += 2;

			/* TODO: optimize */
			M_INTMOVE(s1, d);
			i386_alu_imm_reg(I386_AND, iptr->val.i, d);
			i386_test_reg_reg(s1, s1);
			i386_jcc(I386_CC_GE, offset);
 			i386_mov_reg_reg(s1, d);
  			i386_neg_reg(d);
  			i386_alu_imm_reg(I386_AND, iptr->val.i, d);
			i386_neg_reg(d);
			}

/*  			M_INTMOVE(s1, I386_EAX); */
/*  			i386_cltd(); */
/*  			i386_alu_reg_reg(I386_XOR, I386_EDX, I386_EAX); */
/*  			i386_alu_reg_reg(I386_SUB, I386_EDX, I386_EAX); */
/*  			i386_alu_reg_reg(I386_AND, iptr->val.i, I386_EAX); */
/*  			i386_alu_reg_reg(I386_XOR, I386_EDX, I386_EAX); */
/*  			i386_alu_reg_reg(I386_SUB, I386_EDX, I386_EAX); */
/*  			M_INTMOVE(I386_EAX, d); */

/*  			i386_alu_reg_reg(I386_XOR, d, d); */
/*  			i386_mov_imm_reg(iptr->val.i, I386_ECX); */
/*  			i386_shrd_reg_reg(s1, d); */
/*  			i386_shift_imm_reg(I386_SHR, 32 - iptr->val.i, d); */

			store_reg_to_var_int(iptr->dst, d);
			break;

		case ICMD_IREM0X10001:  /* ..., value  ==> ..., value % 0x100001      */
		
/*          b = value & 0xffff;
			a = value >> 16;
			a = ((b - a) & 0xffff) + (b < a);
*/
			{
				int offset = 0;
			var_to_reg_int(s1, src, REG_ITMP1);
			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (s1 == d) {
				M_MOV(s1, REG_ITMP3);
				s1 = REG_ITMP3;
			}

/*  			M_BLTZ(s1, 7); */
			i386_test_reg_reg(s1, s1);
			i386_jcc(I386_CC_L, 2 + 6 + 2 + 3 + 2 + 2 + 3 + 2 + 2 + 6 + 2 + 5);

/*              M_CZEXT(s1, REG_ITMP2); */
  			M_INTMOVE(s1, REG_ITMP2);
			i386_alu_imm_reg(I386_AND, 0x0000ffff, REG_ITMP2);

/*  			M_SRA_IMM(s1, 16, d); */
			M_INTMOVE(s1, d);
			i386_shift_imm_reg(I386_SAR, 16, d);

/*  			M_CMPLT(REG_ITMP2, d, REG_ITMP1); */
			i386_alu_reg_reg(I386_XOR, REG_ITMP1, REG_ITMP1);
			i386_alu_reg_reg(I386_CMP, d, REG_ITMP2);
			i386_setcc_reg(I386_CC_L, REG_ITMP1);

/*  			M_ISUB(REG_ITMP2, d, d); */
			i386_alu_reg_reg(I386_SUB, d, REG_ITMP2);
			M_INTMOVE(REG_ITMP2, d);

/*              M_CZEXT(d, d); */
			i386_alu_imm_reg(I386_AND, 0x0000ffff, d);

/*  			M_IADD(d, REG_ITMP1, d); */
			i386_alu_reg_reg(I386_ADD, REG_ITMP1, d);

/*  			M_BR(11 + (s1 == REG_ITMP1)); */
			offset = 2 + 2 + 2 + 6 + 2 + 3 + 2 + 2 + 3 + 2 + 2 + 6 + 2 + 2;
			if (s1 == REG_ITMP1) {
				offset += 3;
				CALCOFFSETBYTES(src->regoff * 8);
			}
			offset += 2 + 2 + 2 + 5 + 3 + 2;
			i386_jmp(offset);

/*  			M_ISUB(REG_ZERO, s1, REG_ITMP1); */
			i386_mov_reg_reg(s1, REG_ITMP1);
			i386_neg_reg(REG_ITMP1);

/*              M_CZEXT(REG_ITMP1, REG_ITMP2); */
			M_INTMOVE(REG_ITMP1, REG_ITMP2);
			i386_alu_imm_reg(I386_AND, 0x0000ffff, REG_ITMP2);

/*  			M_SRA_IMM(REG_ITMP1, 16, d); */
			M_INTMOVE(REG_ITMP1, d);
			i386_shift_imm_reg(I386_SAR, 16, d);

/*  			M_CMPLT(REG_ITMP2, d, REG_ITMP1); */
			i386_alu_reg_reg(I386_XOR, REG_ITMP1, REG_ITMP1);
			i386_alu_reg_reg(I386_CMP, d, REG_ITMP2);
			i386_setcc_reg(I386_CC_L, REG_ITMP1);

/*  			M_ISUB(REG_ITMP2, d, d); */
			i386_alu_reg_reg(I386_SUB, d, REG_ITMP2);
			M_INTMOVE(REG_ITMP2, d);

/*              M_CZEXT(d, d); */
			i386_alu_imm_reg(I386_AND, 0x0000ffff, d);

/*  			M_IADD(d, REG_ITMP1, d); */
			i386_alu_reg_reg(I386_ADD, REG_ITMP1, d);

/*  			M_ISUB(REG_ZERO, d, d); */
			i386_neg_reg(d);

  			if (s1 == REG_ITMP1) {
  				var_to_reg_int(s1, src, REG_ITMP1);
			}
/*  			M_SLL_IMM(s1, 33, REG_ITMP2); */
			M_INTMOVE(s1, REG_ITMP2);
			i386_shift_imm_reg(I386_SHL, 1, REG_ITMP2);

/*  			M_CMPEQ(REG_ITMP2, REG_ZERO, REG_ITMP2); */
			i386_test_reg_reg(REG_ITMP2, REG_ITMP2);
			i386_mov_imm_reg(0, REG_ITMP2);
			i386_setcc_reg(I386_CC_E, REG_ITMP2);

/*  			M_ISUB(d, REG_ITMP2, d); */
			i386_alu_reg_reg(I386_SUB, REG_ITMP2, d);

			store_reg_to_var_int(iptr->dst, d);
			}
			break;

		case ICMD_LREMPOW2:   /* ..., value  ==> ..., value % constant        */
		                      /* val.l = constant                             */

			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->dst->flags & INMEMORY) {
				if (src->flags & INMEMORY) {
  					int offset = 0;
/*  					i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1); */
/*  					i386_mov_membase_reg(REG_SP, src->regoff * 8 + 4, REG_ITMP2); */

/*  					M_INTMOVE(REG_ITMP1, REG_ITMP3); */
/*  					i386_test_reg_reg(REG_ITMP2, REG_ITMP2); */
/*  					i386_jcc(I386_CC_NS, offset); */
/*  					i386_alu_imm_reg(I386_ADD, (1 << iptr->val.l) - 1, REG_ITMP3); */
/*  					i386_alu_imm_reg(I386_ADC, 0, REG_ITMP2); */
					
/*  					i386_shrd_imm_reg_reg(iptr->val.l, REG_ITMP2, REG_ITMP3); */
/*  					i386_shift_imm_reg(I386_SAR, iptr->val.l, REG_ITMP2); */
/*  					i386_shld_imm_reg_reg(iptr->val.l, REG_ITMP3, REG_ITMP2); */
/*  					i386_shift_imm_reg(I386_SHL, iptr->val.l, REG_ITMP3); */

/*  					i386_alu_reg_reg(I386_SUB, REG_ITMP3, REG_ITMP1); */
/*  					i386_mov_membase_reg(REG_SP, src->regoff * 8 + 4, REG_ITMP3); */
/*  					i386_alu_reg_reg(I386_SBB, REG_ITMP2, REG_ITMP3); */

/*  					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8); */
/*  					i386_mov_reg_membase(REG_ITMP3, REG_SP, iptr->dst->regoff * 8 + 4); */

					offset += 3;
					CALCOFFSETBYTES(src->regoff * 8);
					offset += 3;
					CALCOFFSETBYTES(src->regoff * 8 + 4);

					offset += 2;
					offset += 3;
					offset += 2;

					/* TODO: hmm, don't know if this is always correct */
					offset += 2;
					CALCIMMEDIATEBYTES(iptr->val.l & 0x00000000ffffffff);
					offset += 2;
					CALCIMMEDIATEBYTES(iptr->val.l >> 32);

					offset += 2;
					offset += 3;
					offset += 2;

					i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
					i386_mov_membase_reg(REG_SP, src->regoff * 8 + 4, REG_ITMP2);
					
					i386_alu_imm_reg(I386_AND, iptr->val.l, REG_ITMP1);
					i386_alu_imm_reg(I386_AND, iptr->val.l >> 32, REG_ITMP2);
					i386_alu_imm_membase(I386_CMP, 0, REG_SP, src->regoff * 8 + 4);
					i386_jcc(I386_CC_GE, offset);

					i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
					i386_mov_membase_reg(REG_SP, src->regoff * 8 + 4, REG_ITMP2);
					
					i386_neg_reg(REG_ITMP1);
					i386_alu_imm_reg(I386_ADC, 0, REG_ITMP2);
					i386_neg_reg(REG_ITMP2);
					
					i386_alu_imm_reg(I386_AND, iptr->val.l, REG_ITMP1);
					i386_alu_imm_reg(I386_AND, iptr->val.l >> 32, REG_ITMP2);
					
					i386_neg_reg(REG_ITMP1);
					i386_alu_imm_reg(I386_ADC, 0, REG_ITMP2);
					i386_neg_reg(REG_ITMP2);

					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
					i386_mov_reg_membase(REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);
				}
			}
			break;

		case ICMD_LREM0X10001:/* ..., value  ==> ..., value % 0x10001         */

			var_to_reg_int(s1, src, REG_ITMP1);
			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (s1 == d) {
				M_MOV(s1, REG_ITMP3);
				s1 = REG_ITMP3;
				}
			M_CZEXT(s1, REG_ITMP2);
			M_SRA_IMM(s1, 16, d);
			M_CMPLT(REG_ITMP2, d, REG_ITMP1);
			M_LSUB(REG_ITMP2, d, d);
            M_CZEXT(d, d);
			M_LADD(d, REG_ITMP1, d);
			M_LDA(REG_ITMP2, REG_ZERO, -1);
			M_SRL_IMM(REG_ITMP2, 33, REG_ITMP2);
			if (s1 == REG_ITMP1) {
				var_to_reg_int(s1, src, REG_ITMP1);
				}
			M_CMPULT(s1, REG_ITMP2, REG_ITMP2);
			M_BNEZ(REG_ITMP2, 11);
			M_LDA(d, REG_ZERO, -257);
			M_ZAPNOT_IMM(d, 0xcd, d);
			M_LSUB(REG_ZERO, s1, REG_ITMP2);
			M_CMOVGE(s1, s1, REG_ITMP2);
			M_UMULH(REG_ITMP2, d, REG_ITMP2);
			M_SRL_IMM(REG_ITMP2, 16, REG_ITMP2);
			M_LSUB(REG_ZERO, REG_ITMP2, d);
			M_CMOVGE(s1, REG_ITMP2, d);
			M_SLL_IMM(d, 16, REG_ITMP2);
			M_LADD(d, REG_ITMP2, d);
			M_LSUB(s1, d, d);
			store_reg_to_var_int(iptr->dst, d);
			break;

		case ICMD_IOR:        /* ..., val1, val2  ==> ..., val1 | val2        */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if (iptr->dst->flags & INMEMORY) {
				if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					if (src->regoff == iptr->dst->regoff) {
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_alu_reg_membase(I386_OR, REG_ITMP1, REG_SP, iptr->dst->regoff * 8);

					} else if (src->prev->regoff == iptr->dst->regoff) {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
						i386_alu_reg_membase(I386_OR, REG_ITMP1, REG_SP, iptr->dst->regoff * 8);

					} else {
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_alu_membase_reg(I386_OR, REG_SP, src->regoff * 8, REG_ITMP1);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
					}

				} else if ((src->flags & INMEMORY) && !(src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
					i386_alu_reg_reg(I386_OR, src->prev->regoff, REG_ITMP1);
					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);

				} else if (!(src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
					i386_alu_reg_reg(I386_OR, src->regoff, REG_ITMP1);
					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);

				} else {
					i386_mov_reg_membase(src->prev->regoff, REG_SP, iptr->dst->regoff * 8);
					i386_alu_reg_membase(I386_OR, src->regoff, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, iptr->dst->regoff);
					i386_alu_membase_reg(I386_OR, REG_SP, src->regoff * 8, iptr->dst->regoff);

				} else if ((src->flags & INMEMORY) && !(src->prev->flags & INMEMORY)) {
					M_INTMOVE(src->prev->regoff, iptr->dst->regoff);
					i386_alu_membase_reg(I386_OR, REG_SP, src->regoff * 8, iptr->dst->regoff);

				} else if (!(src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					M_INTMOVE(src->regoff, iptr->dst->regoff);
					i386_alu_membase_reg(I386_OR, REG_SP, src->prev->regoff * 8, iptr->dst->regoff);

				} else {
					if (src->regoff == iptr->dst->regoff) {
						i386_alu_reg_reg(I386_OR, src->prev->regoff, iptr->dst->regoff);

					} else {
						M_INTMOVE(src->prev->regoff, iptr->dst->regoff);
						i386_alu_reg_reg(I386_OR, src->regoff, iptr->dst->regoff);
					}
				}
			}
			break;

		case ICMD_IORCONST:   /* ..., value  ==> ..., value | constant        */
		                      /* val.i = constant                             */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if (iptr->dst->flags & INMEMORY) {
				if (src->flags & INMEMORY) {
					if (src->regoff == iptr->dst->regoff) {
						i386_alu_imm_membase(I386_OR, iptr->val.i, REG_SP, iptr->dst->regoff * 8);

					} else {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
						i386_alu_imm_reg(I386_OR, iptr->val.i, REG_ITMP1);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
					}

				} else {
					i386_mov_reg_membase(src->regoff, REG_SP, iptr->dst->regoff * 8);
					i386_alu_imm_membase(I386_OR, iptr->val.i, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if (src->flags & INMEMORY) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, iptr->dst->regoff);
					i386_alu_imm_reg(I386_OR, iptr->val.i, iptr->dst->regoff);

				} else {
					M_INTMOVE(src->regoff, iptr->dst->regoff);
					i386_alu_imm_reg(I386_OR, iptr->val.i, iptr->dst->regoff);
				}
			}
			break;

		case ICMD_LOR:        /* ..., val1, val2  ==> ..., val1 | val2        */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if (iptr->dst->flags & INMEMORY) {
				if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					if (src->regoff == iptr->dst->regoff) {
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8 + 4, REG_ITMP2);
						i386_alu_reg_membase(I386_OR, REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
						i386_alu_reg_membase(I386_OR, REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);

					} else if (src->prev->regoff == iptr->dst->regoff) {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->regoff * 8 + 4, REG_ITMP2);
						i386_alu_reg_membase(I386_OR, REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
						i386_alu_reg_membase(I386_OR, REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);

					} else {
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8 + 4, REG_ITMP2);
						i386_alu_membase_reg(I386_OR, REG_SP, src->regoff * 8, REG_ITMP1);
						i386_alu_membase_reg(I386_OR, REG_SP, src->regoff * 8 + 4, REG_ITMP2);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
						i386_mov_reg_membase(REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);
					}
				}
			}
			break;

		case ICMD_LORCONST:   /* ..., value  ==> ..., value | constant        */
		                      /* val.l = constant                             */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if (iptr->dst->flags & INMEMORY) {
				if (src->flags & INMEMORY) {
					if (src->regoff == iptr->dst->regoff) {
						i386_alu_imm_membase(I386_OR, iptr->val.l, REG_SP, iptr->dst->regoff * 8);
						i386_alu_imm_membase(I386_OR, iptr->val.l >> 32, REG_SP, iptr->dst->regoff * 8 + 4);

					} else {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->regoff * 8 + 4, REG_ITMP2);
						i386_alu_imm_reg(I386_OR, iptr->val.l, REG_ITMP1);
						i386_alu_imm_reg(I386_OR, iptr->val.l >> 32, REG_ITMP2);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
						i386_mov_reg_membase(REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);
					}
				}
			}
			break;

		case ICMD_IXOR:       /* ..., val1, val2  ==> ..., val1 ^ val2        */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if (iptr->dst->flags & INMEMORY) {
				if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					if (src->regoff == iptr->dst->regoff) {
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_alu_reg_membase(I386_XOR, REG_ITMP1, REG_SP, iptr->dst->regoff * 8);

					} else if (src->prev->regoff == iptr->dst->regoff) {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
						i386_alu_reg_membase(I386_XOR, REG_ITMP1, REG_SP, iptr->dst->regoff * 8);

					} else {
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_alu_membase_reg(I386_XOR, REG_SP, src->regoff * 8, REG_ITMP1);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
					}

				} else if ((src->flags & INMEMORY) && !(src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
					i386_alu_reg_reg(I386_XOR, src->prev->regoff, REG_ITMP1);
					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);

				} else if (!(src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
					i386_alu_reg_reg(I386_XOR, src->regoff, REG_ITMP1);
					i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);

				} else {
					i386_mov_reg_membase(src->prev->regoff, REG_SP, iptr->dst->regoff * 8);
					i386_alu_reg_membase(I386_XOR, src->regoff, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, iptr->dst->regoff);
					i386_alu_membase_reg(I386_XOR, REG_SP, src->regoff * 8, iptr->dst->regoff);

				} else if ((src->flags & INMEMORY) && !(src->prev->flags & INMEMORY)) {
					M_INTMOVE(src->prev->regoff, iptr->dst->regoff);
					i386_alu_membase_reg(I386_XOR, REG_SP, src->regoff * 8, iptr->dst->regoff);

				} else if (!(src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					M_INTMOVE(src->regoff, iptr->dst->regoff);
					i386_alu_membase_reg(I386_XOR, REG_SP, src->prev->regoff * 8, iptr->dst->regoff);

				} else {
					if (src->regoff == iptr->dst->regoff) {
						i386_alu_reg_reg(I386_XOR, src->prev->regoff, iptr->dst->regoff);

					} else {
						M_INTMOVE(src->prev->regoff, iptr->dst->regoff);
						i386_alu_reg_reg(I386_XOR, src->regoff, iptr->dst->regoff);
					}
				}
			}
			break;

		case ICMD_IXORCONST:  /* ..., value  ==> ..., value ^ constant        */
		                      /* val.i = constant                             */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if (iptr->dst->flags & INMEMORY) {
				if (src->flags & INMEMORY) {
					if (src->regoff == iptr->dst->regoff) {
						i386_alu_imm_membase(I386_XOR, iptr->val.i, REG_SP, iptr->dst->regoff * 8);

					} else {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
						i386_alu_imm_reg(I386_XOR, iptr->val.i, REG_ITMP1);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
					}

				} else {
					i386_mov_reg_membase(src->regoff, REG_SP, iptr->dst->regoff * 8);
					i386_alu_imm_membase(I386_XOR, iptr->val.i, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if (src->flags & INMEMORY) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, iptr->dst->regoff);
					i386_alu_imm_reg(I386_XOR, iptr->val.i, iptr->dst->regoff);

				} else {
					M_INTMOVE(src->regoff, iptr->dst->regoff);
					i386_alu_imm_reg(I386_XOR, iptr->val.i, iptr->dst->regoff);
				}
			}
			break;

		case ICMD_LXOR:       /* ..., val1, val2  ==> ..., val1 ^ val2        */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if (iptr->dst->flags & INMEMORY) {
				if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
					if (src->regoff == iptr->dst->regoff) {
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8 + 4, REG_ITMP2);
						i386_alu_reg_membase(I386_XOR, REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
						i386_alu_reg_membase(I386_XOR, REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);

					} else if (src->prev->regoff == iptr->dst->regoff) {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->regoff * 8 + 4, REG_ITMP2);
						i386_alu_reg_membase(I386_XOR, REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
						i386_alu_reg_membase(I386_XOR, REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);

					} else {
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->prev->regoff * 8 + 4, REG_ITMP2);
						i386_alu_membase_reg(I386_XOR, REG_SP, src->regoff * 8, REG_ITMP1);
						i386_alu_membase_reg(I386_XOR, REG_SP, src->regoff * 8 + 4, REG_ITMP2);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
						i386_mov_reg_membase(REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);
					}
				}
			}
			break;

		case ICMD_LXORCONST:  /* ..., value  ==> ..., value ^ constant        */
		                      /* val.l = constant                             */

			d = reg_of_var(iptr->dst, REG_ITMP1);
			if (iptr->dst->flags & INMEMORY) {
				if (src->flags & INMEMORY) {
					if (src->regoff == iptr->dst->regoff) {
						i386_alu_imm_membase(I386_XOR, iptr->val.l, REG_SP, iptr->dst->regoff * 8);
						i386_alu_imm_membase(I386_XOR, iptr->val.l >> 32, REG_SP, iptr->dst->regoff * 8 + 4);

					} else {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->regoff * 8 + 4, REG_ITMP2);
						i386_alu_imm_reg(I386_XOR, iptr->val.l, REG_ITMP1);
						i386_alu_imm_reg(I386_XOR, iptr->val.l >> 32, REG_ITMP2);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, iptr->dst->regoff * 8);
						i386_mov_reg_membase(REG_ITMP2, REG_SP, iptr->dst->regoff * 8 + 4);
					}
				}
			}
			break;


		case ICMD_LCMP:       /* ..., val1, val2  ==> ..., val1 cmp val2      */

			var_to_reg_int(s1, src->prev, REG_ITMP1);
			var_to_reg_int(s2, src, REG_ITMP2);
			d = reg_of_var(iptr->dst, REG_ITMP3);
			M_CMPLT(s1, s2, REG_ITMP3);
			M_CMPLT(s2, s1, REG_ITMP1);
			M_LSUB (REG_ITMP1, REG_ITMP3, d);
			store_reg_to_var_int(iptr->dst, d);
			break;

		case ICMD_IINC:       /* ..., value  ==> ..., value + constant        */
		                      /* op1 = variable, val.i = constant             */

			var = &(locals[iptr->op1][TYPE_INT]);
			if (var->flags & INMEMORY) {
				if (iptr->val.i == 1) {
					i386_inc_membase(REG_SP, var->regoff * 8);
 
				} else if (iptr->val.i == -1) {
					i386_dec_membase(REG_SP, var->regoff * 8);

				} else {
					i386_alu_imm_membase(I386_ADD, iptr->val.i, REG_SP, var->regoff * 8);
				}

			} else {
				if (iptr->val.i == 1) {
					i386_inc_reg(var->regoff);
 
				} else if (iptr->val.i == -1) {
					i386_dec_reg(var->regoff);

				} else {
					i386_alu_imm_reg(I386_ADD, iptr->val.i, var->regoff);
				}
			}
			break;


		/* floating operations ************************************************/

		case ICMD_FNEG:       /* ..., value  ==> ..., - value                 */
		case ICMD_DNEG:       /* ..., value  ==> ..., - value                 */

			var_to_reg_flt(s1, src, REG_FTMP1);
			d = reg_of_var(iptr->dst, REG_FTMP3);
			i386_fchs();
			store_reg_to_var_flt(iptr->dst, d);
			break;

		case ICMD_FADD:       /* ..., val1, val2  ==> ..., val1 + val2        */
		case ICMD_DADD:       /* ..., val1, val2  ==> ..., val1 + val2        */

			var_to_reg_flt(s1, src->prev, REG_FTMP1);
			var_to_reg_flt(s2, src, REG_FTMP2);
			d = reg_of_var(iptr->dst, REG_FTMP3);
			i386_faddp();
			store_reg_to_var_flt(iptr->dst, d);
			break;

		case ICMD_FSUB:       /* ..., val1, val2  ==> ..., val1 - val2        */
		case ICMD_DSUB:       /* ..., val1, val2  ==> ..., val1 - val2        */

			var_to_reg_flt(s1, src->prev, REG_FTMP1);
			var_to_reg_flt(s2, src, REG_FTMP2);
			d = reg_of_var(iptr->dst, REG_FTMP3);
			i386_fsubp();
			store_reg_to_var_flt(iptr->dst, d);
			break;

		case ICMD_FMUL:       /* ..., val1, val2  ==> ..., val1 * val2        */
		case ICMD_DMUL:       /* ..., val1, val2  ==> ..., val1 * val2        */

			var_to_reg_flt(s1, src->prev, REG_FTMP1);
			var_to_reg_flt(s2, src, REG_FTMP2);
			d = reg_of_var(iptr->dst, REG_FTMP3);
			i386_fmulp();
			store_reg_to_var_flt(iptr->dst, d);
			break;

		case ICMD_FDIV:       /* ..., val1, val2  ==> ..., val1 / val2        */
		case ICMD_DDIV:       /* ..., val1, val2  ==> ..., val1 / val2        */

			var_to_reg_flt(s1, src->prev, REG_FTMP1);
			var_to_reg_flt(s2, src, REG_FTMP2);
			d = reg_of_var(iptr->dst, REG_FTMP3);
			i386_fdivp();
			store_reg_to_var_flt(iptr->dst, d);
			break;

		case ICMD_FREM:       /* ..., val1, val2  ==> ..., val1 % val2        */
		case ICMD_DREM:       /* ..., val1, val2  ==> ..., val1 % val2        */

			/* exchanged to skip fxch */
			var_to_reg_flt(s2, src, REG_FTMP2);
			var_to_reg_flt(s1, src->prev, REG_FTMP1);
			d = reg_of_var(iptr->dst, REG_FTMP3);
/*  			i386_fxch(); */
			i386_fprem();
			i386_wait();
			i386_fnstsw();
			i386_sahf();
			i386_jcc(I386_CC_P, -(2 + 1 + 2 + 1 + 6));
			store_reg_to_var_flt(iptr->dst, d);
			i386_ffree(0);
			break;

		case ICMD_I2F:       /* ..., value  ==> ..., (float) value            */
		case ICMD_I2D:       /* ..., value  ==> ..., (double) value           */

			d = reg_of_var(iptr->dst, REG_FTMP1);
			if (src->flags & INMEMORY) {
				i386_fildl_membase(REG_SP, src->regoff * 8);

			} else {
				a = dseg_adds4(0);
				i386_mov_imm_reg(0, REG_ITMP1);
				dseg_adddata(mcodeptr);
				i386_mov_reg_membase(src->regoff, REG_ITMP1, a);
				i386_fildl_membase(REG_ITMP1, a);
			}
  			store_reg_to_var_flt(iptr->dst, d);
			break;

		case ICMD_L2F:       /* ..., value  ==> ..., (float) value            */
		case ICMD_L2D:       /* ..., value  ==> ..., (double) value           */

			d = reg_of_var(iptr->dst, REG_FTMP1);
			if (src->flags & INMEMORY) {
				i386_fildll_membase(REG_SP, src->regoff * 8);

			} else {
				panic("L2F: longs have to be in memory");
			}
  			store_reg_to_var_flt(iptr->dst, d);
			break;
			
		case ICMD_F2I:       /* ..., value  ==> ..., (int) value              */
		case ICMD_D2I:

			var_to_reg_flt(s1, src, REG_FTMP1);
			d = reg_of_var(iptr->dst, REG_ITMP1);

			a = dseg_adds4(0x0d7f);    /* Round to zero, 53-bit mode, exception masked */
			i386_mov_imm_reg(0, REG_ITMP1);
			dseg_adddata(mcodeptr);
			i386_fldcw_membase(REG_ITMP1, a);

			if (iptr->dst->flags & INMEMORY) {
				i386_fistpl_membase(REG_SP, iptr->dst->regoff * 8);

			} else {
				a = dseg_adds4(0);
				i386_fistpl_membase(REG_ITMP1, a);
				i386_mov_membase_reg(REG_ITMP1, a, iptr->dst->regoff);
			}

			a = dseg_adds4(0x027f);    /* Round to nearest, 53-bit mode, exceptions masked */
			i386_fldcw_membase(REG_ITMP1, a);
			break;

		case ICMD_F2L:       /* ..., value  ==> ..., (long) value             */
		case ICMD_D2L:

			var_to_reg_flt(s1, src, REG_FTMP1);
			d = reg_of_var(iptr->dst, REG_ITMP1);

			a = dseg_adds4(0x0d7f);    /* Round to zero, 53-bit mode, exception masked */
			i386_mov_imm_reg(0, REG_ITMP1);
			dseg_adddata(mcodeptr);
			i386_fldcw_membase(REG_ITMP1, a);

			if (iptr->dst->flags & INMEMORY) {
				i386_fistpll_membase(REG_SP, iptr->dst->regoff * 8);

			} else {
				panic("F2L: longs have to be in memory");
			}

			a = dseg_adds4(0x027f);    /* Round to nearest, 53-bit mode, exceptions masked */
			i386_fldcw_membase(REG_ITMP1, a);
			break;

		case ICMD_F2D:       /* ..., value  ==> ..., (double) value           */

			var_to_reg_flt(s1, src, REG_FTMP1);
			d = reg_of_var(iptr->dst, REG_ITMP3);
			/* nothing to do */
			store_reg_to_var_flt(iptr->dst, d);
			break;

		case ICMD_D2F:       /* ..., value  ==> ..., (float) value            */

			var_to_reg_flt(s1, src, REG_FTMP1);
			d = reg_of_var(iptr->dst, REG_ITMP3);
			/* nothing to do */
			store_reg_to_var_flt(iptr->dst, d);
			break;

		case ICMD_FCMPL:      /* ..., val1, val2  ==> ..., val1 fcmpl val2    */
		case ICMD_DCMPL:

			/* exchanged to skip fxch */
			var_to_reg_flt(s2, src->prev, REG_FTMP1);
			var_to_reg_flt(s1, src, REG_FTMP1);
			d = reg_of_var(iptr->dst, REG_ITMP3);
			i386_alu_reg_reg(I386_XOR, d, d);
/*    			i386_fxch(); */
			i386_fucompp();
			i386_fnstsw();
			i386_sahf();
			i386_jcc(I386_CC_E, 6 + 1 + 5 + 1);
			i386_jcc(I386_CC_B, 1 + 5);
			i386_dec_reg(d);
			i386_jmp(1);
			i386_inc_reg(d);
			store_reg_to_var_int(iptr->dst, d);
			break;

		case ICMD_FCMPG:      /* ..., val1, val2  ==> ..., val1 fcmpg val2    */
		case ICMD_DCMPG:

			/* exchanged to skip fxch */
			var_to_reg_flt(s2, src->prev, REG_FTMP1);
			var_to_reg_flt(s1, src, REG_FTMP1);
			d = reg_of_var(iptr->dst, REG_ITMP3);
			i386_alu_reg_reg(I386_XOR, d, d);
/*    			i386_fxch(); */
			i386_fucompp();
			i386_fnstsw();
			i386_sahf();
			i386_jcc(I386_CC_E, 6 + 1 + 5 + 1);
			i386_jcc(I386_CC_B, 1 + 5);
			i386_dec_reg(d);
			i386_jmp(1);
			i386_inc_reg(d);
			store_reg_to_var_int(iptr->dst, d);
			break;


		/* memory operations **************************************************/

			/* #define gen_bound_check \
			if (checkbounds) {\
				M_ILD(REG_ITMP3, s1, OFFSET(java_arrayheader, size));\
				M_CMPULT(s2, REG_ITMP3, REG_ITMP3);\
				M_BEQZ(REG_ITMP3, 0);\
				mcode_addxboundrefs(mcodeptr);\
				}
			*/

#define gen_bound_check \
            if (checkbounds) { \
                i386_alu_membase_reg(I386_CMP, s1, OFFSET(java_arrayheader, size), s2); \
                i386_jcc(I386_CC_A, 0); \
				mcode_addxboundrefs(mcodeptr); \
            }

		case ICMD_ARRAYLENGTH: /* ..., arrayref  ==> ..., length              */

			var_to_reg_int(s1, src, REG_ITMP1);
			d = reg_of_var(iptr->dst, REG_ITMP3);
			gen_nullptr_check(s1);
			i386_mov_membase_reg(s1, OFFSET(java_arrayheader, size), d);
			store_reg_to_var_int(iptr->dst, d);
			break;

		case ICMD_AALOAD:     /* ..., arrayref, index  ==> ..., value         */

			var_to_reg_int(s1, src->prev, REG_ITMP1);
			var_to_reg_int(s2, src, REG_ITMP2);
			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			i386_mov_memindex_reg(OFFSET(java_objectarray, data[0]), s1, s2, 2, d);
			store_reg_to_var_int(iptr->dst, d);
			break;

		case ICMD_LALOAD:     /* ..., arrayref, index  ==> ..., value         */

			var_to_reg_int(s1, src->prev, REG_ITMP1);
			var_to_reg_int(s2, src, REG_ITMP2);
			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			
			if (iptr->dst->flags & INMEMORY) {
				i386_mov_memindex_reg(OFFSET(java_longarray, data[0]), s1, s2, 2, REG_ITMP3);
				i386_mov_reg_membase(REG_ITMP3, REG_SP, iptr->dst->regoff * 8);
				i386_mov_memindex_reg(OFFSET(java_longarray, data[0]) + 4, s1, s2, 2, REG_ITMP3);
				i386_mov_reg_membase(REG_ITMP3, REG_SP, iptr->dst->regoff * 8 + 4);
			}
			break;

		case ICMD_IALOAD:     /* ..., arrayref, index  ==> ..., value         */

			var_to_reg_int(s1, src->prev, REG_ITMP1);
			var_to_reg_int(s2, src, REG_ITMP2);
			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			i386_mov_memindex_reg(OFFSET(java_intarray, data[0]), s1, s2, 2, d);
			store_reg_to_var_int(iptr->dst, d);
			break;

		case ICMD_FALOAD:     /* ..., arrayref, index  ==> ..., value         */

			var_to_reg_int(s1, src->prev, REG_ITMP1);
			var_to_reg_int(s2, src, REG_ITMP2);
			d = reg_of_var(iptr->dst, REG_FTMP3);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			i386_flds_memindex(OFFSET(java_floatarray, data[0]), s1, s2, 2);
			store_reg_to_var_flt(iptr->dst, d);
			break;

		case ICMD_DALOAD:     /* ..., arrayref, index  ==> ..., value         */

			var_to_reg_int(s1, src->prev, REG_ITMP1);
			var_to_reg_int(s2, src, REG_ITMP2);
			d = reg_of_var(iptr->dst, REG_FTMP3);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			i386_fldl_memindex(OFFSET(java_doublearray, data[0]), s1, s2, 3);
			store_reg_to_var_flt(iptr->dst, d);
			break;

		case ICMD_CALOAD:     /* ..., arrayref, index  ==> ..., value         */

			var_to_reg_int(s1, src->prev, REG_ITMP1);
			var_to_reg_int(s2, src, REG_ITMP2);
			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			i386_movzwl_memindex_reg(OFFSET(java_chararray, data[0]), s1, s2, 1, d);
			store_reg_to_var_int(iptr->dst, d);
			break;			

		case ICMD_SALOAD:     /* ..., arrayref, index  ==> ..., value         */

			var_to_reg_int(s1, src->prev, REG_ITMP1);
			var_to_reg_int(s2, src, REG_ITMP2);
			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			i386_movswl_memindex_reg(OFFSET(java_shortarray, data[0]), s1, s2, 1, d);
			store_reg_to_var_int(iptr->dst, d);
			break;

		case ICMD_BALOAD:     /* ..., arrayref, index  ==> ..., value         */

			var_to_reg_int(s1, src->prev, REG_ITMP1);
			var_to_reg_int(s2, src, REG_ITMP2);
			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
   			i386_movzbl_memindex_reg(OFFSET(java_bytearray, data[0]), s1, s2, 0, d);
/*    			i386_alu_reg_reg(I386_XOR, REG_ITMP3, REG_ITMP3); */
/*    			i386_movb_memindex_reg(OFFSET(java_bytearray, data[0]), s1, s2, 0, REG_ITMP3); */
/*  			M_INTMOVE(REG_ITMP3, d); */
			store_reg_to_var_int(iptr->dst, d);
			break;


		case ICMD_AASTORE:    /* ..., arrayref, index, value  ==> ...         */

			var_to_reg_int(s1, src->prev->prev, REG_ITMP1);
			var_to_reg_int(s2, src->prev, REG_ITMP2);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			var_to_reg_int(s3, src, REG_ITMP3);
			i386_mov_reg_memindex(s3, OFFSET(java_objectarray, data[0]), s1, s2, 2);
			break;

		case ICMD_LASTORE:    /* ..., arrayref, index, value  ==> ...         */

			var_to_reg_int(s1, src->prev->prev, REG_ITMP1);
			var_to_reg_int(s2, src->prev, REG_ITMP2);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}

			if (src->flags & INMEMORY) {
				i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP3);
				i386_mov_reg_memindex(REG_ITMP3, OFFSET(java_longarray, data[0]), s1, s2, 2);
				i386_mov_membase_reg(REG_SP, src->regoff * 8 + 4, REG_ITMP3);
				i386_mov_reg_memindex(REG_ITMP3, OFFSET(java_longarray, data[0]) + 4, s1, s2, 2);
			}
			break;

		case ICMD_IASTORE:    /* ..., arrayref, index, value  ==> ...         */

			var_to_reg_int(s1, src->prev->prev, REG_ITMP1);
			var_to_reg_int(s2, src->prev, REG_ITMP2);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			var_to_reg_int(s3, src, REG_ITMP3);
			i386_mov_reg_memindex(s3, OFFSET(java_intarray, data[0]), s1, s2, 2);
			break;

		case ICMD_FASTORE:    /* ..., arrayref, index, value  ==> ...         */

			var_to_reg_int(s1, src->prev->prev, REG_ITMP1);
			var_to_reg_int(s2, src->prev, REG_ITMP2);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			var_to_reg_flt(s3, src, REG_FTMP3);
			i386_fstps_memindex(OFFSET(java_floatarray, data[0]), s1, s2, 2);
			break;

		case ICMD_DASTORE:    /* ..., arrayref, index, value  ==> ...         */

			var_to_reg_int(s1, src->prev->prev, REG_ITMP1);
			var_to_reg_int(s2, src->prev, REG_ITMP2);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			var_to_reg_flt(s3, src, REG_FTMP3);
			i386_fstpl_memindex(OFFSET(java_doublearray, data[0]), s1, s2, 3);
			break;

		case ICMD_CASTORE:    /* ..., arrayref, index, value  ==> ...         */

			var_to_reg_int(s1, src->prev->prev, REG_ITMP1);
			var_to_reg_int(s2, src->prev, REG_ITMP2);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			var_to_reg_int(s3, src, REG_ITMP3);
			i386_movw_reg_memindex(s3, OFFSET(java_chararray, data[0]), s1, s2, 1);
			break;

		case ICMD_SASTORE:    /* ..., arrayref, index, value  ==> ...         */

			var_to_reg_int(s1, src->prev->prev, REG_ITMP1);
			var_to_reg_int(s2, src->prev, REG_ITMP2);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			var_to_reg_int(s3, src, REG_ITMP3);
			i386_movw_reg_memindex(s3, OFFSET(java_shortarray, data[0]), s1, s2, 1);
			break;

		case ICMD_BASTORE:    /* ..., arrayref, index, value  ==> ...         */

			var_to_reg_int(s1, src->prev->prev, REG_ITMP1);
			var_to_reg_int(s2, src->prev, REG_ITMP2);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			var_to_reg_int(s3, src, REG_ITMP3);
  			M_INTMOVE(s3, REG_ITMP3);    /* because EBP, ESI, EDI have no xH and xL bytes */
			i386_movb_reg_memindex(REG_ITMP3, OFFSET(java_bytearray, data[0]), s1, s2, 0);
			break;


		case ICMD_PUTSTATIC:  /* ..., value  ==> ...                          */
		                      /* op1 = type, val.a = field address            */

			a = dseg_addaddress(&(((fieldinfo *)(iptr->val.a))->value));
			/* here it's slightly slower */
  			i386_mov_imm_reg(0, REG_ITMP2);
  			dseg_adddata(mcodeptr);
  			i386_mov_membase_reg(REG_ITMP2, a, REG_ITMP3);
			switch (iptr->op1) {
				case TYPE_INT:
					var_to_reg_int(s2, src, REG_ITMP1);
					i386_mov_reg_membase(s2, REG_ITMP3, 0);
					break;
				case TYPE_LNG:
					if (src->flags & INMEMORY) {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
						i386_mov_membase_reg(REG_SP, src->regoff * 8 + 4, REG_ITMP2);
						i386_mov_reg_membase(REG_ITMP1, REG_ITMP3, 0);
						i386_mov_reg_membase(REG_ITMP2, REG_ITMP3, 0 + 4);
					} else {
						panic("PUTSTATIC: longs have to be in memory");
					}
					break;
				case TYPE_ADR:
					var_to_reg_int(s2, src, REG_ITMP1);
					i386_mov_reg_membase(s2, REG_ITMP3, 0);
					break;
				case TYPE_FLT:
					var_to_reg_flt(s2, src, REG_FTMP2);
					if (src->flags & INMEMORY) {
						i386_fstps_membase(REG_ITMP3, 0);
					} else {
						panic("PUTSTATIC: floats have to be in memory");
					}
					break;
				case TYPE_DBL:
					var_to_reg_flt(s2, src, REG_FTMP2);
					if (src->flags & INMEMORY) {
						i386_fstpl_membase(REG_ITMP3, 0);
					} else {
						panic("PUTSTATIC: doubles have to be in memory");
					}
					break;
				default: panic ("internal error");
				}
			break;

		case ICMD_GETSTATIC:  /* ...  ==> ..., value                          */
		                      /* op1 = type, val.a = field address            */

			a = dseg_addaddress(&(((fieldinfo *)(iptr->val.a))->value));
			i386_mov_imm_reg(0, REG_ITMP1);
			dseg_adddata(mcodeptr);
			i386_mov_membase_reg(REG_ITMP1, a, REG_ITMP1);
			switch (iptr->op1) {
				case TYPE_INT:
					d = reg_of_var(iptr->dst, REG_ITMP3);
					i386_mov_membase_reg(REG_ITMP1, 0, d);
					store_reg_to_var_int(iptr->dst, d);
					break;
				case TYPE_LNG:
					d = reg_of_var(iptr->dst, REG_ITMP3);
					if (iptr->dst->flags & INMEMORY) {
						i386_mov_membase_reg(REG_ITMP1, 0, REG_ITMP2);
						i386_mov_membase_reg(REG_ITMP1, 4, REG_ITMP3);
						i386_mov_reg_membase(REG_ITMP2, REG_SP, iptr->dst->regoff * 8);
						i386_mov_reg_membase(REG_ITMP3, REG_SP, iptr->dst->regoff * 8 + 4);
					} else {
						panic("GETSTATIC: longs have to be in memory");
					}
					break;
				case TYPE_ADR:
					d = reg_of_var(iptr->dst, REG_ITMP3);
					i386_mov_membase_reg(REG_ITMP1, 0, d);
					store_reg_to_var_int(iptr->dst, d);
					break;
				case TYPE_FLT:
					d = reg_of_var(iptr->dst, REG_ITMP3);
					if (iptr->dst->flags & INMEMORY) {
						i386_flds_membase(REG_ITMP1, 0);
					} else {
						panic("GETSTATIC: floats have to be in memory");
					}
					store_reg_to_var_flt(iptr->dst, d);
					break;
				case TYPE_DBL:				
					d = reg_of_var(iptr->dst, REG_ITMP3);
					if (iptr->dst->flags & INMEMORY) {
						i386_fldl_membase(REG_ITMP1, 0);
					} else {
						panic("GETSTATIC: doubles have to be in memory");
					}
					store_reg_to_var_flt(iptr->dst, d);
					break;
				default: panic ("internal error");
				}
			break;

		case ICMD_PUTFIELD:   /* ..., value  ==> ...                          */
		                      /* op1 = type, val.i = field offset             */

			a = ((fieldinfo *)(iptr->val.a))->offset;
			switch (iptr->op1) {
				case TYPE_INT:
					var_to_reg_int(s1, src->prev, REG_ITMP1);
					var_to_reg_int(s2, src, REG_ITMP2);
					gen_nullptr_check(s1);
					i386_mov_reg_membase(s2, s1, a);
					break;
				case TYPE_LNG:
					var_to_reg_int(s1, src->prev, REG_ITMP1);
					gen_nullptr_check(s1);
					if (src->flags & INMEMORY) {
						i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP2);
						i386_mov_membase_reg(REG_SP, src->regoff * 8 + 4, REG_ITMP3);
						i386_mov_reg_membase(REG_ITMP2, s1, a);
						i386_mov_reg_membase(REG_ITMP3, s1, a + 4);
					} else {
						panic("PUTFIELD: longs have to be in memory");
					}
					break;
				case TYPE_ADR:
					var_to_reg_int(s1, src->prev, REG_ITMP1);
					var_to_reg_int(s2, src, REG_ITMP2);
					gen_nullptr_check(s1);
					i386_mov_reg_membase(s2, s1, a);
					break;
				case TYPE_FLT:
					var_to_reg_int(s1, src->prev, REG_ITMP1);
					var_to_reg_flt(s2, src, REG_FTMP2);
					gen_nullptr_check(s1);
					if (src->flags & INMEMORY) {
						i386_fstps_membase(s1, a);
					} else {
						panic("PUTFIELD: floats have to be in memory");
					}
					break;
				case TYPE_DBL:
					var_to_reg_int(s1, src->prev, REG_ITMP1);
					var_to_reg_flt(s2, src, REG_FTMP2);
					gen_nullptr_check(s1);
					if (src->flags & INMEMORY) {
						i386_fstpl_membase(s1, a);
					} else {
						panic("PUTFIELD: doubles have to be in memory");
					}
					break;
				default: panic ("internal error");
				}
			break;

		case ICMD_GETFIELD:   /* ...  ==> ..., value                          */
		                      /* op1 = type, val.i = field offset             */

			a = ((fieldinfo *)(iptr->val.a))->offset;
			switch (iptr->op1) {
				case TYPE_INT:
					var_to_reg_int(s1, src, REG_ITMP1);
					d = reg_of_var(iptr->dst, REG_ITMP3);
					gen_nullptr_check(s1);
					i386_mov_membase_reg(s1, a, d);
					store_reg_to_var_int(iptr->dst, d);
					break;
				case TYPE_LNG:
					var_to_reg_int(s1, src, REG_ITMP1);
					gen_nullptr_check(s1);
					i386_mov_membase_reg(s1, a, REG_ITMP2);
					i386_mov_membase_reg(s1, a + 4, REG_ITMP3);
					i386_mov_reg_membase(REG_ITMP2, REG_SP, iptr->dst->regoff * 8);
					i386_mov_reg_membase(REG_ITMP3, REG_SP, iptr->dst->regoff * 8 + 4);
					break;
				case TYPE_ADR:
					var_to_reg_int(s1, src, REG_ITMP1);
					d = reg_of_var(iptr->dst, REG_ITMP3);
					gen_nullptr_check(s1);
					i386_mov_membase_reg(s1, a, d);
					store_reg_to_var_int(iptr->dst, d);
					break;
				case TYPE_FLT:
					var_to_reg_int(s1, src, REG_ITMP1);
					d = reg_of_var(iptr->dst, REG_FTMP1);
					gen_nullptr_check(s1);
					i386_flds_membase(s1, a);
   					store_reg_to_var_flt(iptr->dst, d);
					break;
				case TYPE_DBL:				
					var_to_reg_int(s1, src, REG_ITMP1);
					d = reg_of_var(iptr->dst, REG_FTMP1);
					gen_nullptr_check(s1);
					i386_fldl_membase(s1, a);
  					store_reg_to_var_flt(iptr->dst, d);
					break;
				default: panic ("internal error");
				}
			break;


		/* branch operations **************************************************/

			/* TWISTI */
/*  #define ALIGNCODENOP {if((int)((long)mcodeptr&7)){M_NOP;}} */
#define ALIGNCODENOP do {} while (0)

		case ICMD_ATHROW:       /* ..., objectref ==> ... (, objectref)       */

			var_to_reg_int(s1, src, REG_ITMP1);
			M_INTMOVE(s1, REG_ITMP1_XPTR);

			i386_call_imm(0);                    /* passing exception pointer */
			i386_pop_reg(REG_ITMP2_XPC);

			i386_push_imm(0);         /* push data segment pointer onto stack */
			dseg_adddata(mcodeptr);

  			i386_mov_imm_reg(asm_handle_exception, REG_ITMP3);
  			i386_jmp_reg(REG_ITMP3);
			i386_nop();          /* nop ensures that XPC is less than the end */
			                     /* of basic block                            */
			ALIGNCODENOP;
			break;

		case ICMD_GOTO:         /* ... ==> ...                                */
		                        /* op1 = target JavaVM pc                     */

			i386_jmp(0);
			mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
  			ALIGNCODENOP;
			break;

		case ICMD_JSR:          /* ... ==> ...                                */
		                        /* op1 = target JavaVM pc                     */

			i386_call_imm(0);
			mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			break;
			
		case ICMD_RET:          /* ... ==> ...                                */
		                        /* op1 = local variable                       */

			i386_ret();
			break;

		case ICMD_IFNULL:       /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc                     */

			if (src->flags & INMEMORY) {
				i386_alu_imm_membase(I386_CMP, 0, REG_SP, src->regoff * 8);

			} else {
				i386_alu_imm_reg(I386_CMP, 0, src->regoff);
			}
			i386_jcc(I386_CC_E, 0);
			mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			break;

		case ICMD_IFNONNULL:    /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc                     */

			if (src->flags & INMEMORY) {
				i386_alu_imm_membase(I386_CMP, 0, REG_SP, src->regoff * 8);

			} else {
				i386_alu_imm_reg(I386_CMP, 0, src->regoff);
			}
			i386_jcc(I386_CC_NE, 0);
			mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			break;

		case ICMD_IFEQ:         /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc, val.i = constant   */

			if (src->flags & INMEMORY) {
				i386_alu_imm_membase(I386_CMP, iptr->val.i, REG_SP, src->regoff * 8);

			} else {
				i386_alu_imm_reg(I386_CMP, iptr->val.i, src->regoff);
			}
			i386_jcc(I386_CC_E, 0);
			mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			break;

		case ICMD_IFLT:         /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc, val.i = constant   */

			if (src->flags & INMEMORY) {
				i386_alu_imm_membase(I386_CMP, iptr->val.i, REG_SP, src->regoff * 8);

			} else {
				i386_alu_imm_reg(I386_CMP, iptr->val.i, src->regoff);
			}
			i386_jcc(I386_CC_L, 0);
			mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			break;

		case ICMD_IFLE:         /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc, val.i = constant   */

			if (src->flags & INMEMORY) {
				i386_alu_imm_membase(I386_CMP, iptr->val.i, REG_SP, src->regoff * 8);

			} else {
				i386_alu_imm_reg(I386_CMP, iptr->val.i, src->regoff);
			}
			i386_jcc(I386_CC_LE, 0);
			mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			break;

		case ICMD_IFNE:         /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc, val.i = constant   */

			if (src->flags & INMEMORY) {
				i386_alu_imm_membase(I386_CMP, iptr->val.i, REG_SP, src->regoff * 8);

			} else {
				i386_alu_imm_reg(I386_CMP, iptr->val.i, src->regoff);
			}
			i386_jcc(I386_CC_NE, 0);
			mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			break;

		case ICMD_IFGT:         /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc, val.i = constant   */

			if (src->flags & INMEMORY) {
				i386_alu_imm_membase(I386_CMP, iptr->val.i, REG_SP, src->regoff * 8);

			} else {
				i386_alu_imm_reg(I386_CMP, iptr->val.i, src->regoff);
			}
			i386_jcc(I386_CC_G, 0);
			mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			break;

		case ICMD_IFGE:         /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc, val.i = constant   */

			if (src->flags & INMEMORY) {
				i386_alu_imm_membase(I386_CMP, iptr->val.i, REG_SP, src->regoff * 8);

			} else {
				i386_alu_imm_reg(I386_CMP, iptr->val.i, src->regoff);
			}
			i386_jcc(I386_CC_GE, 0);
			mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			break;

		case ICMD_IF_LEQ:       /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc, val.l = constant   */

			if (src->flags & INMEMORY) {
				if (iptr->val.l == 0) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
					i386_alu_membase_reg(I386_OR, REG_SP, src->regoff * 8 + 4, REG_ITMP1);

				} else if (iptr->val.l > 0 && iptr->val.l <= 0x00000000ffffffff) {
					i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
					i386_alu_imm_reg(I386_XOR, iptr->val.l, REG_ITMP1);
					i386_alu_membase_reg(I386_OR, REG_SP, src->regoff * 8 + 4, REG_ITMP1);
					
				} else {
					i386_mov_membase_reg(REG_SP, src->regoff * 8 + 4, REG_ITMP2);
					i386_alu_imm_reg(I386_XOR, iptr->val.l >> 32, REG_ITMP2);
					i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
					i386_alu_imm_reg(I386_XOR, iptr->val.l, REG_ITMP1);
					i386_alu_reg_reg(I386_OR, REG_ITMP2, REG_ITMP1);
				}
			}
			i386_test_reg_reg(REG_ITMP1, REG_ITMP1);
			i386_jcc(I386_CC_E, 0);
			mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			break;

		case ICMD_IF_LLT:       /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc, val.l = constant   */

			/* TODO: optimize as in IF_LEQ */
			if (src->flags & INMEMORY) {
				int offset;
				i386_alu_imm_membase(I386_CMP, iptr->val.l >> 32, REG_SP, src->regoff * 8 + 4);
				i386_jcc(I386_CC_L, 0);
				mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);

				offset = 3 + 6;
				if (src->regoff > 0) offset++;
				if (src->regoff > 31) offset += 3;
				CALCIMMEDIATEBYTES(iptr->val.l);

				i386_jcc(I386_CC_G, offset);

				i386_alu_imm_membase(I386_CMP, iptr->val.l, REG_SP, src->regoff * 8);
				i386_jcc(I386_CC_B, 0);
				mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			}			
			break;

		case ICMD_IF_LLE:       /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc, val.l = constant   */

			/* TODO: optimize as in IF_LEQ */
			if (src->flags & INMEMORY) {
				int offset;
				i386_alu_imm_membase(I386_CMP, iptr->val.l >> 32, REG_SP, src->regoff * 8 + 4);
				i386_jcc(I386_CC_L, 0);
				mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);

				offset = 3 + 6;
				if (src->regoff > 0) offset++;
				if (src->regoff > 31) offset += 3;
				CALCIMMEDIATEBYTES(iptr->val.l);
				
				i386_jcc(I386_CC_G, offset);

				i386_alu_imm_membase(I386_CMP, iptr->val.l, REG_SP, src->regoff * 8);
				i386_jcc(I386_CC_BE, 0);
				mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			}			
			break;

		case ICMD_IF_LNE:       /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc, val.l = constant   */

			/* TODO: optimize for val.l == 0 */
			if (src->flags & INMEMORY) {
				i386_mov_imm_reg(iptr->val.l, REG_ITMP1);
				i386_mov_imm_reg(iptr->val.l >> 32, REG_ITMP2);
				i386_alu_membase_reg(I386_XOR, REG_SP, src->regoff * 8, REG_ITMP1);
				i386_alu_membase_reg(I386_XOR, REG_SP, src->regoff * 8 + 4, REG_ITMP2);
				i386_alu_reg_reg(I386_OR, REG_ITMP2, REG_ITMP1);
				i386_test_reg_reg(REG_ITMP1, REG_ITMP1);
			}			
			i386_jcc(I386_CC_NE, 0);
			mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			break;

		case ICMD_IF_LGT:       /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc, val.l = constant   */

			/* TODO: optimize as in IF_LEQ */
			if (src->flags & INMEMORY) {
				int offset;
				i386_alu_imm_membase(I386_CMP, iptr->val.l >> 32, REG_SP, src->regoff * 8 + 4);
				i386_jcc(I386_CC_G, 0);
				mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);

				offset = 3 + 6;
				if (src->regoff > 0) offset++;
				if (src->regoff > 31) offset += 3;
				CALCIMMEDIATEBYTES(iptr->val.l);

				i386_jcc(I386_CC_L, offset);

				i386_alu_imm_membase(I386_CMP, iptr->val.l, REG_SP, src->regoff * 8);
				i386_jcc(I386_CC_A, 0);
				mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			}			
			break;

		case ICMD_IF_LGE:       /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc, val.l = constant   */

			/* TODO: optimize as in IF_LEQ */
			if (src->flags & INMEMORY) {
				int offset;
				i386_alu_imm_membase(I386_CMP, iptr->val.l >> 32, REG_SP, src->regoff * 8 + 4);
				i386_jcc(I386_CC_G, 0);
				mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);

				offset = 3 + 6;
				if (src->regoff > 0) offset++;
				if (src->regoff > 31) offset += 3;
				CALCIMMEDIATEBYTES(iptr->val.l);

				i386_jcc(I386_CC_L, offset);

				i386_alu_imm_membase(I386_CMP, iptr->val.l, REG_SP, src->regoff * 8);
				i386_jcc(I386_CC_AE, 0);
				mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			}			
			break;

		case ICMD_IF_ICMPEQ:    /* ..., value, value ==> ...                  */
		case ICMD_IF_ACMPEQ:    /* op1 = target JavaVM pc                     */

			if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
				i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
				i386_alu_reg_membase(I386_CMP, REG_ITMP1, REG_SP, src->prev->regoff * 8);

			} else if ((src->flags & INMEMORY) && !(src->prev->flags & INMEMORY)) {
				i386_alu_membase_reg(I386_CMP, REG_SP, src->regoff * 8, src->prev->regoff);

			} else if (!(src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
				i386_alu_reg_membase(I386_CMP, src->regoff, REG_SP, src->prev->regoff * 8);

			} else {
				i386_alu_reg_reg(I386_CMP, src->regoff, src->prev->regoff);
			}
			i386_jcc(I386_CC_E, 0);
			mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			break;

		case ICMD_IF_LCMPEQ:    /* ..., value, value ==> ...                  */
		                        /* op1 = target JavaVM pc                     */

			if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
				i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
				i386_mov_membase_reg(REG_SP, src->prev->regoff * 8 + 4, REG_ITMP2);
				i386_alu_membase_reg(I386_XOR, REG_SP, src->regoff * 8, REG_ITMP1);
				i386_alu_membase_reg(I386_XOR, REG_SP, src->regoff * 8 + 4, REG_ITMP2);
				i386_alu_reg_reg(I386_OR, REG_ITMP2, REG_ITMP1);
				i386_test_reg_reg(REG_ITMP1, REG_ITMP1);
			}			
			i386_jcc(I386_CC_E, 0);
			mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			break;

		case ICMD_IF_ICMPNE:    /* ..., value, value ==> ...                  */
		case ICMD_IF_ACMPNE:    /* op1 = target JavaVM pc                     */

			if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
				i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
				i386_alu_reg_membase(I386_CMP, REG_ITMP1, REG_SP, src->prev->regoff * 8);

			} else if ((src->flags & INMEMORY) && !(src->prev->flags & INMEMORY)) {
				i386_alu_membase_reg(I386_CMP, REG_SP, src->regoff * 8, src->prev->regoff);

			} else if (!(src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
				i386_alu_reg_membase(I386_CMP, src->regoff, REG_SP, src->prev->regoff * 8);

			} else {
				i386_alu_reg_reg(I386_CMP, src->regoff, src->prev->regoff);
			}
			i386_jcc(I386_CC_NE, 0);
			mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			break;

		case ICMD_IF_LCMPNE:    /* ..., value, value ==> ...                  */
		                        /* op1 = target JavaVM pc                     */

			if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
				i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
				i386_mov_membase_reg(REG_SP, src->prev->regoff * 8 + 4, REG_ITMP2);
				i386_alu_membase_reg(I386_XOR, REG_SP, src->regoff * 8, REG_ITMP1);
				i386_alu_membase_reg(I386_XOR, REG_SP, src->regoff * 8 + 4, REG_ITMP2);
				i386_alu_reg_reg(I386_OR, REG_ITMP2, REG_ITMP1);
				i386_test_reg_reg(REG_ITMP1, REG_ITMP1);
			}			
			i386_jcc(I386_CC_NE, 0);
			mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			break;

		case ICMD_IF_ICMPLT:    /* ..., value, value ==> ...                  */
		                        /* op1 = target JavaVM pc                     */

			if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
				i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
				i386_alu_reg_membase(I386_CMP, REG_ITMP1, REG_SP, src->prev->regoff * 8);

			} else if ((src->flags & INMEMORY) && !(src->prev->flags & INMEMORY)) {
				i386_alu_membase_reg(I386_CMP, REG_SP, src->regoff * 8, src->prev->regoff);

			} else if (!(src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
				i386_alu_reg_membase(I386_CMP, src->regoff, REG_SP, src->prev->regoff * 8);

			} else {
				i386_alu_reg_reg(I386_CMP, src->regoff, src->prev->regoff);
			}
			i386_jcc(I386_CC_L, 0);
			mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			break;

		case ICMD_IF_LCMPLT:    /* ..., value, value ==> ...                  */
	                            /* op1 = target JavaVM pc                     */

			if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
				int offset;
				i386_mov_membase_reg(REG_SP, src->prev->regoff * 8 + 4, REG_ITMP1);
				i386_alu_membase_reg(I386_CMP, REG_SP, src->regoff * 8 + 4, REG_ITMP1);
				i386_jcc(I386_CC_L, 0);
				mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);

				offset = 3 + 3 + 6;
				if (src->prev->regoff > 0) offset++;
				if (src->prev->regoff > 31) offset += 3;

				if (src->regoff > 0) offset++;
				if (src->regoff > 31) offset += 3;

				i386_jcc(I386_CC_G, offset);

				i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
				i386_alu_membase_reg(I386_CMP, REG_SP, src->regoff * 8, REG_ITMP1);
				i386_jcc(I386_CC_B, 0);
				mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			}			
			break;

		case ICMD_IF_ICMPGT:    /* ..., value, value ==> ...                  */
		                        /* op1 = target JavaVM pc                     */

			if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
				i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
				i386_alu_reg_membase(I386_CMP, REG_ITMP1, REG_SP, src->prev->regoff * 8);

			} else if ((src->flags & INMEMORY) && !(src->prev->flags & INMEMORY)) {
				i386_alu_membase_reg(I386_CMP, REG_SP, src->regoff * 8, src->prev->regoff);

			} else if (!(src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
				i386_alu_reg_membase(I386_CMP, src->regoff, REG_SP, src->prev->regoff * 8);

			} else {
				i386_alu_reg_reg(I386_CMP, src->regoff, src->prev->regoff);
			}
			i386_jcc(I386_CC_G, 0);
			mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			break;

		case ICMD_IF_LCMPGT:    /* ..., value, value ==> ...                  */
                                /* op1 = target JavaVM pc                     */

			if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
				int offset;
				i386_mov_membase_reg(REG_SP, src->prev->regoff * 8 + 4, REG_ITMP1);
				i386_alu_membase_reg(I386_CMP, REG_SP, src->regoff * 8 + 4, REG_ITMP1);
				i386_jcc(I386_CC_G, 0);
				mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);

				offset = 3 + 3 + 6;
				if (src->prev->regoff > 0) offset++;
				if (src->prev->regoff > 31) offset += 3;

				if (src->regoff > 0) offset++;
				if (src->regoff > 31) offset += 3;

				i386_jcc(I386_CC_L, offset);

				i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
				i386_alu_membase_reg(I386_CMP, REG_SP, src->regoff * 8, REG_ITMP1);
				i386_jcc(I386_CC_A, 0);
				mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			}			
			break;

		case ICMD_IF_ICMPLE:    /* ..., value, value ==> ...                  */
		                        /* op1 = target JavaVM pc                     */

			if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
				i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
				i386_alu_reg_membase(I386_CMP, REG_ITMP1, REG_SP, src->prev->regoff * 8);

			} else if ((src->flags & INMEMORY) && !(src->prev->flags & INMEMORY)) {
				i386_alu_membase_reg(I386_CMP, REG_SP, src->regoff * 8, src->prev->regoff);

			} else if (!(src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
				i386_alu_reg_membase(I386_CMP, src->regoff, REG_SP, src->prev->regoff * 8);

			} else {
				i386_alu_reg_reg(I386_CMP, src->regoff, src->prev->regoff);
			}
			i386_jcc(I386_CC_LE, 0);
			mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			break;

		case ICMD_IF_LCMPLE:    /* ..., value, value ==> ...                  */
		                        /* op1 = target JavaVM pc                     */

			if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
				int offset;
				i386_mov_membase_reg(REG_SP, src->prev->regoff * 8 + 4, REG_ITMP1);
				i386_alu_membase_reg(I386_CMP, REG_SP, src->regoff * 8 + 4, REG_ITMP1);
				i386_jcc(I386_CC_L, 0);
				mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);

				offset = 3 + 3 + 6;
				if (src->prev->regoff > 0) offset++;
				if (src->prev->regoff > 31) offset += 3;

				if (src->regoff > 0) offset++;
				if (src->regoff > 31) offset += 3;

				i386_jcc(I386_CC_G, offset);

				i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
				i386_alu_membase_reg(I386_CMP, REG_SP, src->regoff * 8, REG_ITMP1);
				i386_jcc(I386_CC_BE, 0);
				mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			}			
			break;

		case ICMD_IF_ICMPGE:    /* ..., value, value ==> ...                  */
		                        /* op1 = target JavaVM pc                     */

			if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
				i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
				i386_alu_reg_membase(I386_CMP, REG_ITMP1, REG_SP, src->prev->regoff * 8);

			} else if ((src->flags & INMEMORY) && !(src->prev->flags & INMEMORY)) {
				i386_alu_membase_reg(I386_CMP, REG_SP, src->regoff * 8, src->prev->regoff);

			} else if (!(src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
				i386_alu_reg_membase(I386_CMP, src->regoff, REG_SP, src->prev->regoff * 8);

			} else {
				i386_alu_reg_reg(I386_CMP, src->regoff, src->prev->regoff);
			}
			i386_jcc(I386_CC_GE, 0);
			mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			break;

		case ICMD_IF_LCMPGE:    /* ..., value, value ==> ...                  */
	                            /* op1 = target JavaVM pc                     */

			if ((src->flags & INMEMORY) && (src->prev->flags & INMEMORY)) {
				int offset;
				i386_mov_membase_reg(REG_SP, src->prev->regoff * 8 + 4, REG_ITMP1);
				i386_alu_membase_reg(I386_CMP, REG_SP, src->regoff * 8 + 4, REG_ITMP1);
				i386_jcc(I386_CC_G, 0);
				mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);

				offset = 3 + 3 + 6;
				if (src->prev->regoff > 0) offset++;
				if (src->prev->regoff > 31) offset += 3;

				if (src->regoff > 0) offset++;
				if (src->regoff > 31) offset += 3;

				i386_jcc(I386_CC_L, offset);

				i386_mov_membase_reg(REG_SP, src->prev->regoff * 8, REG_ITMP1);
				i386_alu_membase_reg(I386_CMP, REG_SP, src->regoff * 8, REG_ITMP1);
				i386_jcc(I386_CC_AE, 0);
				mcode_addreference(BlockPtrOfPC(iptr->op1), mcodeptr);
			}			
			break;

		/* (value xx 0) ? IFxx_ICONST : ELSE_ICONST                           */

		case ICMD_ELSE_ICONST:  /* handled by IFxx_ICONST                     */
			break;

		case ICMD_IFEQ_ICONST:  /* ..., value ==> ..., constant               */
		                        /* val.i = constant                           */

			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->dst->flags & INMEMORY) {
				int offset = 0;

				if (src->flags & INMEMORY) {
					i386_alu_imm_membase(I386_CMP, 0, REG_SP, src->regoff * 8);

				} else {
					i386_alu_imm_reg(I386_CMP, 0, src->regoff);
				}

				offset += 7;
				if (iptr->dst->regoff > 0) offset += 1;
				if (iptr->dst->regoff > 31) offset += 3;
	
				i386_jcc(I386_CC_NE, offset + (iptr[1].opc == ICMD_ELSE_ICONST) ? 5 + offset : 0);
				i386_mov_imm_membase(iptr->val.i, REG_SP, iptr->dst->regoff * 8);

				if ((iptr[1].opc == ICMD_ELSE_ICONST)) {
					i386_jmp(offset);
					i386_mov_imm_membase(iptr[1].val.i, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if (src->flags & INMEMORY) {
					i386_alu_imm_membase(I386_CMP, 0, REG_SP, src->regoff * 8);

				} else {
					i386_alu_imm_reg(I386_CMP, 0, src->regoff);
				}

				i386_jcc(I386_CC_NE, (iptr[1].opc == ICMD_ELSE_ICONST) ? 10 : 5);
				i386_mov_imm_reg(iptr->val.i, iptr->dst->regoff);

				if ((iptr[1].opc == ICMD_ELSE_ICONST)) {
					i386_jmp(5);
					i386_mov_imm_reg(iptr[1].val.i, iptr->dst->regoff);
				}
			}
			break;

		case ICMD_IFNE_ICONST:  /* ..., value ==> ..., constant               */
		                        /* val.i = constant                           */

			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->dst->flags & INMEMORY) {
				int offset = 0;

				if (src->flags & INMEMORY) {
					i386_alu_imm_membase(I386_CMP, 0, REG_SP, src->regoff * 8);

				} else {
					i386_alu_imm_reg(I386_CMP, 0, src->regoff);
				}

				offset += 7;
				if (iptr->dst->regoff > 0) offset += 1;
				if (iptr->dst->regoff > 31) offset += 3;
	
				i386_jcc(I386_CC_E, offset + (iptr[1].opc == ICMD_ELSE_ICONST) ? 5 + offset : 0);
				i386_mov_imm_membase(iptr->val.i, REG_SP, iptr->dst->regoff * 8);

				if ((iptr[1].opc == ICMD_ELSE_ICONST)) {
					i386_jmp(offset);
					i386_mov_imm_membase(iptr[1].val.i, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if (src->flags & INMEMORY) {
					i386_alu_imm_membase(I386_CMP, 0, REG_SP, src->regoff * 8);

				} else {
					i386_alu_imm_reg(I386_CMP, 0, src->regoff);
				}

				i386_jcc(I386_CC_E, (iptr[1].opc == ICMD_ELSE_ICONST) ? 10 : 5);
				i386_mov_imm_reg(iptr->val.i, iptr->dst->regoff);

				if ((iptr[1].opc == ICMD_ELSE_ICONST)) {
					i386_jmp(5);
					i386_mov_imm_reg(iptr[1].val.i, iptr->dst->regoff);
				}
			}
			break;

		case ICMD_IFLT_ICONST:  /* ..., value ==> ..., constant               */
		                        /* val.i = constant                           */

			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->dst->flags & INMEMORY) {
				int offset = 0;

				if (src->flags & INMEMORY) {
					i386_alu_imm_membase(I386_CMP, 0, REG_SP, src->regoff * 8);

				} else {
					i386_alu_imm_reg(I386_CMP, 0, src->regoff);
				}

				offset += 7;
				if (iptr->dst->regoff > 0) offset += 1;
				if (iptr->dst->regoff > 31) offset += 3;
	
				i386_jcc(I386_CC_GE, offset + (iptr[1].opc == ICMD_ELSE_ICONST) ? 5 + offset : 0);
				i386_mov_imm_membase(iptr->val.i, REG_SP, iptr->dst->regoff * 8);

				if ((iptr[1].opc == ICMD_ELSE_ICONST)) {
					i386_jmp(offset);
					i386_mov_imm_membase(iptr[1].val.i, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if (src->flags & INMEMORY) {
					i386_alu_imm_membase(I386_CMP, 0, REG_SP, src->regoff * 8);

				} else {
					i386_alu_imm_reg(I386_CMP, 0, src->regoff);
				}

				i386_jcc(I386_CC_GE, (iptr[1].opc == ICMD_ELSE_ICONST) ? 10 : 5);
				i386_mov_imm_reg(iptr->val.i, iptr->dst->regoff);

				if ((iptr[1].opc == ICMD_ELSE_ICONST)) {
					i386_jmp(5);
					i386_mov_imm_reg(iptr[1].val.i, iptr->dst->regoff);
				}
			}
			break;

		case ICMD_IFGE_ICONST:  /* ..., value ==> ..., constant               */
		                        /* val.i = constant                           */

			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->dst->flags & INMEMORY) {
				int offset = 0;

				if (src->flags & INMEMORY) {
					i386_alu_imm_membase(I386_CMP, 0, REG_SP, src->regoff * 8);

				} else {
					i386_alu_imm_reg(I386_CMP, 0, src->regoff);
				}

				offset += 7;
				if (iptr->dst->regoff > 0) offset += 1;
				if (iptr->dst->regoff > 31) offset += 3;
	
				i386_jcc(I386_CC_L, offset + (iptr[1].opc == ICMD_ELSE_ICONST) ? 5 + offset : 0);
				i386_mov_imm_membase(iptr->val.i, REG_SP, iptr->dst->regoff * 8);

				if ((iptr[1].opc == ICMD_ELSE_ICONST)) {
					i386_jmp(offset);
					i386_mov_imm_membase(iptr[1].val.i, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if (src->flags & INMEMORY) {
					i386_alu_imm_membase(I386_CMP, 0, REG_SP, src->regoff * 8);

				} else {
					i386_alu_imm_reg(I386_CMP, 0, src->regoff);
				}

				i386_jcc(I386_CC_L, (iptr[1].opc == ICMD_ELSE_ICONST) ? 10 : 5);
				i386_mov_imm_reg(iptr->val.i, iptr->dst->regoff);

				if ((iptr[1].opc == ICMD_ELSE_ICONST)) {
					i386_jmp(5);
					i386_mov_imm_reg(iptr[1].val.i, iptr->dst->regoff);
				}
			}
			break;

		case ICMD_IFGT_ICONST:  /* ..., value ==> ..., constant               */
		                        /* val.i = constant                           */

			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->dst->flags & INMEMORY) {
				int offset = 0;

				if (src->flags & INMEMORY) {
					i386_alu_imm_membase(I386_CMP, 0, REG_SP, src->regoff * 8);

				} else {
					i386_alu_imm_reg(I386_CMP, 0, src->regoff);
				}

				offset += 7;
				if (iptr->dst->regoff > 0) offset += 1;
				if (iptr->dst->regoff > 31) offset += 3;
	
				i386_jcc(I386_CC_LE, offset + (iptr[1].opc == ICMD_ELSE_ICONST) ? 5 + offset : 0);
				i386_mov_imm_membase(iptr->val.i, REG_SP, iptr->dst->regoff * 8);

				if ((iptr[1].opc == ICMD_ELSE_ICONST)) {
					i386_jmp(offset);
					i386_mov_imm_membase(iptr[1].val.i, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if (src->flags & INMEMORY) {
					i386_alu_imm_membase(I386_CMP, 0, REG_SP, src->regoff * 8);

				} else {
					i386_alu_imm_reg(I386_CMP, 0, src->regoff);
				}

				i386_jcc(I386_CC_LE, (iptr[1].opc == ICMD_ELSE_ICONST) ? 10 : 5);
				i386_mov_imm_reg(iptr->val.i, iptr->dst->regoff);

				if ((iptr[1].opc == ICMD_ELSE_ICONST)) {
					i386_jmp(5);
					i386_mov_imm_reg(iptr[1].val.i, iptr->dst->regoff);
				}
			}
			break;

		case ICMD_IFLE_ICONST:  /* ..., value ==> ..., constant               */
		                        /* val.i = constant                           */

			/* TWISTI: checked */
			d = reg_of_var(iptr->dst, REG_ITMP3);
			if (iptr->dst->flags & INMEMORY) {
				int offset = 0;

				if (src->flags & INMEMORY) {
					i386_alu_imm_membase(I386_CMP, 0, REG_SP, src->regoff * 8);

				} else {
					i386_alu_imm_reg(I386_CMP, 0, src->regoff);
				}

				offset += 7;
				if (iptr->dst->regoff > 0) offset += 1;
				if (iptr->dst->regoff > 31) offset += 3;
	
				i386_jcc(I386_CC_G, offset + (iptr[1].opc == ICMD_ELSE_ICONST) ? 5 + offset : 0);
				i386_mov_imm_membase(iptr->val.i, REG_SP, iptr->dst->regoff * 8);

				if ((iptr[1].opc == ICMD_ELSE_ICONST)) {
					i386_jmp(offset);
					i386_mov_imm_membase(iptr[1].val.i, REG_SP, iptr->dst->regoff * 8);
				}

			} else {
				if (src->flags & INMEMORY) {
					i386_alu_imm_membase(I386_CMP, 0, REG_SP, src->regoff * 8);

				} else {
					i386_alu_imm_reg(I386_CMP, 0, src->regoff);
				}

				i386_jcc(I386_CC_G, (iptr[1].opc == ICMD_ELSE_ICONST) ? 10 : 5);
				i386_mov_imm_reg(iptr->val.i, iptr->dst->regoff);

				if ((iptr[1].opc == ICMD_ELSE_ICONST)) {
					i386_jmp(5);
					i386_mov_imm_reg(iptr[1].val.i, iptr->dst->regoff);
				}
			}
			break;


		case ICMD_IRETURN:      /* ..., retvalue ==> ...                      */
		case ICMD_ARETURN:

#ifdef USE_THREADS
			if (checksync && (method->flags & ACC_SYNCHRONIZED)) {
				i386_mov_membase_reg(REG_SP, 8 * maxmemuse, REG_ITMP1);
				i386_alu_imm_reg(I386_SUB, 4, REG_SP);
				i386_mov_reg_membase(REG_ITMP1, REG_SP, 0);
				i386_mov_imm_reg(builtin_monitorexit, REG_ITMP1);
				i386_call_reg(REG_ITMP1);
				i386_alu_imm_reg(I386_ADD, 4, REG_SP);
			}
#endif
			var_to_reg_int(s1, src, REG_RESULT);
			M_INTMOVE(s1, REG_RESULT);
			goto nowperformreturn;

		case ICMD_LRETURN:      /* ..., retvalue ==> ...                      */

#ifdef USE_THREADS
			if (checksync && (method->flags & ACC_SYNCHRONIZED)) {
				i386_mov_membase_reg(REG_SP, 8 * maxmemuse, REG_ITMP1);
				i386_alu_imm_reg(I386_SUB, 4, REG_SP);
				i386_mov_reg_membase(REG_ITMP1, REG_SP, 0);
				i386_mov_imm_reg(builtin_monitorexit, REG_ITMP1);
				i386_call_reg(REG_ITMP1);
				i386_alu_imm_reg(I386_ADD, 4, REG_SP);
			}
#endif
			if (src->flags & INMEMORY) {
				i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_RESULT);
				i386_mov_membase_reg(REG_SP, src->regoff * 8 + 4, REG_RESULT2);

			} else {
				panic("LRETURN: longs have to be in memory");
			}
			goto nowperformreturn;

		case ICMD_FRETURN:      /* ..., retvalue ==> ...                      */

#ifdef USE_THREADS
			if (checksync && (method->flags & ACC_SYNCHRONIZED)) {
				i386_mov_membase_reg(REG_SP, 8 * maxmemuse, REG_ITMP1);
				i386_alu_imm_reg(I386_SUB, 4, REG_SP);
				i386_mov_reg_membase(REG_ITMP1, REG_SP, 0);
				i386_mov_imm_reg(builtin_monitorexit, REG_ITMP1);
				i386_call_reg(REG_ITMP1);
				i386_alu_imm_reg(I386_ADD, 4, REG_SP);
			}
#endif
			if (src->flags & INMEMORY) {
				i386_flds_membase(REG_SP, src->regoff * 8);

			} else {
				panic("FRETURN: floats have to be in memory");
			}
			goto nowperformreturn;

		case ICMD_DRETURN:      /* ..., retvalue ==> ...                      */

#ifdef USE_THREADS
			if (checksync && (method->flags & ACC_SYNCHRONIZED)) {
				i386_mov_membase_reg(REG_SP, 8 * maxmemuse, REG_ITMP1);
				i386_alu_imm_reg(I386_SUB, 4, REG_SP);
				i386_mov_reg_membase(REG_ITMP1, REG_SP, 0);
				i386_mov_imm_reg(builtin_monitorexit, REG_ITMP1);
				i386_call_reg(REG_ITMP1);
				i386_alu_imm_reg(I386_ADD, 4, REG_SP);
			}
#endif
			if (src->flags & INMEMORY) {
				i386_fldl_membase(REG_SP, src->regoff * 8);

			} else {
				panic("DRETURN: doubles have to be in memory");
			}
			goto nowperformreturn;

		case ICMD_RETURN:      /* ...  ==> ...                                */

#ifdef USE_THREADS
			if (checksync && (method->flags & ACC_SYNCHRONIZED)) {
				i386_mov_membase_reg(REG_SP, 8 * maxmemuse, REG_ITMP1);
				i386_alu_imm_reg(I386_SUB, 4, REG_SP);
				i386_mov_reg_membase(REG_ITMP1, REG_SP, 0);
				i386_mov_imm_reg(builtin_monitorexit, REG_ITMP1);
				i386_call_reg(REG_ITMP1);
				i386_alu_imm_reg(I386_ADD, 4, REG_SP);
			}
#endif

nowperformreturn:
			{
			int r, p;
			
			p = parentargs_base;
			
			/* restore return address                                         */

			if (!isleafmethod) {
				/* p--; M_LLD (REG_RA, REG_SP, 8 * p); -- do we really need this on i386 */
			}

			/* restore saved registers                                        */

			for (r = savintregcnt - 1; r >= maxsavintreguse; r--) {
				p--; i386_mov_membase_reg(REG_SP, p * 8, savintregs[r]);
			}
			for (r = savfltregcnt - 1; r >= maxsavfltreguse; r--) {
/*  				p--; M_DLD(savfltregs[r], REG_SP, 8 * p); */
			}

			/* deallocate stack                                               */

			if (parentargs_base) {
				i386_alu_imm_reg(I386_ADD, parentargs_base * 8, REG_SP);
			}

			/* call trace function */

			if (runverbose) {
				M_LDA (REG_SP, REG_SP, -24);
				M_AST(REG_RA, REG_SP, 0);
				M_LST(REG_RESULT, REG_SP, 8);
				M_DST(REG_FRESULT, REG_SP,16);
				a = dseg_addaddress (method);
				M_ALD(argintregs[0], REG_PV, a);
				M_MOV(REG_RESULT, argintregs[1]);
				M_FLTMOVE(REG_FRESULT, argfltregs[2]);
				a = dseg_addaddress ((void*) (builtin_displaymethodstop));
				M_ALD(REG_PV, REG_PV, a);
				M_JSR (REG_RA, REG_PV);
				s1 = (int)((u1*) mcodeptr - mcodebase);
				if (s1<=32768) M_LDA (REG_PV, REG_RA, -s1);
				else {
					s4 ml=-s1, mh=0;
					while (ml<-32768) { ml+=65536; mh--; }
					M_LDA (REG_PV, REG_RA, ml );
					M_LDAH (REG_PV, REG_PV, mh );
					}
				M_DLD(REG_FRESULT, REG_SP,16);
				M_LLD(REG_RESULT, REG_SP, 8);
				M_ALD(REG_RA, REG_SP, 0);
				M_LDA (REG_SP, REG_SP, 24);
				}

			i386_ret();
 			ALIGNCODENOP;
			}
			break;


		case ICMD_TABLESWITCH:  /* ..., index ==> ...                         */
			{
				s4 i, l, *s4ptr;
				void **tptr;

				tptr = (void **) iptr->target;

				s4ptr = iptr->val.a;
				l = s4ptr[1];                          /* low     */
				i = s4ptr[2];                          /* high    */

				var_to_reg_int(s1, src, REG_ITMP1);
				if (l == 0) {
					M_INTMOVE(s1, REG_ITMP1);
				} else if (l <= 32768) {
					i386_alu_imm_reg(I386_SUB, l, REG_ITMP1);
				}
				i = i - l + 1;

                /* range check */

				i386_alu_imm_reg(I386_CMP, i - 1, REG_ITMP1);
				i386_jcc(I386_CC_A, 0);

                /* mcode_addreference(BlockPtrOfPC(s4ptr[0]), mcodeptr); */
				mcode_addreference((basicblock *) tptr[0], mcodeptr);

				/* build jump table top down and use address of lowest entry */

                /* s4ptr += 3 + i; */
				tptr += i;

				while (--i >= 0) {
					/* dseg_addtarget(BlockPtrOfPC(*--s4ptr)); */
					dseg_addtarget((basicblock *) tptr[0]); 
					--tptr;
				}

				/* length of dataseg after last dseg_addtarget is used by load */

				i386_mov_imm_reg(0, REG_ITMP2);
				dseg_adddata(mcodeptr);
				i386_mov_memindex_reg(-dseglen, REG_ITMP2, REG_ITMP1, 2, REG_ITMP3);
				i386_jmp_reg(REG_ITMP3);
				ALIGNCODENOP;
			}
			break;


		case ICMD_LOOKUPSWITCH: /* ..., key ==> ...                           */
			{
				s4 i, l, val, *s4ptr;
				void **tptr;

				tptr = (void **) iptr->target;

				s4ptr = iptr->val.a;
				l = s4ptr[0];                          /* default  */
				i = s4ptr[1];                          /* count    */
			
				MCODECHECK((i<<2)+8);
				var_to_reg_int(s1, src, REG_ITMP1);    /* reg compare should always be faster */
				while (--i >= 0) {
					s4ptr += 2;
					++tptr;

					val = s4ptr[0];
					i386_alu_imm_reg(I386_CMP, val, s1);
					i386_jcc(I386_CC_E, 0);
					/* mcode_addreference(BlockPtrOfPC(s4ptr[1]), mcodeptr); */
					mcode_addreference((basicblock *) tptr[0], mcodeptr); 
				}

				i386_jmp(0);
				/* mcode_addreference(BlockPtrOfPC(l), mcodeptr); */
			
				tptr = (void **) iptr->target;
				mcode_addreference((basicblock *) tptr[0], mcodeptr);

				ALIGNCODENOP;
			}
			break;


		case ICMD_BUILTIN3:     /* ..., arg1, arg2, arg3 ==> ...              */
		                        /* op1 = return type, val.a = function pointer*/
			s3 = 3;
			goto gen_method;

		case ICMD_BUILTIN2:     /* ..., arg1, arg2 ==> ...                    */
		                        /* op1 = return type, val.a = function pointer*/
			s3 = 2;
			goto gen_method;

		case ICMD_BUILTIN1:     /* ..., arg1 ==> ...                          */
		                        /* op1 = return type, val.a = function pointer*/
			s3 = 1;
			goto gen_method;

		case ICMD_INVOKESTATIC: /* ..., [arg1, [arg2 ...]] ==> ...            */
		                        /* op1 = arg count, val.a = method pointer    */

		case ICMD_INVOKESPECIAL:/* ..., objectref, [arg1, [arg2 ...]] ==> ... */
		                        /* op1 = arg count, val.a = method pointer    */

		case ICMD_INVOKEVIRTUAL:/* ..., objectref, [arg1, [arg2 ...]] ==> ... */
		                        /* op1 = arg count, val.a = method pointer    */

		case ICMD_INVOKEINTERFACE:/*.., objectref, [arg1, [arg2 ...]] ==> ... */
		                        /* op1 = arg count, val.a = method pointer    */

			s3 = iptr->op1;

gen_method: {
			methodinfo   *m;
			classinfo    *ci;

			MCODECHECK((s3 << 1) + 64);

			/* copy arguments to registers or stack location                  */

			for (; --s3 >= 0; src = src->prev) {
				if (src->varkind == ARGVAR) {
					continue;
				}

				if (IS_INT_LNG_TYPE(src->type)) {
					if (s3 < intreg_argnum) {
						panic("No integer argument registers available!");

					} else {
						if (src->flags & INMEMORY) {
							i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
							i386_mov_reg_membase(REG_ITMP1, REG_SP, s3 * 8);

						} else {
							i386_mov_reg_membase(src->regoff, REG_SP, s3 * 4);
						}
					}

				} else {
					if (s3 < fltreg_argnum) {
						panic("No float argument registers available!");

					} else {
						var_to_reg_flt(d, src, REG_FTMP1);
						M_DST(d, REG_SP, 8 * (s3 - FLT_ARG_CNT));
					}
				}
			} /* end of for */

			m = iptr->val.a;
			switch (iptr->opc) {
				case ICMD_BUILTIN3:
				case ICMD_BUILTIN2:
				case ICMD_BUILTIN1:

					a = (s4) m;
					d = iptr->op1;

					i386_mov_imm_reg(0, REG_ITMP3);    /* we need the data segment address in asmpart */
					dseg_adddata(mcodeptr);

					i386_mov_imm_reg(a, REG_ITMP1);
					i386_call_reg(REG_ITMP1);
					break;

				case ICMD_INVOKESTATIC:
				case ICMD_INVOKESPECIAL:

					a = (s4) m->stubroutine;
					d = m->returntype;
					i386_mov_imm_reg(a, REG_ITMP2);
					i386_call_reg(REG_ITMP2);
					break;

				case ICMD_INVOKEVIRTUAL:

					i386_mov_membase_reg(REG_SP, 0, REG_ITMP2);
					gen_nullptr_check(REG_ITMP2);
					i386_mov_membase_reg(REG_ITMP2, OFFSET(java_objectheader, vftbl), REG_ITMP3);
					i386_mov_membase32_reg(REG_ITMP3, OFFSET(vftbl, table[0]) + sizeof(methodptr) * m->vftblindex, REG_ITMP1);

					d = m->returntype;
					i386_call_reg(REG_ITMP1);
					break;

				case ICMD_INVOKEINTERFACE:
					ci = m->class;

					i386_mov_membase_reg(REG_SP, 0, REG_ITMP2);
					gen_nullptr_check(REG_ITMP2);
					i386_mov_membase_reg(REG_ITMP2, OFFSET(java_objectheader, vftbl), REG_ITMP3);
					i386_mov_membase_reg(REG_ITMP3, OFFSET(vftbl, interfacetable[0]) - sizeof(methodptr) * ci->index, REG_ITMP3);
					i386_mov_membase32_reg(REG_ITMP3, sizeof(methodptr) * (m - ci->methods), REG_ITMP1);

					d = m->returntype;
					i386_call_reg(REG_ITMP1);
					break;

				default:
					d = 0;
					sprintf (logtext, "Unkown ICMD-Command: %d", iptr->opc);
					error ();
				}

			/* d contains return type */

			if (d != TYPE_VOID) {
				d = reg_of_var(iptr->dst, REG_ITMP3);

				if (IS_INT_LNG_TYPE(iptr->dst->type)) {
					if (IS_2_WORD_TYPE(iptr->dst->type)) {
						if (iptr->dst->flags & INMEMORY) {
							i386_mov_reg_membase(REG_RESULT, REG_SP, iptr->dst->regoff * 8);
							i386_mov_reg_membase(REG_RESULT2, REG_SP, iptr->dst->regoff * 8 + 4);

						} else {
							panic("RETURN: longs have to be in memory");
						}

					} else {
						if (iptr->dst->flags & INMEMORY) {
							i386_mov_reg_membase(REG_RESULT, REG_SP, iptr->dst->regoff * 8);

						} else {
							M_INTMOVE(REG_RESULT, iptr->dst->regoff);
						}
					}

				} else {
					if (iptr->dst->type == TYPE_FLT) {
						if (iptr->dst->flags & INMEMORY) {
							i386_fstps_membase(REG_SP, iptr->dst->regoff * 8);

						} else {
  							panic("RETURN: floats have to be in memory");
						}

					} else {
						if (iptr->dst->flags & INMEMORY) {
							i386_fstpl_membase(REG_SP, iptr->dst->regoff * 8);

						} else {
  							panic("RETURN: doubles have to be in memory");
						}
					}
				}
			}
			}
			break;


		case ICMD_INSTANCEOF: /* ..., objectref ==> ..., intresult            */

		                      /* op1:   0 == array, 1 == class                */
		                      /* val.a: (classinfo*) superclass               */

/*          superclass is an interface:
 *
 *          return (sub != NULL) &&
 *                 (sub->vftbl->interfacetablelength > super->index) &&
 *                 (sub->vftbl->interfacetable[-super->index] != NULL);
 *
 *          superclass is a class:
 *
 *          return ((sub != NULL) && (0
 *                  <= (sub->vftbl->baseval - super->vftbl->baseval) <=
 *                  super->vftbl->diffvall));
 */

			{
			classinfo *super = (classinfo*) iptr->val.a;
			
			var_to_reg_int(s1, src, REG_ITMP1);
			d = reg_of_var(iptr->dst, REG_ITMP3);
/*  			if (s1 == d) { */
/*  				M_MOV(s1, REG_ITMP1); */
/*  				s1 = REG_ITMP1; */
/*  			} */
			i386_alu_reg_reg(I386_XOR, d, d);
			if (iptr->op1) {                               /* class/interface */
				if (super->flags & ACC_INTERFACE) {        /* interface       */
					int offset = 0;
					i386_alu_imm_reg(I386_CMP, 0, s1);

					/* TODO: clean up this calculation */
					offset += 2;
					CALCOFFSETBYTES(OFFSET(java_objectheader, vftbl));

					offset += 2;
					CALCOFFSETBYTES(OFFSET(vftbl, interfacetablelength));
					
					offset += 2;
					CALCOFFSETBYTES(-super->index);
					
					offset += 3;
					offset += 6;

					offset += 2;
					CALCOFFSETBYTES(OFFSET(vftbl, interfacetable[0]) - super->index * sizeof(methodptr*));

					offset += 3;

					offset += 6;    /* jcc */
					offset += 5;

					i386_jcc(I386_CC_E, offset);

					i386_mov_membase_reg(s1, OFFSET(java_objectheader, vftbl), REG_ITMP1);
					i386_mov_membase_reg(REG_ITMP1, OFFSET(vftbl, interfacetablelength), REG_ITMP2);
					i386_alu_imm_reg(I386_SUB, super->index, REG_ITMP2);
					i386_alu_imm_reg(I386_CMP, 0, REG_ITMP2);

					/* TODO: clean up this calculation */
					offset = 0;
					offset += 2;
					CALCOFFSETBYTES(OFFSET(vftbl, interfacetable[0]) - super->index * sizeof(methodptr*));

					offset += 3;

					offset += 6;    /* jcc */
					offset += 5;

					i386_jcc(I386_CC_LE, offset);
					i386_mov_membase_reg(REG_ITMP1, OFFSET(vftbl, interfacetable[0]) - super->index * sizeof(methodptr*), REG_ITMP1);
					i386_alu_imm_reg(I386_CMP, 0, REG_ITMP1);
/*  					i386_setcc_reg(I386_CC_A, d); */
/*  					i386_jcc(I386_CC_BE, 5); */
					i386_jcc(I386_CC_E, 5);
					i386_mov_imm_reg(1, d);
					

				} else {                                   /* class           */
					int offset = 0;
					i386_alu_imm_reg(I386_CMP, 0, s1);

					/* TODO: clean up this calculation */
					offset += 2;
					CALCOFFSETBYTES(OFFSET(java_objectheader, vftbl));

					offset += 5;

					offset += 2;
					CALCOFFSETBYTES(OFFSET(vftbl, baseval));
					
					offset += 2;
					CALCOFFSETBYTES(OFFSET(vftbl, baseval));
					
					offset += 2;
					CALCOFFSETBYTES(OFFSET(vftbl, diffval));
					
					offset += 2;
					offset += 2;

					offset += 2;    /* xor */

					offset += 6;    /* jcc */
					offset += 5;

					i386_jcc(I386_CC_E, offset);

					i386_mov_membase_reg(s1, OFFSET(java_objectheader, vftbl), REG_ITMP1);
					i386_mov_imm_reg((void *) super->vftbl, REG_ITMP2);
					i386_mov_membase_reg(REG_ITMP1, OFFSET(vftbl, baseval), REG_ITMP1);
					i386_mov_membase_reg(REG_ITMP2, OFFSET(vftbl, baseval), REG_ITMP3);
					i386_mov_membase_reg(REG_ITMP2, OFFSET(vftbl, diffval), REG_ITMP2);
					i386_alu_reg_reg(I386_SUB, REG_ITMP3, REG_ITMP1);
					i386_alu_reg_reg(I386_XOR, d, d);
					i386_alu_reg_reg(I386_CMP, REG_ITMP2, REG_ITMP1);
/*  					i386_setcc_reg(I386_CC_BE, d); */
					i386_jcc(I386_CC_A, 5);
					i386_mov_imm_reg(1, d);
					
				}
			}
			else
				panic ("internal error: no inlined array instanceof");
			}
  			store_reg_to_var_int(iptr->dst, d);
			break;

		case ICMD_CHECKCAST:  /* ..., objectref ==> ..., objectref            */

		                      /* op1:   0 == array, 1 == class                */
		                      /* val.a: (classinfo*) superclass               */

/*          superclass is an interface:
 *
 *          OK if ((sub == NULL) ||
 *                 (sub->vftbl->interfacetablelength > super->index) &&
 *                 (sub->vftbl->interfacetable[-super->index] != NULL));
 *
 *          superclass is a class:
 *
 *          OK if ((sub == NULL) || (0
 *                 <= (sub->vftbl->baseval - super->vftbl->baseval) <=
 *                 super->vftbl->diffvall));
 */

			{
			classinfo *super = (classinfo*) iptr->val.a;
			
			d = reg_of_var(iptr->dst, REG_ITMP3);
			var_to_reg_int(s1, src, d);
			if (iptr->op1) {                               /* class/interface */
				if (super->flags & ACC_INTERFACE) {        /* interface       */
					int offset = 0;
					i386_alu_imm_reg(I386_CMP, 0, s1);

					/* TODO: clean up this calculation */
					offset += 2;
					CALCOFFSETBYTES(OFFSET(java_objectheader, vftbl));

					offset += 2;
					CALCOFFSETBYTES(OFFSET(vftbl, interfacetablelength));

					offset += 2;
					CALCOFFSETBYTES(-super->index);

					offset += 3;
					offset += 6;

					offset += 2;
					CALCOFFSETBYTES(OFFSET(vftbl, interfacetable[0]) - super->index * sizeof(methodptr*));

					offset += 3;
					offset += 6;

					i386_jcc(I386_CC_E, offset);

					i386_mov_membase_reg(s1, OFFSET(java_objectheader, vftbl), REG_ITMP1);
					i386_mov_membase_reg(REG_ITMP1, OFFSET(vftbl, interfacetablelength), REG_ITMP2);
					i386_alu_imm_reg(I386_SUB, super->index, REG_ITMP2);
					i386_alu_imm_reg(I386_CMP, 0, REG_ITMP2);
					i386_jcc(I386_CC_LE, 0);
					mcode_addxcastrefs(mcodeptr);
					i386_mov_membase_reg(REG_ITMP1, OFFSET(vftbl, interfacetable[0]) - super->index * sizeof(methodptr*), REG_ITMP2);
					i386_alu_imm_reg(I386_CMP, 0, REG_ITMP2);
					i386_jcc(I386_CC_E, 0);
					mcode_addxcastrefs(mcodeptr);

				} else {                                     /* class           */
					int offset = 0;
					i386_alu_imm_reg(I386_CMP, 0, s1);

					/* TODO: clean up this calculation */
					offset += 2;
					CALCOFFSETBYTES(OFFSET(java_objectheader, vftbl));

					offset += 5;

					offset += 2;
					CALCOFFSETBYTES(OFFSET(vftbl, baseval));

					if (d != REG_ITMP3) {
						offset += 2;
						CALCOFFSETBYTES(OFFSET(vftbl, baseval));
						
						offset += 2;
						CALCOFFSETBYTES(OFFSET(vftbl, diffval));

						offset += 2;
						
					} else {
						offset += 2;
						CALCOFFSETBYTES(OFFSET(vftbl, baseval));

						offset += 2;

						offset += 5;

						offset += 2;
						CALCOFFSETBYTES(OFFSET(vftbl, diffval));
					}

					offset += 2;

					offset += 6;

					i386_jcc(I386_CC_E, offset);

					i386_mov_membase_reg(s1, OFFSET(java_objectheader, vftbl), REG_ITMP1);
					i386_mov_imm_reg((void *) super->vftbl, REG_ITMP2);
					i386_mov_membase_reg(REG_ITMP1, OFFSET(vftbl, baseval), REG_ITMP1);
					if (d != REG_ITMP3) {
						i386_mov_membase_reg(REG_ITMP2, OFFSET(vftbl, baseval), REG_ITMP3);
						i386_mov_membase_reg(REG_ITMP2, OFFSET(vftbl, diffval), REG_ITMP2);
						i386_alu_reg_reg(I386_SUB, REG_ITMP3, REG_ITMP1);

					} else {
						i386_mov_membase_reg(REG_ITMP2, OFFSET(vftbl, baseval), REG_ITMP2);
						i386_alu_reg_reg(I386_SUB, REG_ITMP2, REG_ITMP1);
						i386_mov_imm_reg((void *) super->vftbl, REG_ITMP2);
						i386_mov_membase_reg(REG_ITMP2, OFFSET(vftbl, diffval), REG_ITMP2);
					}
					i386_alu_reg_reg(I386_CMP, REG_ITMP2, REG_ITMP1);
					i386_jcc(I386_CC_A, 0);    /* (u) REG_ITMP1 > (u) REG_ITMP2 -> jump */
					mcode_addxcastrefs(mcodeptr);
				}

			} else
				panic ("internal error: no inlined array checkcast");
			}
			M_INTMOVE(s1, d);
			store_reg_to_var_int(iptr->dst, d);
			break;

		case ICMD_CHECKASIZE:  /* ..., size ==> ..., size                     */

			if (src->flags & INMEMORY) {
				i386_alu_imm_membase(I386_CMP, 0, REG_SP, src->regoff * 8);
				
			} else {
				i386_alu_imm_reg(I386_CMP, 0, src->regoff);
			}
			i386_jcc(I386_CC_L, 0);
			mcode_addxcheckarefs(mcodeptr);
			break;

		case ICMD_MULTIANEWARRAY:/* ..., cnt1, [cnt2, ...] ==> ..., arrayref  */
		                      /* op1 = dimension, val.a = array descriptor    */

			/* check for negative sizes and copy sizes to stack if necessary  */

  			MCODECHECK((iptr->op1 << 1) + 64);

			for (s1 = iptr->op1; --s1 >= 0; src = src->prev) {
				if (src->flags & INMEMORY) {
					i386_alu_imm_membase(I386_CMP, 0, REG_SP, src->regoff * 8);

				} else {
					i386_alu_imm_reg(I386_CMP, 0, src->regoff);
				}
				i386_jcc(I386_CC_LE, 0);
				mcode_addxcheckarefs(mcodeptr);

				/* 
				 * copy sizes to new stack location, be cause native function
				 * builtin_nmultianewarray access them as (int *)
				 */
				i386_mov_membase_reg(REG_SP, src->regoff * 8, REG_ITMP1);
				i386_mov_reg_membase(REG_ITMP1, REG_SP, -(iptr->op1 - s1) * 4);

				/* copy sizes to stack (argument numbers >= INT_ARG_CNT)      */

				if (src->varkind != ARGVAR) {
					if (src->flags & INMEMORY) {
						i386_mov_membase_reg(REG_SP, (src->regoff + intreg_argnum) * 8, REG_ITMP1);
						i386_mov_reg_membase(REG_ITMP1, REG_SP, (s1 + intreg_argnum) * 8);

					} else {
						i386_mov_reg_membase(src->regoff, REG_SP, (s1 + intreg_argnum) * 8);
					}
				}
			}
			i386_alu_imm_reg(I386_SUB, iptr->op1 * 4, REG_SP);

			/* a0 = dimension count */

			/* save stack pointer */
			M_INTMOVE(REG_SP, REG_ITMP1);

			i386_alu_imm_reg(I386_SUB, 12, REG_SP);
			i386_mov_imm_membase(iptr->op1, REG_SP, 0);

			/* a1 = arraydescriptor */

			i386_mov_imm_membase(iptr->val.a, REG_SP, 4);

			/* a2 = pointer to dimensions = stack pointer */

			i386_mov_reg_membase(REG_ITMP1, REG_SP, 8);

			i386_mov_imm_reg((void*) (builtin_nmultianewarray), REG_ITMP1);
			i386_call_reg(REG_ITMP1);
			i386_alu_imm_reg(I386_ADD, 12 + iptr->op1 * 4, REG_SP);

			s1 = reg_of_var(iptr->dst, REG_RESULT);
			M_INTMOVE(REG_RESULT, s1);
			store_reg_to_var_int(iptr->dst, s1);
			break;


		default: sprintf (logtext, "Unknown pseudo command: %d", iptr->opc);
		         error();
	
   

	} /* switch */
		
	} /* for instruction */
		
	/* copy values to interface registers */

	src = bptr->outstack;
	len = bptr->outdepth;
	MCODECHECK(64+len);
	while (src) {
		len--;
		if ((src->varkind != STACKVAR)) {
			s2 = src->type;
			if (IS_FLT_DBL_TYPE(s2)) {
				var_to_reg_flt(s1, src, REG_FTMP1);
				if (!(interfaces[len][s2].flags & INMEMORY)) {
					M_FLTMOVE(s1,interfaces[len][s2].regoff);
					}
				else {
					M_DST(s1, REG_SP, 8 * interfaces[len][s2].regoff);
					}
				}
			else {
				var_to_reg_int(s1, src, REG_ITMP1);
				if (!(interfaces[len][s2].flags & INMEMORY)) {
					M_INTMOVE(s1,interfaces[len][s2].regoff);
					}
				else {
					M_LST(s1, REG_SP, 8 * interfaces[len][s2].regoff);
					}
				}
			}
		src = src->prev;
		}
	} /* if (bptr -> flags >= BBREACHED) */
	} /* for basic block */

	/* bptr -> mpc = (int)((u1*) mcodeptr - mcodebase); */

	{

	/* generate bound check stubs */
	s4 *xcodeptr = NULL;
	
	for (; xboundrefs != NULL; xboundrefs = xboundrefs->next) {
		if ((exceptiontablelength == 0) && (xcodeptr != NULL)) {
			gen_resolvebranch((u1*) mcodebase + xboundrefs->branchpos, 
				xboundrefs->branchpos, (u1*) xcodeptr - (u1*) mcodebase - (5 + 5 + 2));
			continue;
			}


		gen_resolvebranch((u1*) mcodebase + xboundrefs->branchpos, 
		                  xboundrefs->branchpos, (u1*) mcodeptr - mcodebase);

		MCODECHECK(8);

		i386_mov_imm_reg(0, REG_ITMP2_XPC);    /* 5 bytes */
		dseg_adddata(mcodeptr);
		i386_mov_imm_reg(xboundrefs->branchpos - 4, REG_ITMP1);    /* 5 bytes */
		i386_alu_reg_reg(I386_ADD, REG_ITMP1, REG_ITMP2_XPC);    /* 2 bytes */

		if (xcodeptr != NULL) {
			i386_jmp(((u1 *) xcodeptr - (u1 *) mcodeptr) - 4);

		} else {
			xcodeptr = mcodeptr;

			i386_push_imm(0);    /* push data segment pointer onto stack */
			dseg_adddata(mcodeptr);

			i386_mov_imm_reg(proto_java_lang_ArrayIndexOutOfBoundsException, REG_ITMP1_XPTR);
			i386_mov_imm_reg(asm_handle_exception, REG_ITMP3);
			i386_jmp_reg(REG_ITMP3);
		}
	}

	/* generate negative array size check stubs */
	xcodeptr = NULL;
	
	for (; xcheckarefs != NULL; xcheckarefs = xcheckarefs->next) {
		if ((exceptiontablelength == 0) && (xcodeptr != NULL)) {
			gen_resolvebranch((u1*) mcodebase + xcheckarefs->branchpos, 
				xcheckarefs->branchpos, (u1*) xcodeptr - (u1*) mcodebase - (5 + 5 + 2));
			continue;
			}

		gen_resolvebranch((u1*) mcodebase + xcheckarefs->branchpos, 
		                  xcheckarefs->branchpos, (u1*) mcodeptr - mcodebase);

		MCODECHECK(8);

		i386_mov_imm_reg(0, REG_ITMP2_XPC);    /* 5 bytes */
		dseg_adddata(mcodeptr);
		i386_mov_imm_reg(xcheckarefs->branchpos - 4, REG_ITMP1);    /* 5 bytes */
		i386_alu_reg_reg(I386_ADD, REG_ITMP1, REG_ITMP2_XPC);    /* 2 bytes */

		if (xcodeptr != NULL) {
			i386_jmp(((u1 *) xcodeptr - (u1 *) mcodeptr) - 4);

		} else {
			xcodeptr = mcodeptr;

			i386_push_imm(0);    /* push data segment pointer onto stack */
			dseg_adddata(mcodeptr);

			i386_mov_imm_reg(proto_java_lang_NegativeArraySizeException, REG_ITMP1_XPTR);
			i386_mov_imm_reg(asm_handle_exception, REG_ITMP3);
			i386_jmp_reg(REG_ITMP3);
		}
	}

	/* generate cast check stubs */
	xcodeptr = NULL;
	
	for (; xcastrefs != NULL; xcastrefs = xcastrefs->next) {
		if ((exceptiontablelength == 0) && (xcodeptr != NULL)) {
			gen_resolvebranch((u1*) mcodebase + xcastrefs->branchpos, 
				xcastrefs->branchpos, (u1*) xcodeptr - (u1*) mcodebase - (5 + 5 + 2));
			continue;
		}

		gen_resolvebranch((u1*) mcodebase + xcastrefs->branchpos, 
		                  xcastrefs->branchpos, (u1*) mcodeptr - mcodebase);

		MCODECHECK(8);

		i386_mov_imm_reg(0, REG_ITMP2_XPC);    /* 5 bytes */
		dseg_adddata(mcodeptr);
		i386_mov_imm_reg(xcastrefs->branchpos - 4, REG_ITMP1);    /* 5 bytes */
		i386_alu_reg_reg(I386_ADD, REG_ITMP1, REG_ITMP2_XPC);    /* 2 bytes */

		if (xcodeptr != NULL) {
			i386_jmp(((u1 *) xcodeptr - (u1 *) mcodeptr) - 4);
		
		} else {
			xcodeptr = mcodeptr;

			i386_push_imm(0);    /* push data segment pointer onto stack */
			dseg_adddata(mcodeptr);

			i386_mov_imm_reg(proto_java_lang_ClassCastException, REG_ITMP1_XPTR);
			i386_mov_imm_reg(asm_handle_exception, REG_ITMP3);
			i386_jmp_reg(REG_ITMP3);
		}
	}

	/* generate divide by zero check stubs */
	xcodeptr = NULL;
	
	for (; xdivrefs != NULL; xdivrefs = xdivrefs->next) {
		if ((exceptiontablelength == 0) && (xcodeptr != NULL)) {
			gen_resolvebranch((u1*) mcodebase + xdivrefs->branchpos, 
				xdivrefs->branchpos, (u1*) xcodeptr - (u1*) mcodebase - (5 + 5 + 2));
			continue;
		}

		gen_resolvebranch((u1*) mcodebase + xdivrefs->branchpos, 
		                  xdivrefs->branchpos, (u1*) mcodeptr - mcodebase);

		MCODECHECK(8);

		i386_mov_imm_reg(0, REG_ITMP2_XPC);    /* 5 bytes */
		dseg_adddata(mcodeptr);
		i386_mov_imm_reg(xdivrefs->branchpos - 4, REG_ITMP1);    /* 5 bytes */
		i386_alu_reg_reg(I386_ADD, REG_ITMP1, REG_ITMP2_XPC);    /* 2 bytes */

		if (xcodeptr != NULL) {
			i386_jmp(((u1 *) xcodeptr - (u1 *) mcodeptr) - 4);
		
		} else {
			xcodeptr = mcodeptr;

			i386_push_imm(0);    /* push data segment pointer onto stack */
			dseg_adddata(mcodeptr);

			i386_mov_imm_reg(proto_java_lang_ArithmeticException, REG_ITMP1_XPTR);
			i386_mov_imm_reg(asm_handle_exception, REG_ITMP3);
			i386_jmp_reg(REG_ITMP3);
		}
	}

#ifdef SOFTNULLPTRCHECK
	/* generate null pointer check stubs */
	xcodeptr = NULL;
	
	for (; xnullrefs != NULL; xnullrefs = xnullrefs->next) {
		if ((exceptiontablelength == 0) && (xcodeptr != NULL)) {
			gen_resolvebranch((u1*) mcodebase + xnullrefs->branchpos, 
				xnullrefs->branchpos, (u1*) xcodeptr - (u1*) mcodebase - (5 + 3));
			continue;
		}

		gen_resolvebranch((u1*) mcodebase + xnullrefs->branchpos, 
		                  xnullrefs->branchpos, (u1*) mcodeptr - mcodebase);

		MCODECHECK(8);

		i386_mov_imm_reg(0, REG_ITMP2_XPC);    /* 5 bytes */
		dseg_adddata(mcodeptr);
		i386_alu_imm_reg(I386_ADD, xnullrefs->branchpos - 4, REG_ITMP2_XPC);    /* 3 bytes */

		if (xcodeptr != NULL) {
			i386_jmp(((u1 *) xcodeptr - (u1 *) mcodeptr) - 4);
		
		} else {
			xcodeptr = mcodeptr;

			i386_push_imm(0);    /* push data segment pointer onto stack */
			dseg_adddata(mcodeptr);

			i386_mov_imm_reg(proto_java_lang_NullPointerException, REG_ITMP1_XPTR);
			i386_mov_imm_reg(asm_handle_exception, REG_ITMP3);
			i386_jmp_reg(REG_ITMP3);
		}
	}

#endif
	}

	mcode_finish((int)((u1*) mcodeptr - mcodebase));
}


/* function createcompilerstub *************************************************

	creates a stub routine which calls the compiler
	
*******************************************************************************/

#define COMPSTUBSIZE 3

u1 *createcompilerstub (methodinfo *m)
{
	u8 *s = CNEW (u8, COMPSTUBSIZE);    /* memory to hold the stub            */
	s4 *p = (s4*) s;                    /* code generation pointer            */

	s4 *mcodeptr = p;					/* make macros work                   */
	
	                                    /* code for the stub                  */
	i386_mov_imm_reg(m, I386_EAX);      /* pass method pointer to compiler    */
	i386_mov_imm_reg(asm_call_jit_compiler, REG_ITMP2);    /* load address    */
	i386_jmp_reg(REG_ITMP2);            /* jump to compiler                   */

#ifdef STATISTICS
	count_cstub_len += COMPSTUBSIZE * 8;
#endif

	return (u1*) s;
}


/* function removecompilerstub *************************************************

     deletes a compilerstub from memory  (simply by freeing it)

*******************************************************************************/

void removecompilerstub (u1 *stub) 
{
	CFREE (stub, COMPSTUBSIZE * 8);
}

/* function: createnativestub **************************************************

	creates a stub routine which calls a native method

*******************************************************************************/

#define NATIVESTUBSIZE 18

u1 *createnativestub (functionptr f, methodinfo *m)
{
	u8 *s = CNEW (u8, NATIVESTUBSIZE);  /* memory to hold the stub            */
	s4 *p = (s4*) s;                    /* code generation pointer            */

	/* TWISTI: get rid of those 2nd defines */
	s4 *mcodeptr = p;

	u1 *tptr;
	int i;
	int stackframesize = 4;           /* initial 4 bytes is space for jni env */
	int stackframeoffset = 4;

	reg_init();

  	descriptor2types(m);                     /* set paramcount and paramtypes */


/*  	utf_fprint(stdout, m->class->name); */
/*  	fprintf(stdout, ":"); */
/*    	utf_fprint(stdout, m->name); */
/*    	fprintf(stdout, "\nparamcount=%d paramtypes=%x\n", m->paramcount, m->paramtypes); */


	/*
	 * calculate stackframe size for native function
	 */
	tptr = m->paramtypes;
	for (i = 0; i < m->paramcount; i++) {
		switch (*tptr++) {
		case TYPE_INT:
		case TYPE_FLT:
		case TYPE_ADR:
			stackframesize += 4;
			break;

		case TYPE_LNG:
		case TYPE_DBL:
			stackframesize += 8;
			break;

		default:
			panic("unknown parameter type in native function");
		}
	}

	i386_alu_imm_reg(I386_SUB, stackframesize, REG_SP);

	tptr = m->paramtypes;
	for (i = 0; i < m->paramcount; i++) {
		switch (*tptr++) {
		case TYPE_INT:
		case TYPE_FLT:
		case TYPE_ADR:
			i386_mov_membase_reg(REG_SP, stackframesize + 4 + i * 8, REG_ITMP1);
			i386_mov_reg_membase(REG_ITMP1, REG_SP, stackframeoffset);
			stackframeoffset += 4;
			break;

		case TYPE_LNG:
		case TYPE_DBL:
			i386_mov_membase_reg(REG_SP, stackframesize + 4 + i * 8, REG_ITMP1);
			i386_mov_membase_reg(REG_SP, stackframesize + 4 + i * 8 + 4, REG_ITMP2);
			i386_mov_reg_membase(REG_ITMP1, REG_SP, stackframeoffset);
			i386_mov_reg_membase(REG_ITMP2, REG_SP, stackframeoffset + 4);
			stackframeoffset += 8;
			break;

		default:
			panic("unknown parameter type in native function");
		}
	}

 	i386_mov_imm_membase(&env, REG_SP, 0);

	i386_mov_imm_reg(f, REG_ITMP1);
	i386_call_reg(REG_ITMP1);

	i386_alu_imm_reg(I386_ADD, stackframesize, REG_SP);

/*  	M_LDA  (REG_PV, REG_RA, -15*4);      /* recompute pv from ra               */
/*  	M_ALD  (REG_ITMP3, REG_PV, 15*8);    /* get address of exceptionptr        */

/*  	M_ALD  (REG_RA, REG_SP, 0);         /* load return address                */
/*  	M_ALD  (REG_ITMP1, REG_ITMP3, 0);   /* load exception into reg. itmp1     */

/*  	M_LDA  (REG_SP, REG_SP, 8);         /* remove stackframe                  */
/*  	M_BNEZ (REG_ITMP1, 1);              /* if no exception then return        */

/*  	M_RET  (REG_ZERO, REG_RA);          /* return to caller                   */
	i386_ret();

/*  	M_AST  (REG_ZERO, REG_ITMP3, 0);    /* store NULL into exceptionptr       */
/*  	M_LDA  (REG_ITMP2, REG_RA, -4);     /* move fault address into reg. itmp2 */

/*  	M_ALD  (REG_ITMP3, REG_PV,16*8);    /* load asm exception handler address */
/*  	M_JMP  (REG_ZERO, REG_ITMP3);       /* jump to asm exception handler      */


	/* TWISTI */	
/*  	s[14] = (u8) f;                      /* address of native method          */
/*  	s[15] = (u8) (&exceptionptr);        /* address of exceptionptr           */
/*  	s[16] = (u8) (asm_handle_nat_exception); /* addr of asm exception handler */
/*  	s[17] = (u8) (&env);                  /* addr of jni_environement         */

#ifdef STATISTICS
	count_nstub_len += NATIVESTUBSIZE * 8;
#endif

	return (u1*) s;
}

/* function: removenativestub **************************************************

    removes a previously created native-stub from memory
    
*******************************************************************************/

void removenativestub (u1 *stub)
{
	CFREE (stub, NATIVESTUBSIZE * 8);
}


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
