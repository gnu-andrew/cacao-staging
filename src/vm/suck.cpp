/* src/vm/suck.cpp - functions to read LE ordered types from a buffer

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


#include "config.h"

#include <assert.h>
#include <stdlib.h>

#include "vm/types.h"

#include "arch.h"

#include "mm/memory.h"

#include "threads/mutex.hpp"

#include "toolbox/list.hpp"
#include "toolbox/logging.h"
#include "toolbox/util.h"

#include "vm/exceptions.hpp"
#include "vm/loader.hpp"
#include "vm/options.h"
#include "vm/os.hpp"
#include "vm/properties.hpp"
#include "vm/suck.hpp"
#include "vm/vm.hpp"
#include "vm/zip.h"


/* global variables ***********************************************************/

List<list_classpath_entry*>* list_classpath_entries;


/* suck_init *******************************************************************

   Initializes the suck subsystem like initializing the classpath
   entries list.

*******************************************************************************/

bool suck_init(void)
{
	TRACESUBSYSTEMINITIALIZATION("suck_init");

#warning Move this list into VM.
	list_classpath_entries = new List<list_classpath_entry*>();

	/* everything's ok */

	return true;
}


/* scandir_filter **************************************************************

   Filters for zip/jar files.

*******************************************************************************/

static int scandir_filter(const struct dirent *a)
{
	s4 namlen;

#if defined(_DIRENT_HAVE_D_NAMLEN)
	namlen = a->d_namlen;
#else
	namlen = strlen(a->d_name);
#endif

	if ((strncasecmp(a->d_name + namlen - 4, ".zip", 4) == 0) ||
		(strncasecmp(a->d_name + namlen - 4, ".jar", 4) == 0))
		return 1;

	return 0;
}


/* suck_add ********************************************************************

   Adds a classpath to the global classpath entries list.

*******************************************************************************/

void suck_add(char *classpath)
{
	list_classpath_entry *lce;
	char                 *start;
	char                 *end;
	char                 *filename;
	s4                    filenamelen;
	bool                  is_zip;
	char                 *cwd;
	s4                    cwdlen;
#if defined(ENABLE_ZLIB)
	hashtable            *ht;
#endif

	/* parse the classpath string */

	for (start = classpath; (*start) != '\0'; ) {

		/* search for ':' delimiter to get the end of the current entry */
		for (end = start; ((*end) != '\0') && ((*end) != ':'); end++);

		if (start != end) {
			is_zip = false;
			filenamelen = end - start;

			if (filenamelen > 4) {
				if ((strncasecmp(end - 4, ".zip", 4) == 0) ||
					(strncasecmp(end - 4, ".jar", 4) == 0)) {
					is_zip = true;
				}
			}

			/* save classpath entries as absolute pathnames */

			cwd = NULL;
			cwdlen = 0;

			if (*start != '/') {                      /* XXX fix me for win32 */
				cwd = _Jv_getcwd();
				cwdlen = strlen(cwd) + strlen("/");
			}

			/* allocate memory for filename and fill it */

			filename = MNEW(char, filenamelen + cwdlen + strlen("/") +
							strlen("0"));

			if (cwd) {
				strcpy(filename, cwd);
				strcat(filename, "/");
				strncat(filename, start, filenamelen);

				/* add cwd length to file length */
				filenamelen += cwdlen;

			} else {
				strncpy(filename, start, filenamelen);
				filename[filenamelen] = '\0';
			}

			lce = NULL;

			if (is_zip) {
#if defined(ENABLE_ZLIB)
				ht = zip_open(filename);

				if (ht != NULL) {
					lce = NEW(list_classpath_entry);

					lce->type      = CLASSPATH_ARCHIVE;
					lce->htclasses = ht;
					lce->path      = filename;
					lce->pathlen   = filenamelen;

					/* SUN compatible -verbose:class output */

					if (opt_verboseclass)
						printf("[Opened %s]\n", filename);
				}

#else
				VM::get_current()->abort("suck_add: zip/jar files not supported");
#endif
			}
			else {
				if (filename[filenamelen - 1] != '/') {/* XXX fixme for win32 */
					filename[filenamelen] = '/';
					filename[filenamelen + 1] = '\0';
					filenamelen++;
				}

				lce = NEW(list_classpath_entry);

				lce->type    = CLASSPATH_PATH;
				lce->path    = filename;
				lce->pathlen = filenamelen;
			}

			/* add current classpath entry, if no error */

			if (lce != NULL)
				list_classpath_entries->push_back(lce);
		}

		/* goto next classpath entry, skip ':' delimiter */

		if ((*end) == ':')
			start = end + 1;
		else
			start = end;
	}
}


