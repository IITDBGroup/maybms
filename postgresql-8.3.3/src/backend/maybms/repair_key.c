/*-------------------------------------------------------------------------
 *
 * repair_key.c
 *	  implementation of the query rewriting for repair-key. 
 *
 *
 * Copyright (c) 2008, MayBMS Development Group
 *
 *-------------------------------------------------------------------------
 */
 
/* Repair-key is done purely by rewriting queries. Right now, the operation is 
 * compressed in only one query. 
 *
 * We consider the general situation in which the input is a bag in which the 
 * same tuple occurs several times. The rewriting will be much simpler in case 
 * the input is a set.
 * 
 * Logically, we separate repair-key into the following 4 steps:   
 *
 * (1)Eliminate the duplicates in the input relation or result of a select statement
 *    and calculate the corresponding weight of every tuple.
 * (2)On top of the result of (1), assign a domain value to every tuple.
 *    Note that we do NOT perform this in every group of tuples sharing the 
 *    same key because it is not feasible only via query rewriting. As a result, domain
 *    values are unique in the whole database, which satisfies the requirement that they
 *    are unique in every disjoint block. 
 * (3)On top of the result of (1), sum up the weights of every group of tuples 
 *    sharing the same key and assign a variable value to this group. This can be easily 
 *    achieved using aggregate function SUM. Note that, the results may be wrong without
 *    duplicate elimination performed first.
 * (4)Do a join on the input,results of (2) and (3) and calculate the weight of 
 *    a tuple by dividing the original weight by the sum in (2). 
 *
 * In addition to what is described above, the issue of non-positive weight 
 * values should also be taken into account. Right now, all tuples with weight 
 * 0 removed silently while a tuple with negative weight will stop the whole 
 * repair-key operation and issue an error message.
 *
 * Let the query of repair-key be:
 *   REPAIR KEY (keys) IN (SELECT target-list FROM ...) WEIGHT BY weight-expression; 
 *
 * The current implementation generates a query tree representing the following query:
 *
 *  SELECT target-list, _V, _D, _W / _S as _P FROM
 *   input 
 *   NATURAL JOIN
 *   ( SELECT key, SUM(_W) as _S, nextval('varid') as _V 
       FROM duplicate-elimination AS temp GROUP BY key ) AS var
 *   NATURAL JOIN 
 *   ( SELECT *, nextval('domid') as _D, _W 
       FROM duplicate-elimination as temp ) AS domain;
 *
 *  where input is (SELECT target-list FROM ...) and duplicate-elimination is
 *  SELECT DISTINCT target-list, test_nagative( cast( weight-expression as real ) ) AS _W 
 *  FROM input WHERE weight-expression <> 0.
 *
 *  There are several points worth noticing in duplicate-elimination:
 *  (1) The type casting of the weight-expression to a real is necessary because
 *      if both _W and _S are integers, _W / _S is also an integer. This will cause
 *      the bugs like 1 / 3 = 0. 
 *  (2) test_nagative is a scalar function that issues an error message if the 
 *      input is a negative, otherwise return the input. In postgres, an error 
 *      message stops the whole query so repair-key will not continue.
 *  (3) When weight-expression is missing, we assign a weight 1 to every 
 *      distinct tuple.
 *  (4) Tuples with weight as 0 are filtered in duplicate-elimination. 
 *
 *  In this approach, as the database is initialized, two sequences varid and 
 *  domid are created, which are used to generate variable and domain IDs in 
 *  the rewriting. They both begin from 1. Variable 0 is a reserved variable
 *  with binary distribution 0 and 1 whose probability is 0 and 1 respectively. 
 *
 *  One drawback of the above-mentioned implementation is that some tasks are
 *  performed several times because PostgreSQL can not identify the same sub-query
 *  and execute them more than once. For example, duplicate-elimination are 
 *  included twice and according to the query plans of repair-key, it is actually 
 *  executed twice in the plan. A possible approach is to dump the temporary 
 *  results to the disc. This is especially efficient should the input query of 
 *  repair-key is extremely huge.
 *
 */
 
