/*
 * supported.c
 *		implementation of the functions verifying whether a query is supported,
 *		as well as for checking whether a query produces a t-certain result.
 *
 *
 * Copyright (c) 2008, MayBMS Development Group
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

static bool requiresRewritingSelect(const SelectStmt *select);
static bool requiresRewritingFromClause(List *fromClause);
static bool requiresRewritingWhereClause(const Node* node);
static char *err_msg(const char *error);
static bool hasApproxAggregate(List *targetList);
static bool isApproxAggregate(FuncCall *func);

/**
 * isCertain
 * 		Checks whether a query is certain
 */
bool isCertain(const Node *node)
{
	bool certain;
	RangeVar *rv;
	SelectStmt *select;
	SelectStmt *subselect;
	JoinExpr *join;

	certain = false;

	if (node == NULL)
	{
		return true;
	}

	switch (nodeTag(node))
	{
		case T_RangeVar:
			rv = (RangeVar *) node;
			/* check relation type in the catalog */
			certain = isCertainRelation(rv);
			break;
		case T_SelectStmt:
			select = (SelectStmt*) node;
			certain = isCertainSelect(select);
			break;
		case T_RangeSubselect:
			subselect = (SelectStmt*) ((RangeSubselect*) node)->subquery;
			certain = isCertainSimpleSelect(subselect);
			break;
		case T_JoinExpr:
			join = (JoinExpr*) node;
			certain = isCertain(join->larg) && isCertain(join->rarg);
			break;
			/*TODO: add funccalls */
		default:
			/* not supported */
			certain = false;
	}
	return certain;
}

/**
 * isCertainRelation
 * 		Checks whether a relation is certain by checking its type in the
 * 		catalog
 */
bool isCertainRelation(const RangeVar *rv)
{
	bool certain = false;
	/* check relation type in the catalog */
	Relation rel = relation_openrv(rv, NoLock);
	if (rel->rd_rel->tabletype == TABLETYPE_CERTAIN)
		certain = true;

	/* TODO: fix the labeling of tables in the catalog
	 * currently some tables don't have a type stored - in this case should
	 * we interpret them as certain? */
	else if (rel->rd_rel->tabletype != TABLETYPE_INDEPENDENT
			&& rel->rd_rel->tabletype != TABLETYPE_URELATION)
		certain = true;

	relation_close(rel, NoLock);

	return certain;
}

/**
 * isCertainSelect
 * 		Checks whether a Select statement produces a certain result.
 *
 * This function takes into account the repairkey field.
 */
bool isCertainSelect(const SelectStmt *select)
{
	if (select->repairkey != NULL || select->pickingType == 'I')
	{
		/* TODO: possible optimization: if the repair-key attributes
		 * substitute a superkey, the result of repair-key is certain if the
		 * underlying query is certain. */
		return false;
	}

	return isCertainSimpleSelect(select);
}

/**
 * isCertainSimpleSelect
 * 		Checks whether a Select statement produces a certain result.
 *
 * This function ignores the repairkey and pickingType fields;
 * these cases are checked in the function isCertain()
 */
bool isCertainSimpleSelect(const SelectStmt *select)
{
	bool certain = false;
	List *targetList;

	if (select->possible)
		return true;

	if (select->op != SETOP_NONE)
		return isCertain((Node*) select->larg) && isCertain(
				(Node*) select->rarg);

	/* targetlist: check whether select computes confidence or
	 * an approximate aggregate */
	targetList = select->targetList;
	/* TODO: check whether a more complex expression involving conf or
	 * approximate predicate is used */
	if (hasApproxAggregate(targetList))
	{
		return true;
	}

	/* from clause: check whether all items are certain */
	if (!isCertainFromClause(select->fromClause))
		return false;
	/* where condition: check whether where condition is certain */
	certain = isCertainWhereClause(select->whereClause);
	return certain;
}

/**
 * isCertainFromClause
 * 		Checks whether all items in a from clause are certain.
 *
 */
bool isCertainFromClause(List *fromClause)
{
	/* from clause: check whether all items are certain */
	ListCell *cell;
	foreach(cell, fromClause)
	{
		Node *node = lfirst(cell);
		if (!isCertain(node))
			return false;
	}
	return true;
}

/**
 * isCertainWhereClause
 * 		Checks whether a where condition is certain.
 *
 * A condition is uncertain if there is a subquery introduced with
 * exists/in/notin which is not later closed with one of the constructs that
 * close the possible-worlds semantics (conf, tconf, asum,...)
 */
