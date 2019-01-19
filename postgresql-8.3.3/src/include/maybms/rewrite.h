/*
 * rewrite.h
 *	  Rewriting queries for conf(), tconf(), aconf() and repair-key.
 *
 * Copyright (c) 2008, MayBMS Development Group
 *
 *-------------------------------------------------------------------------
 */

#ifndef REWRITE_H_
#define REWRITE_H_

#include "nodes/parsenodes.h"

#define TUPLECONF "tconf"
#define CONF "conf"
#define ACONF "aconf"
#define ARGMAX "argmax"
#define ESUM "esum"
#define ECOUNT "ecount"
#define TESTNEGATIVE "test_negative"
#define TESTFROMZEROTOONE "test_from_0_to_1"

#define DOMAINIDSEQ "domid"
#define VARIDSEQ "varid"
#define WORLDTABLE "world_table"

/* Utility functions */
extern A_Const *makeFloatConst(char *value);
extern ColumnDef *makeColumnDef(char *name, int count);
extern Constraint *makeDefaultConstraint(Node *default_expr);
extern FuncCall *makeNextValFuncCall(char *seqname);
extern List *nodeGetFields(Node *node);
extern Node *makeColumnRef(char *attr, int number, List *rel);
extern void printSubselect(SelectStmt *sel);
extern char *catStrInt(char *str, int number);
extern Node* processWhereClause(Node* whereClause);
extern int *getTripleCounts(List *flist);
extern ResTarget *makeResTargetWithColumnRef(char *resname, char *columnname);
extern RangeSubselect *makeRangeSubselect(char *alias, SelectStmt *sel);
extern SelectStmt* rewrite_set_operation(SelectStmt *sel);
extern void fill_where_clause(SelectStmt *sel);
extern List * remove_relation_reference(List *targetList);
extern ResTarget *type_cast_to_int4(ResTarget *res);
extern ResTarget *make_ResTarget_with_func_char(char *resname, char *funcname, char *constname);
extern ResTarget *type_cast_weight_by(ResTarget *weightby);
extern ResTarget *make_ResTarget_with_float(char *resname, char *constant);
extern ResTarget *make_ResTarget_with_int(char *resname, int constant);

/* Functions related to signature */
extern List *getVarOrder(sigNode *node, List *list);
extern sgList *geneSGList(A_Expr *node, sgList * sg_list, List *fromClause);
extern void buildSGTree(sgTreeNode *node, int parentSGListCount);

/* Functions related to HQ */
extern bool isHQ(sgList *list);

/* Functions related to function lookup */
extern int count_func_in_list(List *l, char *name);
extern FuncCall *lookup_func_in_list(List *l, char *name);

/* Interface of repair-key */
extern SelectStmt *ProcessRepairKey(SelectStmt *sel);

/* Interface of repair-key */
extern SelectStmt *ProcessPickTuples(SelectStmt *sel);

/* Interface of signature */
extern void ProcessSignature(SelectStmt *result, sgList	*sglist);

#endif /* REWRITE_H_ */
