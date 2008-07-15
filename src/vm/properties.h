/* src/vm/properties.h - handling commandline properties

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


#ifndef _PROPERTIES_H
#define _PROPERTIES_H

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "vm/global.h"


/* function prototypes ********************************************************/

void  properties_init(void);
void  properties_set(void);

void        properties_add(const char *key, const char *value);
const char *properties_get(const char *key);

void  properties_system_add(java_handle_t *p, const char *key, const char *value);

#if defined(ENABLE_JAVASE)
void  properties_system_add_all(java_handle_t *p);
#endif

void  properties_dump(void);

#ifdef __cplusplus
}
#endif

#endif /* _PROPERTIES_H */


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
