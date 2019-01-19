/*-------------------------------------------------------------------------
 *
 * rewrite.c
 *	  Implementation of the rewriting queries for conf(), tconf(), aconf()
 *	  , esum(), ecount(), and repair-key.
 *
 * Copyright (c) 2008, MayBMS Development Group
 *
 *-------------------------------------------------------------------------
 */
/* This file mainly handles the rewriting of select statement. The other statements 
 * like insert, update and delete are processed in rewrite_updates.c. The rewriting 
 * for repair-key and pick-tuples is dispatched here and implemented in 
 * repair_key.c and pick_tuples.c. 
 *
 * A select statement go through the following 7 steps in the rewriting:
 *
 * 1. Check whether the query is supported or needs rewriting.
 * 
 * 2. Rewrite the sub-queries in the from clause or the two arguments if this is 
 * a set operation. This should be done before main select is processed.
 *
 * 3. Rewrite the queries involving esum and ecount using tconf. Search 
 * rewrite_esum and rewrite_ecount for details. This should be done before the 
 * rewriting for tconf.
 * 
 * 4. Collect the information of relations and sub-queries in the from clause for
 * use in rewriting. This includes
 *       (1) The type of the relations and sub-queries. It can be 
 *           tuple-independent, urelations or certain.
 *       (2) The dimension of condition column triples. It is 0 for certain relations 
 *           , 1 for tuple-independent relations and any positive integer for
 *           urelations.
 *
 * 4. Expand the targets involving "*". In PostgreSQL, this is done in 
 * parser/parse_target.c and parser/parse_relation.c afterwards. However, we can
 * not leave them until then because the rewriting for repair-key and confidence 
 * computation functions need the column names in the rewriting. Here we only 
 * expand the data columns and condition columns are handled centrally in step 7.
 *
 * 5. Process the confidence computation functions, namely conf, tconf and aconf. 
 * Currently, we do not allow them to be in the same select statement. For tconf,
 * the rewriting is simply to feed tconf with the condition columns.  The
 * rewriting for conf and aconf is much more complicated. For hierarchical
 * conjunctive query without self-join, conf is handled by operator HQ, whose
 * rewriting requires sorting on data and variable columns. Otherwise, conf and 
 * aconf are treated in the same way by sorting on data columns. The sorting is 
 * not necessary for correct confidence computation but leads to less memory 
 * consumption. Search function generalRewrite() for more details.   
 *
 * 6. The next step is the rewriting of repair-key and pick-tuples, which must
 * be placed after the rewriting of confidence computation functions whose 
 * results may be used in introducing new uncertainty. More detailed explanations
 * of these two constructs can be found in repair_key.c and pick_tuples.c.
 *
 * 7. If the select is not processed in steps 5 and 6, it is possible that the 
 * output of the select is uncertain relations and thus requires appending the 
 * condition columns to the output. In addition, some extra information are also
 * attached to the select statement, such as relation type and number of triples
 * of condition columns for later use.  
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

/* Functions related to esum and count. */
static FuncCall *rewrite_esum(List *targetList, FuncCall *esum);
static FuncCall *rewrite_ecount(List *targetList, FuncCall *ecount);

/* Functions related to possible. */
static SelectStmt* rewrite_possible(SelectStmt* sel, char typeArray[], 
		int tripleCount[], List *fields);

/* Functions related to tconf. */
static void put_args_to_tconf(SelectStmt *sel, char typeArray[],
		int tripleCount[], List *fields, FuncCall *func);
static void put_args_to_tconf_independent(char typeArray[], List *fields, FuncCall *func);

/* Functions related to rewriting of confidence computation operators. */
static void put_args_general(FuncCall *func, int n);
static void put_args_HQ(FuncCall *func, int n);
static void HQ_rewriting(SelectStmt *sel, List *varOrder, FuncCall *func);
static List *newResTargets(List *varOrder, char *name);
static List *put_args_to_conf_general(char typeArray[], int tripleCount[], 
	List *fields, char *name);
static void generalRewrite(SelectStmt *sel, char typeArray[], int tripleCount[], 
	List *fields, FuncCall *func);

/* Functions related to handling "*". */
static void transform_targetList(char typeArray[], int tripleCount[],
					List *fields, SelectStmt *sel);
static List* expand_columnref_star(char typeArray[], int tripleCount[],
					List *fields, SelectStmt *sel, List *star);
static List* expand_columnref_star_one_relation(char type, int tripleCount,
					List *fields, Node *relation);

/* Functions related to handling relation references after rewriting. */
static void handle_relation_reference(SelectStmt *outSel, SelectStmt *inSel);
static char *concat_fields(List *fields);
static char *remove_relation_reference_in_node(Node *node);
static void add_referenced_columns(SelectStmt *outer, SelectStmt *inner);

/* Functions related to retrieving information of relations from fromClause. */
static bool **get_repairkey_columns(List *fromClause, int tripleCount[]);
static bool all_relations_are_certain(int length, char typeArray[]);
static int calculate_total_triples(int tripleCount[], int n);
static List *generateFields(List *flist);

/* Functions related to query processing. */
static SelectStmt *process_subquery(SelectStmt *sel);
static SelectStmt *process_select(SelectStmt *sel);

/* Functions related to function lookup. */
static FuncCall *lookup_func_in_node(Node *node, char *name);
static ResTarget *lookup_res_by_func(List *targetList, char *name);
static int count_func_in_node(Node *node,	char *name);

/* Functions related to HQ. */
static bool is_self_join(List *fromClause);
static void put_args_one_relation_HQ(FuncCall *conf);
static bool is_a_general_case(List *flist, char typeArray[]);
static bool has_inequalities(A_Expr *node);

/* Functions related to rewriting. */
static void remove_mutual_exclusiveness(SelectStmt *sel, char typeArray[], 
	int tripleCount[], List *fields);
static SelectStmt *add_condition_columns(SelectStmt *sel, char typeArray[],
	int tripleCount[], List *fields, bool **isFromRepairKey);
static List *get_sort_clause(List *targetList);
	
/* process
 *
 * This is the dispatcher for different commands.
 */
Node *
process(Node *parsetree)
{
	char *error = NULL;

	/* Test code */
	#ifdef TEST
		myLog( pretty_format_node_dump(nodeToString(parsetree)));
	#endif

	/* If the query does not require rewriting, simply return the parsetree */
	if(!requiresRewriting(parsetree))
	{
		/* elog(WARNING, "No rewriting necessary");
		elog(WARNING,  pretty_format_node_dump(nodeToString(parsetree)));
		*/
		return parsetree;
	}
	
	/* If the query is not supported, give an error message */
	if (!isSupported(parsetree, &error))
	{
		/* elog(INFO,  pretty_format_node_dump(nodeToString(parsetree))); */
		//elog(ERROR, "Query not supported: %s", error);
	}

	/* Process the parse tree according to the its type */
	switch (nodeTag(parsetree))
	{
		case T_SelectStmt:
			return (Node *) process_select((SelectStmt *) parsetree);
			break;
		case T_InsertStmt:
			return (Node *) processInsert((InsertStmt *) parsetree);
		case T_UpdateStmt:
			return (Node *) processUpdate((UpdateStmt *) parsetree);
		case T_DeleteStmt:
			return (Node *) processDelete((DeleteStmt *) parsetree);
		default:
			return parsetree;
	}
}

/* process_subquery
 *
 * Process the sub-selects in fromClause and the two arguments of set operation. 
 * TODO: Add more processable sub-queies.
 */
static SelectStmt *
process_subquery(SelectStmt *sel)
{
	ListCell 		*cell;
	RangeSubselect	*sub;
	
	/* Recursively process the sub-queries */
	foreach(cell, sel->fromClause)
	{
		Node *node = lfirst(cell);
		
		switch (nodeTag(node))
		{
			case T_RangeSubselect:
				sub = (RangeSubselect *) node;
				sub->subquery = (Node *) process_select((SelectStmt *) sub->subquery);
				break;
			default:
			;
		}
	}
	
	/* If there is a set operation, process the left and right arguments */
	if (sel->op != SETOP_NONE)
	{
		/* TODO: if left and right argument have different number of condition
		 * columns, align them. Currently this case is not allowed by the 
		 * isSupported check. 
		 */
		sel->larg = process_select(sel->larg);
		sel->rarg = process_select(sel->rarg);
	}
	
	return sel;	
}

