/*
 * supported.h
 *		functions for verifying whether a query is supported, as well as for
 *		checking whether a query produces a t-certain result.
 *
 * Copyright (c) 2008, MayBMS Development Group
 *
 */

#ifndef SUPPORTED_H_
#define SUPPORTED_H_

/* functions for checking whether a query is t-certain */
extern bool isCertain(const Node *node);
extern bool isCertainRelation(const RangeVar *rv);
extern bool isCertainSelect(const SelectStmt *select);
extern bool isCertainSimpleSelect(const SelectStmt *select);
extern bool isCertainFromClause(List *fromClause);
extern bool isCertainWhereClause(const Node* whereClause);

/* check whether a query needs to be rewritten */
extern bool requiresRewriting(const Node *node);
extern bool requiresRewritingExpression(A_Expr *a_expr);

/* functions for checking whether a query is supported */
extern bool isSupported(const Node *node, char **error);
extern bool isSupportedSelect(const SelectStmt *select, char **error);
extern bool isSupportedSimpleSelect(const SelectStmt *select, char **error);
extern bool isSupportedNode(const Node *node, const SelectStmt *select,
		char **error);
extern bool isSupportedInsert(const InsertStmt *insert, char **error);
extern bool isSupportedUpdate(const UpdateStmt *update, char **error);
extern bool isSupportedDelete(const DeleteStmt *update, char **error);
extern bool isSimpleCondition(const Node *where);
extern bool isConditionAttribute(const char *name);

#endif /* SUPPORTED_H_ */
