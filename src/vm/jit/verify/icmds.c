/* src/vm/jit/verify/icmds.c - ICMD-specific type checking code

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

#if 0 /* (needed for code examples in the following comment) */
/******************************************************************************/
/* This file contains ICMD-specific code for type checking and type
 * inference. It is an input file for the verifier generator
 * (src/vm/jit/verify/generate.pl). The verifier generator creates
 * code for two compiler passes:
 *     - stack-based type-infering verification
 *     - vasiables-based type-infering verification
 *
 * The rest of this file must consist of "case" clauses starting in
 * the first column. Each clause can be marked with tags like this:
 *
 */          case ICMD_CONSTANT: /* {TAG, TAG, ...} */
/*
 * This must be on one line. The following tags are defined:
 *     STACKBASED..........use this clause for the stack-based verifier
 *     VARIABLESBASED......use this clause for the variables-based verifier
 *
 * If no tag is specified, {STACKBASED,VARIABLESBASED} is assumed.
 *
 * There are also tags that can be used inside a clause like this:
 *
 */          /* {TAG} */
/*
 * The following tags are defined within clauses:
 *     RESULTNOW...........generate code for modelling the stack action
 *                         _before_ the user-defined code in the clause
 *                         (Default is to model the stack action afterwards.)
 *
 * The following macros are pre-defined:
 *
 *     TYPECHECK_STACKBASED.......iff compiling the stack-based verifier
 *     TYPECHECK_VARIABLESBASED...iff compiling the variables-based verifier
 *
/******************************************************************************/
#endif /* (end #if 0) */


/* this marker is needed by generate.pl: */
/* {START_OF_CODE} */

	/****************************************/
	/* MOVE/COPY                            */

	/* We just need to copy the typeinfo */
	/* for slots containing addresses.   */

	/* (These are only used by the variables based verifier.) */

case ICMD_MOVE: /* {VARIABLESBASED} */
case ICMD_COPY: /* {VARIABLESBASED} */
	TYPECHECK_COUNT(stat_ins_stack);
	COPYTYPE(IPTR->s1, IPTR->dst);
	DST->type = OP1->type;
	break;

	/****************************************/
	/* LOADING ADDRESS FROM VARIABLE        */

case ICMD_ALOAD:
	TYPECHECK_COUNT(stat_ins_aload);

	/* loading a returnAddress is not allowed */
	if (!TYPEDESC_IS_REFERENCE(*OP1)) {
		VERIFY_ERROR("illegal instruction: ALOAD loading non-reference");
	}
	TYPEINFO_COPY(OP1->typeinfo,DST->typeinfo);
	break;

	/****************************************/
	/* STORING ADDRESS TO VARIABLE          */

case ICMD_ASTORE:
	TYPEINFO_COPY(OP1->typeinfo, DST->typeinfo);
	break;

	/****************************************/
	/* LOADING ADDRESS FROM ARRAY           */

case ICMD_AALOAD:
	if (!TYPEINFO_MAYBE_ARRAY_OF_REFS(OP1->typeinfo))
		VERIFY_ERROR("illegal instruction: AALOAD on non-reference array");

	if (!typeinfo_init_component(&OP1->typeinfo,&DST->typeinfo))
		EXCEPTION;
	break;

	/****************************************/
	/* FIELD ACCESS                         */

case ICMD_PUTFIELD: /* {STACKBASED} */
	CHECK_STACK_DEPTH(2);
	if (!IS_CAT1(stack[0])) {
		CHECK_STACK_DEPTH(3);
		stack -= 1;
	}
	CHECK_STACK_TYPE(stack[-1], TYPE_ADR);
	stack = typecheck_stackbased_verify_fieldaccess(STATE, stack-1, stack, stack-2);
	if (stack == NULL)
		EXCEPTION;
	break;

case ICMD_PUTSTATIC: /* {STACKBASED} */
	CHECK_STACK_DEPTH(1);
	if (!IS_CAT1(stack[0])) {
		/* (stack depth >= 2 is guaranteed) */
		stack -= 1;
	}
	stack = typecheck_stackbased_verify_fieldaccess(STATE, NULL, stack, stack-1);
	if (stack == NULL)
		EXCEPTION;
	break;