#include "postgres.h"
#include "nodes/print.h"
#include "nodes/pg_list.h"
#include "nodes/makefuncs.h"
#include "access/heapam.h"
#include "access/genam.h"
#include "catalog/pg_constraint.h"
#include "catalog/pg_type.h"
#include "catalog/indexing.h"
#include "maybms/rewrite.h"
#include "maybms/rewrite_updates.h"
#include "maybms/supported.h"
#include "utils/fmgroids.h"
#include "utils/array.h"
#include "utils/syscache.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"

/* Local functions */

static A_Expr *make_division_expr(char *dividend, char *divisor);
static List *get_repair_key_target_list(SelectStmt *sel);
static List *target_list_to_ColumnRef(List *target_list);
static Node *make_natural_join(Node *table1, Node *table2);
static ResTarget *make_ResTarget_with_AExpr(char *resname, A_Expr *expr);
static ResTarget *make_ResTarget_with_Func_ColumnRef(char *resname, char *funcname, char *columnname);
static SelectStmt *rewrite_repair_key(SelectStmt *sel);
static SelectStmt *assign_domain_ID(SelectStmt *sel, List *targetList, List *distinctClause);
static SelectStmt *assign_var_ID(SelectStmt *sel, List *targetList, List *distinctClause, List *keys);
static SelectStmt *duplicate_removal(SelectStmt *sel, List *targetList, List *distinctClause);

/* #define TEST */

SelectStmt *
ProcessRepairKey(SelectStmt *sel)
{
	/* Rewrite the set operation. */
	sel = rewrite_set_operation(sel);
		
 	/* In the current implementation of repair-key, we simply copy the
 	 * targetList of the outermost select in the sub-query.
 	 * If the ResTarget is a ColumnRef with fields "R.attribute", this will
 	 * cause a problem, because after the rewriting of repair-key,
 	 * the relation R will not be visible to the new outermost select.
 	 * Therefore, we remove the all the references to the relations, namely,
 	 * "R.attribute" becomes "attribute".
 	 */
 	/* TODO: The method above is not always working, a systematic way is needed. */
 		
 	sel->targetList = remove_relation_reference(sel->targetList);

	sel = rewrite_repair_key(sel);

	sel->tabletype = TABLETYPE_URELATION;
		
	/* Set the type of the table being created */
	if (sel->intoClause != NULL)
		sel->intoClause->tabletype = TABLETYPE_URELATION;

	sel->dimension = 1;
		
	/* Record that the triple of condition columns come from a repair-key */
	sel->isFromRepairKey = (bool *) palloc0(sizeof(bool));
	sel->isFromRepairKey[0] = true;

	#ifdef TEST
		myLog( pretty_format_node_dump(nodeToString(sel)));
	#endif
	
	return sel;
}

/*  rewriteRepairKey
 *
 *  This is the entry function for repair-key. All tasks of repair-key are 
 *  dispatched here. 
 */
static SelectStmt *
rewrite_repair_key(SelectStmt *sel)
{
	List *distinctClause;
	List *keys = sel->repairkey;
	RangeSubselect *sub1, *sub2, *sub3;
	SelectStmt *new = makeNode(SelectStmt);
	Node *join;

	/* The output of repair-key have all targets in the input  */
	new->targetList = get_repair_key_target_list(sel);
	
	/* The distinct clause is used in the duplicate elimination.  */
	distinctClause = target_list_to_ColumnRef(new->targetList);
	
	/* Move the into clause to the output select.  */
	new->intoClause = sel->intoClause;
	
	/* Set the into clause of the input query to NULL */
	sel->intoClause = NULL;

	/* Set the from clause of the output */
	sub1 = makeRangeSubselect("temp1", sel);
	sub2 = makeRangeSubselect("temp2", assign_domain_ID(sel, new->targetList, distinctClause));
	sub3 = makeRangeSubselect("temp3", assign_var_ID(sel, new->targetList, distinctClause, keys));
	
	join = make_natural_join(make_natural_join((Node *) sub1, (Node *) sub2), (Node *) sub3);

	new->fromClause = lappend(new->fromClause, join);

	/* Add variable ID to the target list */
	new->targetList = lappend(new->targetList,
		type_cast_to_int4(makeResTargetWithColumnRef(NULL, catStrInt(VARNAME, 0))));
	
	/* Add domain ID to the target list */
	new->targetList = lappend(new->targetList,
		type_cast_to_int4(makeResTargetWithColumnRef(NULL, catStrInt(DOMAINNAME, 0))));
	
	/* Add _W / _S to the target list */
	new->targetList =
		lappend(new->targetList,
				type_cast_weight_by(make_ResTarget_with_AExpr(catStrInt(PROBNAME, 0), 
				make_division_expr("_W", "_S") )));

	#ifdef TEST
		myLog("********************rewriteRepairKey\n");
		myLog( pretty_format_node_dump(nodeToString(new)));
	#endif

	return new;
}

