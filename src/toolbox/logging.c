/* src/toolbox/logging.c - contains logging functions

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

   Changes: Christian Thalinger

   $Id: logging.c 2299 2005-04-14 05:17:27Z edwin $

*/


#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "mm/memory.h"
#include "toolbox/logging.h"
#include "vm/global.h"
#include "vm/tables.h"
#include "vm/statistics.h"

#if defined(USE_THREADS)
# if defined(NATIVE_THREADS)
#  include "threads/native/threads.h"
# endif
#endif


/***************************************************************************
                        LOG FILE HANDLING 
***************************************************************************/

FILE *logfile = NULL;


void log_init(const char *fname)
{
	if (fname) {
		if (fname[0]) {
			logfile = fopen(fname, "w");
		}
	}
}


/*********************** Function: dolog ************************************

Writes logtext to the protocol file (if opened) or to stdout.

**************************************************************************/

void dolog(const char *txt, ...)
{
	char logtext[MAXLOGTEXT];
	va_list ap;

	va_start(ap, txt);
	vsprintf(logtext, txt, ap);
	va_end(ap);

	if (logfile) {
#if defined(USE_THREADS) && defined(NATIVE_THREADS)
		fprintf(logfile, "[%p] %s\n",(void*)THREADOBJECT,logtext);
#else
		fprintf(logfile, "%s\n",logtext);
#endif
		fflush(logfile);

	} else {
#if defined(USE_THREADS) && defined(NATIVE_THREADS)
		fprintf(stdout,"LOG: [%p] %s\n",(void*)THREADOBJECT,logtext);
#else
		fprintf(stdout,"LOG: %s\n",logtext);
#endif
		fflush(stdout);
	}
}


/******************** Function: dolog_plain *******************************

Writes logtext to the protocol file (if opened) or to stdout.

**************************************************************************/

void dolog_plain(const char *txt, ...)
{
	char logtext[MAXLOGTEXT];
	va_list ap;

	va_start(ap, txt);
	vsprintf(logtext, txt, ap);
	va_end(ap);

	if (logfile) {
		fprintf(logfile, "%s", logtext);
		fflush(logfile);

	} else {
		fprintf(stdout,"%s", logtext);
		fflush(stdout);
	}
}


/********************* Function: log_text ********************************/

void log_text(const char *text)
{
	dolog("%s", text);
}


/******************** Function: log_plain *******************************/

void log_plain(const char *text)
{
	dolog_plain("%s", text);
}


/****************** Function: get_logfile *******************************/

FILE *get_logfile(void)
{
	return (logfile) ? logfile : stdout;
}


/****************** Function: log_flush *********************************/

void log_flush(void)
{
	fflush(get_logfile());
}


/********************* Function: log_nl *********************************/

void log_nl(void)
{
	log_plain("\n");
	fflush(get_logfile());
}


/* log_cputime *****************************************************************

   XXX

*******************************************************************************/

#if defined(STATISTICS)
void log_cputime(void)
{
	s8 t;
	int sec, usec;
	char logtext[MAXLOGTEXT];

	t = getcputime();
	sec = t / 1000000;
	usec = t % 1000000;

	sprintf(logtext, "Total CPU usage: %d seconds and %d milliseconds",
			sec, usec / 1000);
	log_text(logtext);
}
#endif


/* log_message_method **********************************************************

   outputs log text like this:

   LOG: Loading class: java/lang/Object

*******************************************************************************/

void log_message_class(const char *msg, classinfo *c)
{
	char *buf;
	s4    len;

	len = strlen(msg) + utf_strlen(c->name) + strlen("0");

	buf = MNEW(char, len);

	strcpy(buf, msg);
	utf_strcat(buf, c->name);

	log_text(buf);

	MFREE(buf, char, len);
}


/* log_message_method **********************************************************

   outputs log text like this:

   LOG: Compiling: java.lang.Object.clone()Ljava/lang/Object;

*******************************************************************************/

void log_message_method(const char *msg, methodinfo *m)
{
	char *buf;
	s4    len;

	len = strlen(msg) + utf_strlen(m->class->name) + strlen(".") +
		utf_strlen(m->name) + utf_strlen(m->descriptor) + strlen("0");

	buf = MNEW(char, len);

	strcpy(buf, msg);
	utf_strcat_classname(buf, m->class->name);
	strcat(buf, ".");
	utf_strcat(buf, m->name);
	utf_strcat(buf, m->descriptor);

	log_text(buf);

	MFREE(buf, char, len);
}


/* error ***********************************************************************

   Like dolog(), but terminates the program immediately.

*******************************************************************************/

void error(const char *txt, ...)
{
	char logtext[MAXLOGTEXT];
	va_list ap;

	va_start(ap, txt);
	vsprintf(logtext, txt, ap);
	va_end(ap);

	if (logfile) {
		fprintf(logfile, "ERROR: %s\n", logtext);
	}

	fprintf(stderr, "ERROR: %s\n", logtext);

	exit(1);
}


/* panic ***********************************************************************

   Like error(), takes the text to output as an argument.

*******************************************************************************/

void panic(const char *txt)
{
	error("%s", txt);
}


/* log_utf *********************************************************************

   Log utf symbol.

*******************************************************************************/

void log_utf(utf *u)
{
	char buf[MAXLOGTEXT];
	utf_sprint(buf, u);
	dolog("%s", buf);
}


/* log_plain_utf ***************************************************************

   Log utf symbol (without printing "LOG: " and newline).

*******************************************************************************/

void log_plain_utf(utf *u)
{
	char buf[MAXLOGTEXT];
	utf_sprint(buf, u);
	dolog_plain("%s", buf);
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