case ICMD_GETFIELD: /* {STACKBASED} */
	CHECK_STACK_TYPE(stack[0], TYPE_ADR);
	stack = typecheck_stackbased_verify_fieldaccess(STATE, stack, NULL, stack-1);
	if (stack == NULL)
		EXCEPTION;
	break;

case ICMD_GETSTATIC:      /* {STACKBASED} */
	stack = typecheck_stackbased_verify_fieldaccess(STATE, NULL, NULL, stack);
	if (stack == NULL)
		EXCEPTION;
	break;

case ICMD_PUTFIELD:       /* {VARIABLESBASED} */
	if (!verify_fieldaccess(state, VAROP(iptr->s1), VAROP(iptr->sx.s23.s2)))
		return false;
	maythrow = true;
	break;

case ICMD_PUTSTATIC:      /* {VARIABLESBASED} */
	if (!verify_fieldaccess(state, NULL, VAROP(iptr->s1)))
		return false;
	maythrow = true;
	break;

case ICMD_PUTFIELDCONST:  /* {VARIABLESBASED} */
	/* XXX this mess will go away with const operands */
	INSTRUCTION_GET_FIELDREF(state->iptr, fieldref);
	constvalue.type = fieldref->parseddesc.fd->type;
	if (IS_ADR_TYPE(constvalue.type)) {
		if (state->iptr->sx.val.anyptr) {
			classinfo *cc = (state->iptr->flags.bits & INS_FLAG_CLASS)
				? class_java_lang_Class : class_java_lang_String;
			assert(cc);
			assert(cc->state & CLASS_LINKED);
			typeinfo_init_classinfo(&(constvalue.typeinfo), cc);
		}
		else {
			TYPEINFO_INIT_NULLTYPE(constvalue.typeinfo);
		}
	}
	if (!verify_fieldaccess(state, VAROP(iptr->s1), &constvalue))
		return false;
	maythrow = true;
	break;

case ICMD_PUTSTATICCONST: /* {VARIABLESBASED} */
	/* XXX this mess will go away with const operands */
	INSTRUCTION_GET_FIELDREF(state->iptr, fieldref);
	constvalue.type = fieldref->parseddesc.fd->type;
	if (IS_ADR_TYPE(constvalue.type)) {
		if (state->iptr->sx.val.anyptr) {
			classinfo *cc = (state->iptr->flags.bits & INS_FLAG_CLASS)
				? class_java_lang_Class : class_java_lang_String;
			assert(cc);
			assert(cc->state & CLASS_LINKED);
			typeinfo_init_classinfo(&(constvalue.typeinfo), cc);
		}
		else {
			TYPEINFO_INIT_NULLTYPE(constvalue.typeinfo);
		}
	}
	if (!verify_fieldaccess(state, NULL, &constvalue))
		return false;
	maythrow = true;
	break;

case ICMD_GETFIELD:       /* {VARIABLESBASED} */
	if (!verify_fieldaccess(state, VAROP(iptr->s1), NULL))
		return false;
	maythrow = true;
	break;

case ICMD_GETSTATIC:      /* {VARIABLESBASED} */
	if (!verify_fieldaccess(state, NULL, NULL))
		return false;
	maythrow = true;
	break;

	/****************************************/
	/* PRIMITIVE ARRAY ACCESS               */

case ICMD_ARRAYLENGTH:
	if (!TYPEINFO_MAYBE_ARRAY(OP1->typeinfo)
			&& OP1->typeinfo.typeclass.cls != pseudo_class_Arraystub)
		VERIFY_ERROR("illegal instruction: ARRAYLENGTH on non-array");
	break;

case ICMD_BALOAD:
	if (!TYPEINFO_MAYBE_PRIMITIVE_ARRAY(OP1->typeinfo,ARRAYTYPE_BOOLEAN)
			&& !TYPEINFO_MAYBE_PRIMITIVE_ARRAY(OP1->typeinfo,ARRAYTYPE_BYTE))
		VERIFY_ERROR("Array type mismatch");
	break;

case ICMD_CALOAD:
	if (!TYPEINFO_MAYBE_PRIMITIVE_ARRAY(OP1->typeinfo,ARRAYTYPE_CHAR))
		VERIFY_ERROR("Array type mismatch");
	break;

