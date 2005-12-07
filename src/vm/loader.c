/* src/vm/loader.c - class loader functions

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

   Changes: Andreas Krall
            Roman Obermaiser
            Mark Probst
            Edwin Steiner
            Christian Thalinger

   $Id: loader.c 3903 2005-12-07 17:43:29Z twisti $

*/

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "config.h"
#include "vm/types.h"
#include "mm/memory.h"
#include "native/native.h"
#include "native/include/java_lang_Throwable.h"

#if defined(USE_THREADS)
# if defined(NATIVE_THREADS)
#  include "threads/native/threads.h"
# else
#  include "threads/green/threads.h"
#  include "threads/green/locks.h"
# endif
#endif

#include "toolbox/logging.h"
#include "vm/exceptions.h"
#include "vm/builtin.h"
#include "vm/global.h"
#include "vm/linker.h"
#include "vm/loader.h"
#include "vm/options.h"
#include "vm/statistics.h"
#include "vm/stringlocal.h"
#include "vm/suck.h"
#include "vm/classcache.h"

#if defined(USE_ZLIB)
# include "vm/unzip.h"
#endif

#include "vm/jit/asmpart.h"
#include "vm/jit/codegen.inc.h"

/******************************************************************************/
/* DEBUG HELPERS                                                              */
/******************************************************************************/

#ifndef NDEBUG
#define LOADER_DEBUG
#endif

#ifdef LOADER_DEBUG
#define LOADER_ASSERT(cond)  assert(cond)
#else
#define LOADER_ASSERT(cond)
#endif


/********************************************************************
   list of classpath entries (either filesystem directories or 
   ZIP/JAR archives
********************************************************************/

classpath_info *classpath_entries = NULL;


/* loader_init *****************************************************************

   Initializes all lists and loads all classes required for the system
   or the compiler.

*******************************************************************************/
 
bool loader_init(u1 *stackbottom)
{
#if defined(USE_THREADS) && defined(NATIVE_THREADS)
	classpath_info *cpi;

	/* Initialize the monitor pointer for zip/jar file locking. */

	for (cpi = classpath_entries; cpi != NULL; cpi = cpi->next) {
		if (cpi->type == CLASSPATH_ARCHIVE)
			initObjectLock(&cpi->header);
	}
#endif

	/* load some important classes */

	if (!(class_java_lang_Object = load_class_bootstrap(utf_java_lang_Object)))
		return false;

	if (!(class_java_lang_String = load_class_bootstrap(utf_java_lang_String)))
		return false;

	if (!(class_java_lang_Cloneable =
		  load_class_bootstrap(utf_java_lang_Cloneable)))
		return false;

	if (!(class_java_io_Serializable =
		  load_class_bootstrap(utf_java_io_Serializable)))
		return false;


	/* load classes for wrapping primitive types */

	if (!(class_java_lang_Void = load_class_bootstrap(utf_java_lang_Void)))
		return false;

	if (!(class_java_lang_Boolean =
		  load_class_bootstrap(utf_java_lang_Boolean)))
		return false;

	if (!(class_java_lang_Byte = load_class_bootstrap(utf_java_lang_Byte)))
		return false;

	if (!(class_java_lang_Character =
		  load_class_bootstrap(utf_java_lang_Character)))
		return false;

	if (!(class_java_lang_Short = load_class_bootstrap(utf_java_lang_Short)))
		return false;

	if (!(class_java_lang_Integer =
		  load_class_bootstrap(utf_java_lang_Integer)))
		return false;

	if (!(class_java_lang_Long = load_class_bootstrap(utf_java_lang_Long)))
		return false;

	if (!(class_java_lang_Float = load_class_bootstrap(utf_java_lang_Float)))
		return false;

	if (!(class_java_lang_Double = load_class_bootstrap(utf_java_lang_Double)))
		return false;


	/* load some other important classes */

	if (!(class_java_lang_Class = load_class_bootstrap(utf_java_lang_Class)))
		return false;

	if (!(class_java_lang_ClassLoader =
		  load_class_bootstrap(utf_java_lang_ClassLoader)))
		return false;

	if (!(class_java_lang_SecurityManager =
		  load_class_bootstrap(utf_java_lang_SecurityManager)))
		return false;

	if (!(class_java_lang_System = load_class_bootstrap(utf_java_lang_System)))
		return false;

	if (!(class_java_lang_Thread =
		  load_class_bootstrap(utf_new_char("java/lang/Thread"))))
		return false;

	if (!(class_java_lang_ThreadGroup =
		  load_class_bootstrap(utf_java_lang_ThreadGroup)))
		return false;

	if (!(class_java_lang_VMThread =
		  load_class_bootstrap(utf_new_char("java/lang/VMThread"))))
		return false;


	/* some classes which may be used more often */

	if (!(class_java_lang_StackTraceElement =
		  load_class_bootstrap(utf_java_lang_StackTraceElement)))
		return false;

	if (!(class_java_lang_reflect_Constructor =
		  load_class_bootstrap(utf_java_lang_reflect_Constructor)))
		return false;

	if (!(class_java_lang_reflect_Field =
		  load_class_bootstrap(utf_java_lang_reflect_Field)))
		return false;

	if (!(class_java_lang_reflect_Method =
		  load_class_bootstrap(utf_java_lang_reflect_Method)))
		return false;

	if (!(class_java_security_PrivilegedAction =
		  load_class_bootstrap(utf_new_char("java/security/PrivilegedAction"))))
		return false;

	if (!(class_java_util_Vector = load_class_bootstrap(utf_java_util_Vector)))
		return false;

	if (!(arrayclass_java_lang_Object =
		  load_class_bootstrap(utf_new_char("[Ljava/lang/Object;"))))
		return false;

#if defined(USE_THREADS)
	if (stackbottom != 0)
		initLocks();
#endif

	return true;
}


/* loader_load_all_classes *****************************************************

   Loads all classes specified in the BOOTCLASSPATH.

*******************************************************************************/

void loader_load_all_classes(void)
{
	classpath_info *cpi;
	classinfo      *c;

	for (cpi = classpath_entries; cpi != 0; cpi = cpi->next) {
#if defined(USE_ZLIB)
		if (cpi->type == CLASSPATH_ARCHIVE) {
			cacao_entry_s *ce;
			unz_s *s;

			s = (unz_s *) cpi->uf;
			ce = s->cacao_dir_list;
				
			while (ce) {
				/* skip all entries in META-INF and .properties, .png files */

				if (strncmp(ce->name->text, "META-INF", strlen("META-INF")) &&
					!strstr(ce->name->text, ".properties") &&
					!strstr(ce->name->text, ".png"))
					c = load_class_bootstrap(ce->name);

				ce = ce->next;
			}

		} else {
#endif
#if defined(USE_ZLIB)
		}
#endif
	}
}


/******************************************************************************/
/******************* Some support functions ***********************************/
/******************************************************************************/

void fprintflags (FILE *fp, u2 f)
{
   if ( f & ACC_PUBLIC )       fprintf (fp," PUBLIC");
   if ( f & ACC_PRIVATE )      fprintf (fp," PRIVATE");
   if ( f & ACC_PROTECTED )    fprintf (fp," PROTECTED");
   if ( f & ACC_STATIC )       fprintf (fp," STATIC");
   if ( f & ACC_FINAL )        fprintf (fp," FINAL");
   if ( f & ACC_SYNCHRONIZED ) fprintf (fp," SYNCHRONIZED");
   if ( f & ACC_VOLATILE )     fprintf (fp," VOLATILE");
   if ( f & ACC_TRANSIENT )    fprintf (fp," TRANSIENT");
   if ( f & ACC_NATIVE )       fprintf (fp," NATIVE");
   if ( f & ACC_INTERFACE )    fprintf (fp," INTERFACE");
   if ( f & ACC_ABSTRACT )     fprintf (fp," ABSTRACT");
}


/********** internal function: printflags  (only for debugging) ***************/

void printflags(u2 f)
{
	fprintflags(stdout,f);
}


/********************** Function: skipattributebody ****************************

	skips an attribute after the 16 bit reference to attribute_name has already
	been read
	
*******************************************************************************/

static bool skipattributebody(classbuffer *cb)
{
	u4 len;

	if (!suck_check_classbuffer_size(cb, 4))
		return false;

	len = suck_u4(cb);

	if (!suck_check_classbuffer_size(cb, len))
		return false;

	suck_skip_nbytes(cb, len);

	return true;
}


