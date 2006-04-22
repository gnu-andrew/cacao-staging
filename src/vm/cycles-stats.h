/* src/vm/cycles-stats.h - macros for cycle count statistics

   Copyright (C) 1996-2005, 2006 R. Grafl, A. Krall, C. Kruegel,
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

   Contact: cacao@cacaojvm.org

   Authors: Edwin Steiner

   Changes:

   $Id$

*/

#ifndef _CYCLES_STATS_H
#define _CYCLES_STATS_H

#include "config.h"
#include "vm/types.h"

#if defined(ENABLE_CYCLES_STATS)

#include <stdio.h>

#define CYCLES_STATS_DECLARE(name,nbins,divisor)                            \
    static const int CYCLES_STATS_##name##_MAX = (nbins);                   \
    static const int CYCLES_STATS_##name##_DIV = (divisor);                 \
    static u4 cycles_stats_##name##_bins[(nbins) + 1] = { 0 };              \
    static u4 cycles_stats_##name##_count = 0;                              \
    static u8 cycles_stats_##name##_max = 0;                                \
    static u8 cycles_stats_##name##_min = 1000000000;

#define CYCLES_STATS_GET(var)                                               \
	(var) = asm_get_cycle_count()                                           \

#define CYCLES_STATS_COUNT(name,cyclesexpr)                                 \
    do {                                                                    \
        u8 cyc = (cyclesexpr);                                              \
        if (cyc > cycles_stats_##name##_max)                                \
            cycles_stats_##name##_max = cyc;                                \
        if (cyc < cycles_stats_##name##_min)                                \
            cycles_stats_##name##_min = cyc;                                \
        cyc /= CYCLES_STATS_##name##_DIV;                                   \
        if (cyc < CYCLES_STATS_##name##_MAX)                                \
            cycles_stats_##name##_bins[cyc]++;                              \
        else                                                                \
            cycles_stats_##name##_bins[CYCLES_STATS_##name##_MAX]++;        \
        cycles_stats_##name##_count++;                                      \
    } while (0)

#define CYCLES_STATS_PRINT(name,file)                                       \
    do {                                                                    \
        cycles_stats_print((file), #name,                                   \
            CYCLES_STATS_##name##_MAX, CYCLES_STATS_##name##_DIV,           \
            cycles_stats_##name##_bins, cycles_stats_##name##_count,        \
            cycles_stats_##name##_min, cycles_stats_##name##_max);          \
    } while (0)

void cycles_stats_print(FILE *file,
					    const char *name, int nbins, int div,
					    u4 *bins, u8 count, u8 min, u8 max);


#else /* !defined(ENABLE_CYCLES_STATS) */

#define CYCLES_STATS_DECLARE(name,nbins,divisor)
#define CYCLES_STATS_GET(var)
#define CYCLES_STATS_COUNT(name,cyclesexpr)
#define CYCLES_STATS_PRINT(name,file)

#endif /* defined(ENABLE_CYCLES_STATS) */

#endif /* _CYCLES_STATS_H */

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
