.class public test_verify_ok_jsr
.super java/lang/Object

; ======================================================================

.method public <init>()V
   aload_0
   invokenonvirtual java/lang/Object/<init>()V
   return
.end method

; ======================================================================

.method public static checkI(I)V
	.limit locals 1
	.limit stack 10
	getstatic java/lang/System/out Ljava/io/PrintStream;
	iload_0
	invokevirtual java/io/PrintStream/println(I)V
	return
.end method

; ======================================================================

.method public static main([Ljava/lang/String;)V
	.limit stack 2
	.limit locals 3

	ldc 35
	istore 1

	aload 0
	ifnull force_basic_block_boundary

	; --------------------------------------------------

	jsr sbr_1

	; --------------------------------------------------

force_basic_block_boundary:

	iload 1
	invokestatic test_verify_ok_jsr/checkI(I)V
	; OUTPUT: 777

	return
	
sbr_1:
	astore 2
	ldc 777
	istore 1
	ret 2

.end method