/************************* Function: skipattributes ****************************

	skips num attribute structures
	
*******************************************************************************/

static bool skipattributes(classbuffer *cb, u4 num)
{
	u4 i;
	u4 len;

	for (i = 0; i < num; i++) {
		if (!suck_check_classbuffer_size(cb, 2 + 4))
			return false;

		suck_u2(cb);
		len = suck_u4(cb);

		if (!suck_check_classbuffer_size(cb, len))
			return false;

		suck_skip_nbytes(cb, len);
	}

	return true;
}


/* load_constantpool ***********************************************************

   Loads the constantpool of a class, the entries are transformed into
   a simpler format by resolving references (a detailed overview of
   the compact structures can be found in global.h).

*******************************************************************************/

static bool load_constantpool(classbuffer *cb, descriptor_pool *descpool)
{

	/* The following structures are used to save information which cannot be 
	   processed during the first pass. After the complete constantpool has 
	   been traversed the references can be resolved. 
	   (only in specific order)						   */
	
	/* CONSTANT_Class entries */
	typedef struct forward_class {
		struct forward_class *next;
		u2 thisindex;
		u2 name_index;
	} forward_class;

	/* CONSTANT_String */
	typedef struct forward_string {
		struct forward_string *next;
		u2 thisindex;
		u2 string_index;
	} forward_string;

	/* CONSTANT_NameAndType */
	typedef struct forward_nameandtype {
		struct forward_nameandtype *next;
		u2 thisindex;
		u2 name_index;
		u2 sig_index;
	} forward_nameandtype;

	/* CONSTANT_Fieldref, CONSTANT_Methodref or CONSTANT_InterfaceMethodref */
	typedef struct forward_fieldmethint {
		struct forward_fieldmethint *next;
		u2 thisindex;
		u1 tag;
		u2 class_index;
		u2 nameandtype_index;
	} forward_fieldmethint;


	classinfo *c;
	u4 idx;

	forward_class *forward_classes = NULL;
	forward_string *forward_strings = NULL;
	forward_nameandtype *forward_nameandtypes = NULL;
	forward_fieldmethint *forward_fieldmethints = NULL;

	forward_class *nfc;
	forward_string *nfs;
	forward_nameandtype *nfn;
	forward_fieldmethint *nff;

	u4 cpcount;
	u1 *cptags;
	voidptr *cpinfos;

	c = cb->class;

	/* number of entries in the constant_pool table plus one */
	if (!suck_check_classbuffer_size(cb, 2))
		return false;

	cpcount = c->cpcount = suck_u2(cb);

	/* allocate memory */
	cptags  = c->cptags  = MNEW(u1, cpcount);
	cpinfos = c->cpinfos = MNEW(voidptr, cpcount);

	if (cpcount < 1) {
		*exceptionptr = new_classformaterror(c, "Illegal constant pool size");
		return false;
	}
	
#if defined(STATISTICS)
	if (opt_stat)
		count_const_pool_len += (sizeof(voidptr) + 1) * cpcount;
#endif
	
	/* initialize constantpool */
	for (idx = 0; idx < cpcount; idx++) {
		cptags[idx] = CONSTANT_UNUSED;
		cpinfos[idx] = NULL;
	}

			
	/******* first pass *******/
	/* entries which cannot be resolved now are written into 
	   temporary structures and traversed again later        */
		   
	idx = 1;
	while (idx < cpcount) {
		u4 t;

		/* get constant type */
		if (!suck_check_classbuffer_size(cb, 1))
			return false;

		t = suck_u1(cb);

		switch (t) {
		case CONSTANT_Class:
			nfc = DNEW(forward_class);

			nfc->next = forward_classes;
			forward_classes = nfc;

			nfc->thisindex = idx;
			/* reference to CONSTANT_NameAndType */
			if (!suck_check_classbuffer_size(cb, 2))
				return false;

			nfc->name_index = suck_u2(cb);

			idx++;
			break;
			
		case CONSTANT_String:
			nfs = DNEW(forward_string);
				
			nfs->next = forward_strings;
			forward_strings = nfs;
				
			nfs->thisindex = idx;

			/* reference to CONSTANT_Utf8_info with string characters */
			if (!suck_check_classbuffer_size(cb, 2))
				return false;

			nfs->string_index = suck_u2(cb);
				
			idx++;
			break;

		case CONSTANT_NameAndType:
			nfn = DNEW(forward_nameandtype);
				
			nfn->next = forward_nameandtypes;
			forward_nameandtypes = nfn;
				
			nfn->thisindex = idx;

			if (!suck_check_classbuffer_size(cb, 2 + 2))
				return false;

			/* reference to CONSTANT_Utf8_info containing simple name */
			nfn->name_index = suck_u2(cb);

			/* reference to CONSTANT_Utf8_info containing field or method
			   descriptor */
			nfn->sig_index = suck_u2(cb);
				
			idx++;
			break;

		case CONSTANT_Fieldref:
		case CONSTANT_Methodref:
		case CONSTANT_InterfaceMethodref:
			nff = DNEW(forward_fieldmethint);
			
			nff->next = forward_fieldmethints;
			forward_fieldmethints = nff;

			nff->thisindex = idx;
			/* constant type */
			nff->tag = t;

			if (!suck_check_classbuffer_size(cb, 2 + 2))
				return false;

			/* class or interface type that contains the declaration of the
			   field or method */
			nff->class_index = suck_u2(cb);

			/* name and descriptor of the field or method */
			nff->nameandtype_index = suck_u2(cb);

			idx++;
			break;
				
		case CONSTANT_Integer: {
			constant_integer *ci = NEW(constant_integer);

#if defined(STATISTICS)
			if (opt_stat)
				count_const_pool_len += sizeof(constant_integer);
#endif

			if (!suck_check_classbuffer_size(cb, 4))
				return false;

			ci->value = suck_s4(cb);
			cptags[idx] = CONSTANT_Integer;
			cpinfos[idx] = ci;

			idx++;
			break;
		}
				
		case CONSTANT_Float: {
			constant_float *cf = NEW(constant_float);

#if defined(STATISTICS)
			if (opt_stat)
				count_const_pool_len += sizeof(constant_float);
#endif

			if (!suck_check_classbuffer_size(cb, 4))
				return false;

			cf->value = suck_float(cb);
			cptags[idx] = CONSTANT_Float;
			cpinfos[idx] = cf;

			idx++;
			break;
		}
				
		case CONSTANT_Long: {
			constant_long *cl = NEW(constant_long);
					
#if defined(STATISTICS)
			if (opt_stat)
				count_const_pool_len += sizeof(constant_long);
#endif

			if (!suck_check_classbuffer_size(cb, 8))
				return false;

			cl->value = suck_s8(cb);
			cptags[idx] = CONSTANT_Long;
			cpinfos[idx] = cl;
			idx += 2;
			if (idx > cpcount) {
				*exceptionptr =
					new_classformaterror(c, "Invalid constant pool entry");
				return false;
			}
			break;
		}
			
		case CONSTANT_Double: {
			constant_double *cd = NEW(constant_double);
				
#if defined(STATISTICS)
			if (opt_stat)
				count_const_pool_len += sizeof(constant_double);
#endif

			if (!suck_check_classbuffer_size(cb, 8))
				return false;

			cd->value = suck_double(cb);
			cptags[idx] = CONSTANT_Double;
			cpinfos[idx] = cd;
			idx += 2;
			if (idx > cpcount) {
				*exceptionptr =
					new_classformaterror(c, "Invalid constant pool entry");
				return false;
			}
			break;
		}
				
		case CONSTANT_Utf8: { 
			u4 length;

			/* number of bytes in the bytes array (not string-length) */
			if (!suck_check_classbuffer_size(cb, 2))
				return false;

			length = suck_u2(cb);
			cptags[idx] = CONSTANT_Utf8;

			/* validate the string */
			if (!suck_check_classbuffer_size(cb, length))
				return false;

#ifdef ENABLE_VERIFIER
			if (opt_verify &&
				!is_valid_utf((char *) cb->pos, (char *) (cb->pos + length))) 
			{
				*exceptionptr = new_classformaterror(c,"Invalid UTF-8 string");
				return false;
			}
#endif /* ENABLE_VERIFIER */
			/* insert utf-string into the utf-symboltable */
			cpinfos[idx] = utf_new((char *) cb->pos, length);

			/* skip bytes of the string (buffer size check above) */
			suck_skip_nbytes(cb, length);
			idx++;
			break;
		}
										
		default:
			*exceptionptr =
				new_classformaterror(c, "Illegal constant pool type");
			return false;
		}  /* end switch */
	} /* end while */


	/* resolve entries in temporary structures */

	while (forward_classes) {
		utf *name =
			class_getconstant(c, forward_classes->name_index, CONSTANT_Utf8);
		if (!name)
			return false;

#ifdef ENABLE_VERIFIER
		if (opt_verify && !is_valid_name_utf(name)) {
			*exceptionptr = 
				new_classformaterror(c, "Class reference with invalid name");
			return false;
		}
#endif /* ENABLE_VERIFIER */

		/* add all class references to the descriptor_pool */

		if (!descriptor_pool_add_class(descpool, name))
			return false;

		cptags[forward_classes->thisindex] = CONSTANT_Class;

		if (opt_eager) {
			classinfo *tc;

			if (!(tc = load_class_bootstrap(name)))
				return false;

			/* link the class later, because we cannot link the class currently
			   loading */
			list_addfirst(&unlinkedclasses, tc);
		}

		/* the classref is created later */
		cpinfos[forward_classes->thisindex] = name;

		nfc = forward_classes;
		forward_classes = forward_classes->next;
	}

	while (forward_strings) {
		utf *text =
			class_getconstant(c, forward_strings->string_index, CONSTANT_Utf8);
		if (!text)
			return false;

		/* resolve utf-string */
		cptags[forward_strings->thisindex] = CONSTANT_String;
		cpinfos[forward_strings->thisindex] = text;
		
		nfs = forward_strings;
		forward_strings = forward_strings->next;
	}

	while (forward_nameandtypes) {
		constant_nameandtype *cn = NEW(constant_nameandtype);	

#if defined(STATISTICS)
		if (opt_stat)
			count_const_pool_len += sizeof(constant_nameandtype);
#endif

		/* resolve simple name and descriptor */
		cn->name = class_getconstant(c,
									 forward_nameandtypes->name_index,
									 CONSTANT_Utf8);
		if (!cn->name)
			return false;

		cn->descriptor = class_getconstant(c,
										   forward_nameandtypes->sig_index,
										   CONSTANT_Utf8);
		if (!cn->descriptor)
			return false;

#ifdef ENABLE_VERIFIER
		if (opt_verify) {
			/* check name */
			if (!is_valid_name_utf(cn->name)) {
				*exceptionptr =
					new_classformaterror(c,
										 "Illegal Field name \"%s\"",
										 cn->name->text);

				return false;
			}

			/* disallow referencing <clinit> among others */
			if (cn->name->text[0] == '<' && cn->name != utf_init) {
				*exceptionptr =
					new_classformaterror(c,"Illegal reference to special method");
				return false;
			}
		}
#endif /* ENABLE_VERIFIER */

		cptags[forward_nameandtypes->thisindex] = CONSTANT_NameAndType;
		cpinfos[forward_nameandtypes->thisindex] = cn;

		nfn = forward_nameandtypes;
		forward_nameandtypes = forward_nameandtypes->next;
	}

	while (forward_fieldmethints) {
		constant_nameandtype *nat;
		constant_FMIref *fmi = NEW(constant_FMIref);

#if defined(STATISTICS)
		if (opt_stat)
			count_const_pool_len += sizeof(constant_FMIref);
#endif
		/* resolve simple name and descriptor */

		nat = class_getconstant(c,
								forward_fieldmethints->nameandtype_index,
								CONSTANT_NameAndType);
		if (!nat)
			return false;

		/* add all descriptors in {Field,Method}ref to the descriptor_pool */

		if (!descriptor_pool_add(descpool, nat->descriptor, NULL))
			return false;

		/* the classref is created later */

		fmi->classref = (constant_classref *) (size_t) forward_fieldmethints->class_index;
		fmi->name = nat->name;
		fmi->descriptor = nat->descriptor;

		cptags[forward_fieldmethints->thisindex] = forward_fieldmethints->tag;
		cpinfos[forward_fieldmethints->thisindex] = fmi;
	
		nff = forward_fieldmethints;
		forward_fieldmethints = forward_fieldmethints->next;
	}

	/* everything was ok */

	return true;
}