/* process_select
 *
 * If the any of the special construct in MayBMS, namely, tconf, conf, aconf, 
 * esum, ecount, possible and repair-key is present, rewrite the query.
 */
static SelectStmt *
process_select(SelectStmt *sel)
{
	int 			*tripleCount;
	char 			*typeArray = NULL;
	bool 			isCertainSel = false, generalCase;
	bool			**isFromRepairKey;
	List 			*fields, *varOrder;
	sgList 			*sglist;
	FuncCall 		*conf = NULL, *tconf = NULL, *aconf = NULL, *esum, *ecount;
	SelectStmt 		*result = sel;
	MemoryContext 	oldcxt;

	/* Test code */
	#ifdef TEST
		myLog( pretty_format_node_dump(nodeToString(result)));
	#endif

	/* process the subqueries first */
	result = process_subquery(result);

	/* Loop up esum and ecount */
	esum = lookup_func_in_list( result->targetList, ESUM );
	ecount = lookup_func_in_list( result->targetList, ECOUNT );

	/* Note that esum rewriting should be prior to searching for tconf,
	 * otherwise tconf will not be handled properly.
	 */
	if (esum != NULL)
		tconf = rewrite_esum(result->targetList, esum);

	if (ecount != NULL)
		tconf = rewrite_ecount(result->targetList, ecount);

	/* If tconf has been initialized by esum or ecount, do not look it up again!
	 * Another lookup will not give the correct result.
	 */
	if (tconf == NULL)
		tconf = lookup_func_in_list(result->targetList, TUPLECONF );

	/* Search conf and aconf */
	conf = lookup_func_in_list(result->targetList, CONF );
	
	aconf = lookup_func_in_list(result->targetList, ACONF );
	
	/* Following commands retrieve the information of relations and subqueries 
	 * in the fromClause. 
	 *
	 * TODO: It will be better to put the information of a relation into a 
	 * structure and keep an array of such structures.
	 */
	
	typeArray = palloc0(list_length(result->fromClause) * sizeof(char));
	
	tripleCount = getTripleCounts(result->fromClause);
	
	fields = generateFields(result->fromClause);
	
	generalCase = is_a_general_case(result->fromClause, typeArray);
	
	isFromRepairKey = get_repairkey_columns(result->fromClause, tripleCount);

	/* Transform the targetList so that no "*" appears in it. */
 	transform_targetList(typeArray, tripleCount, fields, result);

	/* If all relations in the fromClause are certain, confidence computation
	 * operators simply returns 1 without doing any rewriting.
	 */
	 if (all_relations_are_certain(list_length(result->fromClause), typeArray) 
	 	&& result->repairkey == NULL && result->pickingType != 'I' && !result->possible)
	 {
	 	/* If any of the following is not NULL, set its agg_star to true. */
	 	if ( tconf != NULL || conf != NULL || aconf != NULL )
	 		elog(ERROR, "Query not supported: tconf, conf, aconf, esum and ecount cannot used be in a certain query");
	 }

	/* If any of this confidence computation operators are used, the SELECT are certain. */
	/* TODO: This may be wrong, we should take into account where clause. */
	if (tconf != NULL || conf != NULL || aconf != NULL)
		isCertainSel = true;

	/* Processing of tconf */
	if(tconf != NULL)
	{
		if(!generalCase)
		{
			put_args_to_tconf_independent(typeArray, fields, tconf);
		}
		else
		{
			put_args_to_tconf(result, typeArray, tripleCount, fields, tconf);
		}

		tconf->agg_star = false;
	}
	/* Processing of aconf */
	else if (aconf != NULL)
	{
		if ( list_length(aconf->args) != 2 )
			elog(ERROR, "aconf takes two parameters");
		
		generalRewrite(result, typeArray, tripleCount, fields, aconf);
	}
	/* Processing of conf */
	else if (conf != NULL)
	{
		if ( list_length(conf->args) == 2 )
			generalCase = true;
		
		if(!generalCase)
		{
			/* This is the special case of only one tuple-independent relation */
			if (list_length(result->fromClause) == 1 && typeArray[ 0 ] == TABLETYPE_INDEPENDENT)
			{
				put_args_one_relation_HQ(conf);

				isCertainSel = true;
				goto repairkey;
			}

			/* Initialize the signature context if it is NULL  */
			if(signaturecxt == NULL)
			{
				signaturecxt = AllocSetContextCreate(NULL, "SignatureContext", ALLOCSET_DEFAULT_MINSIZE,
						ALLOCSET_DEFAULT_INITSIZE, ALLOCSET_DEFAULT_MAXSIZE);
			}

			/* Context switch should be done before the subgoal list generation */
			oldcxt = MemoryContextSwitchTo(signaturecxt);

			sglist = geneSGList((A_Expr *) result->whereClause, NULL, result->fromClause);			
			
			#ifdef HQ_TEST
				printsglist(sglist);
			#endif

			/* Process the hierarchical queries */
			if(isHQ(sglist) && !is_self_join(result->fromClause) 
				/*&& !has_inequalities((A_Expr *) result->whereClause)*/ )
			{
				/* Generate the signature */
				ProcessSignature(result, sglist);
				
				/* Switch back to the old context */
				MemoryContextSwitchTo(oldcxt);
	
				/* Get the variable order for sorting */
				varOrder = getVarOrder(sigTreeRoot, NULL);

				/* Rewrite the query */
				HQ_rewriting(result, varOrder, conf);
			}
			/* The processing of the general case in conf */
			else
			{
				/* Switch back to the old context */
				MemoryContextSwitchTo(oldcxt);
				
				/* Rewrite the query */
				generalRewrite(result, typeArray, tripleCount, fields, conf);
			}
		}
		/* The processing of the general case in conf */
		else
		{
			generalRewrite(result, typeArray, tripleCount, fields, conf);
		}
	}

repairkey:
	/* This must come before the repair-key and pick-tuples rewriting because 
	 * it's possible that the result of "possible" is used in repair-key.
	 */
	if (result->possible)
	{			
		result = rewrite_possible(result, typeArray, tripleCount, fields);
		
		isCertainSel = true;
	}

     /* If this is a repair-key statement, procees it and return the result */
	if (result->repairkey != NULL)
	{
		return ProcessRepairKey(result);
	}
	
	/* If this is a pick-tuples statement, procees it and return the result */
	if (result->pickingType == 'I')
	{
		//elog(WARNING,"ProcessPickTuples");
		return ProcessPickTuples(result);
	}
	
	/* After the rewriting for confidence computation operators, the certainty
	 * of the select statement should be checked. If the select is not
	 * certain, the condition columns are to be added to the end of targetlist
	 * so that these columns can be used for future confidence computation.
	 * For example, let R and S be one-dimensional urelations and the schema be (A, B), (C,D).
	 * The query
	 *      SELECT A, C FROM R, S;
	 * is rewritten to
	 *      SELECT A, C, R._v0, R._d0, R._p0, S._v0, S._d0, S._p0 FROM R, S;
	 *
     */
     
/* A label */
/*attachConditionColumns:*/

	/* If all relations are certain or the isCertainSel label has been set
	 * this select is certain.
	 */
	if(all_relations_are_certain(list_length(result->fromClause), typeArray) || isCertainSel)
	{
		/* Set the tag */
		result->tabletype = TABLETYPE_CERTAIN;
	}
	/* Otherwise the select will carry some condition triples */
	else
	{
		/* Set the type of the select. 
		 * If there is only one relation and it's tuple-independent, the output
		 * of select also keeps this property. However, if this relation is 
		 * joined with the original relation, we are not able to identify that 
		 * this is a self-join. For the time being, we simply treat all output
		 * of select as U-relations.
		 * TODO: Handle the table type in a cleverer way.
		 */
		result->tabletype = TABLETYPE_URELATION;
		
		/* Set the type of the table being created */
		if (result->intoClause != NULL)
			result->intoClause->tabletype = result->tabletype;

		/* Calculate the the number of dimensions of the urelation.
		 * For example, let R and S be urelations and the schemas be (A, B), (C,D).
		 * The query
		 *      SELECT A, C FROM R, S;
		 * has (dimensions(R) + dimensions(S)) triples of WSDs.
 		 */
		result->dimension = calculate_total_triples(tripleCount,
												list_length(result->fromClause));
		
		/* This is to record which condition triples come from newly generated,
		 * used in the implementation of world table.
		 */
		result->isFromRepairKey = palloc0(result->dimension * sizeof(bool));

		/* Append the condition triples to the target list */
		result = add_condition_columns(result, typeArray, tripleCount, fields,
										isFromRepairKey);

		/* Add consistency check */
		remove_mutual_exclusiveness(sel, typeArray, tripleCount, fields);
		
		/* Test Code */
		#ifdef TEST		
			myLog( pretty_format_node_dump(nodeToString(result)));
		#endif
	}
	
	return result;
}