/* assign_domain_ID
 *
 *  This generates a query that assigns domain IDs to each distinct tuple.
 *  The output of the function is like the following:
 *
 *	SELECT *, nextval('domid') as _D, _W FROM duplicate-elimination;
 */
static SelectStmt *
assign_domain_ID(SelectStmt *sel, List *targetList, List *distinctClause)
{
	/* Get the results of duplicate-elimination */
	SelectStmt *new = (SelectStmt *) duplicate_removal(sel, targetList, distinctClause);

	/* Add the domain ID to the target list */
	new->targetList =
		lappend(new->targetList, make_ResTarget_with_func_char(catStrInt(DOMAINNAME, 0), "nextval", DOMAINIDSEQ));

	#ifdef TEST
		myLog("********************assignDomainID\n");
		myLog( pretty_format_node_dump(nodeToString(new)));
	#endif

	return new;
}

/* assign_var_ID
 *
 *  This generates a query that assign variable ID to each distinct tuples on 
 *  key values.
 *  The output is a query tree representing a query like the following:
 *
 *	   SELECT keys, SUM(_W) as _S, nextval('varid') as _V 
 *     FROM duplicate-elimination GROUP BY keys
 */
static SelectStmt *
assign_var_ID(SelectStmt *sel, List *targetList, List *distinctClause, List *keys)
{
	SelectStmt *new = (SelectStmt *) makeNode(SelectStmt);
	SelectStmt *sub = duplicate_removal(sel, targetList, distinctClause);

	/* Add the key to the target list */
	new->targetList = list_copy(keys);
	
	/* Add variable ID to the target list */
	new->targetList = lappend(new->targetList,
				make_ResTarget_with_func_char(catStrInt(VARNAME, 0), "nextval", VARIDSEQ));
				
	/* Add aggregate function SUM to the target list */
	new->targetList = lappend(new->targetList,
				make_ResTarget_with_Func_ColumnRef("_S", "sum", "_W"));

	/* Add duplicate-elimination to the from clause */
	new->fromClause = lappend(new->fromClause, makeRangeSubselect("temp", sub));

	/* Add the key to the group clause */
	new->groupClause = target_list_to_ColumnRef(keys);
	
	/* NOTE: This is necessary otherwise the results are sometimes wrong.  */
	new->distinctClause = list_make1(NIL);

	#ifdef TEST
		myLog("********************assignVarID\n");
		myLog( pretty_format_node_dump(nodeToString(new)));
	#endif

	return new;
}

/*  duplicate_removal
 * 
 *  This generates a query that removes the duplicates from input select statement.
 *  The output is a query tree representing a query like the following:
 * 
 *  SELECT DISTINCT target-list, test_nagative( cast( weight-expression as real ) ) AS _W 
 *  FROM input WHERE weight-expression <> 0.
 */
static SelectStmt *
duplicate_removal(SelectStmt *sel, List *targetList, List *distinctClause)
{
	SelectStmt *new = (SelectStmt *) makeNode(SelectStmt);
	A_Const *con = (A_Const *) makeNode(A_Const);
	
	/* Set the target list */
	new->targetList = list_copy(targetList);
	
	new->groupClause = list_copy(distinctClause);
	
	/* Add keyword DISTINCT */
	new->distinctClause = list_make1(NIL);
	
	/* Add keyword DISTINCT */
	new->fromClause = lappend(new->fromClause, makeRangeSubselect("temp", sel));
	
	/* Make a constant 0 */
	con->val.type = T_Float;
	con->val.val.str = "0";

	if(sel->weightby != NULL)
	{
		/* Put the cast value in the test function */
		ResTarget 	*res = type_cast_weight_by(sel->weightby);
		FuncCall	*func = makeNode(FuncCall);
		func->funcname = list_make1(makeString(TESTNEGATIVE));
		func->args = list_make1(res->val);
		res->val = (Node *) func;
		
		new->targetList = lappend(new->targetList, res);
		
		/* Add the constraint that the weight is not 0 */
		new->whereClause = 
			(Node *) makeA_Expr(AEXPR_OP, list_make1(makeString("<>")), 
								sel->weightby->val, (Node *) con, 0);
	}
	else
		new->targetList = lappend(new->targetList, make_ResTarget_with_float("_W",
				"1.0"));

	#ifdef TEST
		myLog("********************duplicate removal\n");
		myLog( pretty_format_node_dump(nodeToString(new)));
		myLog("********************duplicate removal sel\n");
		myLog( pretty_format_node_dump(nodeToString(sel)));
	#endif

	return new;
}

