/* -*- mode: c; tab-width: 4; c-basic-offset: 4 -*- */
/***************************** ncomp/ncomp.c ***********************************

	Copyright (c) 1997 A. Krall, R. Grafl, M. Gschwind, M. Probst

	See file COPYRIGHT for information on usage and disclaimer of warranties.

	Contains the functions which translates a JavaVM method into native code.
	This is the new version of the compiler which is a lot faster and has new
	exception handling schemes. The main function is new_comp.

	Authors: Andreas  Krall      EMAIL: cacao@complang.tuwien.ac.at
	         Reinhard Grafl      EMAIL: cacao@complang.tuwien.ac.at

	Last Change: 1997/11/05

*******************************************************************************/

#include "signal.h"
#include "global.h"
#include "ncomp/ncomp.h"

#include "loader.h"
#include "tables.h"
#include "builtin.h"
#include "native.h"
#include "asmpart.h"

#include "threads/thread.h"


/*************************** global switches **********************************/

bool compileverbose = false;
bool showstack = false;
bool showdisassemble = false; 
bool showddatasegment = false; 
bool showintermediate = false;
int  optimizelevel = 0;

bool checkbounds = true;
bool checknull = true;
bool checkfloats = true;
bool checksync = true;

bool getcompilingtime = false;
long compilingtime = 0;

int  has_ext_instr_set = 0;

bool statistics = false;         

int count_jit_calls = 0;
int count_methods = 0;
int count_spills = 0;
int count_pcmd_activ = 0;
int count_pcmd_drop = 0;
int count_pcmd_zero = 0;
int count_pcmd_const_store = 0;
int count_pcmd_const_alu = 0;
int count_pcmd_const_bra = 0;
int count_pcmd_load = 0;
int count_pcmd_move = 0;
int count_load_instruction = 0;
int count_pcmd_store = 0;
int count_pcmd_store_comb = 0;
int count_dup_instruction = 0;
int count_pcmd_op = 0;
int count_pcmd_mem = 0;
int count_pcmd_met = 0;
int count_pcmd_bra = 0;
int count_pcmd_table = 0;
int count_pcmd_return = 0;
int count_pcmd_returnx = 0;
int count_check_null = 0;
int count_check_bound = 0;
int count_max_basic_blocks = 0;
int count_basic_blocks = 0;
int count_javainstr = 0;
int count_max_javainstr = 0;
int count_javacodesize = 0;
int count_javaexcsize = 0;
int count_calls = 0;
int count_tryblocks = 0;
int count_code_len = 0;
int count_data_len = 0;
int count_cstub_len = 0;
int count_nstub_len = 0;
int count_max_new_stack = 0;
int count_upper_bound_new_stack = 0;
static int count_block_stack_init[11] = {0, 0, 0, 0, 0,  0, 0, 0, 0, 0,  0};
int *count_block_stack = count_block_stack_init;
static int count_analyse_iterations_init[5] = {0, 0, 0, 0, 0};
int *count_analyse_iterations = count_analyse_iterations_init;
static int count_method_bb_distribution_init[9] = {0, 0, 0, 0, 0,  0, 0, 0, 0};
int *count_method_bb_distribution = count_method_bb_distribution_init;
static int count_block_size_distribution_init[18] = {0, 0, 0, 0, 0,
	0, 0, 0, 0, 0,   0, 0, 0, 0, 0,   0, 0, 0};
int *count_block_size_distribution = count_block_size_distribution_init;
static int count_store_length_init[21] = {0, 0, 0, 0, 0,  0, 0, 0, 0, 0,
                                          0, 0, 0, 0, 0,  0, 0, 0, 0, 0,  0};
int *count_store_length = count_store_length_init;
static int count_store_depth_init[11] = {0, 0, 0, 0, 0,  0, 0, 0, 0, 0,   0};
int *count_store_depth = count_store_depth_init;


/*********************** include compiler data types **************************/ 

#include "ncomp/ncompdef.h"


/*********************** global compiler variables ****************************/

                                /* data about the currently compiled method   */

static classinfo  *class;       /* class the compiled method belongs to       */
static methodinfo *method;      /* pointer to method info of compiled method  */
static unicode    *descriptor;  /* type descriptor of compiled method         */
static u2         mparamcount;  /* number of parameters (incl. this)          */
static u1         *mparamtypes; /* types of all parameters (TYPE_INT, ...)    */
static u2         mreturntype;  /* return type of method                      */
	
static int maxstack;            /* maximal JavaVM stack size                  */
static int maxlocals;           /* maximal number of local JavaVM variables   */
static int jcodelength;         /* length of JavaVM-codes                     */
static u1 *jcode;               /* pointer to start of JavaVM-code            */
static int exceptiontablelength;/* length of exception table                  */
static exceptiontable *extable; /* pointer to start of exception table        */

static int block_count;         /* number of basic blocks                     */
static basicblock *block;       /* points to basic block array                */
static int *block_index;        /* a table which contains for every byte of   */
                                /* JavaVM code a basic block index if at this */
                                /* byte there is the start of a basic block   */

static int instr_count;         /* number of JavaVM instructions              */
static instruction *instr;      /* points to intermediate code instructions   */

