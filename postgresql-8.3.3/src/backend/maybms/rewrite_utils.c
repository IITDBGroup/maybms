/*-------------------------------------------------------------------------
 *
 * rewrite_utils.c
 *		Utility functions used in the rewriting phase.
 *
 * Copyright (c) 2008, MayBMS Development Group
 *
 *-------------------------------------------------------------------------
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

/* Local funcitons */
static void my_itoa(int n, char s[]);
static void reverse(char s[]);
static List* delLastAppendOne(List *list, char *p);
static List* get_targets_from_set_operation(List *old);

/**
 * processWhereClause
 * 		rewrites a where condition by rewriting all subqueries in it
 *
 *
 */
Node* processWhereClause(Node* whereClause)
{
	if (whereClause == NULL)
		return whereClause;

	if (IsA(whereClause, ColumnRef) || IsA(whereClause, A_Const))
		return whereClause;

	if (IsA(whereClause, A_Expr))
	{
		A_Expr *a_expr = (A_Expr*) whereClause;
		A_Expr_Kind kind = a_expr->kind;
		if (kind == AEXPR_OP || kind == AEXPR_AND || kind == AEXPR_OR)
		{
			a_expr->lexpr = processWhereClause(a_expr->lexpr);
			a_expr->rexpr = processWhereClause(a_expr->rexpr);
			return whereClause;
		}
		else if (kind == AEXPR_NOT)
		{
			a_expr->rexpr = processWhereClause(a_expr->rexpr);
			return whereClause;
		}
	}
	else if (IsA(whereClause, SubLink))
	{
		SubLink *sublink = (SubLink*) whereClause;
		sublink->subselect = process(sublink->subselect);
		return whereClause;
	}

	/* This case should not be reached as all other types of expressions
	 * are forbidden by the isSupported check*/
	/* TODO: can we support other types of nodes? */
	return whereClause;
}

// get the number of triple (V, D, P) for the fromClause of a select
int *getTripleCounts(List *flist)
{

	ListCell 		*cell;
	Node 			*node;
	RangeVar 		*rv;
	SelectStmt		*select;
	Relation 		rel;
	int 			count = 0;
	int 			*result = palloc0(list_length(flist) * sizeof(int));
	char 			type;
	int 			i, conColCount = 0;
	RangeSubselect 	*sub;
	SelectStmt 		*subsel;

	foreach(cell, flist){
		node = lfirst(cell);
		switch(nodeTag(node))
		{
			case T_RangeVar:
				rv = (RangeVar *) node;
				rel = relation_openrv(rv, NoLock);
				type = rel->rd_rel->tabletype;

				if(type == TABLETYPE_INDEPENDENT)
					*(result + count) = 1;
				else if (type == TABLETYPE_URELATION)
				{
					/* reset the counter */
					conColCount = 0;
					
					for (i = 0; i < rel->rd_att->natts; i++)
					{
						/* We assume that all columns prefixed by VARNAME, 
						 * PROBNAME or DOMAINNAME are condition columns. 
						 */
						if (isConditionAttribute(NameStr(rel->rd_att->attrs[i]->attname)))
							conColCount++;
					}

					*(result + count) = conColCount / 3;
				}

					//count++;
				relation_close(rel, NoLock);
				break;

			case T_SelectStmt:
				select = (SelectStmt *) node;
				
				if (select->tabletype == TABLETYPE_URELATION)
					*(result + count) = select->dimension;
				else if(select->tabletype == TABLETYPE_INDEPENDENT)
					*(result + count) = 1;
					
				break;
			
			case T_RangeSubselect:
				sub = (RangeSubselect *) node;
				subsel = (SelectStmt *) sub->subquery;

				if (subsel->tabletype == TABLETYPE_URELATION)
					*(result + count) = subsel->dimension;
				else if(subsel->tabletype == TABLETYPE_INDEPENDENT)
					*(result + count) = 1;
				
				break;

			default:
				;
		}

		count++;
	}

	return result;
}