/* suck_add_from_property ******************************************************

   Adds a classpath form a property entry to the global classpath
   entries list.

*******************************************************************************/

void suck_add_from_property(const char *key)
{
	const char     *value;
	const char     *start;
	const char     *end;
	s4              pathlen;
	struct dirent **namelist;
	s4              n;
	s4              i;
	s4              namlen;
	char           *p;

	/* get the property value */
	Properties properties = VM::get_current()->get_properties();
	value = properties.get(key);

	if (value == NULL)
		return;

	/* get the directory entries of the property */

	for (start = value; (*start) != '\0'; ) {

		/* search for ':' delimiter to get the end of the current entry */

		for (end = start; ((*end) != '\0') && ((*end) != ':'); end++);

		/* found an entry */

		if (start != end) {
			/* allocate memory for the path entry */

			pathlen = end - start;
			char* path = MNEW(char, pathlen + strlen("0"));

			/* copy and terminate the string */

			strncpy(path, start, pathlen);
			path[pathlen] = '\0';

			/* Reset namelist to NULL for the freeing in an error case
			   (see below). */

			namelist = NULL;

			/* scan the directory found for zip/jar files */

			n = os::scandir((const char*) path, &namelist, &scandir_filter, (int (*)(const void*, const void*)) &alphasort);

			/* On error, just continue, this should be ok. */

			if (n > 0) {
				for (i = 0; i < n; i++) {
#if defined(_DIRENT_HAVE_D_NAMLEN)
					namlen = namelist[i]->d_namlen;
#else
					namlen = strlen(namelist[i]->d_name);
#endif

					/* Allocate memory for bootclasspath. */

					// FIXME Make boot_class_path const char*.
					char* boot_class_path = (char*) properties.get("sun.boot.class.path");

					p = MNEW(char,
							 pathlen + strlen("/") + namlen +
							 strlen(":") +
							 strlen(boot_class_path) +
							 strlen("0"));

					/* Prepend the file found to the bootclasspath. */

					strcpy(p, path);
					strcat(p, "/");
					strcat(p, namelist[i]->d_name);
					strcat(p, ":");
					strcat(p, boot_class_path);

					properties.put("sun.boot.class.path", p);
					properties.put("java.boot.class.path", p);

					MFREE(boot_class_path, char, strlen(boot_class_path));

					/* free the memory allocated by scandir */
					/* (We use `free` as the memory came from the C library.) */

					free(namelist[i]);
				}
			}

			/* On some systems (like Linux) when n == 0, then namelist
			   returned from scnadir is NULL, thus we don't have to
			   free it.
			   (Use `free` as the memory came from the C library.) */

			if (namelist != NULL)
				free(namelist);

			MFREE(path, char, pathlen + strlen("0"));
		}

		/* goto next entry, skip ':' delimiter */

		if ((*end) == ':')
			start = end + 1;
		else
			start = end;
	}
}


/* suck_check_classbuffer_size *************************************************

   Assert that at least <len> bytes are left to read <len> is limited
   to the range of non-negative s4 values.

*******************************************************************************/

bool suck_check_classbuffer_size(classbuffer *cb, s4 len)
{
#ifdef ENABLE_VERIFIER
	if (len < 0 || ((cb->data + cb->size) - cb->pos) < len) {
		exceptions_throw_classformaterror(cb->clazz, "Truncated class file");
		return false;
	}
#endif /* ENABLE_VERIFIER */

	return true;
}


u1 suck_u1(classbuffer *cb)
{
	u1 a;

	a = SUCK_BE_U1(cb->pos);
	cb->pos++;

	return a;
}


u2 suck_u2(classbuffer *cb)
{
	u2 a;

	a = SUCK_BE_U2(cb->pos);
	cb->pos += 2;

	return a;
}


u4 suck_u4(classbuffer *cb)
{
	u4 a;

	a = SUCK_BE_U4(cb->pos);
	cb->pos += 4;

	return a;
}


u8 suck_u8(classbuffer *cb)
{
#if U8_AVAILABLE == 1
	u8 a;

	a = SUCK_BE_U8(cb->pos);
	cb->pos += 8;

	return a;
#else
	u8 v;

	v.high = suck_u4(cb);
	v.low = suck_u4(cb);

	return v;
#endif
}


float suck_float(classbuffer *cb)
{
	float f;

#if WORDS_BIGENDIAN == 0
	u1 buffer[4];
	u2 i;

	for (i = 0; i < 4; i++)
		buffer[3 - i] = suck_u1(cb);

	MCOPY((u1 *) (&f), buffer, u1, 4);
#else
	suck_nbytes((u1*) (&f), cb, 4);
#endif

	assert(sizeof(float) == 4);
	
	return f;
}


