/* src/vm/global.h - global definitions

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
            Andreas Krall

   Changes: Mark Probst
            Philipp Tomsich
            Edwin Steiner
            Joseph Wenninger
            Christian Thalinger

   $Id: global.h 2157 2005-03-30 20:05:55Z twisti $

*/


#ifndef _GLOBAL_H
#define _GLOBAL_H

#include "config.h"
#include "types.h"


/* additional data types ******************************************************/

typedef void *voidptr;                  /* generic pointer                    */
typedef void (*functionptr) (void);     /* generic function pointer           */
typedef u1* methodptr;

typedef int   bool;                     /* boolean data type                  */

#define true  1
#define false 0


/* immediate data union */

typedef union {
	s4          i;
	s8          l;
	float       f;
	double      d;
	void       *a;
	functionptr fp;
	u1          b[8];
} imm_union;


/* forward typedefs ***********************************************************/

typedef struct java_objectheader java_objectheader; 
typedef struct java_objectarray java_objectarray;


/* define some cacao paths ****************************************************/

#define CACAO_JRE_DIR               "/jre"
#define CACAO_LIBRARY_PATH          "/jre/lib/"ARCH_DIR"/"
#define CACAO_RT_JAR_PATH           "/jre/lib/rt.jar"
#define CACAO_EXT_DIR               "/jre/lib/ext"

#if defined(WITH_EXTERNAL_CLASSPATH)
#define CACAO_VM_ZIP_PATH           "/jre/lib/vm.zip"
#define CLASSPATH_LIBRARY_PATH      "/lib/classpath"
#define CLASSPATH_GLIBJ_ZIP_PATH    "/share/classpath/glibj.zip"
#endif


/*
 * CACAO_TYPECHECK activates typechecking (part of bytecode verification)
 */
#define CACAO_TYPECHECK

/*
 * TYPECHECK_STACK_COMPCAT activates full checking of computational
 * categories for stack manipulations (POP,POP2,SWAP,DUP,DUP2,DUP_X1,
 * DUP2_X1,DUP_X2,DUP2_X2).
 */
#define TYPECHECK_STACK_COMPCAT

/* if we have threads disabled this one is not defined ************************/

#if !defined(USE_THREADS)
#define THREADSPECIFIC
#endif


#define PRIMITIVETYPE_COUNT  9  /* number of primitive types */

/* CAUTION: Don't change the numerical values! These constants are
 * used as indices into the primitive type table.
 */
#define PRIMITIVETYPE_INT     0
#define PRIMITIVETYPE_LONG    1
#define PRIMITIVETYPE_FLOAT   2
#define PRIMITIVETYPE_DOUBLE  3
#define PRIMITIVETYPE_BYTE    4
#define PRIMITIVETYPE_CHAR    5
#define PRIMITIVETYPE_SHORT   6
#define PRIMITIVETYPE_BOOLEAN 7
#define PRIMITIVETYPE_VOID    8


#define MAX_ALIGN 8             /* most generic alignment for JavaVM values   */


/* basic data types ***********************************************************/

/* CAUTION: jit/jit.h relies on these numerical values! */
#define TYPE_INT      0         /* the JavaVM types must numbered in the      */
#define TYPE_LONG     1         /* same order as the ICMD_Ixxx to ICMD_Axxx   */
#define TYPE_FLOAT    2         /* instructions (LOAD and STORE)              */
#define TYPE_DOUBLE   3         /* integer, long, float, double, address      */
#define TYPE_ADDRESS  4         /* all other types can be numbered arbitrarly */

#define TYPE_VOID    10


/* Java class file constants **************************************************/

#define MAGIC             0xCAFEBABE
#define MAJOR_VERSION     48
#define MINOR_VERSION     0

#define CONSTANT_Class                 7
#define CONSTANT_Fieldref              9
#define CONSTANT_Methodref            10
#define CONSTANT_InterfaceMethodref   11
#define CONSTANT_String                8
#define CONSTANT_Integer               3
#define CONSTANT_Float                 4
#define CONSTANT_Long                  5
#define CONSTANT_Double                6
#define CONSTANT_NameAndType          12
#define CONSTANT_Utf8                  1

#define CONSTANT_UNUSED                0

#define ACC_PUBLIC                0x0001
#define ACC_PRIVATE               0x0002
#define ACC_PROTECTED             0x0004
#define ACC_STATIC                0x0008
#define ACC_FINAL                 0x0010
#define ACC_SUPER                 0x0020
#define ACC_SYNCHRONIZED          0x0020
#define ACC_VOLATILE              0x0040
#define ACC_TRANSIENT             0x0080
#define ACC_NATIVE                0x0100
#define ACC_INTERFACE             0x0200
#define ACC_ABSTRACT              0x0400
#define ACC_STRICT                0x0800


/* data structure for calls from c code to java methods */

struct jni_callblock {
	u8 itemtype;
	u8 item;
};

typedef struct jni_callblock jni_callblock;


/* data structures of the runtime system **************************************/

/* java_objectheader ***********************************************************

   All objects (and arrays) which resides on the heap need the
   following header at the beginning of the data structure.

*******************************************************************************/

struct java_objectheader {              /* header for all objects             */
	struct _vftbl *vftbl;               /* pointer to virtual function table  */
#if defined(USE_THREADS) && defined(NATIVE_THREADS)
	void          *monitorPtr;
#endif
};