static int stack_count;         /* number of stack elements                   */
static stackelement *stack;     /* points to intermediate code instructions   */

static bool isleafmethod;       /* true if a method doesn't call subroutines  */

/* list of all classes used by the compiled method which have to be           */
/* initialised (if not already done) before execution of this method          */

static chain *uninitializedclasses;  
                                

/******************** include compiler subsystems *****************************/

#include "sysdep/ngen.h"        /* code generator header file                 */ 
#include "ncomp/ntools.c"       /* compiler tool functions                    */ 
#include "ncomp/mcode.c"        /* code generation tool functions             */ 
#include "sysdep/disass.c"      /* disassembler (for debug purposes only)     */ 
#include "ncomp/nparse.c"       /* parsing of JavaVM code                     */ 
#include "ncomp/nreg.c"         /* register allocation and support routines   */ 
#include "ncomp/nstack.c"       /* analysing the stack operations             */ 
#include "sysdep/ngen.c"        /* code generator                             */ 




/* dummy function, used when there is no JavaVM code available                */

static void* do_nothing_function() 
{
	return NULL;
}


/*******************************************************************************

	new_compile, new version of compiler, translates one method to machine code

*******************************************************************************/

methodptr new_compile(methodinfo *m)
{
	int  dumpsize;
	long starttime = 0;
	long stoptime  = 0;

	/* if method has been already compiled return immediately */

	count_jit_calls++;

	if (m->entrypoint)
		return m->entrypoint;

	count_methods++;

	intsDisable();      /* disable interrupts */
	

	/* mark start of dump memory area */

	dumpsize = dump_size ();

	/* measure time */

	if (getcompilingtime)
		starttime = getcputime();

	/* if there is no javacode print error message and return empty method    */

	if (! m->jcode) {
		sprintf(logtext, "No code given for: ");
		unicode_sprint(logtext+strlen(logtext), m->class->name);
		strcpy(logtext+strlen(logtext), ".");
		unicode_sprint(logtext+strlen(logtext), m->name);
		unicode_sprint(logtext+strlen(logtext), m->descriptor);
		dolog();
		intsRestore();                             /* enable interrupts again */
		return (methodptr) do_nothing_function;    /* return empty method     */
		}

	/* print log message for compiled method */

	if (compileverbose) {
		sprintf(logtext, "Compiling: ");
		unicode_sprint(logtext+strlen(logtext), m->class->name);
		strcpy(logtext+strlen(logtext), ".");
		unicode_sprint(logtext+strlen(logtext), m->name);
		unicode_sprint(logtext+strlen(logtext), m->descriptor);
		dolog ();
		}


	/* initialisation of variables and subsystems */

	isleafmethod = true;

	method = m;
	class = m->class;
	descriptor = m->descriptor;
	maxstack = m->maxstack;
	maxlocals = m->maxlocals;
	jcodelength = m->jcodelength;
	jcode = m->jcode;
	exceptiontablelength = m->exceptiontablelength;
	extable = m->exceptiontable;

#ifdef STATISTICS
	count_tryblocks += exceptiontablelength;
	count_javacodesize += jcodelength + 18;
	count_javaexcsize += exceptiontablelength * 8;
#endif

	/* initialise parameter type descriptor */

	descriptor2types (m);
	mreturntype = m->returntype;
	mparamcount = m->paramcount;
	mparamtypes = m->paramtypes;

	/* initialize class list with class the compiled method belongs to */

	uninitializedclasses = chain_new(); 
	compiler_addinitclass (m->class);


	/********************** call the compiler passes **************************/
	
	reg_init();
	local_init();
	mcode_init();

	if (runverbose)
		allocate_literals();

	parse();

	analyse_stack();

	interface_regalloc();

	allocate_scratch_registers();
	
	local_regalloc();
	
	gen_mcode();

	
	/*********** Zwischendarstellungen auf Wunsch ausgeben **********/
		
	if (showintermediate)
		show_icmd_method();
	else if (showdisassemble)
		disassemble((void*) (m->mcode + dseglen), m->mcodelength - dseglen);

	if (showddatasegment)
		dseg_display((void*) (m->mcode));



	/* release dump area */

	dump_release (dumpsize);

	/* measure time */

	if (getcompilingtime) {
		stoptime = getcputime();
		compilingtime += (stoptime-starttime); 
		}

	/* initialize all used classes */
	/* because of reentrant code global variables are not allowed here        */

	{
	chain *ul = uninitializedclasses;   /* list of uninitialized classes      */ 
	classinfo *c;                       /* single class                       */

	while ((c = chain_first(ul)) != NULL) {
		chain_remove (ul);
		class_init (c);          	 	/* may again call the compiler        */
		}
	chain_free (ul);
	}

	intsRestore();    /* enable interrupts again */

	/* return pointer to the methods entry point */
	
	return m -> entrypoint;
}


/************ functions for compiler initialisation and finalisation **********/

