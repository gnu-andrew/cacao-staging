/****************************** global.h ***************************************

	Copyright (c) 1997 A. Krall, R. Grafl, M. Gschwind, M. Probst

	See file COPYRIGHT for information on usage and disclaimer of warranties

	Contains global definitions which are used in the whole program, includes
	some files and contains global used macros.

	Authors: Reinhard Grafl      EMAIL: cacao@complang.tuwien.ac.at
	Changes: Andreas  Krall      EMAIL: cacao@complang.tuwien.ac.at
	         Mark Probst         EMAIL: cacao@complang.tuwien.ac.at
			 Philipp Tomsich     EMAIL: cacao@complang.tuwien.ac.at

	Last Change: 1998/10/29

*******************************************************************************/

#ifndef __global_h_
#define __global_h_                        /* schani */

#define STATISTICS                         /* andi   */

/* JIT_MARKER_SUPPORT is the define used to toggle Just-in-time generated
   marker functions on and off. */
#undef JIT_MARKER_SUPPORT                  /* phil   */

/***************************** standard includes ******************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "toolbox/memory.h"
#include "toolbox/chain.h"
#include "toolbox/list.h"
#include "toolbox/loging.h"


/**************************** system dependent types **************************/

#include "sysdep/types.h"


/**************************** additional data types ***************************/

typedef void *voidptr;          /* generic pointer */


typedef u1    bool;             /* boolean data type */

#define true  1
#define false 0

typedef void (*functionptr) (); /* generic function pointer */


#define MAX_ALIGN 8             /* most generic alignment for JavaVM values   */


/**************************** shutdown function *******************************/

void cacao_shutdown(s4 status);


/**************************** basic data types ********************************/

#define TYPE_INT      0         /* the JavaVM types must numbered in the      */
#define TYPE_LONG     1         /* same order as the ICMD_Ixxx to ICMD_Axxx   */
#define TYPE_FLOAT    2         /* instructions (LOAD and STORE)              */
#define TYPE_DOUBLE   3         /* integer, long, float, double, address      */
#define TYPE_ADDRESS  4         /* all other types can be numbered arbitrarily*/

#define TYPE_VOID    10


/**************************** Java class file constants ***********************/

#define MAGIC         0xcafebabe
#define MINOR_VERSION 3
#define MAJOR_VERSION 45

#define CONSTANT_Class                7
#define CONSTANT_Fieldref             9
#define CONSTANT_Methodref           10
#define CONSTANT_InterfaceMethodref  11
#define CONSTANT_String               8
#define CONSTANT_Integer              3
#define CONSTANT_Float                4
#define CONSTANT_Long                 5
#define CONSTANT_Double               6
#define CONSTANT_NameAndType         12
#define CONSTANT_Utf8                 1

#define CONSTANT_Arraydescriptor     13
#define CONSTANT_UNUSED               0


#define ACC_PUBLIC                0x0001
#define ACC_PRIVATE               0x0002
#define ACC_PROTECTED             0x0004
#define ACC_STATIC                0x0008
#define ACC_FINAL                 0x0010
#define ACC_SYNCHRONIZED          0x0020
#define ACC_VOLATILE              0x0040
#define ACC_TRANSIENT             0x0080
#define ACC_NATIVE                0x0100
#define ACC_INTERFACE             0x0200
#define ACC_ABSTRACT              0x0400



/**************************** resolve typedef-cycles **************************/

typedef struct unicode unicode;
typedef struct classinfo classinfo; 
typedef struct vftbl vftbl;
typedef u1* methodptr;


/********************** data structures of UNICODE symbol *********************/

