/*
 * rewrite_updates.h
 *	rewriting of SQL insert/update/delete queries
 *
 * Copyright (c) 2008, MayBMS Development Group
 *
 */

#ifndef REWRITE_UPDATES_H_
#define REWRITE_UPDATES_H_

#include "nodes/parsenodes.h"

extern List *getAttributes(const RangeVar *rv);

extern InsertStmt *processInsert(InsertStmt *insert);
extern UpdateStmt *processUpdate(UpdateStmt *insert);
extern DeleteStmt *processDelete(DeleteStmt *insert);

extern int getTripleCount(Node *node);
extern int getTripleCountSelect(SelectStmt *select);
extern int getTripleCountRelation(RangeVar *rv);

#endif /* REWRITE_UPDATES_H_ */
