/* typeinfo.h - type system used by the type checker

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

   Authors: Edwin Steiner

   $Id: typeinfo.h 3440 2005-10-17 11:52:00Z edwin $

*/

#ifndef _TYPEINFO_H
#define _TYPEINFO_H

#include "vm/global.h"
#include "vm/references.h"

/* configuration **************************************************************/

/*
 * TYPECHECK_STATISTICS activates gathering statistical information.
 * TYPEINFO_DEBUG activates debug checks and debug helpers in typeinfo.c
 * TYPECHECK_DEBUG activates debug checks in typecheck.c
 * TYPEINFO_DEBUG_TEST activates the typeinfo test at startup.
 * TYPECHECK_VERBOSE_IMPORTANT activates important debug messages
 * TYPECHECK_VERBOSE activates all debug messages
 * TYPEINFO_VERBOSE activates debug prints in typeinfo.c
 */
#ifdef CACAO_TYPECHECK
#ifndef NDEBUG
/*#define TYPECHECK_STATISTICS*/
#define TYPEINFO_DEBUG
/*#define TYPEINFO_VERBOSE*/
#define TYPECHECK_DEBUG
/*#define TYPEINFO_DEBUG_TEST*/
/*#define TYPECHECK_VERBOSE*/
/*#define TYPECHECK_VERBOSE_IMPORTANT*/
#if defined(TYPECHECK_VERBOSE) || defined(TYPECHECK_VERBOSE_IMPORTANT)
#define TYPECHECK_VERBOSE_OPT
#endif
#endif
#endif

#ifdef TYPECHECK_VERBOSE_OPT
extern bool typecheckverbose;
#endif

/* resolve typedef cycles *****************************************************/

typedef struct typeinfo typeinfo;
typedef struct typeinfo_mergedlist typeinfo_mergedlist;
typedef struct typedescriptor typedescriptor;
typedef struct typevector typevector;
typedef struct typeinfo_retaddr_set typeinfo_retaddr_set;

/* types **********************************************************************/

/* typecheck_result - return type for boolean and tristate  functions     */
/*                    which may also throw exceptions (typecheck_FAIL).   */

/* NOTE: Use the enum values, not the uppercase #define macros!          */
#define TYPECHECK_MAYBE  0x02
#define TYPECHECK_FAIL   0x04

typedef enum {
	typecheck_FALSE = false,
	typecheck_TRUE  = true,
	typecheck_MAYBE = TYPECHECK_MAYBE,
	typecheck_FAIL  = TYPECHECK_FAIL
} typecheck_result;

/* check that typecheck_MAYBE is not ambiguous */
#if TYPECHECK_MAYBE == true
#error "`typecheck_MAYBE` must not be the same as `true`"
#endif
#if TYPECHECK_MAYBE == false
#error "`typecheck_MAYBE` must not be the same as `false`"
#endif

/* check that typecheck_FAIL is not ambiguous */
#if (true & TYPECHECK_FAIL) != 0
#error "`true` must not have bit 0x02 set (conflicts with typecheck_FAIL)"
#endif

/* data structures for the type system ****************************************/

/* The typeinfo structure stores detailed information on address types.
 * (stack elements, variables, etc. with type == TYPE_ADR.)
 *
 * There are two kinds of address types which can be distinguished by
 * the value of the typeclass field:
 *
 * 1) typeclass == NULL: returnAddress type
 *                       use TYPEINFO_IS_PRIMITIVE to test for this
 *
 * 2) typeclass != NULL: reference type
 *                       use TYPEINFO_IS_REFERENCE to test for this
 *
 * Note: For non-address types either there is no typeinfo allocated
 * or the fields of the typeinfo struct contain undefined values!
 * DO NOT access the typeinfo for non-address types!
 *
 * CAUTION: The typeinfo structure should be considered opaque outside of
 *          typeinfo.[ch]. Please use the macros and functions defined here to
 *          access typeinfo structures!
 */