/* put_args_one_relation_HQ
 * 
 * This handles the situation in which only one tuple-independent relation is 
 * involved. In this case, we only need to put the condition columns into conf
 * , rewriting is not necessary.
 *	
 */	
static void
put_args_one_relation_HQ(FuncCall *conf)
{
	ColumnRef 	*cref;

	conf->agg_star = false;

	cref = makeNode(ColumnRef);
	cref->fields = list_make1(makeString(catStrInt(VARNAME, 0)));
	conf->args = lappend(conf->args, cref);

	cref = makeNode(ColumnRef);
	cref->fields = list_make1(makeString(catStrInt(PROBNAME, 0)));
	conf->args = lappend(conf->args, cref);
}

/* rewrite_possible 
 *
 * Rewrite the query for POSSIBLE construct. 
 * Let query be 
 *		select possible A1,.., An from R1,..,Rn where condition;
 * It is rewritten to 
 *      select distinct on ( A1,.., An ) A1,.., An from R1,..,Rn 
 *      where ( condition AND consistency check );		
 */	
static SelectStmt*
rewrite_possible(SelectStmt* sel, char typeArray[], int tripleCount[], List *fields)
{
	sel->whereClause = processWhereClause(sel->whereClause);
	
	/* Add consistency check */
	remove_mutual_exclusiveness(sel, typeArray, tripleCount, fields);
		
	/* Add distinct clause */
	sel->distinctClause = list_make1(NIL);
	
	return sel;	
}


/* all_relations_are_certain
 *
 * Return true if all relations are certain.
 */
static bool
all_relations_are_certain(int length, char typeArray[])
{
	int i;
	
	for (i = 0; i < length; i++)
	{
		switch (typeArray[i])
		{
			case TABLETYPE_INDEPENDENT:
			case TABLETYPE_URELATION:
				return false;
				
			default:
				;
		}
	}
	
	return true;
}

/* get_repairkey_columns
 *
 * Combine isFromRepairKey arrays from all relations and subqueries in a list. 
 */
static bool**
get_repairkey_columns(List *fromClause, int tripleCount[])
{
	ListCell		*cell;
	Node			*node;
	RangeSubselect	*sub;
	SelectStmt		*subsel; 
	bool 			**isFromRepairKey; 
	int				count = 0;
	
	isFromRepairKey = palloc0(list_length(fromClause) * sizeof(bool *));
	
	foreach(cell, fromClause)
	{
		node = lfirst(cell);
		
		switch (nodeTag(node))
		{
			/* For a RangeVar, create an array with size as the number of 
			 * the triples. 
			 */
			case T_RangeVar:
				isFromRepairKey[count] = palloc0(tripleCount[count] * sizeof(bool));
				break;

			/* For a RangeSubselect, simply copy isFromRepairKey. */
			case T_RangeSubselect:
				sub = (RangeSubselect *) node;
				subsel = (SelectStmt *) sub->subquery;			
				isFromRepairKey[count] = subsel->isFromRepairKey;
				break;

			default:
				;
		}
		
		count++;
	}
	
	return isFromRepairKey;
}

/*  transform_targetList
 *
 *  This function goes through the targetList and expand "*" it encounters.
 *
 */
static void
transform_targetList(char typeArray[], int tripleCount[], List *fields,
						SelectStmt *sel)
{
	ResTarget	*res;
	List		*new, *temp;
	int 		i, j;

	for(i = 0; i < list_length(sel->targetList); i++)
	{
		res = (ResTarget *) list_nth(sel->targetList, i);

		if (IsA(res->val, ColumnRef))
		{
			ColumnRef  *cref = (ColumnRef *) res->val;

			/* If the "*" is used, do the following steps:
			 * (1) expand the * to a targetList
			 * (2) remove the * from the original list
			 * (3) insert the new targetList the place of *
			 */
			if (strcmp( strVal(llast(cref->fields)), "*" ) == 0)
			{
			    /* Expand the * to a targetList.  */
			    new = expand_columnref_star(typeArray, tripleCount, fields, sel,
											cref->fields);

			    /* Remove all ResTargets after "*" inclusively and append
			     * the new list to the rear
			     */
			    temp = list_truncate(list_copy(sel->targetList), i);

			    temp = list_concat(temp, new);

			    /* Insert the deleted ResTargets to the temp list one by one
			     * except "*".
			     */
			    for(j = i + 1; j < list_length(sel->targetList); j++)
			   		temp = lappend(temp, list_nth(sel->targetList, j));

			   	sel->targetList = temp;
			}
		}
	}
}

/* expand_columnref_star
 *
 * This expand the star in the form of "*" or "R.*".
 * In case of "*", we simply treat is as "R1.*", "R2.*", ... , "Rn.*" and
 * expand them one by one.
 */
static List*
expand_columnref_star(char typeArray[], int tripleCount[], List *fields,
						SelectStmt *sel, List *star)
{
	List	*result = NULL;
	List	*relName;
	int		i, j;
	bool 	match;

	/* If the ColumnRef is "*", include all columns from all relations. */
	if(list_length(star) == 1)
	{
		for(i = 0; i < list_length(sel->fromClause); i++)
		{
			relName = list_copy((List *) list_nth(fields, i));
			relName = list_truncate(relName, list_length(relName) - 1);
			result = list_concat(result, expand_columnref_star_one_relation
				(typeArray[i], tripleCount[i], relName,
						list_nth(sel->fromClause, i)));
		}
	}
	/* If the ColumnRef is "R.*", include all columns from R. */
	else
	{
		/* Check every relation in the fromClause */
		for(i = 0; i < list_length(sel->fromClause); i++)
		{
			relName = list_copy((List *) list_nth(fields, i));
			/* Check every relation in the fromClause */
			/* TODO: Now there is a fake attribute in the field, if this is
			 * removed in the future, the length of the match should be less than
			 * that of star by 1.
			 */
			match = true;
			if(list_length(star) == list_length(relName))
			{
				/* Compare every string in the two fields except the attribute. */
				for(j = 0; j < list_length(star) - 1; j++)
				{
					if (strcmp(strVal(list_nth(star, j)), strVal(list_nth(relName, j))) != 0)
					{
						match = false;
						break;
					}
				}

				/* If a match is found, expand the relation */
				if (match)
				{
					result = expand_columnref_star_one_relation (typeArray[i],
																 tripleCount[i],
																 list_truncate(relName, list_length(relName) - 1),
																 list_nth(sel->fromClause, i));


					break;
				}
			}
		}
	}

	return result;
}

/* handle_relation_reference
 *
 * For instance, let query be select R.a, S.a, conf() from R, S group by R.a, S.a;
 * Due to conf(), it's rewritten to
 * select R.a, S.a, conf( condition columns ) from
 * ( select R.a, S.a, condition columns from R, S orde by .... ) group by R.a, S.a;
 * Because R and S are no longer in the fromClause of the outermost select, there
 * will be an error message.
 *
 * To solve this problem, the rewritten should be
 * select R_a as a, S_a as a, conf( condition columns ) from
 * ( select R.a as _R_a, S.a as _S_a, condition columns from R, S orde by .... ) group by _R_a, _S_a;
 */
