/* src/cacao/cacao.c - contains main() of cacao

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

   Authors: Reinhard Grafl

   Changes: Andi Krall
            Mark Probst
            Philipp Tomsich
            Christian Thalinger

   This module does the following tasks:
     - Command line option handling
     - Calling initialization routines
     - Calling the class loader
     - Running the main method

   $Id: cacao.c 2131 2005-03-29 22:34:19Z twisti $

*/


#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "cacao/cacao.h"
#include "mm/boehm.h"
#include "mm/memory.h"
#include "native/jni.h"
#include "native/native.h"
#include "toolbox/logging.h"
#include "vm/exceptions.h"
#include "vm/global.h"
#include "vm/loader.h"
#include "vm/options.h"
#include "vm/statistics.h"
#include "vm/stringlocal.h"
#include "vm/tables.h"
#include "vm/jit/asmpart.h"
#include "vm/jit/jit.h"

#ifdef TYPEINFO_DEBUG_TEST
#include "vm/jit/verify/typeinfo.h"
#endif


/* define heap sizes **********************************************************/

#define HEAP_MAXSIZE      64 * 1024 * 1024; /* default 64MB                   */
#define HEAP_STARTSIZE    2 * 1024 * 1024;  /* default 2MB                    */


/* Invocation API variables ***************************************************/

JavaVM *jvm;                        /* denotes a Java VM                      */
JNIEnv *env;                        /* pointer to native method interface     */
 
JDK1_1InitArgs vm_args;             /* JDK 1.1 VM initialization arguments    */


bool cacao_initializing;

char *bootclasspath;                    /* contains the boot classpath        */
char *classpath;                        /* contains the classpath             */

char *mainstring;
static classinfo *mainclass;

#if defined(USE_THREADS) && !defined(NATIVE_THREADS)
void **stackbottom = 0;
#endif


/* define command line options ************************************************/

#define OPT_CLASSPATH        2
#define OPT_D                3
#define OPT_MS               4
#define OPT_MX               5
#define OPT_VERBOSE1         6
#define OPT_VERBOSE          7
#define OPT_VERBOSEGC        8
#define OPT_VERBOSECALL      9
#define OPT_NOIEEE           10
#define OPT_SOFTNULL         11
#define OPT_TIME             12

#if defined(STATISTICS)
#define OPT_STAT             13
#endif /* defined(STATISTICS) */

#define OPT_LOG              14
#define OPT_CHECK            15
#define OPT_LOAD             16
#define OPT_METHOD           17
#define OPT_SIGNATURE        18
#define OPT_SHOW             19
#define OPT_ALL              20
#define OPT_OLOOP            24
#define OPT_INLINING	     25

#define STATIC_ANALYSIS
#if defined(STATIC_ANALYSIS)
# define OPT_RT              26
# define OPT_XTA             27 
# define OPT_VTA             28
#endif /* defined(STATIC_ANALYSIS) */

#define OPT_VERBOSETC        29
#define OPT_NOVERIFY         30
#define OPT_LIBERALUTF       31
#define OPT_VERBOSEEXCEPTION 32
#define OPT_EAGER            33

#if defined(LSRA)
# define OPT_LSRA            34
#endif /* defined(LSRA) */

#define OPT_JAR              35
#define OPT_BOOTCLASSPATH    36
#define OPT_BOOTCLASSPATH_A  37
#define OPT_BOOTCLASSPATH_P  38
#define OPT_VERSION          39