/* At all times *exactly one* of the following conditions is true for
 * a particular typeinfo struct:
 *
 * A) typeclass == NULL
 *
 *        This is a returnAddress type. The interpretation of the
 *        elementclass field depends on wether this typeinfo describes
 *        a stack slot or a local variable:
 *
 *        stack slot: elementclass is a pointer to a
 *            typeinfo_retaddr_set which contains a return target for
 *            every vector in the current set of local variable vectors.
 *            See typeinfo_retaddr_set and typevector below.
 *
 *        local variable: elementclass is the return target (when cast
 *            to basicblock *)
 *
 *        Use TYPEINFO_IS_PRIMITIVE to check for this.
 *        Use TYPEINFO_RETURNADDRESS to access the pointer in elementclass.
 *        Don't access other fields of the struct.
 *
 * B) typeclass == pseudo_class_Null
 *
 *        This is the null-reference type. 
 *        Use TYPEINFO_IS_NULLTYPE to check for this.
 *        Don't access other fields of the struct.
 *
 * C) typeclass == pseudo_class_New
 *
 *        This is an 'uninitialized object' type. elementclass can be
 *        cast to instruction* and points to the NEW instruction
 *        responsible for creating this type.
 *
 *        Use TYPEINFO_NEWOBJECT_INSTRUCTION to access the pointer in
 *        elementclass.
 *        Don't access other fields of the struct.
 *
 * D) typeclass == pseudo_class_Arraystub
 *
 *        This type is used to represent the result of merging array types
 *        with incompatible component types. An arraystub allows no access
 *        to its components (since their type is undefined), but it allows
 *        operations which act directly on an arbitrary array type (such as
 *        requesting the array size).
 *
 *        NOTE: An array stub does *not* count as an array. It has dimension
 *              zero.
 *
 *        Otherwise like a normal class reference type.
 *        Don't access other fields of the struct.
 *
 * E) typeclass is an array class
 *
 *        An array reference.
 *            elementclass...typeclass of the element type
 *            dimension......dimension of the array (>=1)
 *            elementtype....element type (ARRAYTYPE_...)
 *            merged.........mergedlist of the element type
 *
 *        Use TYPEINFO_IS_ARRAY to check for this case.
 *
 *        The elementclass may be one of the following:
 *        1) pseudo_class_Arraystub
 *        2) an unresolved type
 *        3) a loaded interface
 *        4) a loaded (non-pseudo-,non-array-)class != (BOOTSTRAP)java.lang.Object
 *                Note: `merged` may be used
 *        5) (BOOTSTRAP)java.lang.Object
 *                Note: `merged` may be used
 *
 *        For the semantics of the merged field in cases 4) and 5) consult the 
 *        corresponding descriptions with `elementclass` replaced by `typeclass`.
 *
 * F) typeclass is an unresolved type (a symbolic class/interface reference)
 *
 *        The type has not been resolved yet. (Meaning it corresponds to an
 *        unloaded class or interface).
 *        Don't access other fields of the struct.
 *
 * G) typeclass is a loaded interface
 *
 *        An interface reference type.
 *        Don't access other fields of the struct.
 *
 * H) typeclass is a loaded (non-pseudo-,non-array-)class != (BOOTSTRAP)java.lang.Object
 *
 *        A loaded class type.
 *        All classref_or_classinfos in u.merged.list (if any) are
 *        loaded subclasses of typeclass (no interfaces, array classes, or
 *        unresolved types).
 *        Don't access other fields of the struct.
 *
 * I) typeclass is (BOOTSTRAP)java.lang.Object
 *
 *        The most general kind of reference type.
 *        In this case u.merged.count and u.merged.list
 *        are valid and may be non-zero.
 *        The classref_or_classinfos in u.merged.list (if any) may be
 *        classes, interfaces, pseudo classes or unresolved types.
 *        Don't access other fields of the struct.
 */

/* The following algorithm is used to determine if the type described
 * by this typeinfo struct supports the interface X:  * XXX add MAYBE *
 *
 *     1) If typeclass is X or a subinterface of X the answer is "yes".
 *     2) If typeclass is a (pseudo) class implementing X the answer is "yes".
 *     3) If typeclass is not an array and u.merged.count>0
 *        and all classes/interfaces in u.merged.list implement X
 *        the answer is "yes".
 *     4) If none of the above is true the answer is "no".
 */

/*
 * CAUTION: The typeinfo structure should be considered opaque outside of
 *          typeinfo.[ch]. Please use the macros and functions defined here to
 *          access typeinfo structures!
 */
struct typeinfo {
	classref_or_classinfo  typeclass;
	classref_or_classinfo  elementclass; /* valid if dimension>0 */ /* various uses! */
	typeinfo_mergedlist   *merged;
	u1                     dimension;
	u1                     elementtype;  /* valid if dimension>0           */
};

struct typeinfo_mergedlist {
	s4                    count;
	classref_or_classinfo list[1];       /* variable length!                        */
};

/*-----------------------------------------------------------------------*/
/* a typeinfo_retaddr_set stores the set of possible returnAddresses     */
/* that may be in a particular stack slot at a particular point in the   */
/* program.                                                              */
/*                                                                       */
/* There may be one or more alternative returnAddresses if the           */
/* instruction can be reached via one or more JSR jumps (among other     */
/* control-flow paths                                                    */
/*-----------------------------------------------------------------------*/