static void
handle_relation_reference(SelectStmt *outSel, SelectStmt *inSel)
{
	ListCell	*cell;
	Node		*node;
	ResTarget	*res;
	ColumnRef	*cref;
	char		*name;

	/* Name the targets of inner selection, if one does not have a name
	 * and reference a relation.
	 */
	foreach(cell, inSel->targetList)
	{
		node = (Node *) lfirst(cell);

		if (IsA(node, ResTarget))
		{
			res = (ResTarget *) node;

			/* If the res->name is not NULL, this is a condition column like r._v0 as _v1 */
			if (res->name == NULL)
			{
				/* TODO: ResTarget->val can be an expression.
				 * For instance, let the query be
				 *    select R.a, (R.a + S.a), conf() from R, S group by R.a, S.a.
				 */
				if(IsA(res->val, ColumnRef))
				{
					cref = (ColumnRef *) res->val;

					/* If the ColumnRef references a relation, name of the ResTarget must be
					 * set. Otherwise, it's not visible to the outermost selection.
					 * For example, ResTarget with ColumnRef R.a should have a name "_R_a".
					 */
					if (list_length(cref->fields) > 1)
						res->name = concat_fields(cref->fields);
				}	
			}
		}
	}

	/* Remove the relation references in the ResTargets. */
	foreach(cell, outSel->targetList)
	{
		node = (Node *) lfirst(cell);

		if (IsA(node, ResTarget))
		{
			res = (ResTarget *) node;
			
			/* Remove the relation reference in the val of the ResTarget */
			name = remove_relation_reference_in_node(res->val);
			
			/* Use the alias returned if ResTarget does not have a name */
			if (res->name == NULL)
				res->name = name;
		}
	}

	/* Set the ColumnRefs in groupClause if a relation is referenced  */
	foreach(cell, outSel->groupClause)
	{
		node = (Node *) lfirst(cell);

		if (IsA(node, ColumnRef))
		{
			cref = (ColumnRef *) node;

			if (list_length(cref->fields) > 1)
				cref->fields = list_make1(makeString(concat_fields(cref->fields)));
		}
	}
}

/* remove_relation_reference_in_node
 *
 * Remove the relation references in the node. If the node is a ColumnRef, 
 * also return the column which may be used as the alias of the ResTarget.
 *
 * For example, R.a becomes _R_a.
 */
static char *
remove_relation_reference_in_node(Node *node)
{
	ListCell	*cell;
	ColumnRef	*cref;
	char		*alias = NULL;
	FuncCall 	*func;
	
	switch (nodeTag(node))
	{
		case T_ColumnRef:
			cref = (ColumnRef *) node;
			
			/* Remove the reference and return the column name as the alias */
			if (list_length(cref->fields) > 1)
			{
				alias = (char *) strVal(llast(cref->fields));
				cref->fields = list_make1(makeString(concat_fields(cref->fields)));
			}
			
			return alias;
			
			break;
		
		/* Remove the references in the left and right expressions */
		case T_A_Expr:
			remove_relation_reference_in_node(((A_Expr *)node)->lexpr);
			remove_relation_reference_in_node(((A_Expr *)node)->rexpr);
			break;
		
		/* Remove the references in the argument */
		case T_TypeCast:
			remove_relation_reference_in_node(((TypeCast *)node)->arg);
			break;
		
		/* Remove the references in all arguments */
		case T_FuncCall:
			func = (FuncCall *) node;
			
			foreach(cell, func->args)
			{
				remove_relation_reference_in_node((Node *) lfirst(cell));	
			}
			
			break;
			
		default:
			; 
	}
	
	return NULL;
}

/* concat_fields
 *
 * Concatenate all the strings in the fields of a ColumnRef.
 * For example, R.a is concatenated as _R_a.
 */
static char*
concat_fields(List *fields)
{
	char 		*result = NULL, *str1 = NULL, *str2;
	ListCell	*cell;
	int 		currentSize = 0;

	foreach(cell, fields)
	{
		str2 = strVal(lfirst(cell));
		result = (char *) palloc0((currentSize + strlen(str2) + 10) * sizeof(char));

		if (str1 != NULL)
			strcat(result, str1);

		strcat(result, "_");
  		strcat(result, str2);

  		str1 = result;
  		currentSize = strlen(str1);
	}

	return result;
}

/* expand_columnref_star_one_relation
 *
 * This expans the "*" in a ColumnRef in the form of "R.*".
 */

static List*
expand_columnref_star_one_relation(char type, int tripleCount, List *fields,
									Node *relation)
{
	RangeVar 	*rv;
	Relation	rel;
	int			i, conColumnNum = 0;
	List		*result = NULL;
	ResTarget	*res, *temp;
	ColumnRef	*cref;
	SelectStmt 	*subsel;

	/* Decide the number of condition columns according the type and tripleCount */
	switch (type)
	{
		case TABLETYPE_CERTAIN:
			conColumnNum = 0;
			break;
		case TABLETYPE_INDEPENDENT:
		case TABLETYPE_URELATION:
			conColumnNum = tripleCount * 3;
			break;
	}

	if (IsA(relation, RangeVar))
	{
		/* Get the attribute list from catalog */
		rv = (RangeVar *) relation;
		rel = relation_openrv(rv, NoLock);
		
		for (i = 0; i < rel->rd_att->natts; i++)
		{
			/* We assume that all columns prefixed by VARNAME, 
			 * PROBNAME or DOMAINNAME are condition columns. 
			 */
			if (!isConditionAttribute(NameStr(rel->rd_att->attrs[i]->attname)))
			{
				res = makeNode(ResTarget);
				cref = makeNode(ColumnRef);

				cref->fields = lappend(list_copy(fields), 
									makeString(NameStr(rel->rd_att->attrs[i]->attname)));

				res->val = (Node *) cref;

				result = lappend(result, res);
			}
		}

		relation_close(rel, NoLock);
	}
	else if (IsA(relation, RangeSubselect))
	{
		/* Get the attribute list from targetList */
		/* TODO: Now if the subquery is not SelectStmt, there will be en error.  */
		subsel = (SelectStmt *) ((RangeSubselect *) relation)->subquery;

		for (i = 0; i < list_length(subsel->targetList) - conColumnNum; i++)
		{
			temp = (ResTarget *) list_nth(subsel->targetList, i);
			res = makeNode(ResTarget);
			cref = makeNode(ColumnRef);

			/* If the name of res is not null,
			 * set its name as the columnref of the new res */
			if (temp->name != NULL)
			{
				/* list_copy is necessary here, otherwise the changes are
				 * to be made on fields.
				 */
				cref->fields = lappend(list_copy(fields), makeString(temp->name));
			}
			/* TODO: Right now, only ColumnRef and FuncCall is handled. More 
			 * situations should be handled. 
			 */
			else
			{
				if (IsA(temp->val, ColumnRef))
				{
					/* list_copy is necessary here, otherwise the changes are
				     * to be made on fields.
				     * In addition, only the reference of the attribute of the target
				     * is needed. For example, if the ColumnRef is "R.att",
				     * fields are "S", the new reference is "S.att".
				     */
					cref->fields = lappend(list_copy(fields),
									llast(((ColumnRef *)temp->val)->fields));
				}
				else if (IsA(temp->val, FuncCall))
				{
					cref->fields = lappend(list_copy(fields),
									llast(((FuncCall *)temp->val)->funcname));
				}
				else
					elog(ERROR,"Every selected target should have a name");
			}

			res->val = (Node *) cref;
			result = lappend(result, res);
		}
	}

	return result;
}

/* rewrite_esum
 *
 * "Select A1,...,An, esum( A ) from R1,...,Rm where condition group by A1,...,An;"
 *  can be rewritten to
 * "Select A1,...,An, sum( A * tconf() ) from R1,...,Rm where condition group by A1,...,An;"
 */