struct unicode {
	unicode   *hashlink; /* externe Verkettung f"ur die unicode-Hashtabelle */
	u4         key;      /* Hash-Schl"ussel (h"angt nur vom Text ab) */
	int        length;   /* L"ange des Textes */           
	u2        *text;     /* Zeiger auf den Text (jeder Buchstabe 16 Bit) */
	classinfo *class;    /* gegebenenfalls Referenz auf die Klasse dieses 
							Namens (oder NULL, wenn es keine solche gibt)  */
	struct java_objectheader *string;
	/* gegebenenfalls Referenz auf einen konstanten
	   String mit dem entsprechenden Wert */ 
};

/* Alle Unicode-Symbole werden in einer einzigen globalen Tabelle 
	   (Hashtabelle) verwaltet, jedes Symbol wird nur einmal angelegt.
	   -> Speicherersparnis, und "Uberpr"ufung auf Gleichheit durch einfachen
	   Zeigervergleich */


/************ data structures of remaining constant pool entries **************/


typedef struct {
	classinfo *class;
	unicode   *name;
	unicode   *descriptor;
} constant_FMIref;


typedef struct {
	s4 value;
} constant_integer;
	
typedef struct {
	float value;
} constant_float;

typedef struct {
	s8 value;
} constant_long;
	
typedef struct {
	double value;
} constant_double;


typedef struct {
	unicode *name;
	unicode *descriptor;
} constant_nameandtype;


typedef struct constant_arraydescriptor {
	int arraytype;
	classinfo *objectclass;
	struct constant_arraydescriptor *elementdescriptor;
} constant_arraydescriptor;

/* Mit einem Arraydescriptor kann ein Array-Typ dargestellt werden.
	   Bei normalen Arrays (z.B. Array von Bytes,...) gen"ugt dazu,
       dass das Feld arraytype die entsprechende Kennzahl enth"alt
       (z.B. ARRAYTYPE_BYTE).
       Bei Arrays von Objekten (arraytype=ARRAYTYPE_OBJECT) muss das 
       Feld objectclass auf die Klassenstruktur der m"oglichen
       Element-Objekte zeigen.
       Bei Arrays von Arrays (arraytype=ARRAYTYPE_ARRAY) muss das
       Feld elementdescriptor auf eine weiter arraydescriptor-Struktur
       zeigen, die die Element-Typen beschreibt.
	*/



/********* Anmerkungen zum Constant-Pool:

	Die Typen der Eintr"age in den Constant-Pool werden durch die oben
	definierten CONSTANT_.. Werte angegeben.
	Bei allen Typen muss zus"atzlich noch eine Datenstruktur hinzugef"ugt
	werden, die den wirklichen Wert angibt.
	Bei manchen Typen reicht es, einen Verweis auf eine schon bereits
	existierende Struktur (z.B. unicode-Texte) einzutragen, bei anderen
	muss diese Struktur erst extra erzeugt werden.
	Ich habe folgende Datenstrukturen f"ur diese Typen verwendet:
	
		Typ                      Struktur                    extra erzeugt?
	----------------------------------------------------------------------
    CONSTANT_Class               classinfo                         nein
    CONSTANT_Fieldref            constant_FMIref                     ja
    CONSTANT_Methodref           constant_FMIref                     ja
    CONSTANT_InterfaceMethodref  constant_FMIref                     ja
    CONSTANT_String              unicode                           nein
    CONSTANT_Integer             constant_integer                    ja
    CONSTANT_Float               constant_float                      ja
    CONSTANT_Long                constant_long                       ja
    CONSTANT_Double              constant_double                     ja
    CONSTANT_NameAndType         constant_nameandtype                ja
    CONSTANT_Utf8                unicode                           nein
    CONSTANT_Arraydescriptor     constant_arraydescriptor            ja
    CONSTANT_UNUSED              -

*******************************/



/***************** Die Datenstrukturen fuer das Laufzeitsystem ***************/


	/********* Objekte **********

	Alle Objekte (und Arrays), die am Heap gespeichert werden, m"ussen eine	
	folgende spezielle Datenstruktur ganz vorne stehen haben: 

	*/

typedef struct java_objectheader {    /* Der Header f"ur alle Objekte */
	vftbl *vftbl;                     /* Zeiger auf die Function Table */
} java_objectheader;