struct typeinfo_retaddr_set {
	typeinfo_retaddr_set *alt;  /* next alternative in set               */
	void                 *addr; /* return address                        */
};

/* a type descriptor stores a basic type and the typeinfo                */
/* this is used for storing the type of a local variable, and for        */
/* storing types in the signature of a method                            */

struct typedescriptor {
	typeinfo        info;     /* valid if type == TYPE_ADR               */
	u1              type;     /* basic type (TYPE_INT, ...)              */
};

/*-----------------------------------------------------------------------*/
/* typevectors are used to store the types of all local variables        */
/* at a given point in the program.                                      */
/*                                                                       */
/* There may be more than one possible typevector for the local          */
/* variables at a given instruction if the instruction can be reached    */
/* via one or more JSR jumps (among other control-flow paths).           */
/*                                                                       */
/* This is called the set of alternative type vectors at that            */
/* particular point in the program.                                      */
/*-----------------------------------------------------------------------*/

struct typevector {
	typevector      *alt;     /* next alternative in typevector set      */
	int              k;       /* for lining up with the stack set        */
	typedescriptor   td[1];   /* types of locals, variable length!       */
};

/****************************************************************************/
/* MACROS                                                                   */
/****************************************************************************/

/* NOTE: The TYPEINFO macros take typeinfo *structs*, not pointers as
 *       arguments.  You have to dereference any pointers.
 */

/* typevectors **************************************************************/

#define TYPEVECTOR_SIZE(size)						\
    ((sizeof(typevector) - sizeof(typedescriptor))	\
     + (size)*sizeof(typedescriptor))

#define DNEW_TYPEVECTOR(size)						\
    ((typevector*)dump_alloc(TYPEVECTOR_SIZE(size)))

#define DMNEW_TYPEVECTOR(num,size)						\
    ((void*)dump_alloc((num) * TYPEVECTOR_SIZE(size)))

#define MGET_TYPEVECTOR(array,index,size) \
    ((typevector*) (((u1*)(array)) + TYPEVECTOR_SIZE(size) * (index)))

#define COPY_TYPEVECTORSET(src,dst,size)						\
    do {memcpy(dst,src,TYPEVECTOR_SIZE(size));					\
        dst->k = 0;                                             \
        if ((src)->alt) {										\
	        (dst)->alt = typevectorset_copy((src)->alt,1,size);	\
        }} while(0)

/* internally used macros ***************************************************/

/* internal, don't use this explicitly! */
#define TYPEINFO_ALLOCMERGED(mergedlist,count)                  \
    do {(mergedlist) = (typeinfo_mergedlist*)dump_alloc(        \
            sizeof(typeinfo_mergedlist)                         \
            + ((count)-1)*sizeof(classinfo*));} while(0)

/* internal, don't use this explicitly! */
#define TYPEINFO_FREEMERGED(mergedlist)

/* internal, don't use this explicitly! */
#define TYPEINFO_FREEMERGED_IF_ANY(mergedlist)

/* macros for type queries **************************************************/

#define TYPEINFO_IS_PRIMITIVE(info)                             \
            ((info).typeclass.any == NULL)

#define TYPEINFO_IS_REFERENCE(info)                             \
            ((info).typeclass.any != NULL)

#define TYPEINFO_IS_NULLTYPE(info)                              \
            ((info).typeclass.cls == pseudo_class_Null)

#define TYPEINFO_IS_NEWOBJECT(info)                             \
            ((info).typeclass.cls == pseudo_class_New)

/* only use this if TYPEINFO_IS_PRIMITIVE returned true! */
#define TYPEINFO_RETURNADDRESS(info)                            \
            ((void *)(info).elementclass.any)

/* only use this if TYPEINFO_IS_NEWOBJECT returned true! */
#define TYPEINFO_NEWOBJECT_INSTRUCTION(info)                    \
            ((void *)(info).elementclass.any)

/* macros for array type queries ********************************************/

#define TYPEINFO_IS_ARRAY(info)                                 \
            ( TYPEINFO_IS_REFERENCE(info)                       \
              && ((info).dimension != 0) )

#define TYPEINFO_IS_SIMPLE_ARRAY(info)                          \
            ( ((info).dimension == 1) )

#define TYPEINFO_IS_ARRAY_ARRAY(info)                           \
            ( ((info).dimension >= 2) )

#define TYPEINFO_IS_PRIMITIVE_ARRAY(info,arraytype)             \
            ( TYPEINFO_IS_SIMPLE_ARRAY(info)                    \
              && ((info).elementtype == (arraytype)) )

