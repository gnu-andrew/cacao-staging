/* vm/loader.h - class loader header

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

   $Id: loader.h 2062 2005-03-23 11:16:45Z twisti $
*/


#ifndef _LOADER_H
#define _LOADER_H

#include <stdio.h>

#if defined(USE_ZLIB)
# include "vm/unzip.h"
#endif


/* datastruture from a classfile */

typedef struct classbuffer {
	classinfo *class;                   /* pointer to classinfo structure     */
	u1        *data;                    /* pointer to byte code               */
	s4         size;                    /* size of the byte code              */
	u1         *pos;                    /* current read position              */
} classbuffer;



/* classpath_info *************************************************************/

#define CLASSPATH_PATH       0
#define CLASSPATH_ARCHIVE    1

typedef struct classpath_info classpath_info;

struct classpath_info {
#if defined(USE_THREADS)
	/* Required for monitor locking on zip/jar files. */
	java_objectheader  header;
#endif
	s4                 type;
	char              *path;
	s4                 pathlen;
	struct java_security_ProtectionDomain *pd;
#if defined(USE_ZLIB)
	unzFile            uf;
#endif	
	classpath_info    *next;
};


/* export variables */

#if defined(USE_THREADS)
extern int blockInts;
#endif

extern classpath_info *classpath_entries;

/* pseudo classes for the type checker ****************************************/

/*
 * pseudo_class_Arraystub
 *     (extends Object implements Cloneable, java.io.Serializable)
 *
 *     If two arrays of incompatible component types are merged,
 *     the resulting reference has no accessible components.
 *     The result does, however, implement the interfaces Cloneable
 *     and java.io.Serializable. This pseudo class is used internally
 *     to represent such results. (They are *not* considered arrays!)
 *
 * pseudo_class_Null
 *
 *     This pseudo class is used internally to represent the
 *     null type.
 *
 * pseudo_class_New
 *
 *     This pseudo class is used internally to represent the
 *     the 'uninitialized object' type.
 */

extern classinfo *pseudo_class_Arraystub;
extern classinfo *pseudo_class_Null;
extern classinfo *pseudo_class_New;
extern vftbl_t *pseudo_class_Arraystub_vftbl;

extern utf *array_packagename;


/************************ prototypes ******************************************/

/* initialize loader, load important systemclasses */
bool loader_init(u1 *stackbottom);

void suck_init(char *cpath);
void create_all_classes(void);
void suck_stop(classbuffer *cb);

/* free resources */
void loader_close(void);

void loader_compute_subclasses(classinfo *c);

/* retrieve constantpool element */
voidptr class_getconstant(classinfo *class, u4 pos, u4 ctype);

/* determine type of a constantpool element */
u4 class_constanttype(classinfo *class, u4 pos);

s4 class_findmethodIndex(classinfo *c, utf *name, utf *desc);

/* search class for a field */
fieldinfo *class_findfield(classinfo *c, utf *name, utf *desc);
fieldinfo *class_resolvefield(classinfo *c, utf *name, utf *desc, classinfo *referer, bool except);

/* search for a method with a specified name and descriptor */
methodinfo *class_findmethod(classinfo *c, utf *name, utf *desc);
methodinfo *class_fetchmethod(classinfo *c, utf *name, utf *desc);
methodinfo *class_findmethod_w(classinfo *c, utf *name, utf *desc, char*);
methodinfo *class_resolvemethod(classinfo *c, utf *name, utf *dest);
methodinfo *class_resolveclassmethod(classinfo *c, utf *name, utf *dest, classinfo *referer, bool except);
methodinfo *class_resolveinterfacemethod(classinfo *c, utf *name, utf *dest, classinfo *referer, bool except);

/* search for a method with specified name and arguments (returntype ignored) */
methodinfo *class_findmethod_approx(classinfo *c, utf *name, utf *desc);
methodinfo *class_resolvemethod_approx(classinfo *c, utf *name, utf *dest);

bool class_issubclass(classinfo *sub, classinfo *super);

/* call initializer of class */
classinfo *class_init(classinfo *c);

void class_showconstanti(classinfo *c, int ii);

/* debug purposes */
void class_showmethods(classinfo *c);
void class_showconstantpool(classinfo *c);
void print_arraydescriptor(FILE *file, arraydescriptor *desc);

/* return the primitive class inidicated by the given signature character */
classinfo *class_primitive_from_sig(char sig);


/* return the class indicated by the given descriptor */
/* (see loader.c for documentation) */
#define CLASSLOAD_NEW           0      /* default */
#define CLASSLOAD_LOAD          0x0001
#define CLASSLOAD_SKIP          0x0002
#define CLASSLOAD_PANIC         0      /* default */
#define CLASSLOAD_NOPANIC       0x0010
#define CLASSLOAD_PRIMITIVE     0      /* default */
#define CLASSLOAD_NULLPRIMITIVE 0x0020
#define CLASSLOAD_VOID          0      /* default */
#define CLASSLOAD_NOVOID        0x0040
#define CLASSLOAD_NOCHECKEND    0      /* default */
#define CLASSLOAD_CHECKEND      0x1000

classinfo *class_from_descriptor(char *utf_ptr,char *end_ptr,char **next,int mode);
int type_from_descriptor(classinfo **cls,char *utf_ptr,char *end_ptr,char **next,int mode);

/* (used by class_new, don't use directly) */
void class_new_array(classinfo *c);

#define LAZYLOADING(class) { \
        if (!class->loaded) \
            if (!class_load(class)) \
                return 0; \
        if (!class->linked) \
            if (!class_link(class)) \
                return 0; }



#define LAZYLOADING1(class) { \
        if (!class->loaded) \
            if (!class_load(class)) \
                return; \
        if (!class->linked) \
            if (!class_link(class)) \
                return; }


classinfo *class_load(classinfo *c);
classinfo *class_load_intern(classbuffer *cb);
classinfo *class_link(classinfo *c);
void class_free(classinfo *c);

void field_display(fieldinfo *f);

void method_display(methodinfo *m);
void method_display_w_class(methodinfo *m);

utf* clinit_desc(void);
utf* clinit_name(void);

#endif /* _LOADER_H */


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