/********* Arrays ***********
	
	Alle Arrays in Java sind auch gleichzeitig Objekte (d.h. sie haben auch
	den obligatorischen Object-Header und darin einen Verweis auf eine Klasse)
	Es gibt aber (der Einfachheit halber) nur eine einzige Klasse f"ur alle
	m"oglichen Typen von Arrays, deshalb wird der tats"achliche Typ in einem
	Feld im Array-Objekt selbst gespeichert.
	Die Typen sind: */

#define ARRAYTYPE_INT      0
#define ARRAYTYPE_LONG     1
#define ARRAYTYPE_FLOAT    2
#define ARRAYTYPE_DOUBLE   3
#define ARRAYTYPE_BYTE     4
#define ARRAYTYPE_CHAR     5
#define ARRAYTYPE_SHORT    6
#define ARRAYTYPE_BOOLEAN  7
#define ARRAYTYPE_OBJECT   8
#define ARRAYTYPE_ARRAY    9


/** Der Header f"ur ein Java-Array **/

typedef struct java_arrayheader {  /* Der Arrayheader f"ur alle Arrays */
	java_objectheader objheader;       /* Der Object-Header */
	s4 size;                           /* Gr"osse des Arrays */
	s4 arraytype;                      /* Typ der Elemente */
} java_arrayheader;



/** Die Unterschiedlichen Strukturen f"ur alle Typen von Arrays **/

typedef struct java_chararray {
	java_arrayheader header;
	u2 data[1];
} java_chararray;

typedef struct java_floatheader {
	java_arrayheader header;
	float data[1];
} java_floatarray;

typedef struct java_doublearray {
	java_arrayheader header;
	double data[1];
} java_doublearray;


/* achtung: die beiden Stukturen booleanarray und bytearray m"ussen 
      identisches memory-layout haben, weil mit den selben Funktionen 
      darauf zugegriffen wird */

typedef struct java_booleanarray {
	java_arrayheader header;
	u1 data[1];
} java_booleanarray;

typedef struct java_bytearray {
	java_arrayheader header;
	s1 data[1];
} java_bytearray;

typedef struct java_shortarray {
	java_arrayheader header;
	s2 data[1];
} java_shortarray;

typedef struct java_intarray {
	java_arrayheader header;
	s4 data[1];
} java_intarray;

typedef struct java_longarray {
	java_arrayheader header;
	s8 data[1];
} java_longarray;


/* ACHTUNG: die beiden folgenden Strukturen m"ussen unbedingt gleiches
	   Memory-Layout haben, weil mit ein und der selben Funktion auf die
	   data-Eintr"age beider Typen zugegriffen wird !!!! */

typedef struct java_objectarray {
	java_arrayheader header;
	classinfo *elementtype;
	java_objectheader *data[1];
} java_objectarray;

typedef struct java_arrayarray {
	java_arrayheader header;
	constant_arraydescriptor *elementdescriptor;
	java_arrayheader *data[1];
} java_arrayarray;




/************** Strukturen f"ur Klassen, Felder & Methoden *****************/


    /*** Struktur: fieldinfo ***/

typedef struct fieldinfo {   /* Struktur f"ur ein Feld einer Klasse */
	s4       flags;              /* die ACC-Flags */
	s4       type;               /* Grunddatentyp */
	unicode *name;               /* Name des Felds */
	unicode *descriptor;         /* Typedescriptor in JavaVM-Form */
	
	s4       offset;             /* Offset vom Anfang des Objektes */
	/* (bei Instanzvariablen) */

	union {                      /* Speicher f"ur den Wert */
		s4 i;                    /* (bei Klassenvariablen) */    
		s8 l;
		float f;
		double d;
		void *a; 
	} value;
		
} fieldinfo;


/*** Struktur: exceptiontable ***/