bool isCertainWhereClause(const Node* whereClause)
{
	if (whereClause == NULL)
		return true;
	if (IsA(whereClause, ColumnRef)|| IsA(whereClause, A_Const))
	return true;

	if (IsA(whereClause, A_Expr))
	{
		A_Expr *a_expr = (A_Expr*) whereClause;
		A_Expr_Kind kind = a_expr->kind;
		if (kind == AEXPR_OP || kind == AEXPR_AND || kind == AEXPR_OR)
		{
			Node *lexpr = a_expr->lexpr;
			Node *rexpr = a_expr->rexpr;
			return isCertainWhereClause(lexpr) && isCertainWhereClause(rexpr);
		}
		else if (kind == AEXPR_NOT)
		return isCertainWhereClause(a_expr->rexpr);
		else
		return false;
		/* TODO: what other expressions do we need to check for */
	}
	else if (IsA(whereClause, SubLink))
	{
		SubLink *sublink = (SubLink*) whereClause;
		return (sublink->subLinkType == EXISTS_SUBLINK ||
				sublink->subLinkType == ANY_SUBLINK ||
				sublink->subLinkType == EXPR_SUBLINK) &&
		isCertain(sublink->subselect);
	}
	return false;
}

/**
 * requiresRewriting
 * 		checks whether a query needs to be rewritten
 *
 * The function analyzes the parsetree to see whether there are references to
 * uncertain relations, or the repair-key construct is used.
 */
bool requiresRewriting(const Node *node)
{

	InsertStmt *insert;
	UpdateStmt *update;
	DeleteStmt *delete;
	SelectStmt *select;
	JoinExpr *join;
	AlterTableStmt *alterstmt;

	if (node == NULL)
		return false;

	switch (nodeTag(node))
	{
		case T_RangeVar:
			return !isCertain(node);
		case T_JoinExpr:
			join = (JoinExpr*) node;
			return requiresRewriting(join->larg) || requiresRewriting(
					join->rarg);
		case T_FuncCall:
		case T_RangeFunction:
			/* TODO: do we need more checks? */
			return false;
		case T_RangeSubselect:
			if (requiresRewriting(((RangeSubselect*) node)->subquery))
				return true;
			else
				return false;
		case T_SelectStmt:
			if (requiresRewritingSelect((SelectStmt *) node))
				return true;
			else
				return false;
		case T_ViewStmt:
			return requiresRewriting(((ViewStmt *) node)->query);
		case T_CreateStmt:
			return (((CreateStmt *) node)->tabletype != TABLETYPE_CERTAIN);
		case T_InsertStmt:
			insert = (InsertStmt *) node;
			/* if the target relation is uncertain the insert
			 * must be rewritten*/
			if (!isCertainRelation(insert->relation))
				return true;
			select = (SelectStmt *) insert->selectStmt;
			if (select && select->valuesLists != NIL
					&& requiresRewritingSelect(select))
				return true;
			return false;
		case T_DeleteStmt:
			delete = (DeleteStmt *) node;
			if (!isCertain((Node*) delete->relation)
					|| requiresRewritingWhereClause(delete->whereClause))
				return true;
			return false;
		case T_UpdateStmt:
			update = (UpdateStmt *) node;
			if (!isCertainRelation(update->relation)
					|| requiresRewritingFromClause(update->fromClause)
					|| requiresRewritingWhereClause(update->whereClause))
				return true;
			return false;
		case T_AlterTableStmt:
			alterstmt = (AlterTableStmt *) node;
			if (!isCertain((Node*) alterstmt->relation))
				return true;
			else
				return false;
			/* statements that need no rewriting */
		case T_AlterDomainStmt:
		case T_GrantStmt:
		case T_GrantRoleStmt:
		case T_ClosePortalStmt:
		case T_ClusterStmt:
		case T_CopyStmt:
		case T_DefineStmt:
		case T_DropStmt:
		case T_TruncateStmt:
		case T_CommentStmt:
		case T_FetchStmt:
		case T_IndexStmt: /* TODO: create index stmt: supported? */
		case T_CreateFunctionStmt:
		case T_AlterFunctionStmt:
		case T_RemoveFuncStmt:
		case T_RenameStmt:
		case T_RuleStmt: /* TODO: create rule stmt: supported? */
		case T_NotifyStmt:
		case T_ListenStmt:
		case T_UnlistenStmt:
		case T_TransactionStmt:
		case T_LoadStmt:
		case T_CreateDomainStmt:
		case T_CreatedbStmt:
		case T_DropdbStmt:
		case T_VacuumStmt:
		case T_ExplainStmt: /* TODO: supported? */
		case T_CreateSeqStmt:
		case T_AlterSeqStmt:
		case T_VariableSetStmt:
		case T_VariableShowStmt:
		case T_DiscardStmt:
		case T_CreateTrigStmt: /* TODO: supported? */
		case T_DropPropertyStmt:
		case T_CreatePLangStmt:
		case T_DropPLangStmt:
		case T_CreateRoleStmt:
		case T_AlterRoleStmt:
		case T_DropRoleStmt:
		case T_LockStmt:
		case T_ConstraintsSetStmt:
		case T_ReindexStmt:
		case T_CheckPointStmt:
		case T_CreateSchemaStmt:
		case T_AlterDatabaseStmt:
		case T_AlterDatabaseSetStmt:
		case T_AlterRoleSetStmt:
		case T_CreateConversionStmt:
		case T_CreateCastStmt:
		case T_DropCastStmt:
		case T_CreateOpClassStmt:
		case T_CreateOpFamilyStmt:
		case T_AlterOpFamilyStmt:
		case T_RemoveOpClassStmt:
		case T_RemoveOpFamilyStmt:
		case T_PrepareStmt: /* TODO: supported? */
		case T_ExecuteStmt: /* TODO: supported? */
		case T_DeallocateStmt: /* TODO: supported? */
		case T_DeclareCursorStmt: /* TODO: supported? */
		case T_CreateTableSpaceStmt:
		case T_DropTableSpaceStmt:
		case T_AlterObjectSchemaStmt:
		case T_AlterOwnerStmt:
		case T_DropOwnedStmt:
		case T_ReassignOwnedStmt:
		case T_CompositeTypeStmt:
		case T_CreateEnumStmt:
		case T_AlterTSDictionaryStmt:
		case T_AlterTSConfigurationStmt:
			return false;
		default:
			return true;
	}
}

