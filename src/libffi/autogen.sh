#!/bin/sh

libtoolize --automake
if test `uname` = 'FreeBSD'; then
        aclocal -I . -I /usr/local/share/aclocal -I /usr/local/share/aclocal19
else
        aclocal -I m4
fi
autoheader
automake --add-missing
autoconf