static FuncCall *
rewrite_esum(List *targetList, FuncCall *esum)
{
	A_Expr 		*expr = makeNode(A_Expr);
	FuncCall 	*tconf = makeNode(FuncCall);
	ResTarget 	*res = lookup_res_by_func(targetList, ESUM);
	TypeCast *typecast = makeNode(TypeCast);
	TypeName *typename = makeNode(TypeName);

	/* Make a typecast to float4 */
	typename->names = list_make2(makeString("pg_catalog"), makeString("float4"));
	typecast->typename = typename;

	/* if the result of ecount is a target in the selection, set its name to ECOUNT */
	if (res != NULL)
		if (res->name == NULL)
			res->name = ESUM;

	/* esum only accepts one argument */
	if (list_length(esum->args) != 1)
		elog(ERROR, "Number of arguments for esum is not correct");

	esum->funcname = list_make1(makeString("sum"));

	/* set the argument of sum */
	expr->name = list_make1(makeString("*"));
	expr->lexpr = linitial(esum->args);
	tconf->funcname = list_make1(makeString(TUPLECONF));
	expr->rexpr = (Node *) tconf;
	typecast->arg = (Node *) expr;

	esum->args = list_make1(typecast);

	return tconf;
}

/* rewrite_ecount
 *
 * "Select A1,...,An, ecount( A ) from R1,...,Rm where condition group by A1,...,An;"
 *  can be rewritten to
 * "Select A1,...,An, sum( tconf() ) from R1,...,Rm where condition group by A1,...,An;"
 */
static FuncCall *
rewrite_ecount(List *targetList, FuncCall *ecount)
{
	FuncCall *tconf = makeNode(FuncCall);
	ResTarget *res = lookup_res_by_func(targetList, ECOUNT);

	/* if the result of ecount is a target in the selection,
	 * set its name to ECOUNT */
	if (res != NULL)
		if (res->name == NULL)
			res->name = ECOUNT;

	/* ecount only accepts zero or one argument */
	if (list_length(ecount->args) > 1)
		elog(ERROR, "Number of arguments for ecount is not correct");

	ecount->funcname = list_make1(makeString("sum"));

	tconf->funcname = list_make1(makeString(TUPLECONF));
	ecount->args = list_make1(tconf);

	return tconf;
}


/* add_condition_columns
 *
 * Add the condition columns of all relations and subselect in the from clause. 
 * Also record which triples come from a repair-key.
 * (These triples may be stored in W table)
 */
static SelectStmt *
add_condition_columns(SelectStmt *sel, char typeArray[],
		int tripleCount[], List *fields, bool **isFromRepairKey)
{
	ListCell 	*cell;
	ColumnRef 	*cref;
	ResTarget 	*res;
	int 		targetCounter = 0, counter = 0, j = 0, k = 0;
	char 		*name;

	/* A loop on each relation or subquery */
	foreach(cell, fields)
	{
		/* A loop on every triple of condition columns of the current relation */
		for(j = 0; j < tripleCount[ targetCounter ]; j++)
		{
			/* Set the flag of whether the triple comes from a repair-key */
			sel->isFromRepairKey[counter] = isFromRepairKey[targetCounter][j];
			
			/* A loop on the condition columns */
			for(k =0; k < 3; k++)
			{				
				switch (k)
				{
					case 0:
						name = VARNAME;
						break;
					case 1:
						name = DOMAINNAME;
						break;
					default:
						name = PROBNAME;
						break;
				}

				res = makeNode(ResTarget);


				cref = makeNode(ColumnRef);
				cref->fields = list_copy((List *)lfirst(cell));
				cref->fields = list_truncate(cref->fields,
					list_length(cref->fields) - 1);

				cref->fields = lappend(cref->fields,
					makeString(catStrInt(name, j)));
				res->val= (Node *) cref;

				res->name = catStrInt(name, counter);
				sel->targetList = lappend(sel->targetList, res);
			}

			counter++;
		}

		targetCounter++;
	}

	return sel;
}




/* make a column definition */
ColumnDef *makeColumnDef(char *name, int count)
{

	ColumnDef *cdef = makeNode(ColumnDef);
	List *namelist = NULL;

	if (count != -1)
	{
		cdef->colname = catStrInt(name, count);
	}
	else
		cdef->colname = name;

	cdef->is_local = true;
	cdef->is_not_null = true;

	if (strcmp(name, VARNAME) == 0)
	{
		namelist = list_make2(
				makeString("pg_catalog"), makeString(VARTYPENAME));
		/* default value for var columns is nextval(VARIDSEQ)*/
		cdef->constraints =
			list_make1(makeDefaultConstraint((Node *)makeNextValFuncCall(VARIDSEQ)));

	}
	else if (strcmp(name, PROBNAME) == 0)
	{
		namelist = list_make2(
				makeString("pg_catalog"), makeString(PROBTYPENAME));
		/* default value for the prob columns is 1 */
		cdef->constraints =
			list_make1(makeDefaultConstraint((Node *)makeFloatConst("1.0")));
	}
	else if (strcmp(name, DOMAINNAME) == 0)
	{
		namelist = list_make2(
				makeString("pg_catalog"), makeString(DOMAINTYPENAME));
		/* default value for the domain columns is nextval(DOMAINIDSEQ)*/
		cdef->constraints =
			list_make1(makeDefaultConstraint((Node *)makeNextValFuncCall(DOMAINIDSEQ)));
	}

	cdef->typename = makeTypeNameFromNameList(namelist);

	return cdef;
}

/* put_args_to_tconf
 *
 * This is to put the probability columns of all involved relations or
 * sub-queries into the the argument list of tconf.
 *
 * We make a tconf for urelations and tuple-independent respectively.
 * The input tconf take the product of these two tconf as its parameters.
 * This is to increase the number of relations and sub-queries involved by
 * computing two independent probabilities.
 *
 * TODO: In fact, tconf for tuple-independent relations are simply the product
 * of the probabilities of the joined tuples. Therefore, we do not actually
 * tconf of tuple-independent relations. However, to unify the interfaces with
 * tconf of urelations, we can use this: tconf( product of probs ).
 */
static void
put_args_to_tconf(SelectStmt *sel, char typeArray[], int tripleCount[],
	List *fields, FuncCall *func)
{
	ListCell *cell;
	int relCount = 0, i, j;
	char *columnName = NULL;
	FuncCall *func_urelation = copyObject(func);
	FuncCall *func_independent = copyObject(func);
	ColumnRef *cref;
	List *field;
	A_Expr *product = makeNode( A_Expr );

	foreach(cell, fields){
		field = (List *) lfirst(cell);

		switch(typeArray[ relCount ])
		{
			case TABLETYPE_CERTAIN:
				break;
			/* If the table is tuple-independent, its probability column is put
			 * into func_independent. */
			case TABLETYPE_INDEPENDENT:
				cref = makeNode( ColumnRef );
				cref->fields = list_copy( field );
				cref->fields = list_truncate( cref->fields, list_length( cref->fields ) - 1 );
				cref->fields = lappend(cref->fields, makeString( catStrInt( PROBNAME, 0 )));
				func_independent->args = lappend(func_independent->args, cref);
				break;
			/* If the table is a urelation, its probability columns are put into
			 * func_urelation. */
			case TABLETYPE_URELATION:

				for( i = 0; i < tripleCount[ relCount ]; i++ )
				{
					for ( j = 0; j < 3; j++ )
					{
						switch( j )
						{
							case 0:
								columnName = VARNAME;
								break;
							case 1:
								columnName = DOMAINNAME;
								break;
							case 2:
								columnName = PROBNAME;
								break;
							default:
								elog(ERROR, "Invalid column name!\n");
						}

						cref = makeNode(ColumnRef);
						cref->fields = list_copy(field);
						cref->fields = list_truncate(cref->fields, list_length(cref->fields) - 1);
						cref->fields = lappend(cref->fields, makeString(catStrInt(columnName, i)));
						func_urelation->args = lappend(func_urelation->args, cref);

					}
				}
				break;

			default:
				elog( ERROR, "Invalid table type!\n" );
		}

		relCount++;
	}

	/* put the product of two function tconf as the argument of the input tconf */
	product->name = list_make1( makeString( "*" ) );
	product->lexpr = ( Node * ) func_urelation;
	product->rexpr = ( Node * ) func_independent;
	func->args = list_make1(product);

	func_urelation->agg_star = false;
	func_independent->agg_star = false;

	sel->whereClause = processWhereClause(sel->whereClause);
	remove_mutual_exclusiveness(sel, typeArray, tripleCount, fields);
}