/* load_field ******************************************************************

   Load everything about a class field from the class file and fill a
   'fieldinfo' structure. For static fields, space in the data segment
   is allocated.

*******************************************************************************/

#define field_load_NOVALUE  0xffffffff /* must be bigger than any u2 value! */

static bool load_field(classbuffer *cb, fieldinfo *f, descriptor_pool *descpool)
{
	classinfo *c;
	u4 attrnum, i;
	u4 jtype;
	u4 pindex = field_load_NOVALUE;     /* constantvalue_index */
	utf *u;

	c = cb->class;

	if (!suck_check_classbuffer_size(cb, 2 + 2 + 2))
		return false;

	f->flags = suck_u2(cb);

	if (!(u = class_getconstant(c, suck_u2(cb), CONSTANT_Utf8)))
		return false;

	f->name = u;

	if (!(u = class_getconstant(c, suck_u2(cb), CONSTANT_Utf8)))
		return false;

	f->descriptor = u;
	f->parseddesc = NULL;

	if (!descriptor_pool_add(descpool, u, NULL))
		return false;

	/* descriptor_pool_add accepts method descriptors, so we have to check  */
	/* against them here before the call of descriptor_to_basic_type below. */
	if (u->text[0] == '(') {
		*exceptionptr = new_classformaterror(c,"Method descriptor used for field");
		return false;
	}

#ifdef ENABLE_VERIFIER
	if (opt_verify) {
		/* check name */
		if (!is_valid_name_utf(f->name) || f->name->text[0] == '<') {
			*exceptionptr = new_classformaterror(c,
												 "Illegal Field name \"%s\"",
												 f->name->text);
			return false;
		}

		/* check flag consistency */
		i = f->flags & (ACC_PUBLIC | ACC_PRIVATE | ACC_PROTECTED);

		if ((i != 0 && i != ACC_PUBLIC && i != ACC_PRIVATE && i != ACC_PROTECTED) ||
			((f->flags & (ACC_FINAL | ACC_VOLATILE)) == (ACC_FINAL | ACC_VOLATILE))) {
			*exceptionptr =
				new_classformaterror(c,
									 "Illegal field modifiers: 0x%X",
									 f->flags);
			return false;
		}

		if (c->flags & ACC_INTERFACE) {
			if (((f->flags & (ACC_STATIC | ACC_PUBLIC | ACC_FINAL))
				!= (ACC_STATIC | ACC_PUBLIC | ACC_FINAL)) ||
				f->flags & ACC_TRANSIENT) {
				*exceptionptr =
					new_classformaterror(c,
										 "Illegal field modifiers: 0x%X",
										 f->flags);
				return false;
			}
		}
	}
#endif /* ENABLE_VERIFIER */
		
	f->type = jtype = descriptor_to_basic_type(f->descriptor); /* data type */
	f->offset = 0;                           /* offset from start of object */
	f->class = c;
	f->xta = NULL;
	
	switch (f->type) {
	case TYPE_INT:     f->value.i = 0; break;
	case TYPE_FLOAT:   f->value.f = 0.0; break;
	case TYPE_DOUBLE:  f->value.d = 0.0; break;
	case TYPE_ADDRESS: f->value.a = NULL; break;
	case TYPE_LONG:
#if U8_AVAILABLE
		f->value.l = 0; break;
#else
		f->value.l.low = 0; f->value.l.high = 0; break;
#endif
	}

	/* read attributes */
	if (!suck_check_classbuffer_size(cb, 2))
		return false;

	attrnum = suck_u2(cb);
	for (i = 0; i < attrnum; i++) {
		if (!suck_check_classbuffer_size(cb, 2))
			return false;

		if (!(u = class_getconstant(c, suck_u2(cb), CONSTANT_Utf8)))
			return false;

		if (u == utf_ConstantValue) {
			if (!suck_check_classbuffer_size(cb, 4 + 2))
				return false;

			/* check attribute length */
			if (suck_u4(cb) != 2) {
				*exceptionptr =
					new_classformaterror(c, "Wrong size for VALUE attribute");
				return false;
			}
			
			/* constant value attribute */
			if (pindex != field_load_NOVALUE) {
				*exceptionptr =
					new_classformaterror(c,
										 "Multiple ConstantValue attributes");
				return false;
			}
			
			/* index of value in constantpool */		
			pindex = suck_u2(cb);
		
			/* initialize field with value from constantpool */		
			switch (jtype) {
			case TYPE_INT: {
				constant_integer *ci; 

				if (!(ci = class_getconstant(c, pindex, CONSTANT_Integer)))
					return false;

				f->value.i = ci->value;
			}
			break;
					
			case TYPE_LONG: {
				constant_long *cl; 

				if (!(cl = class_getconstant(c, pindex, CONSTANT_Long)))
					return false;

				f->value.l = cl->value;
			}
			break;

			case TYPE_FLOAT: {
				constant_float *cf;

				if (!(cf = class_getconstant(c, pindex, CONSTANT_Float)))
					return false;

				f->value.f = cf->value;
			}
			break;
											
			case TYPE_DOUBLE: {
				constant_double *cd;

				if (!(cd = class_getconstant(c, pindex, CONSTANT_Double)))
					return false;

				f->value.d = cd->value;
			}
			break;
						
			case TYPE_ADDRESS:
				if (!(u = class_getconstant(c, pindex, CONSTANT_String)))
					return false;

				/* create javastring from compressed utf8-string */
				f->value.a = literalstring_new(u);
				break;
	
			default: 
				log_text("Invalid Constant - Type");
			}

		} else {
			/* unknown attribute */
			if (!skipattributebody(cb))
				return false;
		}
	}

	/* everything was ok */

	return true;
}