/**
 * requiresRewritingSelect
 * 		checks whether a select statement needs to be rewritten
 *
 * The function analyzes the parsetree to see whether there are references to
 * uncertain relations, or the repair-key construct is used.
 */
bool requiresRewritingSelect(const SelectStmt *select)
{
	int i;
	List *fromClause;
	List *targetList;

	if (select->repairkey)
		return true;
	if (select->pickingType == 'I')
		return true;
	if (select->op != SETOP_NONE)
		return requiresRewritingSelect(select->larg)
				|| requiresRewritingSelect(select->rarg);

	/* select->op == SETOP_NONE: no set operation */

	/* check whether the from clause needs rewriting */
	fromClause = select->fromClause;
	if (requiresRewritingFromClause(fromClause))
	{
		/*elog(WARNING, "From clause needs rewriting");
		elog(WARNING,  pretty_format_node_dump(nodeToString(fromClause)));*/
		return true;
	}

	/* check whether target list needs rewriting */
	targetList = select->targetList;
	for (i = 0; i < list_length(targetList); ++i)
	{
		ResTarget* resTgt = (ResTarget*) list_nth(targetList, i);
		TypeCast* tc;
		SubLink *sublink;
		NullTest *nt;
		CoalesceExpr *ce;
		ListCell *cell;
		Node *n;
		RowExpr *row_epr;
		XmlSerialize *xml_serialize;
		
		switch (nodeTag(resTgt->val))
		{
			/*elog(WARNING, "node type: %d", resTgt->val->type);*/
			case T_TypeCast:
				tc = (TypeCast*) resTgt->val;
				if (IsA(tc->arg, ColumnRef)|| IsA(tc->arg, A_Const) ||
				IsA(tc->arg, FuncCall) || IsA(tc->arg, CaseExpr))
				continue;
				/* TODO: can the argument of the typecast be a subquery */
				break;
			case T_ColumnRef:
			case T_A_Const:
				continue;
			case T_FuncCall:
				if (isApproxAggregate((FuncCall *) resTgt->val))
				{
					/*elog(WARNING, "approx aggregate: requires rewriting!");*/
					return true;
				}
				continue;
			case T_A_Expr:
				continue;
				/* TODO: fix */
				if (requiresRewritingWhereClause(resTgt->val))
				return true;
				break;
			case T_CaseExpr:
				/* TODO: do we need more checks here? */
				continue;
			case T_SubLink:
				sublink = (SubLink *) resTgt->val;
				if (requiresRewriting(sublink->subselect)) {
					return true;
				}
				break;
			case T_A_Indirection:
				/* TODO: fix */
				continue;
				if (requiresRewritingWhereClause(((A_Indirection *) resTgt->val)->arg))
					return true;
				break;
			case T_CoalesceExpr:
				ce = (CoalesceExpr *) resTgt->val;
				foreach(cell, ce->args) 
				{
					n = (Node *) lfirst(cell);
					if (requiresRewritingWhereClause(n))
						return true;				
				}
			case T_NullTest:
				nt = (NullTest *) resTgt->val;
				if (requiresRewritingWhereClause((Node *)nt->arg))
					return true;
				break;
			case T_ArrayExpr:
				/* TODO: more checks?*/
				continue;
			case T_RowExpr:
				row_epr = (RowExpr *) resTgt->val;
				foreach(cell, row_epr->args) 
				{
					n = (Node *) lfirst(cell);
					if (requiresRewritingWhereClause(n))
						return true;				
				}
				break;
			case T_XmlExpr:
				/* TODO: more checks? */
				continue;
			case T_XmlSerialize:
				xml_serialize = (XmlSerialize *) resTgt->val;
				if (requiresRewritingWhereClause(xml_serialize->expr))
					return true;
				break;
			default:
				/*elog(WARNING, "Target list item type %d", resTgt->val->type);*/
				return true;
				/* TODO: see if we can support other types in the target list */
		}
	}
	/* check whether where clause requires rewriting */
	if (requiresRewritingWhereClause(select->whereClause))
	{
		/*elog(WARNING, "where clause needs rewriting!");
		elog(WARNING,  pretty_format_node_dump(select->whereClause));*/
		return true;
	}
	return false;
}

