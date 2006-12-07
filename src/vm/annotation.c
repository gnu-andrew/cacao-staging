/* src/vm/annotation.c - class annotations

   Copyright (C) 2006 R. Grafl, A. Krall, C. Kruegel, C. Oates,
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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.

   Contact: cacao@cacaojvm.org

   Authors: Christian Thalinger

   $Id: utf8.h 5920 2006-11-05 21:23:09Z twisti $

*/


#include "config.h"
#include "vm/types.h"

#include "mm/memory.h"
#include "vm/annotation.h"
#include "vm/class.h"
#include "vm/suck.h"


/* annotation_load_attribute_runtimevisibleannotations *************************

   RuntimeVisibleAnnotations_attribute {
       u2 attribute_name_index;
       u4 attribute_length;
       u2 num_annotations;
       annotation annotations[num_annotations];
   }

   annotation {
       u2 type_index;
       u2 num_element_value_pairs;
       {
            u2            element_name_index;
            element_value element;
       } element_value_pairs[num_element_value_pairs];
   }
			   
*******************************************************************************/

#if defined(ENABLE_JAVASE)
bool annotation_load_attribute_runtimevisibleannotations(classbuffer *cb)
{
	classinfo       *c;
	u4               attribute_length;
	u2               num_annotations;
	annotation_t    *aa;
	element_value_t *aev;
	u2               type_index;
	u2               num_element_value_pairs;
	u2               element_name_index;
	u4               i, j;

	/* get classinfo */

	c = cb->class;

	if (!suck_check_classbuffer_size(cb, 4 + 2))
		return false;

	/* attribute_length */

	attribute_length = suck_u4(cb);

	if (!suck_check_classbuffer_size(cb, attribute_length))
		return false;

	/* get number of annotations */

	num_annotations = suck_u2(cb);

	printf("num_annotations: %d\n", num_annotations);

	/* allocate annotations-array */

	aa = MNEW(annotation_t, num_annotations);

	/* parse all annotations */

	for (i = 0; i < num_annotations; i++) {
		/* get annotation type */

		type_index = suck_u2(cb);

		if (!(aa[i].type = class_getconstant(c, type_index, CONSTANT_Utf8)))
			return false;

		printf("type: ");
		utf_display_printable_ascii(aa[i].type);
		printf("\n");

		/* get number of element values */

		num_element_value_pairs = suck_u2(cb);

		printf("num_element_value_pairs: %d\n", num_element_value_pairs);

		aev = MNEW(element_value_t, num_element_value_pairs);

		/* parse all element values */

		for (j = 0; j < num_element_value_pairs; j++) {
			/* get element name */

			element_name_index = suck_u2(cb);

			if (!(aev[j].name =
				  class_getconstant(c, element_name_index, CONSTANT_Utf8)))
				return false;

			/* get element tag */

			aev[i].tag = suck_u1(cb);
		}

		/* store element value data */

		aa[i].element_valuescount = num_element_value_pairs;
		aa[i].element_values      = aev;
	}

	/* store annotation variables */

	c->runtimevisibleannotationscount = num_annotations;
	c->runtimevisibleannotations      = aa;

	return true;
}
#endif /* defined(ENABLE_JAVASE) */


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