/* load_method *****************************************************************

   Loads a method from the class file and fills an existing
   'methodinfo' structure. For native methods, the function pointer
   field is set to the real function pointer, for JavaVM methods a
   pointer to the compiler is used preliminarily.
	
*******************************************************************************/

static bool load_method(classbuffer *cb, methodinfo *m, descriptor_pool *descpool)
{
	classinfo *c;
	int argcount;
	s4 i, j;
	u4 attrnum;
	u4 codeattrnum;
	utf *u;

	c = cb->class;

#if defined(USE_THREADS) && defined(NATIVE_THREADS)
	initObjectLock(&m->header);
#endif

#ifdef STATISTICS
	if (opt_stat)
		count_all_methods++;
#endif

	m->thrownexceptionscount = 0;
	m->linenumbercount = 0;
	m->linenumbers = 0;
	m->class = c;
	m->nativelyoverloaded = false;
	
	if (!suck_check_classbuffer_size(cb, 2 + 2 + 2))
		return false;

	m->flags = suck_u2(cb);

	if (!(u = class_getconstant(c, suck_u2(cb), CONSTANT_Utf8)))
		return false;

	m->name = u;

	if (!(u = class_getconstant(c, suck_u2(cb), CONSTANT_Utf8)))
		return false;

	m->descriptor = u;
	m->parseddesc = NULL;

	if (!descriptor_pool_add(descpool, u, &argcount))
		return false;

#ifdef ENABLE_VERIFIER
	if (opt_verify) {
		if (!is_valid_name_utf(m->name)) {
			*exceptionptr = new_classformaterror(c,"Method with invalid name");
			return false;
		}

		if (m->name->text[0] == '<' &&
			m->name != utf_init && m->name != utf_clinit) {
			*exceptionptr = new_classformaterror(c,"Method with invalid special name");
			return false;
		}
	}
#endif /* ENABLE_VERIFIER */
	
	if (!(m->flags & ACC_STATIC))
		argcount++; /* count the 'this' argument */

#ifdef ENABLE_VERIFIER
	if (opt_verify) {
		if (argcount > 255) {
			*exceptionptr =
				new_classformaterror(c, "Too many arguments in signature");
			return false;
		}

		/* check flag consistency */
		if (m->name != utf_clinit) {
			i = (m->flags & (ACC_PUBLIC | ACC_PRIVATE | ACC_PROTECTED));

			if (i != 0 && i != ACC_PUBLIC && i != ACC_PRIVATE && i != ACC_PROTECTED) {
				*exceptionptr =
					new_classformaterror(c,
										 "Illegal method modifiers: 0x%X",
										 m->flags);
				return false;
			}

			if (m->flags & ACC_ABSTRACT) {
				if ((m->flags & (ACC_FINAL | ACC_NATIVE | ACC_PRIVATE |
								 ACC_STATIC | ACC_STRICT | ACC_SYNCHRONIZED))) {
					*exceptionptr =
						new_classformaterror(c,
											 "Illegal method modifiers: 0x%X",
											 m->flags);
					return false;
				}
			}

			if (c->flags & ACC_INTERFACE) {
				if ((m->flags & (ACC_ABSTRACT | ACC_PUBLIC)) != (ACC_ABSTRACT | ACC_PUBLIC)) {
					*exceptionptr =
						new_classformaterror(c,
											 "Illegal method modifiers: 0x%X",
											 m->flags);
					return false;
				}
			}

			if (m->name == utf_init) {
				if (m->flags & (ACC_STATIC | ACC_FINAL | ACC_SYNCHRONIZED |
								ACC_NATIVE | ACC_ABSTRACT)) {
					*exceptionptr = new_classformaterror(c,
							"Instance initialization method has invalid flags set");
					return false;
				}
			}
		}
	}
#endif /* ENABLE_VERIFIER */
		
	m->jcode = NULL;
	m->basicblockcount = 0;
	m->basicblocks = NULL;
	m->basicblockindex = NULL;
	m->instructioncount = 0;
	m->instructions = NULL;
	m->stackcount = 0;
	m->stack = NULL;
	m->exceptiontable = NULL;
	m->stubroutine = NULL;
	m->mcode = NULL;
	m->entrypoint = NULL;
	m->methodUsed = NOTUSED;    
	m->monoPoly = MONO;    
	m->subRedefs = 0;
	m->subRedefsUsed = 0;

	m->xta = NULL;

	if (!suck_check_classbuffer_size(cb, 2))
		return false;
	
	attrnum = suck_u2(cb);
	for (i = 0; i < attrnum; i++) {
		utf *aname;

		if (!suck_check_classbuffer_size(cb, 2))
			return false;

		if (!(aname = class_getconstant(c, suck_u2(cb), CONSTANT_Utf8)))
			return false;

		if (aname == utf_Code) {
			if (m->flags & (ACC_ABSTRACT | ACC_NATIVE)) {
					*exceptionptr =
						new_classformaterror(c,
											 "Code attribute in native or abstract methods");

					return false;
			}
			
			if (m->jcode) {
				*exceptionptr =
					new_classformaterror(c, "Multiple Code attributes");

				return false;
			}

			if (!suck_check_classbuffer_size(cb, 4 + 2 + 2))
				return false;

			suck_u4(cb);
			m->maxstack = suck_u2(cb);
			m->maxlocals = suck_u2(cb);

			if (m->maxlocals < argcount) {
				*exceptionptr =
					new_classformaterror(c, "Arguments can't fit into locals");

				return false;
			}
			
			if (!suck_check_classbuffer_size(cb, 4))
				return false;

			m->jcodelength = suck_u4(cb);

			if (m->jcodelength == 0) {
				*exceptionptr =
					new_classformaterror(c, "Code of a method has length 0");

				return false;
			}
			
			if (m->jcodelength > 65535) {
				*exceptionptr =
					new_classformaterror(c,
										 "Code of a method longer than 65535 bytes");

				return false;
			}

			if (!suck_check_classbuffer_size(cb, m->jcodelength))
				return false;

			m->jcode = MNEW(u1, m->jcodelength);
			suck_nbytes(m->jcode, cb, m->jcodelength);

			if (!suck_check_classbuffer_size(cb, 2))
				return false;

			m->exceptiontablelength = suck_u2(cb);
			if (!suck_check_classbuffer_size(cb, (2 + 2 + 2 + 2) * m->exceptiontablelength))
				return false;

			m->exceptiontable = MNEW(exceptiontable, m->exceptiontablelength);

#if defined(STATISTICS)
			if (opt_stat) {
				count_vmcode_len += m->jcodelength + 18;
				count_extable_len +=
					m->exceptiontablelength * sizeof(exceptiontable);
			}
#endif

			for (j = 0; j < m->exceptiontablelength; j++) {
				u4 idx;
				m->exceptiontable[j].startpc = suck_u2(cb);
				m->exceptiontable[j].endpc = suck_u2(cb);
				m->exceptiontable[j].handlerpc = suck_u2(cb);

				idx = suck_u2(cb);
				if (!idx) {
					m->exceptiontable[j].catchtype.any = NULL;

				} else {
					/* the classref is created later */
					if (!(m->exceptiontable[j].catchtype.any =
						  (utf*)class_getconstant(c, idx, CONSTANT_Class)))
						return false;
				}
			}

			if (!suck_check_classbuffer_size(cb, 2))
				return false;

			codeattrnum = suck_u2(cb);

			for (; codeattrnum > 0; codeattrnum--) {
				utf *caname;

				if (!suck_check_classbuffer_size(cb, 2))
					return false;

				if (!(caname = class_getconstant(c, suck_u2(cb), CONSTANT_Utf8)))
					return false;

				if (caname == utf_LineNumberTable) {
					u2 lncid;

					if (!suck_check_classbuffer_size(cb, 4 + 2))
						return false;

					suck_u4(cb);
					m->linenumbercount = suck_u2(cb);

					if (!suck_check_classbuffer_size(cb,
												(2 + 2) * m->linenumbercount))
						return false;

					m->linenumbers = MNEW(lineinfo, m->linenumbercount);
					
					for (lncid = 0; lncid < m->linenumbercount; lncid++) {
						m->linenumbers[lncid].start_pc = suck_u2(cb);
						m->linenumbers[lncid].line_number = suck_u2(cb);
					}
					codeattrnum--;

					if (!skipattributes(cb, codeattrnum))
						return false;
					
					break;

				} else {
					if (!skipattributebody(cb))
						return false;
				}
			}

		} else if (aname == utf_Exceptions) {
			s4 j;

			if (m->thrownexceptions) {
				*exceptionptr =
					new_classformaterror(c, "Multiple Exceptions attributes");
				return false;
			}

			if (!suck_check_classbuffer_size(cb, 4 + 2))
				return false;

			suck_u4(cb); /* length */
			m->thrownexceptionscount = suck_u2(cb);

			if (!suck_check_classbuffer_size(cb, 2 * m->thrownexceptionscount))
				return false;

			m->thrownexceptions = MNEW(classref_or_classinfo, m->thrownexceptionscount);

			for (j = 0; j < m->thrownexceptionscount; j++) {
				/* the classref is created later */
				if (!((m->thrownexceptions)[j].any =
					  (utf*) class_getconstant(c, suck_u2(cb), CONSTANT_Class)))
					return false;
			}
				
		} else {
			if (!skipattributebody(cb))
				return false;
		}
	}

	if (!m->jcode && !(m->flags & (ACC_ABSTRACT | ACC_NATIVE))) {
		*exceptionptr = new_classformaterror(c, "Missing Code attribute");

		return false;
	}

	/* everything was ok */

	return true;
}