/**
 * requiresRewritingFromClause
 * 		checks whether there is an item in the given from clause that
 * 		needs to be rewritten
 */
bool requiresRewritingFromClause(List *fromClause)
{
	bool result;
	int i;

	if (fromClause == NIL)
		return false;

	for (i = 0; i < list_length(fromClause); ++i)
	{
		Node* fromCl_item = (Node*) list_nth(fromClause, i);
		if (IsA(fromCl_item, RangeVar))
		{
			if (!isCertain(fromCl_item))
				return true;
			else
				continue;
		}
		else if (IsA(fromCl_item, RangeSubselect))
		{
			result = requiresRewriting(
					((RangeSubselect*) fromCl_item)->subquery);
			if (result)
			{
				return true;
			}
			else
			{
				continue;
			}
		}
		else if (IsA(fromCl_item, JoinExpr))
		{
			JoinExpr *join = (JoinExpr*) fromCl_item;
			result = requiresRewriting(join->larg) || requiresRewriting(
					join->rarg);
			if (result)
				return true;
			else
				continue;
		}
		else if (IsA(fromCl_item, FuncCall)|| IsA(fromCl_item, RangeFunction))
		{
			/* TODO: do we need any other checks here? */
			continue;
		}
		else
		/* TODO: check for other types of expressions in the from clause */
		return true;
	}
	return false;
}

/**
 * requiresRewritingWhereClause
 * 		checks whether a where condition needs to be rewritten
 */
bool requiresRewritingWhereClause(const Node *node)
{
	A_Expr *a_expr;
	SubLink *sublink;
	TypeCast *tc;
	CoalesceExpr *ce;
	ListCell *cell;
	Node *n;
	RowExpr *row_epr;
	
	if (node == NULL)
	{
		return false;
	}

	switch (nodeTag(node))
	{
		case T_ColumnRef:
		case T_A_Const:
		case T_NullTest:
			return false;
		case T_TypeCast:
			tc = (TypeCast*) node;
			if (IsA(tc->arg, ColumnRef)|| IsA(tc->arg, A_Const) ||
			IsA(tc->arg, FuncCall) || IsA(tc->arg, CaseExpr))
				return false;
			if (IsA(tc->arg, A_Expr)) {
				a_expr = (A_Expr*) tc->arg;
				return requiresRewritingExpression(a_expr);
			}
			break;
		case T_A_Expr:
			a_expr = (A_Expr*) node;
			if (requiresRewritingExpression(a_expr)) {
				/*elog(WARNING, "expression requires rewriting");
				elog(WARNING,  pretty_format_node_dump(nodeToString(a_expr)));*/
				return true;
			}
			// return requiresRewritingExpression(a_expr);
			else
				return false;
		case T_CaseExpr:
			/* TODO: do we need more checks here? */
				return false;
		case T_FuncCall:
			/* TODO: do we need more checks here? */
				return false;
		case T_SubLink:
			sublink = (SubLink*) node;
			/*elog(WARNING, "requiresRewritingWhereClause: sublink");*/
			return requiresRewriting(sublink->subselect);
		case T_BooleanTest:
			return requiresRewritingWhereClause((Node *)((BooleanTest *) node)->arg);
		case T_A_Indirection:
			return requiresRewritingWhereClause(((A_Indirection *) node)->arg);
		case T_CoalesceExpr:
			ce = (CoalesceExpr *) node;
			foreach(cell, ce->args) 
			{
				n = (Node *) lfirst(cell);
				if (requiresRewritingWhereClause(n))
					return true;				
			}
			return false;
		case T_CurrentOfExpr:
			/* TODO: more tests?*/
			return false;
		case T_ArrayExpr:
			/* TODO: more tests?*/
				return false;
		case T_RowExpr:
			row_epr = (RowExpr *) node;
			foreach(cell, row_epr->args) 
			{
				n = (Node *) lfirst(cell);
				if (requiresRewritingWhereClause(n))
					return true;				
			}
			return false;
		case T_XmlExpr:
			/* TODO: more checks? */
			return false;
		default:
			/* TODO: do we support any other where clause expressions? */
			/*elog(WARNING, "expression type is %d", node->type);*/
			return true;
	}
	return true;
}