typedef struct exceptiontable {  /* Exceptionhandler-Eintrag in einer Methode */ 
	s4         startpc;            /* Anfang des G"ultigkeitsbereichs */
	s4         endpc;              /* Ende des Bereichs (exklusive) */
	s4         handlerpc;          /* JavaVM-Position des Handlers */
	classinfo *catchtype;          /* Typ der behandelten Exceptions (oder 
									  NULL, wenn alle behandelt werden sollen) */
} exceptiontable;



/*** Struktur: methodinfo ***/

typedef struct methodinfo {  /* Struktur f"ur eine Methode einer Klasse */
	s4	       flags;            /* die ACC-Flags */
	unicode   *name;             /* Name der Methode */
	unicode   *descriptor;       /* der JavaVM-Descriptorstring f"ur Methoden */
	s4        returntype;        /* only temporary valid, return type */
	s4        paramcount;        /* only temporary valid, number of parameters */
	u1        *paramtypes;       /* only temporary valid, parameter types */
	classinfo *class;            /* Die Klasse, der die Methode geh"ort */
	u4         vftblindex;       /* Index dieser Methode f"ur die Virtual
									Function Table (wenn es keine statische
									Methode ist) */

	s4	 maxstack;                  /* maximale Stacktiefe des JavaVM-Codes */
	s4   maxlocals;                 /* maximale Anzahl der JavaVM-Variablen */
	u4   jcodelength;               /* L"ange des JavaVM-Codes */
	u1  *jcode;                     /* und Zeiger auf den JavaVM-Code */

	s4   exceptiontablelength;      /* L"ange der Exceptintable */
	exceptiontable *exceptiontable; /* Die Exceptiontable selber */

	u1  *stubroutine;               /* STUB-Routine for compiling or calling 
									   natives */	
	u4   mcodelength;               /* L"ange des generierten Maschinencodes */
	u1  *mcode;                     /* Zeiger auf den Maschinencode */
	u1  *entrypoint;                /* Entrypoint to the Maschine-Code */	
} methodinfo;


/*** Struktur: classinfo ***/

struct classinfo {           /* Datenstruktur f"ur eine Klasse */
	java_objectheader header;     /* Weil auch Klassen als Objekte angesprochen 
									 werden */

	s4 flags;                     /* Die ACC-Flags */
	unicode *name;                /* Name der Klasse */ 
	
	s4       cpcount;             /* Anzahl der Eintr"age im Constant-Pool */
	u1      *cptags;              /* Die TAGs f"ur den Constant-Pool */
	voidptr *cpinfos;             /* Die Zeiger auf die Info-Strukturen */
	
	classinfo *super;             /* Zeiger auf die "ubergeordnete Klasse */
	
	s4          interfacescount;  /* Anzahl der Interfaces */
	classinfo **interfaces;       /* Zeiger auf die Interfaces */
	
	s4          fieldscount;      /* Anzahl der Felder */
	fieldinfo  *fields;           /* Die Tabelle der Felder */
	
	s4          methodscount;     /* Anzahl der Methoden */
	methodinfo *methods;          /* Die Tabelle der Methoden */
	
	
	listnode    listnode;         /* Verkettungsstruktur (f"ur Listen) */

	bool        initialized;      /* true, wenn Klasse bereits Initialisiert */ 
	bool        linked;           /* wird von `class_link` auf true gesetzt */
	s4			index;            /* Hierarchietiefe  (bei normalen Klassen)  
									 oder fortlaufende Nummer (bei Interfaces)*/ 
	u4          instancesize;     /* Gr"osse eines Objektes dieser Klasse */

	vftbl       *vftbl;

	methodinfo* finalizer;        /* Finalizer-Methode f"ur die Klasse */
#ifdef JIT_MARKER_SUPPORT
	methodinfo* marker; 
#endif
};
	

struct vftbl {
	classinfo  *class;            /* Class, the function table belongs to */

	s4          vftbllength;      /* L"aenge der Virtual Function Table */