/*
 *   This make a ResTarget with a column name.
 */
ResTarget *
makeResTargetWithColumnRef(char *resname, char *columnname)
{
	ResTarget *res = (ResTarget *) makeNode(ResTarget);
	ColumnRef *cref = (ColumnRef *) makeNode(ColumnRef);

	cref->fields = list_make1(makeString(columnname));
	res->val = (Node *) cref;
	res->name = resname;

	return res;
}

/*
 *   This make a RangeSubselect with an alias and a SelectStmt.
 */
RangeSubselect *makeRangeSubselect(char *alias, SelectStmt *sel)
{
	RangeSubselect *sub = (RangeSubselect *) makeNode(RangeSubselect);
	sub->alias = (Alias *) makeAlias(alias, NULL);
	sub->subquery = (Node *) sel;

	return sub;
}


/**
 * makeDefaultConstraint
 * 		creates a node representing a default constraint in a create table
 * 		statement with the specified default expression
 */
Constraint *makeDefaultConstraint(Node *default_expr)
{
	Constraint *constraint = (Constraint*) makeNode(Constraint);
	constraint->contype = CONSTR_DEFAULT;
	constraint->name = NULL;
	constraint->raw_expr = default_expr;
	constraint->cooked_expr = NULL;
	constraint->keys = NULL;
	constraint->indexspace = NULL;
	return constraint;
}

/**
 * makeNextValFuncCall
 * 		creates a node representing the funccall nextval('seqname')
 */
FuncCall *makeNextValFuncCall(char *seqname)
{
	FuncCall *func = (FuncCall *) makeNode(FuncCall);
	A_Const *arg;
	
	func->agg_distinct = false;
	func->agg_star = false;
	func->funcname = list_make1(makeString("nextval"));

	arg = (A_Const *) makeNode(A_Const);
	arg->val = *(makeString(seqname));
	func->args = list_make1(arg);
	return func;
}

/* makeFloatConst
 * 
 * Return a constant with a specified value.
 */
A_Const *makeFloatConst(char *value)
{
	A_Const *con = (A_Const *) makeNode(A_Const);
	con->val = *(makeFloat(value));

	return con;
}


// concatenate a integer to a string
char *catStrInt(char *str, int number)
{
	char *s1 = palloc0(20 * sizeof(char));
	char s2[5];
	strcpy(s1, str);
	my_itoa(number, s2);
	strcat(s1, s2);

	return s1;
}

static void 
my_itoa(int n, char s[])
{
	int i, sign;

	if ((sign = n) < 0)
		n = -n;

	i = 0;

	do
	{
		s[i++] = (n % 10) + '0';

	} while ((n /= 10) > 0);

	if (sign < 0)
		s[i++] = '-';

	s[i] = '\0';

	reverse(s);
}

static void 
reverse(char s[])
{
	int c, i, j;

	for (i = 0, j = strlen(s) - 1; i < j; i++, j--)
		c = s[i], s[i] = s[j], s[j] = c;
}

void printSubselect(SelectStmt *sel)
{
	ListCell *cell;
	SelectStmt *sub;
	List *sortClause;
	ListCell *sort;

	foreach(cell , sel->fromClause){

		RangeSubselect *node = (RangeSubselect *) lfirst(cell);

		if(!IsA(node, RangeSubselect))
		{
			continue;
		}

		sub = (SelectStmt *) node->subquery; 
		myLog("get a sub sel\n");

		sortClause = sub->sortClause;

		foreach(sort , sortClause)
		{
			SortBy *sortby = lfirst(sort);
			if(IsA(sortby, SortBy))
			{
				myLog("sortby\n");
				myLog(pretty_format_node_dump(nodeToString(sortby->node)));nl(1);
			}
		}
		myLog(pretty_format_node_dump(nodeToString(sub)));
	}
}

Node *makeColumnRef(char *attr, int number, List *rel)
{
	char *att = catStrInt(attr, number);
	ColumnRef *cref = (ColumnRef *) makeNode(ColumnRef);
	cref->fields = delLastAppendOne(rel, att);

	return ((Node *) cref);
}