bool requiresRewritingExpression(A_Expr *a_expr)
{
	A_Expr_Kind kind;

	kind = a_expr->kind;
	if (kind == AEXPR_OP || kind == AEXPR_AND || kind == AEXPR_OR || kind
			== AEXPR_DISTINCT || kind == AEXPR_OP_ANY || kind == AEXPR_NULLIF)
	{
		Node *lexpr = a_expr->lexpr;
		Node *rexpr = a_expr->rexpr;
		if (requiresRewritingWhereClause(lexpr)) {
			/*elog(WARNING, "Expression requires rewriting");
			elog(WARNING,  pretty_format_node_dump(nodeToString(lexpr)));*/
			return true;
		}
		if (requiresRewritingWhereClause(rexpr)) {
			/*elog(WARNING, "Expression requires rewriting");
			elog(WARNING,  pretty_format_node_dump(nodeToString(rexpr)));*/
			return true;
		}
		return false;
	}
	if (kind == AEXPR_NOT)
		return requiresRewritingWhereClause(a_expr->rexpr);
	if (kind == AEXPR_IN)
		/* TODO: do we need to check the list of in-expressions? */
		return false;

	/* TODO: what other expressions do we need to check for */
	/*elog(WARNING, "expression type: %d", kind);*/
	return true;
}
/**
 * isSupported
 * 		checks whether a query or update operation is supported
 */
bool isSupported(const Node *node, char **error)
{
	SelectStmt *select;
	AlterTableStmt *alterstmt;

	switch (nodeTag(node))
	{
		case T_AlterTableStmt:
			alterstmt = (AlterTableStmt *) node;
			// TODO: should we allow alter table statements that do not touch
			// the condition columns?
			/*if (!isCertain((Node*) alterstmt->relation))
			{
				*error = err_msg("ALTER TABLE not supported on uncertain "
					"relations.");
				return false;
			}*/
			break;
		case T_ViewStmt:
			/* TODO: do not support Views with repairkey since repairkey
			 * produces new variable names each time it is executed */
			return isSupported(((ViewStmt*) node)->query, error);
		case T_CreateStmt:
			/* simple create table statement that creates an empty relation. */
			/* TODO: do we need some more checks? */
			return true;
		case T_SelectStmt:
			select = (SelectStmt*) node;
			return isSupportedSelect(select, error);
		case T_InsertStmt:
			return isSupportedInsert((InsertStmt*) node, error);
		case T_UpdateStmt:
			return isSupportedUpdate((UpdateStmt *) node, error);
		case T_DeleteStmt:
			return isSupportedDelete((DeleteStmt *) node, error);
		default:
			return false;
	}
	return true;
}

/**
 * isSupportedSelect
 * 		checks whether a Select query is supported
 *
 * This function takes into account whether repair-key or pick-tuples
 * is specified.
 */
bool isSupportedSelect(const SelectStmt *select, char **error)
{
	bool supported = true;

	if (!isSupportedSimpleSelect(select, error))
		return false;
	if (select->repairkey != NULL || select->pickingType == 'I')
	{
		/* query must be certain */
		if (select->repairkey != NULL && select->pickingType == 'I')
			supported = false;
		else
		{
			supported = select->possible || hasApproxAggregate(
					select->targetList) || (isCertainFromClause(
					select->fromClause) && isCertainWhereClause(
					select->whereClause));
		}
		if (!supported)
		{
			*error = err_msg("REPAIR KEY and PICK TUPLES not supported on "
				"uncertain relations.");
			return false;
		}
	}
	return supported;
}

/**
 * isSupportedSimpleSelect
 * 		checks whether a Select query is supported
 *
 * This function ignores whether repair-key or pick-tuples is specified;
 * those cases are treated in isSupported.
 */