case ICMD_DALOAD:
	if (!TYPEINFO_MAYBE_PRIMITIVE_ARRAY(OP1->typeinfo,ARRAYTYPE_DOUBLE))
		VERIFY_ERROR("Array type mismatch");
	break;

case ICMD_FALOAD:
	if (!TYPEINFO_MAYBE_PRIMITIVE_ARRAY(OP1->typeinfo,ARRAYTYPE_FLOAT))
		VERIFY_ERROR("Array type mismatch");
	break;

case ICMD_IALOAD:
	if (!TYPEINFO_MAYBE_PRIMITIVE_ARRAY(OP1->typeinfo,ARRAYTYPE_INT))
		VERIFY_ERROR("Array type mismatch");
	break;

case ICMD_SALOAD:
	if (!TYPEINFO_MAYBE_PRIMITIVE_ARRAY(OP1->typeinfo,ARRAYTYPE_SHORT))
		VERIFY_ERROR("Array type mismatch");
	break;

case ICMD_LALOAD:
	if (!TYPEINFO_MAYBE_PRIMITIVE_ARRAY(OP1->typeinfo,ARRAYTYPE_LONG))
		VERIFY_ERROR("Array type mismatch");
	break;

case ICMD_BASTORE:
	if (!TYPEINFO_MAYBE_PRIMITIVE_ARRAY(OP1->typeinfo,ARRAYTYPE_BOOLEAN)
			&& !TYPEINFO_MAYBE_PRIMITIVE_ARRAY(OP1->typeinfo,ARRAYTYPE_BYTE))
		VERIFY_ERROR("Array type mismatch");
	break;

case ICMD_CASTORE:
	if (!TYPEINFO_MAYBE_PRIMITIVE_ARRAY(OP1->typeinfo,ARRAYTYPE_CHAR))
		VERIFY_ERROR("Array type mismatch");
	break;

case ICMD_DASTORE:
	if (!TYPEINFO_MAYBE_PRIMITIVE_ARRAY(OP1->typeinfo,ARRAYTYPE_DOUBLE))
		VERIFY_ERROR("Array type mismatch");
	break;

case ICMD_FASTORE:
	if (!TYPEINFO_MAYBE_PRIMITIVE_ARRAY(OP1->typeinfo,ARRAYTYPE_FLOAT))
		VERIFY_ERROR("Array type mismatch");
	break;

case ICMD_IASTORE:
	if (!TYPEINFO_MAYBE_PRIMITIVE_ARRAY(OP1->typeinfo,ARRAYTYPE_INT))
		VERIFY_ERROR("Array type mismatch");
	break;

case ICMD_SASTORE:
	if (!TYPEINFO_MAYBE_PRIMITIVE_ARRAY(OP1->typeinfo,ARRAYTYPE_SHORT))
		VERIFY_ERROR("Array type mismatch");
	break;

case ICMD_LASTORE:
	if (!TYPEINFO_MAYBE_PRIMITIVE_ARRAY(OP1->typeinfo,ARRAYTYPE_LONG))
		VERIFY_ERROR("Array type mismatch");
	break;

case ICMD_AASTORE:
	/* we just check the basic input types and that the           */
	/* destination is an array of references. Assignability to    */
	/* the actual array must be checked at runtime, each time the */
	/* instruction is performed. (See builtin_canstore.)          */
	if (!TYPEINFO_MAYBE_ARRAY_OF_REFS(OP1->typeinfo))
		VERIFY_ERROR("illegal instruction: AASTORE to non-reference array");
	break;

case ICMD_IASTORECONST:
	if (!TYPEINFO_MAYBE_PRIMITIVE_ARRAY(OP1->typeinfo, ARRAYTYPE_INT))
		VERIFY_ERROR("Array type mismatch");
	break;

case ICMD_LASTORECONST:
	if (!TYPEINFO_MAYBE_PRIMITIVE_ARRAY(OP1->typeinfo, ARRAYTYPE_LONG))
		VERIFY_ERROR("Array type mismatch");
	break;

case ICMD_BASTORECONST:
	if (!TYPEINFO_MAYBE_PRIMITIVE_ARRAY(OP1->typeinfo, ARRAYTYPE_BOOLEAN)
			&& !TYPEINFO_MAYBE_PRIMITIVE_ARRAY(OP1->typeinfo, ARRAYTYPE_BYTE))
		VERIFY_ERROR("Array type mismatch");
	break;

