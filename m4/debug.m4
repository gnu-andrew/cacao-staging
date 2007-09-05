dnl m4/debug.m4
dnl
dnl Copyright (C) 2007 R. Grafl, A. Krall, C. Kruegel,
dnl C. Oates, R. Obermaisser, M. Platter, M. Probst, S. Ring,
dnl E. Steiner, C. Thalinger, D. Thuernbeck, P. Tomsich, C. Ullrich,
dnl J. Wenninger, Institut f. Computersprachen - TU Wien
dnl 
dnl This file is part of CACAO.
dnl 
dnl This program is free software; you can redistribute it and/or
dnl modify it under the terms of the GNU General Public License as
dnl published by the Free Software Foundation; either version 2, or (at
dnl your option) any later version.
dnl 
dnl This program is distributed in the hope that it will be useful, but
dnl WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
dnl General Public License for more details.
dnl 
dnl You should have received a copy of the GNU General Public License
dnl along with this program; if not, write to the Free Software
dnl Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
dnl 02110-1301, USA.


dnl check for debug

AC_DEFUN([AC_CHECK_ENABLE_DEBUG],[
AC_MSG_CHECKING(whether debug code generation should be enabled)
AC_ARG_ENABLE([debug],
              [AS_HELP_STRING(--disable-debug,disable debug code generation [[default=yes]])],
              [case "${enableval}" in
                   no)
                       NDEBUG=yes
                       AC_DEFINE([NDEBUG], 1, [disable debug code])
                       ;;
                   *)
                       NDEBUG=no
                       ;;
               esac],
              [NDEBUG=no])

if test x"${NDEBUG}" = "xno"; then
    AC_MSG_RESULT(yes)
else
    AC_MSG_RESULT(no)
fi
AM_CONDITIONAL([NDEBUG], test x"${NDEBUG}" = "xyes")
])