bool isSupportedSimpleSelect(const SelectStmt *select, char **error)
{
	bool result;
	int i;
	List *fromClause;
	List *targetList;
	bool has_conf;
	int ltriplecount, rtriplecount;

	if (select->op == SETOP_UNION)
	{
		if (!isSupportedSelect(select->larg, error))
			return false;
		if (!isSupportedSelect(select->rarg, error))
			return false;

		if (!select->all)
		{
			/* UNION with duplicate elimination -> the two sides of the union
			 * must be certain */
			result = isCertainSelect(select->larg) && isCertainSelect(
					select->rarg);
			if (!result)
			{
				*error = err_msg(
						"UNION supported only on certain relations. "
							"Use \'UNION ALL\' for uncertain ones.");
				return false;
			}
			return true;
		}
		/* TODO: check that the schemas of the two arguments are the same with
		 * respect to the data columns */
		ltriplecount = getTripleCountSelect(select->larg);
		rtriplecount = getTripleCountSelect(select->rarg);
		if (ltriplecount != rtriplecount)
		{
			*error = err_msg("UNION queries have "
				"different number of condition columns.");
			return false;
		}
		return true;
	}
	if (select->op == SETOP_INTERSECT || select->op == SETOP_EXCEPT)
	{
		if (!isSupportedSelect(select->larg, error))
			return false;
		if (!isSupportedSelect(select->rarg, error))
			return false;
		result = isCertainSelect(select->larg) && isCertainSelect(
				select->rarg);
		/* TODO: add support for difference when the left operand is not
		 * certain */
		if (!result)
		{
			*error = err_msg(
					"set operations supported on certain relations "
						"only.");
			return false;
		}
		return true;
	}

	/* select->op == SETOP_NONE: no set operation */

	/* check whether the from clause is supported */
	if (select->possible && hasApproxAggregate(select->targetList))
	{
		*error
				= err_msg(
						"no confidence computation functions allowed with possible.");
		return false;
	}

	fromClause = select->fromClause;
	for (i = 0; i < list_length(fromClause); ++i)
	{
		Node* fromCl_item = (Node*) list_nth(fromClause, i);
		if (IsA(fromCl_item, RangeVar))
			continue;
		else if (IsA(fromCl_item, RangeSubselect))
		{
			result = isSupported(((RangeSubselect*) fromCl_item)->subquery,
					error);
			if (!result)
				return false;
		}
		else if (IsA(fromCl_item, JoinExpr))
		{
			*error = err_msg("JOIN expression currently not supported.");
			return false;
			/* TODO: add support for joins */
		}
		else
		{
			/* TODO: check for other supported expressions in the from clause */
			*error = err_msg("unsupported expression in the FROM clause.");
			return false;
		}
	}

	/* distinct and group by operations only supported on certain queries */
	if (select->distinctClause != NULL || select->groupClause != NULL)
	{
		result = isCertainSimpleSelect(select);
		if (!result)
		{
			*error = err_msg(
					"DISTINCT and GROUP BY not allowed on uncertain "
						"relations.");
			return false;
		}
	}

	/* check whether target list is supported */

	targetList = select->targetList;
	has_conf = false;
	for (i = 0; i < list_length(targetList); ++i)
	{
		ResTarget* resTgt = (ResTarget*) list_nth(targetList, i);
		if (IsA(resTgt->val, TypeCast))
		{
			TypeCast* tc = (TypeCast*) resTgt->val;
			if (IsA(tc->arg, ColumnRef)|| IsA(tc->arg, A_Const))
			continue;
		}
		else if (IsA(resTgt->val, ColumnRef) || IsA(resTgt->val, A_Const))
		continue;
		else if (IsA(resTgt->val, FuncCall))
		{
			/* check whether this is a conf operation */
			/* TODO: check whether one of the above is involved in a supported
			 * arithmetic expression */
			FuncCall *func = (FuncCall*) resTgt->val;
			if (!isApproxAggregate(func))
			{
				/* check whether all items in the from clause and the where
				 * clause are certain */
				if (!isCertainFromClause(select->fromClause)
						|| !isCertainWhereClause(select->whereClause))
				{
					*error = err_msg("function calls only allowed on certain "
							"relations.");
					return false;
				}
			}
			else
			{
				if (has_conf)
				{
					/* only allow one confidence/approximate aggregate
					 * per target list */
					*error = err_msg("multiple approximate aggregation "\
							"nodes not supported.");
					return false;
				}
				has_conf = true;
			}
		}
		else if (!isSupportedNode(resTgt->val, select, error))
		{
			return false;
		}
		/* TODO: see if we can support other types in the target list */
	}

	/* check whether where clause is supported */
	return isSupportedNode(select->whereClause, select, error);
}