case ICMD_CASTORECONST:
	if (!TYPEINFO_MAYBE_PRIMITIVE_ARRAY(OP1->typeinfo, ARRAYTYPE_CHAR))
		VERIFY_ERROR("Array type mismatch");
	break;

case ICMD_SASTORECONST:
	if (!TYPEINFO_MAYBE_PRIMITIVE_ARRAY(OP1->typeinfo, ARRAYTYPE_SHORT))
		VERIFY_ERROR("Array type mismatch");
	break;

	/****************************************/
	/* ADDRESS CONSTANTS                    */

case ICMD_ACONST:
	if (IPTR->flags.bits & INS_FLAG_CLASS) {
		/* a java.lang.Class reference */
		TYPEINFO_INIT_JAVA_LANG_CLASS(DST->typeinfo,IPTR->sx.val.c);
	}
	else {
		if (IPTR->sx.val.anyptr == NULL)
			TYPEINFO_INIT_NULLTYPE(DST->typeinfo);
		else {
			/* string constant (or constant for builtin function) */
			typeinfo_init_classinfo(&(DST->typeinfo),class_java_lang_String);
		}
	}
	break;

	/****************************************/
	/* CHECKCAST AND INSTANCEOF             */

case ICMD_CHECKCAST:
	/* returnAddress is not allowed */
	if (!TYPEINFO_IS_REFERENCE(OP1->typeinfo))
		VERIFY_ERROR("Illegal instruction: CHECKCAST on non-reference");

	if (!typeinfo_init_class(&(DST->typeinfo),IPTR->sx.s23.s3.c))
		EXCEPTION;
	break;

case ICMD_INSTANCEOF:
	/* returnAddress is not allowed */
	if (!TYPEINFO_IS_REFERENCE(OP1->typeinfo))
		VERIFY_ERROR("Illegal instruction: INSTANCEOF on non-reference");
	break;

	/****************************************/
	/* BRANCH INSTRUCTIONS                  */

case ICMD_GOTO:
case ICMD_IFNULL:
case ICMD_IFNONNULL:
case ICMD_IFEQ:
case ICMD_IFNE:
case ICMD_IFLT:
case ICMD_IFGE:
case ICMD_IFGT:
case ICMD_IFLE:
case ICMD_IF_ICMPEQ:
case ICMD_IF_ICMPNE:
case ICMD_IF_ICMPLT:
case ICMD_IF_ICMPGE:
case ICMD_IF_ICMPGT:
case ICMD_IF_ICMPLE:
case ICMD_IF_ACMPEQ:
case ICMD_IF_ACMPNE:

case ICMD_IF_LEQ:
case ICMD_IF_LNE:
case ICMD_IF_LLT:
case ICMD_IF_LGE:
case ICMD_IF_LGT:
case ICMD_IF_LLE:

case ICMD_IF_LCMPEQ:
case ICMD_IF_LCMPNE:
case ICMD_IF_LCMPLT:
case ICMD_IF_LCMPGE:
case ICMD_IF_LCMPGT:
case ICMD_IF_LCMPLE:

case ICMD_IF_FCMPEQ:
case ICMD_IF_FCMPNE:

case ICMD_IF_FCMPL_LT:
case ICMD_IF_FCMPL_GE:
case ICMD_IF_FCMPL_GT:
case ICMD_IF_FCMPL_LE:

case ICMD_IF_FCMPG_LT:
case ICMD_IF_FCMPG_GE:
case ICMD_IF_FCMPG_GT:
case ICMD_IF_FCMPG_LE:

case ICMD_IF_DCMPEQ:
case ICMD_IF_DCMPNE:

case ICMD_IF_DCMPL_LT:
case ICMD_IF_DCMPL_GE:
case ICMD_IF_DCMPL_GT:
case ICMD_IF_DCMPL_LE:

case ICMD_IF_DCMPG_LT:
case ICMD_IF_DCMPG_GE:
case ICMD_IF_DCMPG_GT:
case ICMD_IF_DCMPG_LE:
	/* {RESULTNOW} */
	TYPECHECK_COUNT(stat_ins_branch);

	/* propagate stack and variables to the target block */
	REACH(IPTR->dst);
	break;

	/****************************************/
	/* SWITCHES                             */