/* generalRewrite
 *
 * Rewrite the query involving conf() of general urelations and aconf().
 * 
 * The rewriting of conf and aconf can be implemented very simply by feeding 
 * the condition columns to the functions. However, this may cause large memory
 * consumption for storing lineage. Smart PostgreSQL can help us to reduce the
 * memory consumption.
 *
 * conf and aconf are both implemented as aggregate functions. There are three 
 * strategies available to eliminate duplicates in PostgreSQL now.
 *  
 * 1. Plain strategy is used if no group-by is present in the query. This is a 
 * sequence scan on all tuples from the outer plan node and returns only one 
 * tuple.  
 *
 * 2. Group strategy is used if the tuples have already been sorted by all 
 * selection columns. Thanks to the sorting, all duplicates in the same group 
 * come consecutively. Duplicate elimination is trivial by comparing every two 
 * adjacent tuples to detect the boundaries of duplicate group.  
 * 
 * 3. Hash strategy is used if the two above strategies are not suitable. The 
 * duplicate removal is performed with hash tables.  
 *
 * For queries with group-by, PostgreSQL will estimate the costs and choose one
 * between Group strategy and Hash strategy. For Hashing strategy, we have to
 * store the lineage of every group of duplicates, which can consume huge amount
 * of memory. However, for Group strategy, we only need to store the lineage for
 * one group of duplicates in the memory. To take advantage of this, we rewrite
 * the query by adding sorting on all group-by columns and force PostgreSQL to 
 * adopt Group strategy in duplicate elimination.  
 *
 * For instance, let R1 and R2 be one-dimension U-relations. Suppose the query is 
 *   
 * SELECT A, B, conf() FROM R1, R2 GROUP BY A, B;
 *
 * It is rewritten into
 *
 * SELECT A, B, conf(_v0, _d0, _p0, _v1, _d1, _p1) 
 * FROM (SELECT A,B, R1._v0 as _v0, R1._d0 as _d0, R1._p0 as _p0, 
                     R2._v0 as _v1, R2._d0 as _d1, R2._p0 as _p1 
 *       FROM R1, R2 ORDER BY A, B ) R
 * GROUP BY A, B; 
 *
 * NOTE: This function should be almost the same as HQ_rewrite. If HQ_rewrite
 * is changed, please also check that whether the change is needed here.
 */
static void 
generalRewrite(SelectStmt *sel, char typeArray[], int tripleCount[], 
	List *fields, FuncCall *func)
{
	SelectStmt *subsel = copyObject(sel);
	RangeSubselect *ransub = makeNode(RangeSubselect);

	if (func == NULL)
		return;

	/* Generate a RangeSubselect. */
	ransub->alias = makeAlias("temp", NULL);
	ransub->subquery = (Node *) subsel;

	/* Set the fromClause of the outer select as the RangeSubselect. */
	sel->fromClause = list_make1(ransub);
	sel->whereClause = NULL;

	/* The groupClause of the sub-selection is NULL */
	subsel->groupClause = NULL;
	
	/* The intoClause of the sub-selection is NULL */
	subsel->intoClause = NULL;
	
	/* The ResTarget related to conf() is deleted */
	/*subsel->targetList = list_delete_ptr(subsel->targetList,
			lookup_res_by_func(subsel->targetList, strVal(linitial(func->funcname))));*/

	/* Add the referenced columns of the outer select to inner select  */
	subsel->targetList = NULL;
	add_referenced_columns(sel, subsel);

	/* Generate the sortClause of the sub-selection */
	/* TODO: This argument of this function should be sel->groupClause */
	subsel->sortClause = get_sort_clause(subsel->targetList);
	
	/* Add the condition columns to the targetList */
	subsel->targetList = list_concat(subsel->targetList, put_args_to_conf_general(
			typeArray, tripleCount, fields, VARNAME));
	subsel->targetList = list_concat(subsel->targetList, put_args_to_conf_general(
			typeArray, tripleCount, fields, DOMAINNAME));
	subsel->targetList = list_concat(subsel->targetList, put_args_to_conf_general(
			typeArray, tripleCount, fields, PROBNAME));

	/* Add the consistency check in the sub-selection */
	subsel->whereClause = processWhereClause(subsel->whereClause);
	remove_mutual_exclusiveness(subsel, typeArray, tripleCount, fields);

	/* Set the arguments of conf() or aconf() as the condition columns in the sub-selection */
	func->agg_star = false;
	put_args_general(func,
			calculate_total_triples(tripleCount, list_length(subsel->fromClause)));

	/* Deal with the relation reference in the outermost selection */
	handle_relation_reference(sel, subsel);
	
	#ifdef TEST		
		myLog("sel-----------------------------------------------------------");	
		myLog( pretty_format_node_dump(nodeToString(sel)));
		myLog("subsel-----------------------------------------------------------");
		myLog( pretty_format_node_dump(nodeToString(subsel)));
	#endif
}

/*  add_referenced_columns
 *
 * Put all ColumnRef in the groupClause to the targetList of subselection 
 * This is needed in the following situations:
 * select conf() from r group by a;
 * select 1+a, conf() from r group by a;
 *
 * TODO: This should be merged with handle_relation_reference.
 * TODO: Now we assume that the values of all ResTargets are ColumnRef.
 * This is to be generalized.
 */
static void
add_referenced_columns(SelectStmt *outer, SelectStmt *inner)
{
	ListCell 	*cell;
	Node		*node;
	ResTarget	*res;
	ColumnRef	*cref,	*newcref;	
	
	/* If there is no group-by clause, simply return. */
	if (outer->groupClause == NULL)
		return;
	
	/* Copy the the ColumnRef of outer->groupClause into the targetList
	 * of inner */
	foreach(cell, outer->groupClause)
	{
		node = (Node *) lfirst(cell);

		if (IsA(node, ColumnRef))
		{
			cref = (ColumnRef *) node;
			
			/* Copying is necessary! */
			newcref = copyObject(cref);
			res = makeNode(ResTarget);
			res->val = (Node *) newcref;
			inner->targetList = lappend(inner->targetList, res);
		}
		/* Other nodes are forbidden */
		else
			elog(ERROR, "Only column ferences are allowed in the group clause");
	}	
}

/* remove_mutual_exclusiveness
 *
 * For example, R->v0 = S->v0  --->  R->d0 = S->d0
 * is added to the where clause as (R->v0 != S->v0) OR (R->d0 = S->d0)
 *
 */
static void 
remove_mutual_exclusiveness(SelectStmt *sel, char typeArray[], int tripleCount[], List *fields)
{
	int i, j, k, l, len = list_length(fields);
	List *rel1, *rel2;
	A_Expr *equal, *inequal, *or;
	Node *v1, *v2, *r1, *r2;
	
	/* Fill the where clause */
	fill_where_clause(sel);

	/* Loop all relation except the last one */
	for (i = 0; i < len - 1; i++)
	{
		/* We do not care certain and tuple-independent relations. */
		if (typeArray[i] != TABLETYPE_URELATION)
			continue;

		/* Retrieve the name of the relation. */
		rel1 = (List *) list_nth(fields, i);
		
		/* Loop all relation after the current relation. */
		for (j = i + 1; j < len; j++)
		{
			/* We do not care certain and tuple-independent relations. */
			if (typeArray[j] != TABLETYPE_URELATION)
				continue;

			/* Retrieve the name of the relation. */
			rel2 = (List *) list_nth(fields, j);
			
			/* Loop all the condition triples in relation 1. */
			for (k = 0; k < tripleCount[i]; k++)
			{
				v1 = makeColumnRef(VARNAME, k, rel1);

				r1 = makeColumnRef(DOMAINNAME, k, rel1);

				/* Loop all the condition triples in relation 2. */
				for (l = 0; l < tripleCount[j]; l++)
				{
					v2 = makeColumnRef(VARNAME, l, rel2);

					r2 = makeColumnRef(DOMAINNAME, l, rel2);
					
					/* Make the inequality expression, v1 <> v2. */
					inequal = makeA_Expr(AEXPR_OP, list_make1(makeString("<>")), v1, v2, 0);
					
					/* Make the equality expression, d1 = d2. */
					equal = makeA_Expr(AEXPR_OP, list_make1(makeString("=")), r1, r2, 0);
					
					/* Combine the two expressions to become  v1 = v2 implies d1 = d2. */
					or = makeA_Expr(AEXPR_OR, NULL, (Node *) inequal,
							(Node *) equal, 0);
					
					/*Add it to the where clause. */
					sel->whereClause = (Node *) makeA_Expr(AEXPR_AND, NULL,
							(Node *) or, sel->whereClause, 0);
				}
			}
		}
	}
}