/* load_attribute **************************************************************

   Read attributes from classfile.
	
*******************************************************************************/

static bool load_attributes(classbuffer *cb, u4 num)
{
	classinfo *c;
	utf       *aname;
	u4 i, j;

	c = cb->class;

	for (i = 0; i < num; i++) {
		/* retrieve attribute name */
		if (!suck_check_classbuffer_size(cb, 2))
			return false;

		if (!(aname = class_getconstant(c, suck_u2(cb), CONSTANT_Utf8)))
			return false;

		if (aname == utf_InnerClasses) {
			/* innerclasses attribute */
			if (c->innerclass) {
				*exceptionptr =
					new_classformaterror(c, "Multiple InnerClasses attributes");
				return false;
			}
				
			if (!suck_check_classbuffer_size(cb, 4 + 2))
				return false;

			/* skip attribute length */
			suck_u4(cb);

			/* number of records */
			c->innerclasscount = suck_u2(cb);

			if (!suck_check_classbuffer_size(cb, (2 + 2 + 2 + 2) * c->innerclasscount))
				return false;

			/* allocate memory for innerclass structure */
			c->innerclass = MNEW(innerclassinfo, c->innerclasscount);

			for (j = 0; j < c->innerclasscount; j++) {
				/* The innerclass structure contains a class with an encoded
				   name, its defining scope, its simple name and a bitmask of
				   the access flags. If an inner class is not a member, its
				   outer_class is NULL, if a class is anonymous, its name is
				   NULL. */
   								
				innerclassinfo *info = c->innerclass + j;

				info->inner_class.ref =
					innerclass_getconstant(c, suck_u2(cb), CONSTANT_Class);
				info->outer_class.ref =
					innerclass_getconstant(c, suck_u2(cb), CONSTANT_Class);
				info->name =
					innerclass_getconstant(c, suck_u2(cb), CONSTANT_Utf8);
				info->flags = suck_u2(cb);
			}

		} else if (aname == utf_SourceFile) {
			if (!suck_check_classbuffer_size(cb, 4 + 2))
				return false;

			if (suck_u4(cb) != 2) {
				*exceptionptr =
					new_classformaterror(c, "Wrong size for VALUE attribute");
				return false;
			}

			if (c->sourcefile) {
				*exceptionptr =
					new_classformaterror(c, "Multiple SourceFile attributes");
				return false;
			}

			if (!(c->sourcefile = class_getconstant(c, suck_u2(cb), CONSTANT_Utf8)))
				return false;

		} else {
			/* unknown attribute */
			if (!skipattributebody(cb))
				return false;
		}
	}

	return true;
}


/* load_class_from_sysloader ***************************************************

   Load the class with the given name using the system class loader

   IN:
       name.............the classname

   RETURN VALUE:
       the loaded class, or
	   NULL if an exception has been thrown

*******************************************************************************/

classinfo *load_class_from_sysloader(utf *name)
{
	methodinfo        *m;
	java_objectheader *cl;
	classinfo         *c;

	LOADER_ASSERT(class_java_lang_Object);
	LOADER_ASSERT(class_java_lang_ClassLoader);
	LOADER_ASSERT(class_java_lang_ClassLoader->state & CLASS_LINKED);
	
	m = class_resolveclassmethod(class_java_lang_ClassLoader,
								 utf_getSystemClassLoader,
								 utf_void__java_lang_ClassLoader,
								 class_java_lang_Object,
								 false);

	if (!m)
		return false;

	cl = (java_objectheader *) asm_calljavafunction(m, NULL, NULL, NULL, NULL);

	if (!cl)
		return false;

	c = load_class_from_classloader(name, cl);

	return c;
}


/* load_class_from_classloader *************************************************

   Load the class with the given name using the given user-defined class loader.

   IN:
       name.............the classname
	   cl...............user-defined class loader
	   
   RETURN VALUE:
       the loaded class, or
	   NULL if an exception has been thrown

*******************************************************************************/