/**
 * isSupportedNode
 *  	check whether a node appearing in a where condition or target list
 *  	is supported
 *
 *  If the node is a condition that contains a subquery returning uncertain
 *  result, the condition is supported iff the select statement to which the
 *  where clause belongs to is certain.
 */
bool isSupportedNode(const Node *node, const SelectStmt *select, char **error)
{
	A_Expr *a_expr;
	A_Expr_Kind kind;
	SubLink *sublink;

	if (node == NULL)
		return true;

	switch (nodeTag(node))
	{
		case T_ColumnRef:
		case T_A_Const:
			return true;
		case T_A_Expr:
			a_expr = (A_Expr*) node;
			kind = a_expr->kind;
			if (kind == AEXPR_OP || kind == AEXPR_AND || kind == AEXPR_OR)
			{
				Node *lexpr = a_expr->lexpr;
				Node *rexpr = a_expr->rexpr;
				if (!isSupportedNode(lexpr, select, error))
					return false;
				if (!isSupportedNode(rexpr, select, error))
					return false;
				if (hasApproxAggregate(list_make1(a_expr->lexpr)) &&
					hasApproxAggregate(list_make1(a_expr->rexpr)))
				{
					*error = err_msg("multiple approximate aggregation "\
							"nodes not supported.");
					return false;
				}
				return true;
			}
			if (kind == AEXPR_NOT)
			{
				if (!isSupportedNode(a_expr->rexpr, select, error))
					return false;
				if (!isCertainWhereClause(a_expr->rexpr))
				{
					*error = err_msg(
							"NOT only supported on certain expressions.");
					return false;
				}
				else
					return true;
			}
			/*elog(WARNING,  pretty_format_node_dump(nodeToString(node)));*/
			*error = err_msg("unsupported expression node");
			return false;
		case T_SubLink:
			sublink = (SubLink*) node;
			if (sublink->subLinkType != EXISTS_SUBLINK
					&& sublink->subLinkType != ANY_SUBLINK
					&& sublink->subLinkType != EXPR_SUBLINK)
			{
				*error = err_msg("subquery in WHERE clause currently not "
					"supported.");
				return false;
			}
			/* EXISTS/IN query */
			if (!isSupported(sublink->subselect, error))
				return false;
			if (!isCertain(sublink->subselect))
			{
				*error = err_msg(
						"currently only certain subqueries supported in "
							"WHERE clause.");
				return false;
			}
			/*TODO: if the subselect is correlated with the
			 * containing select statement -> not supported!! */
			return true;
		case T_FuncCall:
			if (isApproxAggregate((FuncCall*) node))
				return true;
			else if (isCertainFromClause(select->fromClause)
					&& isCertainWhereClause(select->whereClause))
				return true;
			else
			{
				*error = err_msg("function calls not supported on "\
									"uncertain relations.");
				return false;
			}
		default:
			/* TODO: do we support any other where clause expressions? */
			/* any other where clause type requires certain relations */
			if (isCertainFromClause(select->fromClause)
					&& isCertainWhereClause(select->whereClause))
				return true;
			else
			{
				/*elog(WARNING, pretty_format_node_dump(nodeToString(node)));*/
				*error = err_msg("unsupported expression");
				return false;
			}
	}
}

/**
 * isSupportedInsert
 *  	check whether an insert statement is supported
 *
 */
bool isSupportedInsert(const InsertStmt *insert, char **error)
{
	SelectStmt *subquery = (SelectStmt *) insert->selectStmt;
	if (subquery == NULL)
	{
		/* default values specified */
		*error = err_msg(
				"DEFAULT VALUES in INSERT currently not supported.");
		return false;
	}

	/* check whether insert specifies column names and whether it refers to
	 * a condition column */
	if (insert->cols != NIL)
	{
		ListCell *cell;
		foreach(cell, insert->cols) 
		{
			ResTarget *col = (ResTarget *) lfirst(cell);
			char *name = col->name;
			if (isConditionAttribute(name))
			{
				/* TODO: add support for inserting condition attributes */
				*error = err_msg(
						"no direct manipulation allowed on condition"
							" columns.");
				return false;
			}
		}
	}
	if (subquery->valuesLists != NIL)
	{
		/* a list of value expression specified */
		/* TODO: do we need to check the supplied value lists? */
	}
	else
	{
		/* subquery specified */
		if (!isSupportedSelect(subquery, error))
			return false;
	}
	return true;
}

/**
 * isSupportedUpdate
 *  	check whether an update statement is supported
 *
 */
