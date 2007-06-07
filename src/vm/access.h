/* src/vm/access.h - checking access rights

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

   $Id: access.h 7976 2007-05-29 12:22:55Z twisti $

*/


#ifndef _ACCESS_H
#define _ACCESS_H

#include "config.h"
#include "vm/types.h"

#include "vm/global.h"

#include "vmcore/class.h"
#include "vmcore/field.h"
#include "vmcore/method.h"


/* macros *********************************************************************/

#define SAME_PACKAGE(a,b)                                  \
			((a)->classloader == (b)->classloader &&       \
			 (a)->packagename == (b)->packagename)


/* function prototypes ********************************************************/

bool access_is_accessible_class(classinfo *referer, classinfo *cls);

bool access_is_accessible_member(classinfo *referer, classinfo *declarer,
								 s4 memberflags);

bool access_check_field(fieldinfo *f, s4 calldepth);
bool access_check_method(methodinfo *m, s4 calldepth);

#endif /* _ACCESS_H */


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
 * vim:noexpandtab:sw=4:ts=4:
 */