opt_struct opts[] = {
	{ "classpath",         true,  OPT_CLASSPATH },
	{ "cp",                true,  OPT_CLASSPATH },
	{ "D",                 true,  OPT_D },
	{ "Xms",               true,  OPT_MS },
	{ "Xmx",               true,  OPT_MX },
	{ "ms",                true,  OPT_MS },
	{ "mx",                true,  OPT_MX },
	{ "noasyncgc",         false, OPT_IGNORE },
	{ "noverify",          false, OPT_NOVERIFY },
	{ "liberalutf",        false, OPT_LIBERALUTF },
	{ "oss",               true,  OPT_IGNORE },
	{ "ss",                true,  OPT_IGNORE },
	{ "v",                 false, OPT_VERBOSE1 },
	{ "verbose",           false, OPT_VERBOSE },
	{ "verbosegc",         false, OPT_VERBOSEGC },
	{ "verbosecall",       false, OPT_VERBOSECALL },
	{ "verboseexception",  false, OPT_VERBOSEEXCEPTION },
#ifdef TYPECHECK_VERBOSE
	{ "verbosetc",         false, OPT_VERBOSETC },
#endif
#if defined(__ALPHA__)
	{ "noieee",            false, OPT_NOIEEE },
#endif
	{ "softnull",          false, OPT_SOFTNULL },
	{ "time",              false, OPT_TIME },
#if defined(STATISTICS)
	{ "stat",              false, OPT_STAT },
#endif
	{ "log",               true,  OPT_LOG },
	{ "c",                 true,  OPT_CHECK },
	{ "l",                 false, OPT_LOAD },
    { "eager",             false, OPT_EAGER },
	{ "m",                 true,  OPT_METHOD },
	{ "sig",               true,  OPT_SIGNATURE },
	{ "s",                 true,  OPT_SHOW },
	{ "all",               false, OPT_ALL },
	{ "oloop",             false, OPT_OLOOP },
	{ "i",                 true,  OPT_INLINING },
#ifdef STATIC_ANALYSIS
	{ "rt",                false, OPT_RT },
	{ "xta",               false, OPT_XTA },
	{ "vta",               false, OPT_VTA },
#endif
#ifdef LSRA
	{ "lsra",              false, OPT_LSRA },
#endif
	{ "jar",               false, OPT_JAR },
	{ "Xbootclasspath:",   true,  OPT_BOOTCLASSPATH },
	{ "Xbootclasspath/a:", true,  OPT_BOOTCLASSPATH_A },
	{ "Xbootclasspath/p:", true,  OPT_BOOTCLASSPATH_P },
	{ "version",           false, OPT_VERSION },
	{ NULL,                false, 0 }
};


/******************** interne Function: print_usage ************************

Prints the correct usage syntax to stdout.

***************************************************************************/

static void usage()
{
	printf("Usage: cacao [options] classname [program arguments]\n\n");

	printf("Options:\n");
	printf("    -cp <path>               specify a path to look for classes\n");
	printf("    -classpath <path>        specify a path to look for classes\n");
	printf("    -jar jarfile             execute a jar file\n");
	printf("    -D<name>=<value>         add an entry to the property list\n");
	printf("    -Xmx<size>[kK|mM]        specify the size for the heap\n");
	printf("    -Xms<size>[kK|mM]        specify the initial size for the heap\n");
	printf("    -mx<size>[kK|mM]         specify the size for the heap\n");
	printf("    -ms<size>[kK|mM]         specify the initial size for the heap\n");
	printf("    -Xbootclasspath:<path>   set search path for bootstrap classes and resources\n");
	printf("    -Xbootclasspath/a:<path> append to end of bootstrap class path\n");
	printf("    -Xbootclasspath/p:<path> prepend in front of bootstrap class path\n");
	printf("          -v ................... write state-information\n");
	printf("          -verbose ............. write more information\n");
	printf("          -verbosegc ........... write message for each GC\n");
	printf("          -verbosecall ......... write message for each call\n");
	printf("          -verboseexception .... write message for each step of stack unwinding\n");
#ifdef TYPECHECK_VERBOSE
	printf("          -verbosetc ........... write debug messages while typechecking\n");
#endif
	printf("    -version                 print product version and exit\n");
#if defined(__ALPHA__)
	printf("          -noieee .............. don't use ieee compliant arithmetic\n");
#endif
	printf("          -noverify ............ don't verify classfiles\n");
	printf("          -liberalutf........... don't warn about overlong UTF-8 sequences\n");
	printf("          -softnull ............ use software nullpointer check\n");
	printf("          -time ................ measure the runtime\n");
#if defined(STATISTICS)
	printf("          -stat ................ detailed compiler statistics\n");
#endif
	printf("          -log logfile ......... specify a name for the logfile\n");
	printf("          -c(heck)b(ounds) ..... don't check array bounds\n");
	printf("                  s(ync) ....... don't check for synchronization\n");
	printf("          -oloop ............... optimize array accesses in loops\n"); 
	printf("          -l ................... don't start the class after loading\n");
	printf("          -eager ............... perform eager class loading and linking\n");
	printf("          -all ................. compile all methods, no execution\n");
	printf("          -m ................... compile only a specific method\n");
	printf("          -sig ................. specify signature for a specific method\n");
	printf("          -s(how)a(ssembler) ... show disassembled listing\n");
	printf("                 c(onstants) ... show the constant pool\n");
	printf("                 d(atasegment).. show data segment listing\n");
	printf("                 i(ntermediate). show intermediate representation\n");
	printf("                 m(ethods)...... show class fields and methods\n");
	printf("                 u(tf) ......... show the utf - hash\n");
	printf("          -i     n ............. activate inlining\n");
	printf("                 v ............. inline virtual methods\n");
	printf("                                 uses/turns rt option on\n");
	printf("                 e ............. inline methods with exceptions\n");
	printf("                 p ............. optimize argument renaming\n");
	printf("                 o ............. inline methods of foreign classes\n");
#ifdef STATIC_ANALYSIS
	printf("          -rt .................. use rapid type analysis\n");
	printf("          -xta ................. use x type analysis\n");
	printf("          -vta ................. use variable type analysis\n");
#endif
#ifdef LSRA
	printf("          -lsra ................ use linear scan register allocation\n");
#endif

	/* exit with error code */

	exit(1);
}   