bool isSupportedUpdate(const UpdateStmt *update, char **error)
{
	ListCell *cell;

	if (update->fromClause)
	{
		/* TODO: allow from clause in the update in case the relation that is
		 * updated and the subqueries are certain
		 */
		*error = err_msg("no FROM clause allowed in UPDATE statements.");
		return false;
	}

	/* the where clause must not have an uncertain query inside as this will
	 * require a join with the updated relation which will potentially increase
	 * the number of condition columns */
	if (!isCertainWhereClause(update->whereClause))
	{
		*error = err_msg(
				"WHERE condition of UPDATE statement must be certain.");
		return false;
	}

	/* target list: check whether a condition attributes is being updated */
	foreach(cell, update->targetList)
	{
		ResTarget *col = (ResTarget *) lfirst(cell);
		char *name = col->name;
		if (isConditionAttribute(name))
		{
			/* TODO: add support for updating condition attributes */
			*error = err_msg("no direct manipulation allowed on condition"
				" columns.");
			return false;
		}
	}
	return true;
}

/**
 * isSupportedDelete
 *  	check whether a delete statement is supported
 *
 */
bool isSupportedDelete(const DeleteStmt *delete, char **error)
{
	if (delete->usingClause != NULL)
	{
		/* TODO: allow using clause if everything is certain */
		*error = err_msg("USING clause not supported in DELETE.");
		return false;
	}

	if (!isSimpleCondition(delete->whereClause))
	{
		*error = err_msg("subqueries in WHERE condition of DELETE not "
			"supported");
		return false;
	}
	return true;
}

/**
 * hasApproxAggregate
 * 		checks whether a target list contains a reference to an approximate
 * 		aggregate or confidence computation operator
 * */
bool hasApproxAggregate(List *targetList)
{
	return lookup_func_in_list(targetList, CONF) != NULL
			|| lookup_func_in_list(targetList, TUPLECONF) != NULL
			|| lookup_func_in_list(targetList, ACONF) != NULL
			|| lookup_func_in_list(targetList, ARGMAX) != NULL
			|| lookup_func_in_list(targetList, ESUM) != NULL
			|| lookup_func_in_list(targetList, ECOUNT) != NULL;
}

/**
 * isApproxAggregate
 * 		checks whether a function call is referring to an approximate
 * 		aggregate or confidence computation operator
 * */
bool isApproxAggregate(FuncCall *func)
{
	return strcmp(strVal(linitial(func->funcname)), CONF) == 0 ||
	strcmp(strVal(linitial(func->funcname)), ACONF) == 0 ||
	strcmp(strVal(linitial(func->funcname)), TUPLECONF) == 0 ||
	strcmp(strVal(linitial(func->funcname)), ESUM) == 0 ||
	strcmp(strVal(linitial(func->funcname)), ECOUNT) == 0 ||
	strcmp(strVal(linitial(func->funcname)), ARGMAX) == 0;
}

/**
 * isSimpleCondition
 *  	check whether the specified condition is simple, that is it contains no
 *  	subqueries
 *
 */
bool isSimpleCondition(const Node *cond)
{
	A_Expr *a_expr;
	A_Expr_Kind kind;

	if (cond == NULL)
		return true;

	switch (nodeTag(cond))
	{
		case T_ColumnRef:
		case T_A_Const:
			return true;
		case T_A_Expr:
			a_expr = (A_Expr*) cond;
			kind = a_expr->kind;
			if (kind == AEXPR_OP || kind == AEXPR_AND || kind == AEXPR_OR)
			{
				Node *lexpr = a_expr->lexpr;
				Node *rexpr = a_expr->rexpr;
				return isSimpleCondition(lexpr) && isSimpleCondition(rexpr);
			}
			if (kind == AEXPR_NOT)
				return isSimpleCondition(a_expr->rexpr);
			return false;
		default:
			return false;
	}
}

/**
 * isConditionAttribute
 *  	check whether the supplied name denotes a condition attribute by
 *  	whether it's prefixed by one of the predefined prefixes for condition
 *  	attributes.
 *
 */
bool isConditionAttribute(const char *name)
{
	/* check whether the given name is prefixed by VARNAME, PROBNAME  or
	 * DOMAINNAME */
	char* s = strstr(name, VARNAME);
	if (s != NULL && s == name)
		return true;

	s = strstr(name, PROBNAME);
	if (s != NULL && s == name)
		return true;

	s = strstr(name, DOMAINNAME);
	if (s != NULL && s == name)
		return true;

	return false;
}

char *err_msg(const char *error)
{
	char *result = palloc((strlen(error) + 1) * sizeof(char));
	strcpy(result, error);
	return result;
}