classinfo *load_class_from_classloader(utf *name, java_objectheader *cl)
{
	classinfo *c;
	classinfo *tmpc;

	LOADER_ASSERT(name);

	/* lookup if this class has already been loaded */

	c = classcache_lookup(cl, name);

	if (c)
		return c;

	/* if other class loader than bootstrap, call it */

	if (cl) {
		methodinfo *lc;
		char       *text;
		s4          namelen;

		text = name->text;
		namelen = name->blength;

		/* handle array classes */
		if (text[0] == '[') {
			classinfo *comp;
			utf       *u;

			switch (text[1]) {
			case 'L':
				/* check for cases like `[L;' or `[L[I;' or `[Ljava.lang.Object' */
				if (namelen < 4 || text[2] == '[' || text[namelen - 1] != ';') {
					*exceptionptr = new_noclassdeffounderror(name);
					return false;
				}

				u = utf_new(text + 2, namelen - 3);

				if (!(comp = load_class_from_classloader(u, cl)))
					return false;

				/* create the array class */

				c = class_array_of(comp, false);

				tmpc = classcache_store(cl, c, true);

				if (tmpc == NULL) {
					/* exception, free the loaded class */
					c->state &= ~CLASS_LOADING;
					class_free(c);
				}

				return tmpc;

			case '[':
				/* load the component class */

				u = utf_new(text + 1, namelen - 1);

				if (!(comp = load_class_from_classloader(u, cl)))
					return false;

				/* create the array class */

				c = class_array_of(comp, false);

				tmpc = classcache_store(cl, c, true);

				if (tmpc == NULL) {
					/* exception, free the loaded class */
					c->state &= ~CLASS_LOADING;
					class_free(c);
				}

				return tmpc;

			default:
				/* primitive array classes are loaded by the bootstrap loader */

				c = load_class_bootstrap(name);

				return c;
			}
		}
		
		LOADER_ASSERT(class_java_lang_Object);

		lc = class_resolveclassmethod(cl->vftbl->class,
									  utf_loadClass,
									  utf_java_lang_String__java_lang_Class,
									  class_java_lang_Object,
									  true);

		if (!lc)
			return false; /* exception */

		c = (classinfo *) asm_calljavafunction(lc,
											   cl,
											   javastring_new_slash_to_dot(name),
											   NULL, NULL);

		if (c) {
			/* Store this class in the loaded class cache. If another
			   class with the same (initloader,name) pair has been
			   stored earlier it will be returned by classcache_store
			   In this case classcache_store may not free the class
			   because it has already been exposed to Java code which
			   may have kept references to that class. */

		    tmpc = classcache_store(cl, c, false);

			if (tmpc == NULL) {
				/* exception, free the loaded class */
				c->state &= ~CLASS_LOADING;
				class_free(c);
			}

			c = tmpc;

		} else {
			/* loadClass has thrown an exception */
			/* we must convert ClassNotFoundException into NoClassDefFoundException */
			/* XXX maybe we should have a flag that avoids this conversion */
			/* for calling load_class_from_classloader from Class.forName  */
			/* Currently we do a double conversion in these cases          */
			classnotfoundexception_to_noclassdeffounderror();
		}

		/* SUN compatible -verbose:class output */

		if (opt_verboseclass && (c != NULL) && (c->classloader == cl)) {
			printf("[Loaded ");
			utf_display_classname(name);
			printf("]\n");
		}

		return c;
	} 

	c = load_class_bootstrap(name);

	return c;
}


/* load_class_bootstrap ********************************************************
	
   Load the class with the given name using the bootstrap class loader.

   IN:
       name.............the classname

   RETURN VALUE:
       loaded classinfo, or
	   NULL if an exception has been thrown

   SYNCHRONIZATION:
       load_class_bootstrap is synchronized. It can be treated as an
	   atomic operation.

*******************************************************************************/

classinfo *load_class_bootstrap(utf *name)
{
	classbuffer *cb;
	classinfo   *c;
	classinfo   *r;

	/* for debugging */

	LOADER_ASSERT(name);

	/* lookup if this class has already been loaded */

	if ((r = classcache_lookup(NULL, name)))
		return r;

	/* create the classinfo */

	c = class_create_classinfo(name);
	
	/* handle array classes */

	if (name->text[0] == '[') {
		c = load_newly_created_array(c, NULL);
		if (c == NULL)
			return NULL;
		LOADER_ASSERT(c->state & CLASS_LOADED);
		return c;
	}

#if defined(STATISTICS)
	/* measure time */

	if (getcompilingtime)
		compilingtime_stop();

	if (getloadingtime)
		loadingtime_start();
#endif

	/* load classdata, throw exception on error */

	if ((cb = suck_start(c)) == NULL) {
		/* this normally means, the classpath was not set properly */

		if (name == utf_java_lang_Object)
			throw_cacao_exception_exit(string_java_lang_NoClassDefFoundError,
									   "java/lang/Object");

		*exceptionptr =	new_noclassdeffounderror(name);

		return NULL;
	}
	
	/* load the class from the buffer */

	r = load_class_from_classbuffer(cb);

	if (!r) {
		/* the class could not be loaded, free the classinfo struct */

		class_free(c);

	} else {
		/* Store this class in the loaded class cache this step also
		checks the loading constraints. If the class has been loaded
		before, the earlier loaded class is returned. */

	   	classinfo *res = classcache_store(NULL, c, true);

		if (!res) {
			/* exception */
			class_free(c);
		}

		r = res;
	}

	/* SUN compatible -verbose:class output */

	if (opt_verboseclass && r) {
		printf("[Loaded ");
		utf_display_classname(name);
		printf(" from %s]\n", cb->path);
	}

	/* free memory */

	suck_stop(cb);

#if defined(STATISTICS)
	/* measure time */

	if (getloadingtime)
		loadingtime_stop();

	if (getcompilingtime)
		compilingtime_start();
#endif

	return r;
}


/* load_class_from_classbuffer *************************************************
	
   Loads everything interesting about a class from the class file. The
   'classinfo' structure must have been allocated previously.

   The super class and the interfaces implemented by this class need
   not be loaded. The link is set later by the function 'class_link'.

   The loaded class is removed from the list 'unloadedclasses' and
   added to the list 'unlinkedclasses'.
	
   SYNCHRONIZATION:
       This function is NOT synchronized!
   
*******************************************************************************/