case ICMD_TABLESWITCH:
	/* {RESULTNOW} */
	TYPECHECK_COUNT(stat_ins_switch);

	table = IPTR->dst.table;
	i = IPTR->sx.s23.s3.tablehigh
	- IPTR->sx.s23.s2.tablelow + 1 + 1; /* plus default */

	while (--i >= 0) {
		REACH(*table);
		table++;
	}

	LOG("switch done");
	break;

case ICMD_LOOKUPSWITCH:
	/* {RESULTNOW} */
	TYPECHECK_COUNT(stat_ins_switch);

	lookup = IPTR->dst.lookup;
	i = IPTR->sx.s23.s2.lookupcount;
	REACH(IPTR->sx.s23.s3.lookupdefault);

	while (--i >= 0) {
		REACH(lookup->target);
		lookup++;
	}

	LOG("switch done");
	break;


	/****************************************/
	/* ADDRESS RETURNS AND THROW            */

case ICMD_ATHROW:
	TYPECHECK_COUNT(stat_ins_athrow);
	r = typeinfo_is_assignable_to_class(&OP1->typeinfo,
			CLASSREF_OR_CLASSINFO(class_java_lang_Throwable));
	if (r == typecheck_FALSE)
		VERIFY_ERROR("illegal instruction: ATHROW on non-Throwable");
	if (r == typecheck_FAIL)
		EXCEPTION;
	if (r == typecheck_MAYBE) {
		/* the check has to be postponed. we need a patcher */
		TYPECHECK_COUNT(stat_ins_athrow_unresolved);
		IPTR->sx.s23.s2.uc = create_unresolved_class(
				METHOD,
				/* XXX make this more efficient, use class_java_lang_Throwable
				 * directly */
				class_get_classref(METHOD->class,utf_java_lang_Throwable),
				&OP1->typeinfo);
		IPTR->flags.bits |= INS_FLAG_UNRESOLVED;
	}
	break;

case ICMD_ARETURN:
	TYPECHECK_COUNT(stat_ins_areturn);
	if (!TYPEINFO_IS_REFERENCE(OP1->typeinfo))
		VERIFY_ERROR("illegal instruction: ARETURN on non-reference");

	if (STATE->returntype.type != TYPE_ADR
			|| (r = typeinfo_is_assignable(&OP1->typeinfo,&(STATE->returntype.typeinfo)))
			== typecheck_FALSE)
		VERIFY_ERROR("Return type mismatch");
	if (r == typecheck_FAIL)
		EXCEPTION;
	if (r == typecheck_MAYBE) {
		/* the check has to be postponed, we need a patcher */
		TYPECHECK_COUNT(stat_ins_areturn_unresolved);
		IPTR->sx.s23.s2.uc = create_unresolved_class(
				METHOD,
				METHOD->parseddesc->returntype.classref,
				&OP1->typeinfo);
		IPTR->flags.bits |= INS_FLAG_UNRESOLVED;
	}
	goto return_tail;

	/****************************************/
	/* PRIMITIVE RETURNS                    */

case ICMD_IRETURN:
	if (STATE->returntype.type != TYPE_INT)
		VERIFY_ERROR("Return type mismatch");
	goto return_tail;

case ICMD_LRETURN:
	if (STATE->returntype.type != TYPE_LNG)
		VERIFY_ERROR("Return type mismatch");
	goto return_tail;

case ICMD_FRETURN:
	if (STATE->returntype.type != TYPE_FLT)
		VERIFY_ERROR("Return type mismatch");
	goto return_tail;

case ICMD_DRETURN:
	if (STATE->returntype.type != TYPE_DBL)
		VERIFY_ERROR("Return type mismatch");
	goto return_tail;

case ICMD_RETURN:
	if (STATE->returntype.type != TYPE_VOID)
		VERIFY_ERROR("Return type mismatch");

return_tail:
	TYPECHECK_COUNT(stat_ins_primitive_return);

	if (STATE->initmethod && METHOD->class != class_java_lang_Object) {
		/* Check if the 'this' instance has been initialized. */
		LOG("Checking <init> marker");
#if defined(TYPECHECK_VARIABLESBASED)
		if (!typevector_checktype(jd->var,STATE->numlocals-1,TYPE_INT))
#else
		if (STATE->locals[STATE->numlocals-1].type != TYPE_INT)
#endif
			VERIFY_ERROR("<init> method does not initialize 'this'");
	}
	break;

	/****************************************/
	/* SUBROUTINE INSTRUCTIONS              */