#define TYPEINFO_IS_OBJECT_ARRAY(info)                          \
            ( TYPEINFO_IS_SIMPLE_ARRAY(info)                    \
              && ((info).elementclass.any != NULL) )

/* assumes that info describes an array type */
#define TYPEINFO_IS_ARRAY_OF_REFS_NOCHECK(info)                 \
            ( ((info).elementclass.any != NULL)                 \
              || ((info).dimension >= 2) )

#define TYPEINFO_IS_ARRAY_OF_REFS(info)                         \
            ( TYPEINFO_IS_ARRAY(info)                           \
              && TYPEINFO_IS_ARRAY_OF_REFS_NOCHECK(info) )

#define TYPE_IS_RETURNADDRESS(type,info)                        \
            ( ((type)==TYPE_ADDRESS)                            \
              && TYPEINFO_IS_PRIMITIVE(info) )

#define TYPE_IS_REFERENCE(type,info)                            \
            ( ((type)==TYPE_ADDRESS)                            \
              && !TYPEINFO_IS_PRIMITIVE(info) )

#define TYPEDESC_IS_RETURNADDRESS(td)                           \
            TYPE_IS_RETURNADDRESS((td).type,(td).info)

#define TYPEDESC_IS_REFERENCE(td)                               \
            TYPE_IS_REFERENCE((td).type,(td).info)

/* queries allowing the null type ********************************************/

#define TYPEINFO_MAYBE_ARRAY(info)                              \
    (TYPEINFO_IS_ARRAY(info) || TYPEINFO_IS_NULLTYPE(info))

#define TYPEINFO_MAYBE_PRIMITIVE_ARRAY(info,at)                 \
    (TYPEINFO_IS_PRIMITIVE_ARRAY(info,at) || TYPEINFO_IS_NULLTYPE(info))

#define TYPEINFO_MAYBE_ARRAY_OF_REFS(info)                      \
    (TYPEINFO_IS_ARRAY_OF_REFS(info) || TYPEINFO_IS_NULLTYPE(info))

/* macros for initializing typeinfo structures ******************************/

#define TYPEINFO_INIT_PRIMITIVE(info)                           \
         do {(info).typeclass.any = NULL;                       \
             (info).elementclass.any = NULL;                    \
             (info).merged = NULL;                              \
             (info).dimension = 0;                              \
             (info).elementtype = 0;} while(0)

#define TYPEINFO_INIT_RETURNADDRESS(info,adr)                   \
         do {(info).typeclass.any = NULL;                       \
             (info).elementclass.any = (adr);                   \
             (info).merged = NULL;                              \
             (info).dimension = 0;                              \
             (info).elementtype = 0;} while(0)

#define TYPEINFO_INIT_NON_ARRAY_CLASSINFO(info,cinfo)   \
         do {(info).typeclass.cls = (cinfo);            \
             (info).elementclass.any = NULL;            \
             (info).merged = NULL;                      \
             (info).dimension = 0;                      \
             (info).elementtype = 0;} while(0)

#define TYPEINFO_INIT_NULLTYPE(info)                            \
            TYPEINFO_INIT_NON_ARRAY_CLASSINFO(info,pseudo_class_Null)

#define TYPEINFO_INIT_NEWOBJECT(info,instr)             \
         do {(info).typeclass.cls = pseudo_class_New;   \
             (info).elementclass.any = (instr);         \
             (info).merged = NULL;                      \
             (info).dimension = 0;                      \
             (info).elementtype = 0;} while(0)

#define TYPEINFO_INIT_PRIMITIVE_ARRAY(info,arraytype)                   \
    TYPEINFO_INIT_CLASSINFO(info,primitivetype_table[arraytype].arrayclass);

#define TYPEINFO_INIT_CLASSINFO(info,c)                                 \
        do {if (((info).typeclass.cls = (c))->vftbl->arraydesc) {       \
                if ((c)->vftbl->arraydesc->elementvftbl)                \
                    (info).elementclass.cls = (c)->vftbl->arraydesc->elementvftbl->class; \
                else                                                    \
                    (info).elementclass.any = NULL;                     \
                (info).dimension = (c)->vftbl->arraydesc->dimension;    \
                (info).elementtype = (c)->vftbl->arraydesc->elementtype;\
            }                                                           \
            else {                                                      \
                (info).elementclass.any = NULL;                         \
                (info).dimension = 0;                                   \
                (info).elementtype = 0;                                 \
            }                                                           \
            (info).merged = NULL;} while(0)