classinfo *load_class_from_classbuffer(classbuffer *cb)
{
	classinfo *c;
	utf *name;
	utf *supername;
	u4 i,j;
	u4 ma, mi;
	s4 dumpsize;
	descriptor_pool *descpool;
#if defined(STATISTICS)
	u4 classrefsize;
	u4 descsize;
#endif

	/* get the classbuffer's class */

	c = cb->class;

	/* the class is already loaded */

	if (c->state & CLASS_LOADED)
		return c;

#if defined(STATISTICS)
	if (opt_stat)
		count_class_loads++;
#endif

	/* output for debugging purposes */

	if (loadverbose)
		log_message_class("Loading class: ", c);
	
	/* mark start of dump memory area */

	dumpsize = dump_size();

	/* class is currently loading */

	c->state |= CLASS_LOADING;

	if (!suck_check_classbuffer_size(cb, 4 + 2 + 2))
		goto return_exception;

	/* check signature */

	if (suck_u4(cb) != MAGIC) {
		*exceptionptr = new_classformaterror(c, "Bad magic number");

		goto return_exception;
	}

	/* check version */

	mi = suck_u2(cb);
	ma = suck_u2(cb);

	if (!(ma < MAJOR_VERSION || (ma == MAJOR_VERSION && mi <= MINOR_VERSION))) {
		*exceptionptr =
			new_unsupportedclassversionerror(c,
											 "Unsupported major.minor version %d.%d",
											 ma, mi);

		goto return_exception;
	}

	/* create a new descriptor pool */

	descpool = descriptor_pool_new(c);

	/* load the constant pool */

	if (!load_constantpool(cb, descpool))
		goto return_exception;

	c->classUsed = NOTUSED; /* not used initially CO-RT */
	c->impldBy = NULL;

	/* ACC flags */

	if (!suck_check_classbuffer_size(cb, 2))
		goto return_exception;

	c->flags = suck_u2(cb);

	/* check ACC flags consistency */

	if (c->flags & ACC_INTERFACE) {
		if (!(c->flags & ACC_ABSTRACT)) {
			/* We work around this because interfaces in JDK 1.1 are
			 * not declared abstract. */

			c->flags |= ACC_ABSTRACT;
		}

		if (c->flags & ACC_FINAL) {
			*exceptionptr =
				new_classformaterror(c,
									 "Illegal class modifiers: 0x%X", c->flags);

			goto return_exception;
		}

		if (c->flags & ACC_SUPER) {
			c->flags &= ~ACC_SUPER; /* kjc seems to set this on interfaces */
		}
	}

	if ((c->flags & (ACC_ABSTRACT | ACC_FINAL)) == (ACC_ABSTRACT | ACC_FINAL)) {
		*exceptionptr =
			new_classformaterror(c, "Illegal class modifiers: 0x%X", c->flags);

		goto return_exception;
	}

	if (!suck_check_classbuffer_size(cb, 2 + 2))
		goto return_exception;

	/* this class */

	i = suck_u2(cb);
	if (!(name = (utf *) class_getconstant(c, i, CONSTANT_Class)))
		goto return_exception;

	if (c->name == utf_not_named_yet) {
		/* we finally have a name for this class */
		c->name = name;
		class_set_packagename(c);

	} else if (name != c->name) {
		char *msg;
		s4    msglen;

		msglen = utf_strlen(c->name) + strlen(" (wrong name: ") +
			utf_strlen(name) + strlen(")") + strlen("0");

		msg = MNEW(char, msglen);

		utf_sprint(msg, c->name);
		strcat(msg, " (wrong name: ");
		utf_strcat(msg, name);
		strcat(msg, ")");

		*exceptionptr =
			new_exception_message(string_java_lang_NoClassDefFoundError, msg);

		MFREE(msg, char, msglen);

		goto return_exception;
	}
	
	/* retrieve superclass */

	c->super.any = NULL;
	if ((i = suck_u2(cb))) {
		if (!(supername = (utf *) class_getconstant(c, i, CONSTANT_Class)))
			goto return_exception;

		/* java.lang.Object may not have a super class. */

		if (c->name == utf_java_lang_Object) {
			*exceptionptr =
				new_exception_message(string_java_lang_ClassFormatError,
									  "java.lang.Object with superclass");

			goto return_exception;
		}

		/* Interfaces must have java.lang.Object as super class. */

		if ((c->flags & ACC_INTERFACE) &&
			supername != utf_java_lang_Object) {
			*exceptionptr =
				new_exception_message(string_java_lang_ClassFormatError,
									  "Interfaces must have java.lang.Object as superclass");

			goto return_exception;
		}

	} else {
		supername = NULL;

		/* This is only allowed for java.lang.Object. */

		if (c->name != utf_java_lang_Object) {
			*exceptionptr = new_classformaterror(c, "Bad superclass index");

			goto return_exception;
		}
	}
			 
	/* retrieve interfaces */

	if (!suck_check_classbuffer_size(cb, 2))
		goto return_exception;

	c->interfacescount = suck_u2(cb);

	if (!suck_check_classbuffer_size(cb, 2 * c->interfacescount))
		goto return_exception;

	c->interfaces = MNEW(classref_or_classinfo, c->interfacescount);
	for (i = 0; i < c->interfacescount; i++) {
		/* the classrefs are created later */
		if (!(c->interfaces[i].any = (utf *) class_getconstant(c, suck_u2(cb), CONSTANT_Class)))
			goto return_exception;
	}

	/* load fields */
	if (!suck_check_classbuffer_size(cb, 2))
		goto return_exception;

	c->fieldscount = suck_u2(cb);
	c->fields = GCNEW_UNCOLLECTABLE(fieldinfo, c->fieldscount);
/*  	c->fields = MNEW(fieldinfo, c->fieldscount); */
	for (i = 0; i < c->fieldscount; i++) {
		if (!load_field(cb, &(c->fields[i]),descpool))
			goto return_exception;
	}

	/* load methods */
	if (!suck_check_classbuffer_size(cb, 2))
		goto return_exception;

	c->methodscount = suck_u2(cb);
/*  	c->methods = GCNEW(methodinfo, c->methodscount); */
	c->methods = MNEW(methodinfo, c->methodscount);
	for (i = 0; i < c->methodscount; i++) {
		if (!load_method(cb, &(c->methods[i]),descpool))
			goto return_exception;
	}

	/* create the class reference table */

	c->classrefs =
		descriptor_pool_create_classrefs(descpool, &(c->classrefcount));

	/* allocate space for the parsed descriptors */

	descriptor_pool_alloc_parsed_descriptors(descpool);
	c->parseddescs =
		descriptor_pool_get_parsed_descriptors(descpool, &(c->parseddescsize));

#if defined(STATISTICS)
	if (opt_stat) {
		descriptor_pool_get_sizes(descpool, &classrefsize, &descsize);
		count_classref_len += classrefsize;
		count_parsed_desc_len += descsize;
	}
#endif

	/* put the classrefs in the constant pool */
	for (i = 0; i < c->cpcount; i++) {
		if (c->cptags[i] == CONSTANT_Class) {
			utf *name = (utf *) c->cpinfos[i];
			c->cpinfos[i] = descriptor_pool_lookup_classref(descpool, name);
		}
	}

	/* set the super class reference */

	if (supername) {
		c->super.ref = descriptor_pool_lookup_classref(descpool, supername);
		if (!c->super.ref)
			goto return_exception;
	}

	/* set the super interfaces references */

	for (i = 0; i < c->interfacescount; i++) {
		c->interfaces[i].ref =
			descriptor_pool_lookup_classref(descpool,
											(utf *) c->interfaces[i].any);
		if (!c->interfaces[i].ref)
			goto return_exception;
	}

	/* parse field descriptors */

	for (i = 0; i < c->fieldscount; i++) {
		c->fields[i].parseddesc =
			descriptor_pool_parse_field_descriptor(descpool,
												   c->fields[i].descriptor);
		if (!c->fields[i].parseddesc)
			goto return_exception;
	}

	/* parse method descriptors */

	for (i = 0; i < c->methodscount; i++) {
		methodinfo *m = &c->methods[i];
		m->parseddesc =
			descriptor_pool_parse_method_descriptor(descpool, m->descriptor,
													m->flags, class_get_self_classref(m->class));
		if (!m->parseddesc)
			goto return_exception;

		for (j = 0; j < m->exceptiontablelength; j++) {
			if (!m->exceptiontable[j].catchtype.any)
				continue;
			if ((m->exceptiontable[j].catchtype.ref =
				 descriptor_pool_lookup_classref(descpool,
						(utf *) m->exceptiontable[j].catchtype.any)) == NULL)
				goto return_exception;
		}

		for (j = 0; j < m->thrownexceptionscount; j++) {
			if (!m->thrownexceptions[j].any)
				continue;
			if ((m->thrownexceptions[j].ref = descriptor_pool_lookup_classref(descpool,
						(utf *) m->thrownexceptions[j].any)) == NULL)
				goto return_exception;
		}
	}

	/* parse the loaded descriptors */

	for (i = 0; i < c->cpcount; i++) {
		constant_FMIref *fmi;
		s4               index;
		
		switch (c->cptags[i]) {
		case CONSTANT_Fieldref:
			fmi = (constant_FMIref *) c->cpinfos[i];
			fmi->parseddesc.fd =
				descriptor_pool_parse_field_descriptor(descpool,
													   fmi->descriptor);
			if (!fmi->parseddesc.fd)
				goto return_exception;
			index = (int) (size_t) fmi->classref;
			fmi->classref =
				(constant_classref *) class_getconstant(c, index,
														CONSTANT_Class);
			if (!fmi->classref)
				goto return_exception;
			break;
		case CONSTANT_Methodref:
		case CONSTANT_InterfaceMethodref:
			fmi = (constant_FMIref *) c->cpinfos[i];
			index = (int) (size_t) fmi->classref;
			fmi->classref =
				(constant_classref *) class_getconstant(c, index,
														CONSTANT_Class);
			if (!fmi->classref)
				goto return_exception;
			fmi->parseddesc.md =
				descriptor_pool_parse_method_descriptor(descpool,
														fmi->descriptor,
														ACC_UNDEF,
														fmi->classref);
			if (!fmi->parseddesc.md)
				goto return_exception;
			break;
		}
	}

#ifdef ENABLE_VERIFIER
	/* Check if all fields and methods can be uniquely
	 * identified by (name,descriptor). */

	if (opt_verify) {
		/* We use a hash table here to avoid making the
		 * average case quadratic in # of methods, fields.
		 */
		static int shift = 0;
		u2 *hashtab;
		u2 *next; /* for chaining colliding hash entries */
		size_t len;
		size_t hashlen;
		u2 index;
		u2 old;

		/* Allocate hashtable */
		len = c->methodscount;
		if (len < c->fieldscount) len = c->fieldscount;
		hashlen = 5 * len;
		hashtab = MNEW(u2,(hashlen + len));
		next = hashtab + hashlen;

		/* Determine bitshift (to get good hash values) */
		if (!shift) {
			len = sizeof(utf);
			while (len) {
				len >>= 1;
				shift++;
			}
		}

		/* Check fields */
		memset(hashtab, 0, sizeof(u2) * (hashlen + len));

		for (i = 0; i < c->fieldscount; ++i) {
			fieldinfo *fi = c->fields + i;

			/* It's ok if we lose bits here */
			index = ((((size_t) fi->name) +
					  ((size_t) fi->descriptor)) >> shift) % hashlen;

			if ((old = hashtab[index])) {
				old--;
				next[i] = old;
				do {
					if (c->fields[old].name == fi->name &&
						c->fields[old].descriptor == fi->descriptor) {
						*exceptionptr =
							new_classformaterror(c,
												 "Repetitive field name/signature");

						goto return_exception;
					}
				} while ((old = next[old]));
			}
			hashtab[index] = i + 1;
		}
		
		/* Check methods */
		memset(hashtab, 0, sizeof(u2) * (hashlen + hashlen/5));

		for (i = 0; i < c->methodscount; ++i) {
			methodinfo *mi = c->methods + i;

			/* It's ok if we lose bits here */
			index = ((((size_t) mi->name) +
					  ((size_t) mi->descriptor)) >> shift) % hashlen;

			/*{ JOWENN
				int dbg;
				for (dbg=0;dbg<hashlen+hashlen/5;++dbg){
					printf("Hash[%d]:%d\n",dbg,hashtab[dbg]);
				}
			}*/

			if ((old = hashtab[index])) {
				old--;
				next[i] = old;
				do {
					if (c->methods[old].name == mi->name &&
						c->methods[old].descriptor == mi->descriptor) {
						*exceptionptr =
							new_classformaterror(c,
												 "Repetitive method name/signature");

						goto return_exception;
					}
				} while ((old = next[old]));
			}
			hashtab[index] = i + 1;
		}
		
		MFREE(hashtab, u2, (hashlen + len));
	}
#endif /* ENABLE_VERIFIER */

#if defined(STATISTICS)
	if (opt_stat) {
		count_class_infos += sizeof(classinfo*) * c->interfacescount;
		count_class_infos += sizeof(fieldinfo) * c->fieldscount;
		count_class_infos += sizeof(methodinfo) * c->methodscount;
	}
#endif

	/* load attribute structures */

	if (!suck_check_classbuffer_size(cb, 2))
		goto return_exception;

	if (!load_attributes(cb, suck_u2(cb)))
		goto return_exception;

#if 0
	/* Pre java 1.5 version don't check this. This implementation is like
	   java 1.5 do it: for class file version 45.3 we don't check it, older
	   versions are checked.
	 */
	if ((ma == 45 && mi > 3) || ma > 45) {
		/* check if all data has been read */
		s4 classdata_left = ((cb->data + cb->size) - cb->pos);

		if (classdata_left > 0) {
			*exceptionptr =
				new_classformaterror(c, "Extra bytes at the end of class file");
			goto return_exception;
		}
	}
#endif

	/* release dump area */

	dump_release(dumpsize);

	/* revert loading state and class is loaded */

	c->state = (c->state & ~CLASS_LOADING) | CLASS_LOADED;

	if (loadverbose)
		log_message_class("Loading done class: ", c);

	return c;

return_exception:
	/* release dump area */

	dump_release(dumpsize);

	/* an exception has been thrown */

	return NULL;
}