/* make_natural_join
 *
 * Return a natural join with two nodes.
 */
static Node *
make_natural_join(Node *table1, Node *table2)
{
	JoinExpr *join = (JoinExpr *) makeNode(JoinExpr);
	join->jointype = 0;
	join->isNatural = true;
	join->larg = table1;
	join->rarg = table2;

	return (Node *) join;
}

/* get_repair_key_target_list
 *
 * Return the target list of all data columns in a repair-key statement.
 */
static List * 
get_repair_key_target_list(SelectStmt *sel)
{
	ListCell *cell;
	List *result = NULL;

	/* Loop the target list */
	foreach(cell, sel->targetList)
	{
		ResTarget *res = (ResTarget *) lfirst(cell);
		ResTarget *temp = (ResTarget *) copyObject(res);

		result = lappend(result, temp);

		if (res->name != NULL)
		{
			temp->val = (Node *) makeNode(ColumnRef);
			((ColumnRef *) temp->val)->fields = list_make1(makeString(temp->name));
			temp->name = NULL;
		}
		else
		{
			/* TODO: 
			 * If the res is not named and the value is not single ColumnRef, it can cause problems
			 */
		}
	}

	return result;
}

/* target_list_to_ColumnRef
 *
 * Return the column reference of every element in a target list.
 */
static List *
target_list_to_ColumnRef(List *target_list)
{	
	ListCell 	*cell;
	List 		*result = NULL;
	ColumnRef	*cref;

	/* Loop the target list */
	foreach(cell, target_list)
	{
		ResTarget *res = (ResTarget *) lfirst(cell);
		
		/* Use the name directly */
		if (res->name != NULL)
		{
			cref = makeNode(ColumnRef);
			cref->fields = list_make1(makeString(res->name));
			result = lappend(result, cref);
		}
		/* Unnamed target */
		/* TODO: Give an error message if the val is neither a constant or a ColumnRef */
		else
		{		
			result = lappend(result, res->val);
		}
	}

	return result;
}


/* make_division_expr
 * 
 * Make a division expression.
 */
static A_Expr *
make_division_expr(char *dividend, char *divisor)
{
	ColumnRef *cref1 = makeNode(ColumnRef);
	ColumnRef *cref2 = makeNode(ColumnRef);

	cref1->fields = list_make1(makeString(dividend));
	cref2->fields = list_make1(makeString(divisor));

	return makeSimpleA_Expr(AEXPR_OP, "/", (Node *) cref1, (Node *) cref2, 0);
}

/* make_ResTarget_with_Func_ColumnRef
 *
 * Make a ResTarget with a FuncCall name and a Column name.
 */
static ResTarget *
make_ResTarget_with_Func_ColumnRef(char *resname, char *funcname, char *columnname)
{
	ResTarget *res = (ResTarget *) makeNode(ResTarget);
	FuncCall *func = (FuncCall *) makeNode(FuncCall);
	ColumnRef *cref = (ColumnRef *) makeNode(ColumnRef);

	cref->fields = list_make1(makeString(columnname));
	func->funcname = list_make1(makeString(funcname));
	func->args = list_make1(cref);
	res->val = (Node *) func;
	res->name = resname;

	return res;
}

/* make_ResTarget_with_AExpr
 *
 * Make a ResTarget with an expression.
 */
static ResTarget *
make_ResTarget_with_AExpr(char *resname, A_Expr *expr)
{
	ResTarget *res = (ResTarget *) makeNode(ResTarget);
	res->val = (Node *) expr;
	res->name = resname;

	return res;
}
