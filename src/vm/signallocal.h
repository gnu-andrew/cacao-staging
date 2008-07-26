/* src/vm/signallocal.h - machine independent signal functions

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


#ifndef _CACAO_SIGNAL_H
#define _CACAO_SIGNAL_H

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <signal.h>

#include "vm/global.h"


// Signal defines.

#define Signal_THREAD_INTERRUPT    SIGHUP


/* function prototypes ********************************************************/

bool  signal_init(void);
void  signal_register_signal(int signum, functionptr handler, int flags);

void  signal_thread_handler(int sig);
bool  signal_start_thread(void);

/* machine dependent signal handler */

void md_signal_handler_sigsegv(int sig, siginfo_t *siginfo, void *_p);

#if SUPPORT_HARDWARE_DIVIDE_BY_ZERO
void md_signal_handler_sigfpe(int sig, siginfo_t *siginfo, void *_p);
#endif

#if defined(__ARM__) || defined(__I386__) || defined(__S390__) || defined(__X86_64__) || defined(__M68K__)
/* XXX use better defines for that (in arch.h) */
void md_signal_handler_sigill(int sig, siginfo_t *siginfo, void *_p);
#endif

#if defined(__POWERPC__)
/* XXX use better defines for that (in arch.h) */
void md_signal_handler_sigtrap(int sig, siginfo_t *siginfo, void *_p);
#endif

void md_signal_handler_sigusr1(int sig, siginfo_t *siginfo, void *_p);

void md_signal_handler_sigusr2(int sig, siginfo_t *siginfo, void *_p);

#ifdef __cplusplus
}
#endif

#endif /* _CACAO_SIGNAL_H */


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