/* version *********************************************************************

   Only prints cacao version information and exits.

*******************************************************************************/

static void version()
{
	printf("cacao "VERSION"\n");
	exit(0);
}


#ifdef TYPECHECK_STATISTICS
void typecheck_print_statistics(FILE *file);
#endif



/* getmainclassfromjar ************************************************************

   gets the name of the main class form a jar's manifest file

**********************************************************************************/

char *getmainclassnamefromjar(JNIEnv *env, char *mainstring)
{
	jclass class;
	jmethodID mid;
	jobject obj;
		
	class = (*env)->FindClass(env, "java/util/jar/JarFile");
	if (class == NULL) {
		log_text("unable to find java.util.jar.JarFile");
		throw_main_exception_exit();
	}
	
	mid = (*env)->GetMethodID(NULL, class, "<init>","(Ljava/lang/String;)V");
	if (mid == NULL) {
		log_text("unable to find constructor in java.util.jar.JarFile");
		cacao_exit(1);
	}

	/* open jarfile */
	obj = (*env)->NewObject(NULL,class,mid,((*env)->NewStringUTF(NULL,(char*)mainstring)));
	if ((*env)->ExceptionOccurred(NULL) != NULL) {
		(*env)->ExceptionDescribe(NULL);
		cacao_exit(1);
	}
	
	mid = (*env)->GetMethodID(NULL, class, "getManifest","()Ljava/util/jar/Manifest;");
	if (mid == NULL) {
		log_text("unable to find getMainfest method");
		cacao_exit(1);
	}

	/* get manifest object */
	obj = (*env)->CallObjectMethod(NULL,obj,mid);
	if ((*env)->ExceptionOccurred(NULL) != NULL) {
		(*env)->ExceptionDescribe(NULL);
		cacao_exit(1);
	}

	mid = (*env)->GetMethodID(NULL, (jclass)((java_objectheader*) obj)->vftbl->class, "getMainAttributes","()Ljava/util/jar/Attributes;");
	if (mid == NULL) {
		log_text("unable to find getMainAttributes method");
		cacao_exit(1);
	}

	/* get Main Attributes */
	obj = (*env)->CallObjectMethod(NULL,obj,mid);
	if ((*env)->ExceptionOccurred(NULL) != NULL) {
		(*env)->ExceptionDescribe(NULL);
		cacao_exit(1);
	}


	mid = (*env)->GetMethodID(NULL, (jclass)((java_objectheader*) obj)->vftbl->class, "getValue","(Ljava/lang/String;)Ljava/lang/String;");
	if (mid == NULL) {
		log_text("unable to find getValue method");
		cacao_exit(1);
	}

	/* get property Main-Class */
	obj = (*env)->CallObjectMethod(NULL,obj,mid,(*env)->NewStringUTF(NULL,"Main-Class"));
	if ((*env)->ExceptionOccurred(NULL) != NULL) {
		(*env)->ExceptionDescribe(NULL);
		cacao_exit(1);
	}
	
	return javastring_tochar((java_objectheader *) obj);
}