static List* 
delLastAppendOne(List *list, char *p)
{
	return lappend(list_truncate(list_copy(list), list_length(list) - 1),
			makeString(p));
}

// get the name of a range var
List *nodeGetFields(Node *node)
{
	RangeVar *rv;
	RangeSubselect *sub;
	List *fields = NULL;

	switch (nodeTag(node))
	{
	case T_RangeVar:
		rv = (RangeVar *) node;
		if (rv->alias != NULL)
			fields = list_make1(makeString(rv->alias->aliasname));
		else
		{
			if (rv->catalogname == NULL && rv->schemaname == NULL)
				fields = list_make1(makeString(rv->relname));
			else if (rv->catalogname == NULL)
				fields = list_make2(makeString(rv->schemaname),
									makeString(rv->relname));
			else
				fields = list_make3(makeString(rv->catalogname),
									makeString(rv->schemaname),
									makeString(rv->relname));
		}
		break;

	case T_RangeSubselect:
		sub = (RangeSubselect *) node;
		fields = list_make1(makeString(sub->alias->aliasname));
		break;

	default:
			; //elog(WARNING, "unrecognized node!\n");
	}

	fields = lappend(fields, makeString("fake"));

	return fields;
}

/* fill_where_clause
 *
 * If the whereClause is NULL, make it a constant node of TRUE.
 * Otherwise, if a NULL node is a part of an expression, there will be 
 * an error message ERROR:  cache lookup failed for type 0 will appear.
 */
void
fill_where_clause(SelectStmt *sel)
{
	A_Const		*trueNode;
	
	if (sel->whereClause == NULL)
	{
		trueNode = makeNode(A_Const);
		trueNode->val = *(makeString("t"));
		trueNode->typename = makeNode(TypeName);
		trueNode->typename->names = list_make2(makeString("pg_catalog"),
												makeString("bool"));
		sel->whereClause = (Node *) trueNode;
	}
}

/* rewrite_set_operation
 *
 * Add a select on top of the set operation. This is used for the query rewriting.
 * The results of the set operations have the same column names as those in larg.
 */
SelectStmt*
rewrite_set_operation(SelectStmt *sel)
{
	if (sel->op != SETOP_NONE)
	{
		SelectStmt *result = makeNode(SelectStmt);
		
		/* Use the input as a range subselect */
		result->fromClause = list_make1(makeRangeSubselect("temp", sel));
		
		/* Do not use "select *"! */
		/* TODO: Handle the alias and relation reference. */
		result->targetList = get_targets_from_set_operation(sel->larg->targetList);
		
		/* TODO: Handle the alias and relation reference. */
		if (sel->repairkey != NULL)
			result->repairkey = copyObject(sel->repairkey);		
		else if (sel->pickingType != ' ')
			result->pickingType = sel->pickingType;
		
		/* If this is produced by a CreateAsStmt, copy the intoClause  */
		if (sel->larg->intoClause != NULL)
		{
			result->intoClause = sel->larg->intoClause;
			sel->larg->intoClause = NULL;
		}
		
		/* Copy the uncertainty information. */
		result->tabletype = sel->larg->tabletype;
		result->dimension = sel->larg->dimension;
	
		return result;
	}
	else
		return sel;
}

/* get_targets_from_set_operation
 *
 * Generate the targetList for the new select on top of the set operation.
 * We only allow targets with an alias or ColumnRef. This is restrictive but
 * our lives are simpler.
 */