	s4          interfacetablelength;   /* L"ange der Interfacetable */   
	u4         *interfacevftbllength;   /* -> siehe unten */   
	methodptr **interfacevftbl;
	
	methodptr   table[1];
};

/*********** Anmerkungen zur Interfacetable: 

	"Ahnlich wie die 'normalen' virtuellen Methoden k"onnen auch die
	Interface-Methoden mit Hilfe einer Art Virtual Function Table 
	aufgerufen werden.
	Dazu werden alle Interfaces im System fortlaufend nummeriert (beginnend
	bei 0), und f"ur jede Klasse wird eine ganze Tabelle von 
	Virtual Function Tables erzeugt, n"amlich zu jedem Interface, das die
	Klasse implementiert, eine.

	z.B. Nehmen wir an, eine Klasse implementiert zwei Interfaces (die durch
	die Nummerierung die Indizes 0 und 3 bekommen haben)

	Das sieht dann ungef"ahr so aus:
		                --------------           ------------- 
	interfacevftbl ---> | Eintrag 0  |---------> | Methode 0 |---> Methode X 
                        | Eintrag 1  |--> NULL   | Methode 1 |---> Methode Y
                        | Eintrag 2  |--> NULL   | Methode 2 |---> Methode Z
                        | Eintrag 3  |-----+     ------------- 
                        --------------     |
                                           +---> -------------
                                                 | Methode 0 |---> Methode X
                                                 | Methode 1 |---> Methode A
                                                 -------------
                              ---------------
	interfacevftlblength ---> | Wert 0 = 3  |
	                          | Wert 1 = 0  |
	                          | Wert 2 = 0  |
	                          | Wert 3 = 2  |
	                          ---------------

	Der Aufruf einer Interface-Methode geht dann so vor sich:
	Zur Compilezeit steht der Index (i) des Interfaces und die Stelle (s), wo
	in der entsprechenden Untertabelle die Methode eingetragen ist, schon fest.
	Also muss zur Laufzeit nur mehr der n-te Eintrag aus der Interfacetable 
	gelesen werden, von dieser Tabelle aus wird der s-te Eintrag geholt,
	und diese Methode wird angesprungen.

****************/



/************************* Referenzen auf die wichtigen Systemklassen ********************/

extern classinfo *class_java_lang_Object;
extern classinfo *class_java_lang_String;
extern classinfo *class_java_lang_ClassCastException;
extern classinfo *class_java_lang_NullPointerException;
extern classinfo *class_java_lang_ArrayIndexOutOfBoundsException;
extern classinfo *class_java_lang_NegativeArraySizeException;
extern classinfo *class_java_lang_OutOfMemoryError;
extern classinfo *class_java_lang_ArithmeticException;
extern classinfo *class_java_lang_ArrayStoreException;
extern classinfo *class_java_lang_ThreadDeath;              /* schani */
 
extern classinfo *class_array;


/********************** Vorgefertigte Instanzen einiger Systemklassen ********************/

extern java_objectheader *proto_java_lang_ClassCastException;
extern java_objectheader *proto_java_lang_NullPointerException;
extern java_objectheader *proto_java_lang_ArrayIndexOutOfBoundsException;
extern java_objectheader *proto_java_lang_NegativeArraySizeException;
extern java_objectheader *proto_java_lang_OutOfMemoryError;
extern java_objectheader *proto_java_lang_ArithmeticException;
extern java_objectheader *proto_java_lang_ArrayStoreException;
extern java_objectheader *proto_java_lang_ThreadDeath;      /* schani */


/********************** flag variables *********************/

extern bool compileall;
extern bool runverbose;         
extern bool verbose;         
                                

/********************** trace variables ********************/

extern int count_class_infos;
extern int count_const_pool_len;
extern int count_vftbl_len;
extern int count_unicode_len;
extern int count_all_methods;
extern int count_vmcode_len;
extern int count_extable_len;

#endif


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