double suck_double(classbuffer *cb)
{
	double d;

#if WORDS_BIGENDIAN == 0
	u1 buffer[8];
	u2 i;	

# if defined(__ARM__) && defined(__ARMEL__) && !defined(__VFP_FP__)
	/*
	 * On little endian ARM processors when using FPA, word order
	 * of doubles is still big endian. So take that into account
	 * here. When using VFP, word order of doubles follows byte
	 * order. (michi 2005/07/24)
	 */
	for (i = 0; i < 4; i++)
		buffer[3 - i] = suck_u1(cb);
	for (i = 0; i < 4; i++)
		buffer[7 - i] = suck_u1(cb);
# else
	for (i = 0; i < 8; i++)
		buffer[7 - i] = suck_u1(cb);
# endif /* defined(__ARM__) && ... */

	MCOPY((u1 *) (&d), buffer, u1, 8);
#else 
	suck_nbytes((u1*) (&d), cb, 8);
#endif

	assert(sizeof(double) == 8);
	
	return d;
}


/* suck_nbytes *****************************************************************

   Transfer block of classfile data into a buffer.

*******************************************************************************/

void suck_nbytes(u1 *buffer, classbuffer *cb, s4 len)
{
	MCOPY(buffer, cb->pos, u1, len);
	cb->pos += len;
}


/* suck_skip_nbytes ************************************************************

   Skip block of classfile data.

*******************************************************************************/

void suck_skip_nbytes(classbuffer *cb, s4 len)
{
	cb->pos += len;
}


/* suck_start ******************************************************************

   Returns true if classbuffer is already loaded or a file for the
   specified class has succussfully been read in. All directories of
   the searchpath are used to find the classfile (<classname>.class).
   Returns NULL if no classfile is found and writes an error message.
	
*******************************************************************************/

classbuffer *suck_start(classinfo *c)
{
	list_classpath_entry *lce;
	char                 *filename;
	s4                    filenamelen;
	char                 *path;
	FILE                 *classfile;
	s4                    len;
	struct stat           buffer;
	classbuffer          *cb;

	/* initialize return value */

	cb = NULL;

	/* get the classname as char string (do it here for the warning at
       the end of the function) */

	filenamelen = utf_bytes(c->name) + strlen(".class") + strlen("0");
	filename = MNEW(char, filenamelen);

	utf_copy(filename, c->name);
	strcat(filename, ".class");

	/* walk through all classpath entries */

	for (List<list_classpath_entry*>::iterator it = list_classpath_entries->begin(); it != list_classpath_entries->end() && cb == NULL; it++) {
		lce = *it;

#if defined(ENABLE_ZLIB)
		if (lce->type == CLASSPATH_ARCHIVE) {

			/* enter a monitor on zip/jar archives */

			lce->mutex->lock();

			/* try to get the file in current archive */

			cb = zip_get(lce, c);

			/* leave the monitor */

			lce->mutex->unlock();

		} else {
#endif /* defined(ENABLE_ZLIB) */
			path = MNEW(char, lce->pathlen + filenamelen);
			strcpy(path, lce->path);
			strcat(path, filename);

			classfile = os::fopen(path, "r");

			if (classfile) {                                   /* file exists */
				if (!os::stat(path, &buffer)) {     /* read classfile data */
					cb = NEW(classbuffer);
					cb->clazz = c;
					cb->size  = buffer.st_size;
					cb->data  = MNEW(u1, cb->size);
					cb->pos   = cb->data;
					cb->path  = lce->path;

					/* read class data */

					len = os::fread((void *) cb->data, 1, cb->size,
									   classfile);

					if (len != buffer.st_size) {
						suck_stop(cb);
/*  						if (ferror(classfile)) { */
/*  						} */
					}

					/* close the class file */

					os::fclose(classfile);
				}
			}

			MFREE(path, char, lce->pathlen + filenamelen);
#if defined(ENABLE_ZLIB)
		}
#endif
	}

	if (opt_verbose)
		if (cb == NULL)
			dolog("Warning: Can not open class file '%s'", filename);

	MFREE(filename, char, filenamelen);

	return cb;
}


/* suck_stop *******************************************************************

   Frees memory for buffer with classfile data.

   CAUTION: This function may only be called if buffer has been
   allocated by suck_start with reading a file.
	
*******************************************************************************/

void suck_stop(classbuffer *cb)
{
	/* free memory */

	MFREE(cb->data, u1, cb->size);
	FREE(cb, classbuffer);
}


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