case ICMD_JSR: /* {VARIABLESBASED} */
	TYPEINFO_INIT_RETURNADDRESS(DST->typeinfo, BPTR->next);
	REACH(IPTR->sx.s23.s3.jsrtarget);
	break;

case ICMD_JSR: /* {STACKBASED} */
	/* {RESULTNOW} */
	tbptr = BLOCK_OF(IPTR->sx.s23.s3.jsrtarget.insindex);

	TYPEINFO_INIT_RETURNADDRESS(stack[0].typeinfo, tbptr);
	REACH_BLOCK(tbptr);

	stack = typecheck_stackbased_jsr(STATE, stack, stackfloor);
	if (stack == NULL)
		EXCEPTION;
	break;

case ICMD_RET: /* {VARIABLESBASED} */
	/* check returnAddress variable */
	if (!typevector_checkretaddr(jd->var,IPTR->s1.varindex))
		VERIFY_ERROR("illegal instruction: RET using non-returnAddress variable");
	REACH(IPTR->dst);
	break;

case ICMD_RET: /* {STACKBASED} */
	/* {RESULTNOW} */
	CHECK_LOCAL_TYPE(IPTR->s1.varindex, TYPE_RET);
	if (!TYPEINFO_IS_PRIMITIVE(STATE->locals[IPTR->s1.varindex].typeinfo))
		VERIFY_ERROR("illegal instruction: RET using non-returnAddress variable");

	if (!typecheck_stackbased_ret(STATE, stack, stackfloor))
		EXCEPTION;
	break;

	/****************************************/
	/* INVOKATIONS                          */

case ICMD_INVOKEVIRTUAL:   /* {VARIABLESBASED} */
case ICMD_INVOKESPECIAL:   /* {VARIABLESBASED} */
case ICMD_INVOKESTATIC:    /* {VARIABLESBASED} */
case ICMD_INVOKEINTERFACE: /* {VARIABLESBASED} */
	TYPECHECK_COUNT(stat_ins_invoke);
	if (!verify_invocation(state))
		EXCEPTION;
	TYPECHECK_COUNTIF(INSTRUCTION_IS_UNRESOLVED(IPTR), stat_ins_invoke_unresolved);
	break;

case ICMD_INVOKEVIRTUAL:   /* {STACKBASED} */
case ICMD_INVOKESPECIAL:   /* {STACKBASED} */
case ICMD_INVOKESTATIC:    /* {STACKBASED} */
case ICMD_INVOKEINTERFACE: /* {STACKBASED} */
	TYPECHECK_COUNT(stat_ins_invoke);

	INSTRUCTION_GET_METHODDESC(IPTR, md);
	CHECK_STACK_DEPTH(md->paramslots);

	if (!typecheck_stackbased_verify_invocation(STATE, stack, stackfloor))
		EXCEPTION;

	stack -= md->paramslots;

	if (md->returntype.type != TYPE_VOID) {
		if (IS_2_WORD_TYPE(md->returntype.type)) {
			CHECK_STACK_SPACE(2);
			stack += 2;
			stack[0].type = TYPE_VOID;
			stack[-1].type = md->returntype.type;
		}
		else {
			CHECK_STACK_SPACE(1);
			stack += 1;
			stack[0].type = md->returntype.type;
		}
	}
	TYPECHECK_COUNTIF(INSTRUCTION_IS_UNRESOLVED(IPTR), stat_ins_invoke_unresolved);
	break;

	/****************************************/
	/* MULTIANEWARRAY                       */

case ICMD_MULTIANEWARRAY: /* {VARIABLESBASED} */
	if (!verify_multianewarray(STATE))
		EXCEPTION;
	break;

case ICMD_MULTIANEWARRAY: /* {STACKBASED} */
	if (!typecheck_stackbased_multianewarray(STATE, stack, stackfloor))
		EXCEPTION;
	stack -= (IPTR->s1.argcount - 1);
	stack[0].type = TYPE_ADR;
	break;

	/****************************************/
	/* BUILTINS                             */

case ICMD_BUILTIN: /* {VARIABLESBASED} */
	TYPECHECK_COUNT(stat_ins_builtin);
	if (!verify_builtin(state))
		EXCEPTION;
	break;