/* load_newly_created_array ****************************************************

   Load a newly created array class.

	RETURN VALUE:
	    c....................the array class C has been loaded
		other classinfo......the array class was found in the class cache, 
		                     C has been freed
	    NULL.................an exception has been thrown

	Note:
		This is an internal function. Do not use it unless you know exactly
		what you are doing!

		Use one of the load_class_... functions for general array class loading.

*******************************************************************************/

classinfo *load_newly_created_array(classinfo *c, java_objectheader *loader)
{
	classinfo         *comp = NULL;
	methodinfo        *clone;
	methoddesc        *clonedesc;
	constant_classref *classrefs;
	char              *text;
	s4                 namelen;
	utf               *u;

	text = c->name->text;
	namelen = c->name->blength;

	/* Check array class name */

	if (namelen < 2 || text[0] != '[') {
		*exceptionptr = new_noclassdeffounderror(c->name);
		return NULL;
	}

	/* Check the element type */

	switch (text[1]) {
	case '[':
		/* c is an array of arrays. We have to create the component class. */

		u = utf_new(text + 1, namelen - 1);
		if (!(comp = load_class_from_classloader(u, loader)))
			return NULL;

		LOADER_ASSERT(comp->state & CLASS_LOADED);

		if (opt_eager)
			if (!link_class(c))
				return NULL;

		/* the array's flags are that of the component class */
		c->flags = (comp->flags & ~ACC_INTERFACE) | ACC_FINAL | ACC_ABSTRACT;
		c->classloader = comp->classloader;
		break;

	case 'L':
		/* c is an array of objects. */

		/* check for cases like `[L;' or `[L[I;' or `[Ljava.lang.Object' */
		if (namelen < 4 || text[2] == '[' || text[namelen - 1] != ';') {
			*exceptionptr = new_noclassdeffounderror(c->name);
			return NULL;
		}

		u = utf_new(text + 2, namelen - 3);

		if (!(comp = load_class_from_classloader(u, loader)))
			return NULL;

		LOADER_ASSERT(comp->state & CLASS_LOADED);

		if (opt_eager)
			if (!link_class(c))
				return NULL;

		/* the array's flags are that of the component class */
		c->flags = (comp->flags & ~ACC_INTERFACE) | ACC_FINAL | ACC_ABSTRACT;
		c->classloader = comp->classloader;
		break;

	default:
		/* c is an array of a primitive type */

		/* check for cases like `[II' */
		if (namelen > 2) {
			*exceptionptr = new_noclassdeffounderror(c->name);
			return NULL;
		}

		/* the accessibility of the array class is public (VM Spec 5.3.3) */
		c->flags = ACC_PUBLIC | ACC_FINAL | ACC_ABSTRACT;
		c->classloader = NULL;
	}

	LOADER_ASSERT(class_java_lang_Object);
	LOADER_ASSERT(class_java_lang_Cloneable);
	LOADER_ASSERT(class_java_io_Serializable);

	/* setup the array class */

	c->super.cls = class_java_lang_Object;

    c->interfacescount = 2;
    c->interfaces = MNEW(classref_or_classinfo, 2);

	if (opt_eager) {
		classinfo *tc;

		tc = class_java_lang_Cloneable;
		LOADER_ASSERT(tc->state & CLASS_LOADED);
		list_addfirst(&unlinkedclasses, tc);
		c->interfaces[0].cls = tc;

		tc = class_java_io_Serializable;
		LOADER_ASSERT(tc->state & CLASS_LOADED);
		list_addfirst(&unlinkedclasses, tc);
		c->interfaces[1].cls = tc;

	} else {
		c->interfaces[0].cls = class_java_lang_Cloneable;
		c->interfaces[1].cls = class_java_io_Serializable;
	}

	c->methodscount = 1;
	c->methods = MNEW(methodinfo, c->methodscount);

	classrefs = MNEW(constant_classref, 2);
	CLASSREF_INIT(classrefs[0], c, c->name);
	CLASSREF_INIT(classrefs[1], c, utf_java_lang_Object);

	/* create descriptor for clone method */
	/* we need one paramslot which is reserved for the 'this' parameter */
	clonedesc = NEW(methoddesc);
	clonedesc->returntype.type = TYPE_ADDRESS;
	clonedesc->returntype.classref = classrefs + 1;
	clonedesc->returntype.arraydim = 0;
	/* initialize params to "empty", add real params below in
	   descriptor_params_from_paramtypes */
	clonedesc->paramcount = 0;
	clonedesc->paramslots = 0;
	clonedesc->paramtypes[0].classref = classrefs + 0;

	/* create methodinfo */

	clone = c->methods;
	MSET(clone, 0, methodinfo, 1);

#if defined(USE_THREADS) && defined(NATIVE_THREADS)
	initObjectLock(&clone->header);
#endif

	/* if you delete the ACC_NATIVE below, set clone->maxlocals=1 (interpreter 
	   related) */
	clone->flags = ACC_PUBLIC | ACC_NATIVE;
	clone->name = utf_clone;
	clone->descriptor = utf_void__java_lang_Object;
	clone->parseddesc = clonedesc;
	clone->class = c;
	clone->monoPoly = MONO;

	/* parse the descriptor to get the register allocation */

	if (!descriptor_params_from_paramtypes(clonedesc, clone->flags))
		return false;

	clone->entrypoint =
		codegen_createnativestub((functionptr) &builtin_clone_array, clone);

	/* XXX: field: length? */

	/* array classes are not loaded from class files */

	c->state |= CLASS_LOADED;
	c->parseddescs = (u1 *) clonedesc;
	c->parseddescsize = sizeof(methodinfo);
	c->classrefs = classrefs;
	c->classrefcount = 1;

	/* insert class into the loaded class cache */
	/* XXX free classinfo if NULL returned? */

	return classcache_store(loader, c, true);
}


/* loader_close ****************************************************************

   Frees all resources.
	
*******************************************************************************/

void loader_close(void)
{
	/* empty */
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