/* HQ_rewriting
 *
 * Rewrite the query involving 'conf' for tuple-independent u-relations.
 * NOTE: This function should be almost the same as generalRewrite. If generalRewrite
 * is changed, please also check that whether the change is needed here.
 */
static void 
HQ_rewriting(SelectStmt *sel, List *varOrder, FuncCall *func)
{	
	SelectStmt *subsel = copyObject(sel);
	RangeSubselect *ransub = makeNode(RangeSubselect);

	if (func == NULL)
		return;

	/* Generate a RangeSubselect. */
	ransub->alias = makeAlias("temp", NULL);
	ransub->subquery = (Node *) subsel;

	/* Set the fromClause of the outer select as the RangeSubselect. */
	sel->fromClause = list_make1(ransub);
	sel->whereClause = NULL;

	/* The groupClause of the sub-selection is NULL */
	subsel->groupClause = NULL;

	/* The intoClause of the sub-selection is NULL */
	subsel->intoClause = NULL;
	
	/* Add the referenced columns of the outer select to inner select  */
	subsel->targetList = NULL;
	add_referenced_columns(sel, subsel);
	
	/* Add the condition columns to the targetList */
	subsel->targetList = list_concat(subsel->targetList, newResTargets(
			varOrder, VARNAME));

	/* Generate the sortClause of the sub-selection */
	subsel->sortClause = get_sort_clause(subsel->targetList);
	
	subsel->targetList = list_concat(subsel->targetList, newResTargets(
			varOrder, PROBNAME));

	/* Set the arguments of conf() or aconf() as the condition columns in the
	 * sub-selection */
	func->agg_star = false;
	put_args_HQ(func, list_length(subsel->fromClause));

	/* Deal with the relation reference in the outermost selection */
	handle_relation_reference(sel, subsel);
	
	#ifdef TEST
		myLog( pretty_format_node_dump(nodeToString(subsel)));		 
	#endif
}

/* calculate_total_triples
 *
 * Calculate the number of triple of condition columns in a query.
 */
static int
calculate_total_triples(int tripleCount[], int n)
{
	int i, result = 0;
	
	for(i = 0; i < n ; i++)
		result += tripleCount[ i ];
	
	return result;	
}

/* put_args_general
 *
 * Put the condition columns of a query into the the argument list for
 * non-HQ conf() or aconf(). 
 */
static void 
put_args_general(FuncCall *func, int n)
{
	int 		i, k;
	ColumnRef 	*cref;
	char		*name;

	for (i = 0; i < n; i++)
	{		
		for(k =0; k < 3; k++)
		{
			switch (k)
			{
				case 0:
					name = VARNAME;
					break;
				case 1:
					name = DOMAINNAME;
					break;
				default:
					name = PROBNAME;
					break;
			}
			
			cref = makeNode(ColumnRef);
			cref->fields = list_make1(makeString(catStrInt(name, i)));
			func->args = lappend(func->args, cref);
		}
	}
}

/* generateFields
 *
 * Generate a list of table or sub-query names from the from clause.
 */
static List *
generateFields(List *flist)
{
	ListCell *cell;
	Node *node;
	List *result = NULL;

	foreach(cell, flist)
	{
		node = lfirst(cell);

		if(result == NULL)
			result = list_make1(nodeGetFields(node));
		else
			result = lappend(result, nodeGetFields(node));
	}

	return result;
}

/* put_args_HQ
 *
 * Put the condition columns of a query into the the argument list for HQ conf().
 */
static void 
put_args_HQ(FuncCall *func, int n)
{	
	int 		i, k;
	ColumnRef 	*cref;
	char		*name;

	for (i = 0; i < n; i++)
	{		
		for(k =0; k < 2; k++)
		{
			switch (k)
			{
				case 0:
					name = VARNAME;
					break;
				default:
					name = PROBNAME;
					break;
			}
			
			cref = makeNode(ColumnRef);
			cref->fields = list_make1(makeString(catStrInt(name, i)));
			func->args = lappend(func->args, cref);
		}
	}
}

/* put_args_to_tconf_independent
 *
 * This is the rewriting for tconf() without general urelations.
 * The relations or sub-queries involved are certain or tuple-independent.
 */
static void
put_args_to_tconf_independent(char typeArray[], List *fields, FuncCall *func)
{
	ListCell *cell;
	ColumnRef *cref;
	List *field;
	int count = 0;

	foreach(cell, fields){
		field = (List *) lfirst(cell);

		switch (typeArray[count])
		{
			/* Only tuple-independent tables have probability columns */
			case TABLETYPE_INDEPENDENT:
				cref = makeNode( ColumnRef );
				cref->fields = list_copy( field );
				cref->fields = list_truncate(cref->fields,
											list_length(cref->fields) - 1);
				cref->fields = lappend(cref->fields,
										makeString(catStrInt(PROBNAME, 0)));
				func->args = lappend(func->args, cref);
				break;

			default:
				;
		}

		count++;
	}
}

/* put_args_to_conf_general
 *
 * Put the condition columns into conf which involves urelations.
 * TODO: This functions now only one kind of condition columns. Improve it to 
 * put all the condition columns once. Maybe this can be merged with 
 * function add_condition_columns.
 *
 */
static List *
put_args_to_conf_general(char typeArray[], int tripleCount[], List *fields, char *name)
{
	List *list = NULL;
	ListCell *cell;
	ColumnRef *cref;
	ResTarget *res;
	int targetCounter = 1, counter = 0, j = 0;

	/* Loop the all relations */
	foreach(cell, fields)
	{
		/* Loop all the triples of the condition columns. */
		for(j = 0; j < tripleCount[ targetCounter - 1 ]; j++)
		{
			res = makeNode(ResTarget);
			cref = makeNode(ColumnRef);
			cref->fields = list_copy((List *)lfirst(cell));
			cref->fields = list_truncate(cref->fields, list_length(cref->fields) - 1);

			cref->fields = lappend(cref->fields, makeString(catStrInt(name, j)));
			res->val= (Node *) cref;

			res->name = catStrInt(name, counter);

			list = lappend(list, res);

			counter++;
		}

		targetCounter++;
	}

	return list;
}

/* lookup_res_by_func 
 *
 * Look up ResTarget with a specified function name in a list.
 * We only return the first match and do not care the others.
 */
static ResTarget *
lookup_res_by_func(List *targetList, char *name)
{
	ListCell *o_target;

	foreach(o_target, targetList)
	{
		ResTarget *res = (ResTarget *) lfirst(o_target);
		
		if (lookup_func_in_node((Node *) res, name) != NULL)
			return res;	
	}

	return NULL;
}

/* lookup_func_in_list 
 *
 * Look up a function with a specified name in a list.
 * We only return the first match and do not care the others.
 */
FuncCall *
lookup_func_in_list(List *l, char *name)
{
	ListCell	*cell;
	FuncCall	*func;
	Node 		*node;
	
	foreach(cell, l)
	{
		node = lfirst(cell);
		
		func = lookup_func_in_node(node, name);
		
		if (func != NULL)
			return func;
	}
	
	return NULL;
}

/* lookup_func_in_node 
 *
 * Look up a function with a specified name.
 * We only return the first match and do not care the others.
 */