/* arrays **********************************************************************

	All arrays are objects (they need the object header with a pointer
	to a vftbl (array class table). There is one class for each array
	type. The array type is described by an arraydescriptor struct
	which is referenced by the vftbl.
*/

/* CAUTION: Don't change the numerical values! These constants (with
 * the exception of ARRAYTYPE_OBJECT) are used as indices in the
 * primitive type table.
 */
#define ARRAYTYPE_INT      PRIMITIVETYPE_INT
#define ARRAYTYPE_LONG     PRIMITIVETYPE_LONG
#define ARRAYTYPE_FLOAT    PRIMITIVETYPE_FLOAT
#define ARRAYTYPE_DOUBLE   PRIMITIVETYPE_DOUBLE
#define ARRAYTYPE_BYTE     PRIMITIVETYPE_BYTE
#define ARRAYTYPE_CHAR     PRIMITIVETYPE_CHAR
#define ARRAYTYPE_SHORT    PRIMITIVETYPE_SHORT
#define ARRAYTYPE_BOOLEAN  PRIMITIVETYPE_BOOLEAN
#define ARRAYTYPE_OBJECT   PRIMITIVETYPE_VOID     /* don't use as index! */

typedef struct java_arrayheader {       /* header for all arrays              */
	java_objectheader objheader;        /* object header                      */
	s4 size;                            /* array size                         */
} java_arrayheader;



/* structs for all kinds of arrays ********************************************/

typedef struct java_chararray {
	java_arrayheader header;
	u2 data[1];
} java_chararray;

typedef struct java_floatheader {
	java_arrayheader header;
	float data[1];
} java_floatarray;

typedef struct java_doublearray {
	java_arrayheader header;
	double data[1];
} java_doublearray;

/*  booleanarray and bytearray need identical memory layout (access methods
    use the same machine code */

typedef struct java_booleanarray {
	java_arrayheader header;
	u1 data[1];
} java_booleanarray;

typedef struct java_bytearray {
	java_arrayheader header;
	s1 data[1];
} java_bytearray;

typedef struct java_shortarray {
	java_arrayheader header;
	s2 data[1];
} java_shortarray;

typedef struct java_intarray {
	java_arrayheader header;
	s4 data[1];
} java_intarray;

typedef struct java_longarray {
	java_arrayheader header;
	s8 data[1];
} java_longarray;

/*  objectarray and arrayarray need identical memory layout (access methods
    use the same machine code */

struct java_objectarray {
	java_arrayheader   header;
	java_objectheader *data[1];
};


#define VFTBLINTERFACETABLE(v,i)       (v)->interfacetable[-i]


/* flag variables *************************************************************/

extern bool cacao_initializing;


/* macros for descriptor parsing **********************************************/

/* SKIP_FIELDDESCRIPTOR:
 * utf_ptr must point to the first character of a field descriptor.
 * After the macro call utf_ptr points to the first character after
 * the field descriptor.
 *
 * CAUTION: This macro does not check for an unexpected end of the
 * descriptor. Better use SKIP_FIELDDESCRIPTOR_SAFE.
 */
#define SKIP_FIELDDESCRIPTOR(utf_ptr)							\
	do { while (*(utf_ptr)=='[') (utf_ptr)++;					\
		if (*(utf_ptr)++=='L')									\
			while(*(utf_ptr)++ != ';') /* skip */; } while(0)

/* SKIP_FIELDDESCRIPTOR_SAFE:
 * utf_ptr must point to the first character of a field descriptor.
 * After the macro call utf_ptr points to the first character after
 * the field descriptor.
 *
 * Input:
 *     utf_ptr....points to first char of descriptor
 *     end_ptr....points to first char after the end of the string
 *     errorflag..must be initialized (to false) by the caller!
 * Output:
 *     utf_ptr....points to first char after the descriptor
 *     errorflag..set to true if the string ended unexpectedly
 */
#define SKIP_FIELDDESCRIPTOR_SAFE(utf_ptr,end_ptr,errorflag)			\
	do { while ((utf_ptr) != (end_ptr) && *(utf_ptr)=='[') (utf_ptr)++;	\
		if ((utf_ptr) == (end_ptr))										\
			(errorflag) = true;											\
		else															\
			if (*(utf_ptr)++=='L') {									\
				while((utf_ptr) != (end_ptr) && *(utf_ptr)++ != ';')	\
					/* skip */;											\
				if ((utf_ptr)[-1] != ';')								\
					(errorflag) = true; }} while(0)


/* Synchronization ************************************************************/

#if defined(USE_THREADS) && defined(NATIVE_THREADS)
void cast_lock();
void cast_unlock();
void compiler_lock();
void compiler_unlock();
#endif


/**** Methods: called directly by cacao, which defines the callpath ***/
#define MAINCLASS mainstring
#define MAINMETH "main"
#define MAINDESC "([Ljava/lang/String;)V"

#define EXITCLASS "java/lang/System"
#define EXITMETH  "exit"
#define EXITDESC  "(I)V"

#if defined(USE_THREADS)
 #define THREADCLASS "java/lang/Thread"
 #define THREADMETH  "<init>"
 #define THREADDESC  "(Ljava/lang/VMThread;Ljava/lang/String;IZ)V"

 #define THREADGROUPCLASS "java/lang/ThreadGroup"
 #define THREADGROUPMETH  "addThread"
 #define THREADGROUPDESC  "(Ljava/lang/Thread;)V"
#endif

#endif /* _GLOBAL_H */


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