static List*
get_targets_from_set_operation(List *old)
{
	ListCell	*cell;
	List		*result = NULL;
	ResTarget	*res, *new;
	ColumnRef	*cref, *new_cref;
	
	foreach(cell, old)
	{
		res = (ResTarget *) lfirst(cell);
		new = makeNode(ResTarget);
		
		/* Use the alias if one exists */
		if (res->name != NULL)
		{
			new = makeResTargetWithColumnRef(NULL,res->name);
		}
		/* No alias */
		else
		{
			if (nodeTag(res->val) == T_ColumnRef)
			{
				cref = (ColumnRef *) res->val;
				
				/* If a relation is referenced, simply drop it. */
				if (list_length(cref->fields) > 1)
				{
					new_cref = makeNode(ColumnRef);
					new_cref->fields = list_make1(llast(cref->fields));
					new->val = (Node *) new_cref;
				}
				else
					new = copyObject(res);
			}
			else
				elog(ERROR, "Name the column with an alias");
		}
		
		result = lappend(result, new);
	}

	return result;
}

/*  remove_relation_reference
 *
 *  Remove the references to the relation in the ColumnRef, namely, "R.att" is
 *  truncate to "att".
 *
 *  TODO: Now we only handle the ColumnRefs that are directly the values of ResTarget.
 */
List *
remove_relation_reference(List *targetList)
{
	ListCell 	*cell;
	ResTarget 	*res;
	ColumnRef 	*cref;

	foreach(cell, targetList)
	{
		res = (ResTarget *) lfirst(cell);

		if (IsA(res->val, ColumnRef) && res->name == NULL)
		{
			cref = (ColumnRef *) res->val;

			if (list_length(cref->fields) > 1)
				cref->fields = list_make1(llast(cref->fields));
		}
	}

	return targetList;
}

/* type_cast_to_int4
 * 
 * Cast the weight by column into an int4.
 */
ResTarget *
type_cast_to_int4(ResTarget *res)
{
	TypeCast *typecast = makeNode(TypeCast);
	TypeName *typename = makeNode(TypeName);

	typecast->arg = res->val;
	typename->names = list_make2(makeString("pg_catalog"), makeString("int4"));
	typecast->typename = typename;

	res->val = (Node *) typecast;

	return res;
}

/* make_ResTarget_with_func_char
 *
 * Make a ResTarget with a FuncCall name and a char constant.
 */
ResTarget *
make_ResTarget_with_func_char(char *resname, char *funcname, char *constname)
{
	ResTarget *res = (ResTarget *) makeNode(ResTarget);
	FuncCall *func = (FuncCall *) makeNode(FuncCall);
	A_Const *con = (A_Const *) makeNode(A_Const);

	con->val = *(makeString(constname));
	func->funcname = list_make1(makeString(funcname));
	func->args = list_make1(con);
	res->val = (Node *) func;
	res->name = resname;

	return res;
}

/* type_cast_weight_by
 *
 * Cast the weight by column into a real number.
 * It's necessary because if the weight-by column is an integer, the result of
 * _P0 is also an integer.
 */
ResTarget *
type_cast_weight_by(ResTarget *weightby)
{
	TypeCast *typecast = makeNode(TypeCast);
	TypeName *typename = makeNode(TypeName);

	typecast->arg = weightby->val;
	typename->names = list_make2(makeString("pg_catalog"), makeString("float4"));
	typecast->typename = typename;

	weightby->val = (Node *) typecast;

	return weightby;
}

/* make_ResTarget_with_float
 *
 * Make a ResTarget with a float constant.
 */
ResTarget *
make_ResTarget_with_float(char *resname, char *constant)
{
	ResTarget *res = (ResTarget *) makeNode(ResTarget);
	A_Const *con = (A_Const *) makeNode(A_Const);

	con->val.type = T_Float;
	con->val.val.str = constant;

	res->val = (Node *) con;
	res->name = resname;

	return res;
}

/* make_ResTarget_with_int
 *
 * Make a ResTarget with an integer.
 */
ResTarget *
make_ResTarget_with_int(char *resname, int constant)
{
	ResTarget *res = (ResTarget *) makeNode(ResTarget);
	A_Const *con = (A_Const *) makeNode(A_Const);

	con->val = *(makeInteger(constant));

	res->val = (Node *) con;
	res->name = resname;

	return res;
}
