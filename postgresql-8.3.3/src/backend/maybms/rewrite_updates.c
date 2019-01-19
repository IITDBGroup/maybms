/*
 * rewrite_updates.c
 *	implementation of the functions rewriting SQL insert/update/delete queries
 *
 * Copyright (c) 2008, MayBMS Development Group
 *
 */



#include "postgres.h"
#include "nodes/parsenodes.h"
#include "nodes/primnodes.h"
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


/**
 * Returns the size of a ws-descriptor in the result of a query
 */
int getTripleCount(Node *node)
{
	RangeVar *rv;
	SelectStmt *select;
	int count;
	int condition_attr_count;

	count = 0;
	condition_attr_count = 0;
	
	if (isCertain(node))
		return 0;

	switch(nodeTag(node))
	{
		case T_RangeVar:
			rv = (RangeVar *) node;
			count = getTripleCountRelation(rv);
			return count;
		case T_SelectStmt:
			select = (SelectStmt*) node;			
			return getTripleCountSelect(select);
		/* TODO: support other nodes */
		default:
			elog(ERROR, "unsupported node!");
			return 0;
	}
}

int getTripleCountRelation(RangeVar *rv)
{
	Relation rel;
	char table_type;
	int i;
	int count;
	int condition_attr_count = 0;

	count = 0;
	
	rel = relation_openrv(rv, NoLock);
	table_type = rel->rd_rel->tabletype;

	if(table_type == TABLETYPE_INDEPENDENT)
		count= 1;
	else if (table_type == TABLETYPE_URELATION)
	{
		for (i = 0; i < rel->rd_att->natts; i++)
		{
			if(isConditionAttribute(NameStr(rel->rd_att->attrs[i]->attname)))
			{
				condition_attr_count = condition_attr_count + 1;
			}					
		}
		count = condition_attr_count / 3;
	}
	else if(table_type == TABLETYPE_CERTAIN)
		count = 0;
	
	relation_close(rel, NoLock);
	
	return count;
}

int getTripleCountSelect(SelectStmt *select)
{
	int *tripleCounts;
	int i;
	int count = 0;
	tripleCounts = getTripleCounts(select->fromClause);
	
	for (i = 0; i < list_length(select->fromClause); ++i)
		count += tripleCounts[i];
	return count;
}

/**
 * getAttributes
 * 		retrieves a list of ResTargets of all attributes of a relation,
 * 		including the condition attributes
 */
List *getAttributes(const RangeVar *rv)
{
	List *att_names = NULL;
	Relation rel = relation_openrv(rv, NoLock);

	int i;
	for (i = 0; i < rel->rd_att->natts; i++)
	{
		ResTarget *rs = makeNode(ResTarget);
		rs->name = NameStr(rel->rd_att->attrs[i]->attname);
		att_names = lappend(att_names, rs);
	}

	relation_close(rel, NoLock);
	return att_names;
}

/**
 * processInsert
 * 		rewrites an InsertStmt
 *
 * If the insert is specified using a subquery, the subquery is rewritten.
 * If the target relation is uncertain, we pad the ws-descriptors with the
 * right number of condition columns, in case the value lists or the specified
 * subquery of the insert have smaller arity. The default values for each triple
 * are generated as follows:
 * (nextval(VARIDSEQ), nextval(DOMAINIDSEQ), 1)
 *
 * Note: We assume that for each i, the i-th triple is either specified
 * completely by the insert, or is not present at all. Thus we only pad with
 * complete triples.
 * If the target table has less condition columns than the source, Postgres will
 * produce a runtime error when trying to evaluate the query.
 */
InsertStmt *processInsert(InsertStmt *insert)
{
	SelectStmt *subquery;
	int triples_to_insert;
	int *tripleCount;
	int i;
	int targetTripleCount;
	
	tripleCount = getTripleCounts(list_make1(insert->relation));
	targetTripleCount = tripleCount[0];
	
	if (insert->cols == NIL)
	{
		/* no column names specified: retrieve all from catalog
		 * including the condition columns */
		insert->cols = getAttributes(insert->relation);
	}
	else
	{
		/* Pad the column list with the condition columns */
		int i;
		for (i = 0; i < targetTripleCount; ++i)
		{
			/* TODO: check whether the ith triple is already in the
			 * column list*/
			ResTarget *d, *p;
			ResTarget *v = makeNode(ResTarget);
			v->name = catStrInt(VARNAME, i);
			d = makeNode(ResTarget);
			d->name = catStrInt(DOMAINNAME, i);
			p = makeNode(ResTarget);
			p->name = catStrInt(PROBNAME, i);
			insert->cols = list_concat(insert->cols, list_make3(v, d, p));
		}
	}

	subquery = (SelectStmt *) insert->selectStmt;
	
	if (subquery->valuesLists != NIL)
	{
		/* insert into <relname> <value_list> */
		ListCell *cell;
		
		triples_to_insert = targetTripleCount; 

		
		foreach(cell, subquery->valuesLists)
		{
			/* pad each values list with default values for the
			 * condition columns */
			List  *values_list = (List *) lfirst(cell);
						
			
			int i;
			for (i = 0; i < triples_to_insert; ++i)
			{
				/* generate a triple of the form:
					 nextval(VARIDSEQ), nextval(DOMAINIDSEQ), 1
				 */
				FuncCall *var_value = makeNextValFuncCall(VARIDSEQ);
				FuncCall *dom_value = makeNextValFuncCall(DOMAINIDSEQ);
				A_Const *prob_value = makeFloatConst("1.0");
				values_list = list_concat(values_list,
						list_make3(var_value, dom_value, prob_value));
			}
		}
		return insert;
	}

	/* insert into <relname> <subquery> */
	subquery = (SelectStmt*) process((Node *) subquery);

	tripleCount = getTripleCounts(list_make1(subquery)); 
	triples_to_insert = targetTripleCount - tripleCount[0];
	
	
	for (i = 0; i < triples_to_insert; ++i)
	{
		/* add a triple of the form:
			nextval(VARIDSEQ), nextval(DOMAINIDSEQ), 1
			to the target list of the subquery
		 */
		ResTarget *vrs = makeNode(ResTarget);
		ResTarget *drs, *prs;
		vrs->val = (Node *) makeNextValFuncCall(VARIDSEQ);
		drs = makeNode(ResTarget);
		drs->val = (Node *) makeNextValFuncCall(DOMAINIDSEQ);
		prs = makeNode(ResTarget);
		prs->val = (Node *) makeFloatConst("1.0");
		subquery->targetList = list_concat(subquery->targetList,
				list_make3(vrs, drs, prs));
	}

	insert->selectStmt = (Node *) subquery;

	/* TODO: insert newly created variables and domain values into W */
	return insert;
}

/**
 * processUpdate
 * 		rewrites an UpdateStmt
 */
UpdateStmt *processUpdate(UpdateStmt *update)
{
	update->whereClause = processWhereClause(update->whereClause);
	return update;
}

/**
 * processDelete
 * 		rewrites a DeleteStmt
 *
 * Currently only delete statements with simple where conditions are supported;
 * thus no rewriting is necessary.
 */
DeleteStmt *processDelete(DeleteStmt *delete)
{
	return delete;
	/* TODO: extend when support for more complex where conditions is added */
}