/*
 * void exit_handler(void)
 * -----------------------
 * The exit_handler function is called upon program termination to shutdown
 * the various subsystems and release the resources allocated to the VM.
 */
void exit_handler(void)
{
	/********************* Print debug tables ************************/
				
	if (showmethods) class_showmethods(mainclass);
	if (showconstantpool) class_showconstantpool(mainclass);
	if (showutf) utf_show();

#if defined(USE_THREADS) && !defined(NATIVE_THREADS)
	clear_thread_flags();		/* restores standard file descriptor
	                               flags */
#endif

	/************************ Free all resources *******************/

	loader_close();
	tables_close();

	MFREE(classpath, u1, strlen(classpath));

	if (opt_verbose || getcompilingtime || opt_stat) {
		log_text("CACAO terminated");

#if defined(STATISTICS)
		if (opt_stat) {
			print_stats();
#ifdef TYPECHECK_STATISTICS
			typecheck_print_statistics(get_logfile());
#endif
		}

		if (getcompilingtime)
			print_times();
		mem_usagelog(1);
#endif
	}
}


/************************** Function: main *******************************

   The main program.
   
**************************************************************************/

int main(int argc, char **argv)
{
	s4 i, j;
	void *dummy;
	
	/********** interne (nur fuer main relevante Optionen) **************/
   
	char logfilename[200] = "";
	u4 heapmaxsize;
	u4 heapstartsize;
	char *cp;
	s4    cplen;
	bool startit = true;
	char *specificmethodname = NULL;
	char *specificsignature = NULL;
	bool jar = false;

#if defined(USE_THREADS) && !defined(NATIVE_THREADS)
	stackbottom = &dummy;
#endif
	
	if (atexit(exit_handler))
		throw_cacao_exception_exit(string_java_lang_InternalError,
								   "Unable to register exit_handler");


	/************ Collect info from the environment ************************/

	/* set the bootclasspath */

	cp = getenv("BOOTCLASSPATH");
	if (cp) {
		bootclasspath = MNEW(char, strlen(cp) + 1);
		strcpy(bootclasspath, cp);

	} else {
#if !defined(WITH_EXTERNAL_CLASSPATH)
		cplen = strlen(CACAO_INSTALL_PREFIX) + strlen(CACAO_RT_JAR_PATH);

		bootclasspath = MNEW(char, cplen + 1);
		strcpy(bootclasspath, CACAO_INSTALL_PREFIX);
		strcat(bootclasspath, CACAO_RT_JAR_PATH);
#else
		cplen = strlen(CACAO_INSTALL_PREFIX) + strlen(CACAO_VM_ZIP_PATH) +
			strlen(EXTERNAL_CLASSPATH_PREFIX) + strlen(CLASSPATH_GLIBJ_ZIP_PATH);

		bootclasspath = MNEW(char, cplen + 1 + 1);
		strcpy(bootclasspath, CACAO_INSTALL_PREFIX);
		strcat(bootclasspath, CACAO_VM_ZIP_PATH);
		strcat(bootclasspath, ":");
		strcat(bootclasspath, EXTERNAL_CLASSPATH_PREFIX);
		strcat(bootclasspath, CLASSPATH_GLIBJ_ZIP_PATH);
#endif
	}


	/* set the classpath */

	cp = getenv("CLASSPATH");
	if (cp) {
		classpath = MNEW(char, strlen(cp) + 1);
		strcat(classpath, cp);

	} else {
		classpath = MNEW(char, 2);
		strcpy(classpath, ".");
	}


	/***************** Interpret the command line *****************/
   
	checknull = false;
	opt_noieee = false;

	heapmaxsize = HEAP_MAXSIZE;
	heapstartsize = HEAP_STARTSIZE;


	while ((i = get_opt(argc, argv, opts)) != OPT_DONE) {
		switch (i) {
		case OPT_IGNORE:
			break;
			
		case OPT_BOOTCLASSPATH:
			/* Forget default bootclasspath and set the argument as new boot  */
			/* classpath.                                                     */
			MFREE(bootclasspath, char, strlen(bootclasspath));

			bootclasspath = MNEW(char, strlen(opt_arg) + 1);
			strcpy(bootclasspath, opt_arg);
			break;

		case OPT_BOOTCLASSPATH_A:
			/* append to end of bootclasspath */
			cplen = strlen(bootclasspath);

			bootclasspath = MREALLOC(bootclasspath,
									 char,
									 cplen,
									 cplen + 1 + strlen(opt_arg) + 1);

			strcat(bootclasspath, ":");
			strcat(bootclasspath, opt_arg);
			break;

		case OPT_BOOTCLASSPATH_P:
			/* prepend in front of bootclasspath */
			cp = bootclasspath;
			cplen = strlen(cp);

			bootclasspath = MNEW(char, strlen(opt_arg) + 1 + cplen + 1);

			strcpy(bootclasspath, opt_arg);
			strcat(bootclasspath, ":");
			strcat(bootclasspath, cp);

			MFREE(cp, char, cplen);
			break;

		case OPT_CLASSPATH:
			/* forget old classpath and set the argument as new classpath */
			MFREE(classpath, char, strlen(classpath));

			classpath = MNEW(char, strlen(opt_arg) + 1);
			strcpy(classpath, opt_arg);
			break;

		case OPT_JAR:
			jar = true;
			break;
			
		case OPT_D:
			{
				for (j = 0; j < strlen(opt_arg); j++) {
					if (opt_arg[j] == '=') {
						opt_arg[j] = '\0';
						create_property(opt_arg, opt_arg + j + 1);
						goto didit;
					}
				}

				/* if no '=' is given, just create an empty property */
				create_property(opt_arg, "");
					
			didit: ;
			}	
			break;

		case OPT_MS:
		case OPT_MX:
			{
				char c;
				c = opt_arg[strlen(opt_arg) - 1];

				if (c == 'k' || c == 'K') {
					j = 1024 * atoi(opt_arg);

				} else if (c == 'm' || c == 'M') {
					j = 1024 * 1024 * atoi(opt_arg);

				} else j = atoi(opt_arg);

				if (i == OPT_MX) heapmaxsize = j;
				else heapstartsize = j;
			}
			break;

		case OPT_VERBOSE1:
			opt_verbose = true;
			break;

		case OPT_VERBOSE:
			opt_verbose = true;
			loadverbose = true;
			linkverbose = true;
			initverbose = true;
			compileverbose = true;
			break;

		case OPT_VERBOSEEXCEPTION:
			verboseexception = true;
			break;

		case OPT_VERBOSEGC:
			collectverbose = true;
			break;

#ifdef TYPECHECK_VERBOSE
		case OPT_VERBOSETC:
			typecheckverbose = true;
			break;
#endif
				
		case OPT_VERBOSECALL:
			runverbose = true;
			break;

		case OPT_VERSION:
			version();
			break;

		case OPT_NOIEEE:
			opt_noieee = true;
			break;

		case OPT_NOVERIFY:
			opt_verify = false;
			break;

		case OPT_LIBERALUTF:
			opt_liberalutf = true;
			break;

		case OPT_SOFTNULL:
			checknull = true;
			break;

		case OPT_TIME:
			getcompilingtime = true;
			getloadingtime = true;
			break;
					
#if defined(STATISTICS)
		case OPT_STAT:
			opt_stat = true;
			break;
#endif
					
		case OPT_LOG:
			strcpy(logfilename, opt_arg);
			break;
			
		case OPT_CHECK:
			for (j = 0; j < strlen(opt_arg); j++) {
				switch (opt_arg[j]) {
				case 'b':
					checkbounds = false;
					break;
				case 's':
					checksync = false;
					break;
				default:
					usage();
				}
			}
			break;
			
		case OPT_LOAD:
			startit = false;
			makeinitializations = false;
			break;

		case OPT_EAGER:
			opt_eager = true;
			break;

		case OPT_METHOD:
			startit = false;
			specificmethodname = opt_arg;
			makeinitializations = false;
			break;
         		
		case OPT_SIGNATURE:
			specificsignature = opt_arg;
			break;
         		
		case OPT_ALL:
			compileall = true;
			startit = false;
			makeinitializations = false;
			break;
         		
		case OPT_SHOW:       /* Display options */
			for (j = 0; j < strlen(opt_arg); j++) {		
				switch (opt_arg[j]) {
				case 'a':
					showdisassemble = true;
					compileverbose = true;
					break;
				case 'c':
					showconstantpool = true;
					break;
				case 'd':
					showddatasegment = true;
					break;
				case 'i':
					showintermediate = true;
					compileverbose = true;
					break;
				case 'm':
					showmethods = true;
					break;
				case 'u':
					showutf = true;
					break;
				default:
					usage();
				}
			}
			break;
			
		case OPT_OLOOP:
			opt_loops = true;
			break;

		case OPT_INLINING:
			for (j = 0; j < strlen(opt_arg); j++) {		
				switch (opt_arg[j]) {
				case 'n':
				     /* define in options.h; Used in main.c, jit.c & inline.c */
#ifdef INAFTERMAIN
					useinliningm = true;
					useinlining = false;
#else
					useinlining = true;
#endif
					break;
				case 'v':
					inlinevirtuals = true;
					opt_rt = true;
					break;
				case 'e':
					inlineexceptions = true;
					break;
				case 'p':
					inlineparamopt = true;
					break;
				case 'o':
					inlineoutsiders = true;
					break;
				default:
					usage();
				}
			}
			break;

#ifdef STATIC_ANALYSIS
		case OPT_RT:
			opt_rt = true; /* default for inlining */
			break;

		case OPT_XTA:
			opt_xta = true; /* in test currently */
			break;

		case OPT_VTA:
			printf("\nVTA is not yet available\n");
			opt_vta = false;
			/***opt_vta = true; not yet **/
			break;
#endif

#ifdef LSRA
		case OPT_LSRA:
#if defined(__I386__) || defined(__ALPHA__) || defined (__X86_64__) || defined(__POWERPC__)
			opt_lsra = true;
#else
			printf("LSRA not available for this architecture\n");
			opt_lsra = false;
#endif
			break;
#endif

		default:
			printf("Unknown option: %s\n", argv[opt_ind]);
			usage();
		}
	}

	if (opt_ind >= argc)
   		usage();


	/* transform dots into slashes in the class name */

   	mainstring = argv[opt_ind++];
	if (!jar) { 
        /* do not mangle jar filename */
		for (i = strlen(mainstring) - 1; i >= 0; i--) {
			if (mainstring[i] == '.') mainstring[i] = '/';
		}

	} else {
		/* put jarfile in classpath */
		cp = classpath;
		classpath = MNEW(char, strlen(mainstring) + 1 + strlen(classpath) + 1);
		strcpy(classpath, mainstring);
		strcat(classpath, ":");
		strcat(classpath, cp);
		
		MFREE(cp, char, strlen(cp));
	}

	/**************************** Program start *****************************/

	log_init(logfilename);

	if (opt_verbose)
		log_text("CACAO started -------------------------------------------------------");

	/* initialize JavaVM */

	vm_args.version = 0x00010001; /* New in 1.1.2: VM version */

	/* Get the default initialization arguments and set the class path */

	JNI_GetDefaultJavaVMInitArgs(&vm_args);

	vm_args.minHeapSize = heapstartsize;
	vm_args.maxHeapSize = heapmaxsize;

	vm_args.classpath = classpath;
 
	/* load and initialize a Java VM, return a JNI interface pointer in env */

	JNI_CreateJavaVM(&jvm, &env, &vm_args);


	/* initialize the garbage collector */

	gc_init(heapmaxsize, heapstartsize);

	tables_init();

	/* initialize the loader with bootclasspath */

	suck_init(bootclasspath);

	cacao_initializing = true;

#if defined(USE_THREADS)
#if defined(NATIVE_THREADS)
  	initThreadsEarly();
#endif
	initLocks();
#endif

	/* install architecture dependent signal handler used for exceptions */
	init_exceptions();

	/* initializes jit compiler and codegen stuff */
	jit_init();

	/* initialize some cacao subsystems */

	utf8_init();
	class_init_foo();

	if (!loader_init((u1 *) &dummy))
		throw_main_exception_exit();

	if (!linker_init())
		throw_main_exception_exit();

	if (!native_init())
		throw_main_exception_exit();

	if (!exceptions_init())
		throw_main_exception_exit();

#if defined(USE_THREADS)
  	initThreads((u1 *) &dummy);
#endif

	*threadrootmethod = NULL;

	/*That's important, otherwise we get into trouble, if the Runtime static
	  initializer is called before (circular dependency. This is with
	  classpath 0.09. Another important thing is, that this has to happen
	  after initThreads!!! */

	if (!class_init(class_java_lang_System))
		throw_main_exception_exit();

	cacao_initializing = false;


	/* start worker routines **************************************************/

	if (startit) {
		classinfo        *cl;           /* java/lang/ClassLoader              */
		classinfo        *mainclass;    /* java/lang/Class                    */
		methodinfo       *m;
		java_objectarray *a; 
		s4                status;

		/* set return value to OK */

		status = 0;

		if (jar) {
			/* open jar file with java.util.jar.JarFile */
			mainstring = getmainclassnamefromjar((JNIEnv *) &env, mainstring);
		}

		/* get system classloader */

		m = class_resolveclassmethod(class_java_lang_ClassLoader,
									 utf_new_char("getSystemClassLoader"),
									 utf_new_char("()Ljava/lang/ClassLoader;"),
									 class_java_lang_Object,
									 false);

		cl = (classinfo *) asm_calljavafunction(m, NULL, NULL, NULL, NULL);

		/* get `loadClass' method */

		m = class_resolveclassmethod(cl->header.vftbl->class,
									 utf_loadClass,
									 utf_java_lang_String__java_lang_Class,
									 class_java_lang_Object,
									 false);

		/* load the main class */

		mainclass =
			(classinfo *) asm_calljavafunction(m,
											   cl,
											   javastring_new_char(mainstring),
											   NULL,
											   NULL);

		/* error loading class, clear exceptionptr for new exception */

		if (*exceptionptr || !mainclass) {
/*  			*exceptionptr = NULL; */

/*  			*exceptionptr = */
/*  				new_exception_message(string_java_lang_NoClassDefFoundError, */
/*  									  mainstring); */
			throw_main_exception_exit();
		}

		/* find the `main' method of the main class */

		m = class_resolveclassmethod(mainclass,
									 utf_new_char("main"), 
									 utf_new_char("([Ljava/lang/String;)V"),
									 class_java_lang_Object,
									 false);

		/* there is no main method or it isn't static */

		if (*exceptionptr || !m || !(m->flags & ACC_STATIC)) {
			*exceptionptr = NULL;

			*exceptionptr =
				new_exception_message(string_java_lang_NoSuchMethodError,
									  "main");
			throw_main_exception_exit();
		}

		/* build argument array */

		a = builtin_anewarray(argc - opt_ind, class_java_lang_String);
		for (i = opt_ind; i < argc; i++) {
			a->data[i - opt_ind] = 
				(java_objectheader *) javastring_new(utf_new_char(argv[i]));
		}

#ifdef TYPEINFO_DEBUG_TEST
		/* test the typeinfo system */
		typeinfo_test();
#endif
		/*class_showmethods(currentThread->group->header.vftbl->class);	*/

		*threadrootmethod = m;

		/* here we go... */

		asm_calljavafunction(m, a, NULL, NULL, NULL);

		/* exception occurred? */
		if (*exceptionptr) {
			throw_main_exception();
			status = 1;
		}

#if defined(USE_THREADS)
#if defined(NATIVE_THREADS)
		joinAllThreads();
#else
  		killThread(currentThread);
#endif
#endif

		/* now exit the JavaVM */

/*  		(*jvm)->DestroyJavaVM(jvm); */

		cacao_exit(status);
	}

	/************* If requested, compile all methods ********************/

	if (compileall) {
		classinfo *c;
		methodinfo *m;
		u4 slot;
		s4 i;

		/* create all classes found in the classpath */
		/* XXX currently only works with zip/jar's */
		create_all_classes();

		/* load and link all classes */
		for (slot = 0; slot < class_hash.size; slot++) {
			c = class_hash.ptr[slot];

			while (c) {
				if (!c->loaded)
					if (!class_load(c))
						throw_main_exception_exit();

				if (!c->linked)
					if (!class_link(c))
						throw_main_exception_exit();

				/* compile all class methods */
				for (i = 0; i < c->methodscount; i++) {
					m = &(c->methods[i]);
					if (m->jcode) {
						(void) jit_compile(m);
					}
				}

				c = c->hashlink;
			}
		}
	}


	/******** If requested, compile a specific method ***************/

	if (specificmethodname) {
		methodinfo *m;

		/* create, load and link the main class */
		mainclass = class_new(utf_new_char(mainstring));

		if (!class_load(mainclass))
			throw_main_exception_exit();

		if (!class_link(mainclass))
			throw_main_exception_exit();

		if (specificsignature) {
			m = class_resolveclassmethod(mainclass,
										 utf_new_char(specificmethodname),
										 utf_new_char(specificsignature),
										 mainclass,
										 false);
		} else {
			m = class_resolveclassmethod(mainclass,
										 utf_new_char(specificmethodname),
										 NULL,
										 mainclass,
										 false);
		}

		if (!m) {
			char message[MAXLOGTEXT];
			sprintf(message, "%s%s", specificmethodname,
					specificsignature ? specificsignature : "");

			*exceptionptr =
				new_exception_message(string_java_lang_NoSuchMethodException,
									  message);
										 
			throw_main_exception_exit();
		}
		
		jit_compile(m);
	}

	cacao_shutdown(0);

	/* keep compiler happy */

	return 0;
}


