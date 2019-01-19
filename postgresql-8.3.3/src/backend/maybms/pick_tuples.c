/*-------------------------------------------------------------------------
 *
 * pick_tuples.c
 *	  Implementation of the query rewriting for pick-tuples. 
 *
 * Copyright (c) 2008, MayBMS Development Group
 *
 *-------------------------------------------------------------------------
 */
/* Pick-tuples construct is now only used for creating tuple-independent relations.
 * It's possible that this will be extended to accommodate other models of uncertain
 * relations.                                       
 *
 * The syntax of pick-tuples is defined as below:  
 * pick tuples from <certain-query> [independently] [with probability <expression>];
 *
 * (1)The certain-query can a select statement whose output is certain or simply a 
 * certain relation stored in the database. If it is a certain relation R, the parser
 * will transform it into "select * from R" so that the two cases can be handled
 * uniformly.
 * 
 * (2)“independently” is optional and the default. Right now, this filed is stored by the parser
 * in the pickingType of SelectStmt and it is always set to 'I'. 
 *
 * (3)“with probability <expression>” is optional; if left out, tuples are picked 
 * with probability 0.5. This is stored in prob of SelectStmt. 
 *
 * The implementation of pick-tuples is simpler than repair-key, because:
 * (1) We assume that the every two tuples are pairwise independent even they have
 * the same values on every columns. Therefore, if the users want to do have the 
 * values of all columns as a key, they should remove the duplicate themselves.
 * In comparison, repair-key perform duplicate removal on the input.
 * (2) A tuple-independent relation does not require domain column and this simplifies
 * job by eliminating the necessity to sum up the the weights within one block.   
 * 
 * The second difference between repair-key and pick-tuples is that in repair-key,
 * weight <= 1 is guaranteed by our rewriting, however, this should be checked in
 * pick-tuples. In repair-key, the sanity check raise an error if the weight is 
 * negative while in pick-tuples, the error range is expanded to include 
 * the situations where probability is larger than 1.
 *
 * The third difference is that the rewriting of repair-key is done on top of 
 * the input query while pick-tuples adds new targets to the target list of the 
 * input query directly. Therefore, the rewriting of pick-tuples is less 
 * error-prone.
 *
 * The current implementation is done by pure rewriting in three steps:
 * (1) Add the variable ID and domain ID to the target list of input query.
 *     The domain ID is constant 1.
 * (2) Add the probability expression to the target list of input query.
 * (3) Add a additional condition to filtered out tuples with probability 0.
 *
 * For example, let the schema of the relations be R(a,b,p), S(c,d,p) and the 
 * query be 
 *    pick tuples from (select a,c from R, S where d < 1 ) with probability R.p+S.p;
 * 
 * The rewriting is 
 *       select 
 *			a, c, 
 *			nextval('varid') as _v0, 
 *			1 as _d0, 
 *			test_from_0_to_1(R.p+S.p) as _p0
 *		 from 
 *		    R, S
 *		 where 
 *          d < 1 AND R.p+S.p <> 0;  
 *  
 */ 
 
 
#include "postgres.h"
#include "nodes/print.h"
#include "nodes/makefuncs.h"
#include "maybms/rewrite.h"

/* Local functions */
static SelectStmt *rewrite_pick_tuples(SelectStmt *sel);

/* #define TEST */

/* ProcessPickTuples
 *
 * This is the entry function for repair-key. This is similar to the processing
 * of repair-key. 
 * IMPORTANT NOTE: If either ProcessRepairKey or ProcessPickTuples is changed,
 * it's possible that the change is needed in the other.
 */
SelectStmt *
ProcessPickTuples(SelectStmt *sel)
{
	/* Rewrite the set operation. */
	sel = rewrite_set_operation(sel);	
	
	/* Remove the relation reference in the target list */
	sel->targetList = remove_relation_reference(sel->targetList);

	/* rewrite the select statement */
	sel = rewrite_pick_tuples(sel);
	
	sel->tabletype = TABLETYPE_INDEPENDENT;
	
	/* Set the type of the table being created */
	if (sel->intoClause != NULL)
		sel->intoClause->tabletype = TABLETYPE_INDEPENDENT;
		
	/* Record that the triple of condition columns are newly created */
	sel->isFromRepairKey = (bool *) palloc0(sizeof(bool));
	sel->isFromRepairKey[0] = true;
	
	return sel;		
}

/*  rewrite_pick_tuples
 *
 *  Rewrite pick-tuples statement. 
 */
static SelectStmt *
rewrite_pick_tuples(SelectStmt *sel)
{
	/* Add variable ID to the target list */
	sel->targetList = lappend(sel->targetList,
		type_cast_to_int4(make_ResTarget_with_func_char(catStrInt(VARNAME, 0), "nextval", VARIDSEQ)));
	
	sel->targetList = lappend(sel->targetList, 
		make_ResTarget_with_int(catStrInt(DOMAINNAME, 0), 1));
	
	/* Add the probability to the target list */
	if (sel->prob != NULL)
	{
		/* Put the cast value in the test function */
		ResTarget 	*res = type_cast_weight_by(sel->prob);
		FuncCall	*func = makeNode(FuncCall);
		A_Const *con = (A_Const *) makeNode(A_Const);
		
		func->funcname = list_make1(makeString(TESTFROMZEROTOONE));
		func->args = list_make1(res->val);
		res->val = (Node *) func;
		
		sel->targetList = lappend(sel->targetList, res);
		
		/* Add the constraint that the weight is not 0 */
		fill_where_clause(sel);
		
		/* Make a constant 0 */
		con->val.type = T_Float;
		con->val.val.str = "0";
		
		sel->whereClause = 
			(Node *) makeA_Expr(AEXPR_AND, NULL, sel->whereClause, 
				(Node *) makeA_Expr(AEXPR_OP, list_make1(makeString("<>")), 
					sel->prob->val, (Node *) con, 0), 0);
	}
	/* If the probability is NULL, set it to 0.5. */
	else
		sel->targetList = lappend(sel->targetList, make_ResTarget_with_float("_p0","0.5"));

	#ifdef TEST
		myLog("********************pick tuples\n");
		myLog( pretty_format_node_dump(nodeToString(sel)));
	#endif

	return sel;
}
