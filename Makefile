################################################################################
#                    Makefile for the JavaVM - compiler CACAO                  #
################################################################################
#
# Copyright (c) 1997 A. Krall, R. Grafl, M. Gschwind, M. Probst
#
# See file COPYRIGHT for information on usage and disclaimer of warranties
#
# Authors: Reinhard Grafl      EMAIL: cacao@complang.tuwien.ac.at
#          Andreas  Krall      EMAIL: cacao@complang.tuwien.ac.at
#
# Last Change: 1998/10/30
#
#
# ATTENTION: This version of the makefile only works with gmake.
#            This Makefile not only generates object files, but also additional
#            files needed during compilation:
#                nativetypes.hh
#                nativetables.hh
#            All object files and the *.hh can be deleted savely. They will be
#            generated automatically.
#
################################################################################

VERSION_MAJOR = 0
VERSION_MINOR = 30
VERSION_POSTFIX = 

VERSION_STRING=$(VERSION_MAJOR).$(VERSION_MINOR)$(VERSION_POSTFIX)

##################### generation of the excutable ##############################

# Enabling/disabling thread support
USE_THREADS = YES
#USE_THREADS = NO

ifeq ($(USE_THREADS),YES)
THREAD_OBJ = threads/threads.a
THREAD_CFLAGS = -DUSE_THREADS -DEXTERNAL_OVERFLOW -DDONT_FREE_FIRST
else
THREAD_OBJ =
THREAD_CFLAGS =
endif

#CC = cc
#CFLAGS = -g -mieee -Wall $(THREAD_CFLAGS)
#CFLAGS = -mieee -O3 -Wall $(THREAD_CFLAGS)

CC = cc
#CFLAGS = -g3 -ieee $(THREAD_CFLAGS)
CFLAGS = -O3 -ieee $(THREAD_CFLAGS)

OBJ = main.o tables.o loader.o compiler.o newcomp.o builtin.o asmpart.o \
	toolbox/toolbox.a native.o $(THREAD_OBJ) mm/mm.o
OBJH = headers.o tables.o loader.o builtin.o toolbox/toolbox.a $(THREAD_OBJ) \
	 mm/mm.o

cacao: $(OBJ)
	$(CC) $(CFLAGS) -o cacao $(OBJ) -lm
cacaoh: $(OBJH)
	$(CC) $(CFLAGS) -o cacaoh $(OBJH) -lm

main.o: main.c global.h tables.h compiler.h ncomp/ncomp.h loader.h \
        asmpart.h builtin.h native.h

headers.o:  headers.c global.h tables.h loader.h

loader.o:   loader.c global.h loader.h tables.h native.h asmpart.h

compiler.o: builtin.h compiler.h global.h loader.h tables.h native.h \
            asmpart.h compiler.c comp/*.c sysdep/gen.c sysdep/disass.c

newcomp.o:  builtin.h ncomp/ncomp.h global.h loader.h tables.h native.h \
            asmpart.h ncomp/ncompdef.h ncomp/*.c sysdep/ngen.h sysdep/ngen.c sysdep/disass.c

builtin.o: builtin.c global.h loader.h builtin.h tables.h sysdep/native-math.h

native.o: native.c global.h tables.h native.h asmpart.h builtin.h \
          nativetypes.hh nativetable.hh nat/*.c

tables.o: tables.c global.h tables.h

global.h: sysdep/types.h toolbox/*.h
	touch global.h

toolbox/toolbox.a: toolbox/*.c toolbox/*.h
	cd toolbox; make toolbox.a "CFLAGS=$(CFLAGS)" "CC=$(CC)" 

ifeq ($(USE_THREADS),YES)
threads/threads.a: threads/*.c threads/*.h sysdep/threads.h
	cd threads; make threads.a "USE_THREADS=$(USE_THREADS)" "CFLAGS=$(CFLAGS)" "CC=$(CC)" 
endif

mm/mm.o: mm/*.[ch] mm/Makefile
	cd mm; make mm.o "USE_THREADS=$(USE_THREADS)" "CFLAGS=$(CFLAGS)" "CC=$(CC)"

asmpart.o: sysdep/asmpart.c sysdep/offsets.h
	rm -f asmpart.s
	$(CC) -E sysdep/asmpart.c > asmpart.s
	$(CC) -c -o asmpart.o asmpart.s
	rm -f asmpart.s


########################### support targets ####################################

clean:
	rm -f *.o cacao cacaoh cacao.tgz nativetable.hh nativetypes.hh \
	      core tst/core
	cd toolbox; make clean
	cd threads; make clean
	cd mm; make clean

tar:
	rm -f cacao.tgz cacao.tar
	tar -cvf cacao.tar Makefile */Makefile README COPYRIGHT tst/*.java \
	    doc/*.doc html/*.html *.[ch] comp/*.[ch] ncomp/*.[ch] alpha/*.doc alpha/*.[ch] \
	    nat/*.[ch] toolbox/*.[ch] threads/*.[ch] # sparc/*.[ch]
	ls -l cacao.tar
	gzip -9 cacao.tar
	mv cacao.tar.gz cacao.tgz
	ls -l cacao.tgz

dist:
	rm -rf cacao-$(VERSION_STRING).tar.gz cacao-$(VERSION_STRING);
	( mkdir cacao-$(VERSION_STRING); \
#	  tar -cf cacao-$(VERSION_STRING).tar -T FILES; \
	  tar -cvf cacao-$(VERSION_STRING).tar Makefile */Makefile README COPYRIGHT \
	    tst/*.java doc/*.doc html/*.html *.[ch] comp/*.[ch] ncomp/*.[ch] \
	    alpha/*.doc alpha/*.[ch] nat/*.[ch] toolbox/*.[ch] threads/*.[ch]; \
	  cd cacao-$(VERSION_STRING); \
	  tar -xf ../cacao-$(VERSION_STRING).tar; \
	  cd ..; \
	  rm cacao-$(VERSION_STRING).tar; \
	  tar -cvf cacao-$(VERSION_STRING).tar cacao-$(VERSION_STRING); \
	  rm -rf cacao-$(VERSION_STRING); )
	gzip -9 cacao-$(VERSION_STRING).tar
	ls -l cacao-$(VERSION_STRING).tar.gz

########################## supported architectures #############################

config-alpha:
	rm -f sysdep
	ln -s alpha sysdep
	rm -f threads/sysdep
	ln -s ../sysdep threads/sysdep
	make clean

config-sparc:
	rm -f sysdep
	ln -s sparc sysdep
	rm -f threads/sysdep
	ln -s ../sysdep threads/sysdep
	make clean



##################### generation of NATIVE - header files ######################

sysdep/offsets.h nativetypes.hh nativetable.hh : cacaoh
	./cacaoh java.lang.Object \
       java.lang.String \
       java.lang.Class \
       java.lang.ClassLoader \
       java.lang.Compiler \
       java.lang.Double \
       java.lang.Float \
       java.lang.Math \
       java.lang.Runtime \
       java.lang.SecurityManager \
       java.lang.System \
       java.lang.Thread \
       java.lang.ThreadGroup \
       java.lang.Throwable \
\
       java.io.File \
       java.io.FileDescriptor \
       java.io.FileInputStream \
       java.io.FileOutputStream \
       java.io.PrintStream \
       java.io.RandomAccessFile \
\
       java.util.Properties \
       java.util.Date
       
       