/* cacao_exit ******************************************************************

   Calls java.lang.Runtime.exit(I)V to exit the JavaVM correctly.

*******************************************************************************/

void cacao_exit(s4 status)
{
	classinfo *c;
	methodinfo *m;

	/* class should already be loaded, but who knows... */

	c = class_new(utf_new_char("java/lang/System"));

	if (!class_load(c))
		throw_main_exception_exit();

	if (!class_link(c))
		throw_main_exception_exit();

	/* call System.exit(I)V */

	m = class_resolveclassmethod(c,
								 utf_new_char("exit"),
								 utf_new_char("(I)V"),
								 class_java_lang_Object,
								 true);
	
	if (!m)
		throw_main_exception_exit();

	/* call the exit function with passed exit status */

	/* both inlinevirtual and outsiders not allowed on exit */
	/*   not sure if permanant or temp restriction          */
	if (inlinevirtuals) inlineoutsiders = false; 

	asm_calljavafunction(m, (void *) (ptrint) status, NULL, NULL, NULL);

	/* this should never happen */

	if (*exceptionptr)
		throw_exception_exit();

	throw_cacao_exception_exit(string_java_lang_InternalError,
							   "System.exit(I)V returned without exception");
}


/*************************** Shutdown function *********************************

	Terminates the system immediately without freeing memory explicitly (to be
	used only for abnormal termination)
	
*******************************************************************************/

void cacao_shutdown(s4 status)
{
	if (opt_verbose || getcompilingtime || opt_stat) {
		log_text("CACAO terminated by shutdown");
		dolog("Exit status: %d\n", (s4) status);
	}

	exit(status);
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