static FuncCall *
lookup_func_in_node(Node *node,	char *name)
{
	FuncCall 	*func;
	A_Expr		*expr;
	
	if (node == NULL)
		return NULL;
	
	switch (nodeTag(node))
	{
		/* If the node is a function, we have two possibilities:
		 * 1. The function mathces.
		 * 2. One of the arguments of the function matches.
		 */
		case T_FuncCall:
			func = (FuncCall *) node;
			
			if(strcmp(strVal(linitial(func->funcname)), name) == 0)
				return func;
			
			return lookup_func_in_list(func->args, name);
			
			break;

		/* If the node is a ResTarget, we recursively look up with the value.
		 */			
		case T_ResTarget:
			return lookup_func_in_node(((ResTarget *)node)->val, name);
			
			break;
		
		/* If a function is found in the left expression, return it; 
		 * otherwise, return the searching result of the right expression.
		 */
		case T_A_Expr:
			expr = (A_Expr *) node;
			
			func = lookup_func_in_node(expr->lexpr, name);
			
			if (func!= 	NULL)
				return func;
			else
				return lookup_func_in_node(expr->rexpr, name);
			
			break;

		/* If the node is a Typecast, we recursively look up with the argument.
		 */			
		case T_TypeCast:
			return lookup_func_in_node(((TypeCast *)node)->arg, name);
			
			break;
					
		/* It is not possible to find the function in other nodes. */
		default:
			return NULL;
	}	
}

/* count_func_in_list 
 *
 * Count the occurrences of the function with a specified name in a list.
 */
int
count_func_in_list(List *l, char *name)
{
	ListCell	*cell;
	Node 		*node;
	int			count = 0;
	
	foreach(cell, l)
	{
		node = lfirst(cell);
		
		count += count_func_in_node(node, name);
	}
	
	return count;
}

/* count_func_in_node 
 *
 * Count the occurrences of the function with a specified name.
 */
static int
count_func_in_node(Node *node,	char *name)
{
	FuncCall 	*func;
	A_Expr		*expr;
	int			count = 0;
	
	if (node == NULL)
		return count;
	
	switch (nodeTag(node))
	{
		/* If the node is a function, we have two possibilities:
		 * 1. The function mathces.
		 * 2. One of the arguments of the function matches.
		 */
		case T_FuncCall:
			func = (FuncCall *) node;
			
			if(strcmp(strVal(linitial(func->funcname)), name) == 0)
				count++;
		
			count += count_func_in_list(func->args, name);
			
			break;

		/* If the node is a ResTarget, we recursively look up with the value.
		 */			
		case T_ResTarget:
			count += count_func_in_node(((ResTarget *)node)->val, name);
			
			break;
		
		/* Return the total occurrences of the functions in left and right expressions. 
		 */
		case T_A_Expr:
			expr = (A_Expr *) node;
			
			count += count_func_in_node(expr->lexpr, name);
			count += count_func_in_node(expr->rexpr, name);
			
			break;

		/* If the node is a Typecast, we recursively look up with the argument.
		 */			
		case T_TypeCast:
			count = count_func_in_node(((TypeCast *)node)->arg, name);
			
			break;
					
		/* It is not possible to find the function in other nodes. */
		default:
			;
	}	
	
	return count;
}

/* is_self_join
 *
 * Return true if any two of the relations in the list are the same.
 */	
static bool
is_self_join(List *fromClause)
{
	int 		i, j;
	Node		*node1, *node2;
	RangeVar	*rv1, *rv2;
	Relation	rel1, rel2;
	bool		self_join = false;
	
	for (i = 0; i < list_length(fromClause) - 2; i++)
	{
		node1 = list_nth(fromClause, i);

		/* Here I assume that a RANGESUBSELECT is never a tuple-independent relation.
		 * This is consistent with other parts in the implementation.
		 */
		if (!IsA(node1, RangeVar))
		{
			continue;
		}
		else
		{
			rv1 = (RangeVar *) node1;
		}
		
		for (j = i+1; j < list_length(fromClause) - 1; j++)
		{
			node2 = list_nth(fromClause, j);
			
			if (!IsA(node2, RangeVar))
				continue;
			else
				rv2 = (RangeVar *) node2;
				
			/* Compare two range vars and return true if they are the same. */	
			rel1 = relation_openrv(rv1, NoLock);
			rel2 = relation_openrv(rv2, NoLock);
			
			/* Do not return until close the relation */
			if (rel1->rd_id == rel2->rd_id)
				self_join = true;
			
			relation_close(rel1, NoLock);
			relation_close(rel2, NoLock);
			
			if (self_join)
				return true;
		}
	}
	
	return false;
}

/* newResTargets
 *
 * Put condition columns to the target list according to the variable order.
 */	
static List 
*newResTargets(List *varOrder, char *name)
{
	List *list = NULL;
	ListCell *cell;
	ColumnRef *cref;
	ResTarget *res;
	int counter = 0;

	foreach(cell, varOrder)
	{
		cref = makeNode(ColumnRef);
		cref->fields = list_copy((List *)lfirst(cell));
		cref->fields = list_truncate(cref->fields, list_length(cref->fields) - 1);
		cref->fields = lappend(cref->fields, makeString(catStrInt(name, 0)));

		res = makeNode(ResTarget);
		res->val= (Node *) cref;
		res->name = catStrInt(name, counter);

		list = lappend(list, res);

		counter++;
	}

	return list;
}

/* is_a_general_case
 *
 * 1. Fill in the typeArray.
 * 2. Return true if all relations are tuple-independent.
 * TODO: This should be split into two functions. 
 */	
static bool 
is_a_general_case(List *flist, char typeArray[])
{
	ListCell *cell;
	Node *node;
	RangeVar *rv;
	Relation rel;
	int count = 0, i = 0;
	RangeSubselect *sub;
	SelectStmt *subsel;

	/* Loop the from clause */
	foreach(cell, flist)
	{
		node = lfirst(cell);
	
		switch(nodeTag(node))
		{
			/* If it is a range variable, get the relation type from catalog.  */
			case T_RangeVar:
				rv = (RangeVar *) node;
				rel = relation_openrv(rv, NoLock);
				typeArray[count] = rel->rd_rel->tabletype;
				relation_close(rel, NoLock);
				break;

			/* If it is a range sub-select, get the relation type from the select. */
			case T_RangeSubselect:
				sub = (RangeSubselect *) node;
				subsel = (SelectStmt *) sub->subquery;
				typeArray[count] = subsel->tabletype;
				break;

			default:
				;
		}

		/* Increase the counter. */
		count++;
	}

	/* If any of the relation is a u-relation, this is a general case. */
	for(; i < count; i++)
	{
		if(typeArray[i] != TABLETYPE_INDEPENDENT)
			return true;
	}

	return false;
}

/* get_sort_clause
 *
 * Add all the columnrefs in the target list to the sort clause
 */	
static List *
get_sort_clause(List *targetList)
{
	ListCell *cell;
	List *result = NULL;

	/* Loop the target list */
	foreach(cell, targetList)
	{
		ResTarget *res = (ResTarget *) lfirst(cell);

		if(IsA(res->val, ColumnRef))
		{
			/* Create the SortBy object */
			ColumnRef *cref = copyObject(res->val);
			SortBy *sortby = makeNode(SortBy);
			sortby->node = (Node *) cref;

			/* Add it the to result */
			result = lappend(result, sortby);
		}
	}

	return result;
}

/* has_inequalities
 *
 * Return ture if the query contains inequalities("<"). 
 */	
static bool
has_inequalities(A_Expr *node)
{
	List *name;
	char *op;

	/* Exception handling */
	if (node == NULL)
		return false;

	name = node->name;

	/* Recursively generate the list */
	if (name == NULL)
	{
		return (has_inequalities((A_Expr *) node->lexpr) || has_inequalities((A_Expr *) node->rexpr)); 
	}
	/* The node is named */
	else
	{
		op = strVal(linitial(node->name));

		/* If the operator is "<" , ">", "<=" or ">=", return true. */
		
		/* TODO: Refine the check. For instance, conditions A < constant and 
		 * R.A < R.B should not be considered as inequalities because they do not
		 * change the tractability of the queries.  */
		if (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 || strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0)
		{
			return true;
		}
	}

	return false;
	 
}