case ICMD_BUILTIN: /* {STACKBASED} */
	TYPECHECK_COUNT(stat_ins_builtin);
	if (!typecheck_stackbased_verify_builtin(STATE, stack, stackfloor))
		EXCEPTION;

	/* pop operands and push return value */
	{
		u1 rtype = IPTR->sx.s23.s3.bte->md->returntype.type;
		stack -=  IPTR->sx.s23.s3.bte->md->paramslots;
		if (rtype != TYPE_VOID) {
			if (IS_2_WORD_TYPE(rtype))
				stack += 2;
			else
				stack += 1;
		}
	}
	break;

/* the following code is only used by the stackbased verifier */

case ICMD_POP: /* {STACKBASED} */
	/* we pop 1 */
	CHECK_CAT1(stack[0]);
	break;

case ICMD_POP2: /* {STACKBASED} */
	/* we pop either 11 or 2 */
	if (IS_CAT1(stack[0]))
		CHECK_CAT1(stack[-1]);
	break;

case ICMD_SWAP: /* {STACKBASED} */
	CHECK_CAT1(stack[0]);
	CHECK_CAT1(stack[-1]);

	COPY_SLOT(stack[ 0], temp     );
	COPY_SLOT(stack[-1], stack[ 0]);
	COPY_SLOT(temp     , stack[-1]);
	break;

case ICMD_DUP: /* {STACKBASED} */
	/* we dup 1 */
	CHECK_CAT1(stack[0]);

	COPY_SLOT(stack[ 0], stack[ 1]);
	break;

case ICMD_DUP_X1: /* {STACKBASED} */
	/* we dup 1 */
	CHECK_CAT1(stack[0]);
	/* we skip 1 */
	CHECK_CAT1(stack[-1]);

	COPY_SLOT(stack[ 0], stack[ 1]);
	COPY_SLOT(stack[-1], stack[ 0]);
	COPY_SLOT(stack[ 1], stack[-1]);
	break;

case ICMD_DUP_X2: /* {STACKBASED} */
	/* we dup 1 */
	CHECK_CAT1(stack[0]);
	/* we skip either 11 or 2 */
	if (IS_CAT1(stack[-1]))
		CHECK_CAT1(stack[-2]);

	COPY_SLOT(stack[ 0], stack[ 1]);
	COPY_SLOT(stack[-1], stack[ 0]);
	COPY_SLOT(stack[-2], stack[-1]);
	COPY_SLOT(stack[ 1], stack[-2]);
	break;

case ICMD_DUP2: /* {STACKBASED} */
	/* we dup either 11 or 2 */
	if (IS_CAT1(stack[0]))
		CHECK_CAT1(stack[-1]);

	COPY_SLOT(stack[ 0], stack[ 2]);
	COPY_SLOT(stack[-1], stack[ 1]);
	break;

case ICMD_DUP2_X1: /* {STACKBASED} */
	/* we dup either 11 or 2 */
	if (IS_CAT1(stack[0]))
		CHECK_CAT1(stack[-1]);
	/* we skip 1 */
	CHECK_CAT1(stack[-2]);

	COPY_SLOT(stack[ 0], stack[ 2]);
	COPY_SLOT(stack[-1], stack[ 1]);
	COPY_SLOT(stack[-2], stack[ 0]);
	COPY_SLOT(stack[ 2], stack[-1]);
	COPY_SLOT(stack[ 1], stack[-2]);
	break;

case ICMD_DUP2_X2: /* {STACKBASED} */
	/* we dup either 11 or 2 */
	if (IS_CAT1(stack[0]))
		CHECK_CAT1(stack[-1]);
	/* we skip either 11 or 2 */
	if (IS_CAT1(stack[-2]))
		CHECK_CAT1(stack[-3]);

	COPY_SLOT(stack[ 0], stack[ 2]);
	COPY_SLOT(stack[-1], stack[ 1]);
	COPY_SLOT(stack[-2], stack[ 0]);
	COPY_SLOT(stack[-3], stack[-1]);
	COPY_SLOT(stack[ 2], stack[-2]);
	COPY_SLOT(stack[ 1], stack[-3]);
	break;


/* this marker is needed by generate.pl: */
/* {END_OF_CODE} */

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
