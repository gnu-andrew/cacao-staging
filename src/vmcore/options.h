/* src/vmcore/options.h - define global options extern

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

*/


#ifndef _OPTIONS_H
#define _OPTIONS_H


#include "config.h"

#include <stdint.h>

#include "vm/types.h"

#include "native/jni.h"

#include "vm/global.h"


/* reserved option numbers ****************************************************/

/* define these negative since the other options are an enum */

#define OPT_DONE       -1
#define OPT_ERROR      -2
#define OPT_IGNORE     -3


typedef struct opt_struct opt_struct;

struct opt_struct {
	char *name;
	bool  arg;
	int   value;
};


typedef struct option_t option_t;

struct option_t {
	char *name;
	int   value;
	int   type;
	char *doc;
};


/* global variables ***********************************************************/

extern s4    opt_index;
extern char *opt_arg;

extern bool opt_foo;

extern bool opt_jit;
extern bool opt_intrp;

extern bool opt_jar;
extern bool opt_run;

extern s4   opt_heapmaxsize;
extern s4   opt_heapstartsize;
extern s4   opt_stacksize;

extern bool opt_verbose;
extern bool opt_debugcolor;
extern bool compileall;

extern bool loadverbose;         /* Print debug messages during loading */
extern bool initverbose;         /* Log class initialization */ 

extern bool opt_verboseclass;
extern bool opt_verbosegc;
extern bool opt_verbosejni;
extern bool opt_verbosecall;

extern bool showmethods;
extern bool showconstantpool;
extern bool showutf;

extern char *opt_method;
extern char *opt_signature;

extern bool compileverbose;
extern bool showstack;

extern bool opt_showdisassemble;
extern bool opt_shownops;
extern bool opt_showddatasegment;
extern bool opt_showintermediate;

extern bool checkbounds;
extern bool opt_noieee;
extern bool checksync;
#if defined(ENABLE_LOOP)
extern bool opt_loops;
#endif

extern bool makeinitializations;

#if defined(ENABLE_STATISTICS)
extern bool opt_stat;
extern bool opt_getloadingtime;
extern bool opt_getcompilingtime;
#endif
#if defined(ENABLE_VERIFIER)
extern bool opt_verify;
#endif

#if defined(ENABLE_PROFILING)
extern bool opt_prof;
extern bool opt_prof_bb;
#endif

/* inlining options ***********************************************************/

#if defined(ENABLE_INLINING)
extern bool opt_inlining;
#if defined(ENABLE_INLINING_DEBUG) || !defined(NDEBUG)
extern s4 opt_inline_debug_min_size;
extern s4 opt_inline_debug_max_size;
extern s4 opt_inline_debug_end_counter;
extern bool opt_inline_debug_all;
#endif /* defined(ENABLE_INLINING_DEBUG) || !defined(NDEBUG) */
#if !defined(NDEBUG)
extern bool opt_inline_debug_log;
#endif /* !defined(NDEBUG) */
#endif /* defined(ENABLE_INLINING) */


/* optimization options *******************************************************/

#if defined(ENABLE_IFCONV)
extern bool opt_ifconv;
#endif

#if defined(ENABLE_LSRA) || defined(ENABLE_SSA)
extern bool opt_lsra;
#endif
#if defined(ENABLE_SSA)
extern bool opt_ssa_dce;          /* enable dead code elemination */
extern bool opt_ssa_cp;           /* enable copy propagation      */
#endif

/* interpreter options ********************************************************/

#if defined(ENABLE_INTRP)
extern bool opt_no_dynamic;
extern bool opt_no_replication;
extern bool opt_no_quicksuper;

extern s4   opt_static_supers;
extern bool vm_debug;
#endif

/* debug output filtering options *********************************************/

#if defined(ENABLE_DEBUG_FILTER)
extern const char *opt_filter_verbosecall_include;
extern const char *opt_filter_verbosecall_exclude;
extern const char *opt_filter_show_method;
#endif


/* -XX options ****************************************************************/

/* NOTE: For better readability keep these alpha-sorted. */

extern int      opt_DebugExceptions;
extern int      opt_DebugFinalizer;
extern int      opt_DebugLocalReferences;
extern int      opt_DebugLocks;
extern int      opt_DebugPatcher;
extern int      opt_DebugPackage;
extern int      opt_DebugProperties;
extern int32_t  opt_DebugStackFrameInfo;
extern int      opt_DebugStackTrace;
extern int      opt_DebugThreads;
#if defined(ENABLE_DISASSEMBLER)
extern int      opt_DisassembleStubs;
#endif
#if defined(ENABLE_GC_CACAO)
extern int32_t  opt_GCDebugRootSet;
extern int32_t  opt_GCStress;
#endif
extern int32_t  opt_MaxPermSize;
extern int32_t  opt_PermSize;
extern int      opt_PrintConfig;
extern int32_t  opt_ProfileGCMemoryUsage;
extern int32_t  opt_ProfileMemoryUsage;
extern FILE    *opt_ProfileMemoryUsageGNUPlot;
#if defined(ENABLE_REPLACEMENT)
extern int      opt_TestReplacement;
#endif
extern int32_t  opt_ThreadStackSize;
extern int      opt_TraceCompilerCalls;
extern int32_t  opt_TraceExceptions;
extern int32_t  opt_TraceJavaCalls;
extern int32_t  opt_TraceJNICalls;
extern int32_t  opt_TraceJVMCalls;
extern int32_t  opt_TraceJVMCallsVerbose;
extern int32_t  opt_TraceLinkClass;
#if defined(ENABLE_REPLACEMENT)
extern int32_t  opt_TraceReplacement;
#endif


/* function prototypes ********************************************************/

s4   options_get(opt_struct *opts, JavaVMInitArgs *vm_args);
void options_xx(JavaVMInitArgs *vm_args);

#endif /* _OPTIONS_H */


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