/* macros for copying types (destinition is not checked or freed) ***********/

/* TYPEINFO_COPY makes a shallow copy, the merged pointer is simply copied. */
#define TYPEINFO_COPY(src,dst)                                  \
    do {(dst) = (src);} while(0)

/* TYPEINFO_CLONE makes a deep copy, the merged list (if any) is duplicated
 * into a newly allocated array.
 */
#define TYPEINFO_CLONE(src,dst)                                 \
    do {(dst) = (src);                                          \
        if ((dst).merged) typeinfo_clone(&(src),&(dst));} while(0)

/****************************************************************************/
/* FUNCTIONS                                                                */
/****************************************************************************/

/* typevector functions *****************************************************/

/* element read-only access */
bool typevectorset_checktype(typevector *set,int index,int type);
bool typevectorset_checkreference(typevector *set,int index);
bool typevectorset_checkretaddr(typevector *set,int index);
int typevectorset_copymergedtype(methodinfo *m,typevector *set,int index,typeinfo *dst);
int typevectorset_mergedtype(methodinfo *m,typevector *set,int index,typeinfo *temp,typeinfo **result);

/* element write access */
void typevectorset_store(typevector *set,int index,int type,typeinfo *info);
void typevectorset_store_retaddr(typevector *set,int index,typeinfo *info);
void typevectorset_store_twoword(typevector *set,int index,int type);
bool typevectorset_init_object(typevector *set,void *ins,classref_or_classinfo initclass,int size);

/* vector functions */
bool typevector_separable_from(typevector *a,typevector *b,int size);
typecheck_result typevector_merge(methodinfo *m,typevector *dst,typevector *y,int size);

/* vector set functions */
typevector *typevectorset_copy(typevector *src,int k,int size);
bool typevectorset_separable_with(typevector *set,typevector *add,int size);
typecheck_result typevectorset_collapse(methodinfo *m,typevector *dst,int size);
void typevectorset_add(typevector *dst,typevector *v,int size);
typevector *typevectorset_select(typevector **set,int retindex,void *retaddr);

/* inquiry functions (read-only) ********************************************/

bool typeinfo_is_array(typeinfo *info);
bool typeinfo_is_primitive_array(typeinfo *info,int arraytype);
bool typeinfo_is_array_of_refs(typeinfo *info);

typecheck_result typeinfo_is_assignable(typeinfo *value,typeinfo *dest);
typecheck_result typeinfo_is_assignable_to_class(typeinfo *value,classref_or_classinfo dest);

/* initialization functions *************************************************/

/* RETURN VALUE (bool):
 *     true.............ok,
 *     false............an exception has been thrown.
 *
 * RETURN VALUE (int):
 *     >= 0.............ok,
 *     -1...............an exception has been thrown.
 */
bool typeinfo_init_class(typeinfo *info,classref_or_classinfo c);
bool typeinfo_init_component(typeinfo *srcarray,typeinfo *dst);

bool typeinfo_init_from_typedesc(typedesc *desc,u1 *type,typeinfo *info);
bool typeinfos_init_from_methoddesc(methoddesc *desc,u1 *typebuf,
                                   typeinfo *infobuf,
                                   int buflen,bool twoword,
                                   u1 *returntype,typeinfo *returntypeinfo);
bool  typedescriptor_init_from_typedesc(typedescriptor *td,
									    typedesc *desc);
int  typedescriptors_init_from_methoddesc(typedescriptor *td,
										  methoddesc *desc,
										  int buflen,bool twoword,int startindex,
										  typedescriptor *returntype);

void typeinfo_clone(typeinfo *src,typeinfo *dest);

/* freeing memory ***********************************************************/

void typeinfo_free(typeinfo *info);

/* functions for merging types **********************************************/

typecheck_result typeinfo_merge(methodinfo *m,typeinfo *dest,typeinfo* y);

/* debugging helpers ********************************************************/

#ifdef TYPEINFO_DEBUG

#include <stdio.h>

void typeinfo_test();
void typeinfo_print_class(FILE *file,classref_or_classinfo c);
void typeinfo_print(FILE *file,typeinfo *info,int indent);
void typeinfo_print_short(FILE *file,typeinfo *info);
void typeinfo_print_type(FILE *file,int type,typeinfo *info);
void typeinfo_print_stacktype(FILE *file,int type,typeinfo *info);
void typedescriptor_print(FILE *file,typedescriptor *td);
void typevector_print(FILE *file,typevector *vec,int size);
void typevectorset_print(FILE *file,typevector *set,int size);

#endif /* TYPEINFO_DEBUG */

#endif /* _TYPEINFO_H */


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
