/* src/vm/loader.hpp - class loader header

   Copyright (C) 1996-2005, 2006, 2007, 2008
   CACAOVM - Verein zur Foerderung der freien virtuellen Maschine CACAO

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


#ifndef _LOADER_HPP
#define _LOADER_HPP

/* forward typedefs ***********************************************************/

typedef struct classbuffer classbuffer;


#include "config.h"

#include <stdio.h>

#include "vm/types.h"

#include "vm/descriptor.hpp"
#include "vm/class.hpp"
#include "vm/global.h"
#include "vm/method.hpp"
#include "vm/references.h"
#include "vm/utf8.hpp"


/* constant pool entries *******************************************************

	All constant pool entries need a data structure which contain the entrys
	value. In some cases this structure exist already, in the remaining cases
	this structure must be generated:

		kind                      structure                     generated?
	----------------------------------------------------------------------
    CONSTANT_Class               constant_classref                  yes
    CONSTANT_Fieldref            constant_FMIref                    yes
    CONSTANT_Methodref           constant_FMIref                    yes
    CONSTANT_InterfaceMethodref  constant_FMIref                    yes
    CONSTANT_String              unicode                             no
    CONSTANT_Integer             constant_integer                   yes
    CONSTANT_Float               constant_float                     yes
    CONSTANT_Long                constant_long                      yes
    CONSTANT_Double              constant_double                    yes
    CONSTANT_NameAndType         constant_nameandtype               yes
    CONSTANT_Utf8                unicode                             no
    CONSTANT_UNUSED              -

*******************************************************************************/

typedef struct {            /* Integer                                        */
	s4 value;
} constant_integer;

	
typedef struct {            /* Float                                          */
	float value;
} constant_float;


typedef struct {            /* Long                                           */
	s8 value;
} constant_long;
	

typedef struct {            /* Double                                         */
	double value;
} constant_double;


typedef struct {            /* NameAndType (Field or Method)                  */
	utf *name;              /* field/method name                              */
	utf *descriptor;        /* field/method type descriptor string            */
} constant_nameandtype;


/* classbuffer ****************************************************************/

struct classbuffer {
	classinfo *clazz;                   /* pointer to classinfo structure     */
	uint8_t   *data;                    /* pointer to byte code               */
	int32_t    size;                    /* size of the byte code              */
	uint8_t   *pos;                     /* current read position              */
	char      *path;                    /* path to file (for debugging)       */
};


/* hashtable_classloader_entry *************************************************

   ATTENTION: The pointer to the classloader object needs to be the
   first field of the entry, so that it can be used as an indirection
   cell. This is checked by gc_init() during startup.

*******************************************************************************/

typedef struct hashtable_classloader_entry hashtable_classloader_entry;

struct hashtable_classloader_entry {
	java_object_t               *object;
	hashtable_classloader_entry *hashlink;
};


/* classloader *****************************************************************

   [!ENABLE_HANDLES]: The classloader is a Java Object which cannot move.
   [ENABLE_HANDLES] : The classloader entry itself is a static handle for a
                      given classloader (use loader_hashtable_classloader_foo).

*******************************************************************************/

#if defined(ENABLE_HANDLES)
typedef hashtable_classloader_entry classloader_t;
#else
typedef java_object_t               classloader_t;
#endif


/* function prototypes ********************************************************/

#ifdef __cplusplus
extern "C" {
#endif

void loader_preinit(void);
void loader_init(void);

/* classloader management functions */
classloader_t *loader_hashtable_classloader_add(java_handle_t *cl);
classloader_t *loader_hashtable_classloader_find(java_handle_t *cl);

void loader_load_all_classes(void);

bool loader_skip_attribute_body(classbuffer *cb);

#if defined(ENABLE_JAVASE)
bool loader_load_attribute_signature(classbuffer *cb, utf **signature);
#endif

/* free resources */
void loader_close(void);

/* class loading functions */
classinfo *load_class_from_sysloader(utf *name);
classinfo *load_class_from_classloader(utf *name, classloader_t *cl);
classinfo *load_class_bootstrap(utf *name);

/* (don't use the following directly) */
classinfo *load_class_from_classbuffer(classbuffer *cb);
classinfo *load_newly_created_array(classinfo *c, classloader_t *loader);

#ifdef __cplusplus
}
#endif

#endif // _LOADER_HPP


/*
 * These are local overrides for various environment variables in Emacs.
 * Please do not remove this and leave it at the end of the file, where
 * Emacs will automagically detect them.
 * ---------------------------------------------------------------------
 * Local variables:
 * mode: c++
 * indent-tabs-mode: t
 * c-basic-offset: 4
 * tab-width: 4
 * End:
 * vim:noexpandtab:sw=4:ts=4:
 */