void ncomp_init ()
{
	int i;

	has_ext_instr_set = ! has_no_x_instr_set();

	for (i = 0; i < 256; i++)
		stackreq[i] = 1;

	stackreq[JAVA_NOP]          = 0;
	stackreq[JAVA_ISTORE]       = 0;
	stackreq[JAVA_LSTORE]       = 0;
	stackreq[JAVA_FSTORE]       = 0;
	stackreq[JAVA_DSTORE]       = 0;
	stackreq[JAVA_ASTORE]       = 0;
	stackreq[JAVA_ISTORE_0]     = 0;
	stackreq[JAVA_ISTORE_1]     = 0;
	stackreq[JAVA_ISTORE_2]     = 0;
	stackreq[JAVA_ISTORE_3]     = 0;
	stackreq[JAVA_LSTORE_0]     = 0;
	stackreq[JAVA_LSTORE_1]     = 0;
	stackreq[JAVA_LSTORE_2]     = 0;
	stackreq[JAVA_LSTORE_3]     = 0;
	stackreq[JAVA_FSTORE_0]     = 0;
	stackreq[JAVA_FSTORE_1]     = 0;
	stackreq[JAVA_FSTORE_2]     = 0;
	stackreq[JAVA_FSTORE_3]     = 0;
	stackreq[JAVA_DSTORE_0]     = 0;
	stackreq[JAVA_DSTORE_1]     = 0;
	stackreq[JAVA_DSTORE_2]     = 0;
	stackreq[JAVA_DSTORE_3]     = 0;
	stackreq[JAVA_ASTORE_0]     = 0;
	stackreq[JAVA_ASTORE_1]     = 0;
	stackreq[JAVA_ASTORE_2]     = 0;
	stackreq[JAVA_ASTORE_3]     = 0;
	stackreq[JAVA_IASTORE]      = 0;
	stackreq[JAVA_LASTORE]      = 0;
	stackreq[JAVA_FASTORE]      = 0;
	stackreq[JAVA_DASTORE]      = 0;
	stackreq[JAVA_AASTORE]      = 0;
	stackreq[JAVA_BASTORE]      = 0;
	stackreq[JAVA_CASTORE]      = 0;
	stackreq[JAVA_SASTORE]      = 0;
	stackreq[JAVA_POP]          = 0;
	stackreq[JAVA_POP2]         = 0;
	stackreq[JAVA_IINC]         = 0;
	stackreq[JAVA_IFEQ]         = 0;
	stackreq[JAVA_IFNE]         = 0;
	stackreq[JAVA_IFLT]         = 0;
	stackreq[JAVA_IFGE]         = 0;
	stackreq[JAVA_IFGT]         = 0;
	stackreq[JAVA_IFLE]         = 0;
	stackreq[JAVA_IF_ICMPEQ]    = 0;
	stackreq[JAVA_IF_ICMPNE]    = 0;
	stackreq[JAVA_IF_ICMPLT]    = 0;
	stackreq[JAVA_IF_ICMPGE]    = 0;
	stackreq[JAVA_IF_ICMPGT]    = 0;
	stackreq[JAVA_IF_ICMPLE]    = 0;
	stackreq[JAVA_IF_ACMPEQ]    = 0;
	stackreq[JAVA_IF_ACMPNE]    = 0;
	stackreq[JAVA_GOTO]         = 0;
	stackreq[JAVA_RET]          = 0;
	stackreq[JAVA_TABLESWITCH]  = 0;
	stackreq[ICMD_LOOKUPSWITCH] = 0;
	stackreq[JAVA_IRETURN]      = 0;
	stackreq[JAVA_LRETURN]      = 0;
	stackreq[JAVA_FRETURN]      = 0;
	stackreq[JAVA_DRETURN]      = 0;
	stackreq[JAVA_ARETURN]      = 0;
	stackreq[JAVA_RETURN]       = 0;
	stackreq[JAVA_PUTSTATIC]    = 0;
	stackreq[JAVA_PUTFIELD]     = 0;
	stackreq[JAVA_MONITORENTER] = 0;
	stackreq[ICMD_MONITOREXIT]  = 0;
	stackreq[JAVA_WIDE]         = 0;
	stackreq[JAVA_IFNULL]       = 0;
	stackreq[JAVA_IFNONNULL]    = 0;
	stackreq[JAVA_GOTO_W]       = 0;
	stackreq[JAVA_BREAKPOINT]   = 0;

	stackreq[JAVA_SWAP] = 2;
	stackreq[JAVA_DUP2] = 2;
	stackreq[JAVA_DUP_X1] = 3;
	stackreq[JAVA_DUP_X2] = 4;
	stackreq[JAVA_DUP2_X1] = 3;
	stackreq[JAVA_DUP2_X2] = 4;
	
	for (i = 0; i < 256; i++) stdopdescriptors[i] = NULL;

	for (i = 0; i < sizeof(stdopdescriptortable)/sizeof(stdopdescriptor); i++) {
		
		if (stdopdescriptortable[i].isfloat && checkfloats) {
			stdopdescriptortable[i].supported = false;
			}

		stdopdescriptors[stdopdescriptortable[i].opcode] = 
		   &(stdopdescriptortable[i]);
		}

	init_exceptions();
}


void ncomp_close()
{
/*	mcode_close(); */
}

