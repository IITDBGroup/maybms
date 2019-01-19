/*-------------------------------------------------------------------------
 *
 * signature.c
 *	  Analyze and recognize the query signatures.   
 *
 * Copyright (c) 2008, MayBMS Development Group
 *
 *-------------------------------------------------------------------------
 */
/* In tuple-independent probabilistic databases, hierarchical queries admit 
 * PTIME evaluation. The technique used here for confidence computation in 
 * hierarchical queries are described in:
 *
 * D. Olteanu, J. Huang and C. Koch, SPROUT: Lazy vs. Eager Query Plans for 
 * Tuple-Independent Probabilistic Databases. In Proc. ICDE, 2009.
 * 
 * Before the confidence computation, a signature is needed. We generate the 
 * signature with following procedures:
 *
 * 1. Analyze the where clause and from clause of the select and generate the
 * subgoals for every join attributes.
 *
 * 2. Use the definition of hierarchical queries to decide whether the query 
 * admits PTIME evaluation.
 *
 * 3. If the query is hierarchical, build the partial order for the subgoals
 * of join attributes in a tree.  
 *
 * 4. Generate the tree representation of the signature with the tree in step 3.
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

#define SG_EQUAL 	0
#define SG_GREAT 	1
#define SG_LESS 	2
#define SG_DISJOINT	3
#define SG_INTERSECT	4
#define SG_UNKNOWCMP 5

/* Local functions */
static bool IsPK(subGoal *sg);
static bool sameRel(Node *n1, Node *n2);
static bool isSameAttr(sgList *list, List *fields);
static void addNewSG(sgList *list, List *fields, List *fromClause);
static subGoal *newSubGoal(List *fields, List *fromClause);
static Node *getRelation(List **address, List *fromClause);
static sgList *findDisjoint(sgList *list, sgList **newList);
static sgList *findMax(sgList *list, sgList **newList);
static sgList *rmNthLists(sgList *list, bool *rm, int n);
static sgList *rmNthList(sgList *list, int n);
static bool sameLists(sgList *list);
static sgList *getNthList(sgList *list, int n);
static int sgListcmp(sgList *l1, sgList *l2);
static bool sgInsgList(subGoal *sg, sgList *list);
static int sgListCount(sgList *list);
static int sgCount(sgList *list);
static bool relIsJoined(Node *rel, sgTreeNode *node);
static List *copyFromList(List *list);
static int nattr(subGoal *sg);
static bool sgInChildSGLists(subGoal *sg, sgTreeNode *sgNode);

/*  ProcessSignature
 *
 *  Process the signature of the query.
 */
void
ProcessSignature(SelectStmt *result, sgList	*sglist)
{
	/* Memory allocation */
	sgTreeRoot = palloc0(1 * sizeof(sgTreeNode)); 
	
	/* Set the subgoal list */
	sgTreeRoot->sglist = sglist;
	
	/* Build the subgoal tree */                  
	buildSGTree(sgTreeRoot, 0);  
	
	/* Get the list of relation names */                  
	relList = copyFromList(result->fromClause);   
	
	#ifdef HQ_TEST		
		drawsgNode(sgTreeRoot, 0);
	#endif
	
	/* Memory allocation for signature tree */      
	sigTreeRoot = palloc0(1 * sizeof(sigNode));  
	
	/* Build the signature tree */      
	buildSigTree(sgTreeRoot, sigTreeRoot); 
	
	/* Add the non-join relations to the signature. */
	sigTreeRoot = addNonJoinedRelation(sgTreeRoot, sigTreeRoot, relList); 

	/* Derive the positions of condition columns in the input tuples */
	derivePos(sigTreeRoot, 0);  
	
	/* Calculate the siblings of leaf nodes */
	calSib(sigTreeRoot);
	
	/* Calculate the domain of leaf nodes */		
	calDomain(sigTreeRoot);		  
	
	/* Compute whether the signature has 1scan property */
	isOneScan = is1Scan(sigTreeRoot); 
	
	/* Test code */
	#ifdef HQ_TEST	
		drawSig(sigTreeRoot, 0);
		
		if(isOneScan)
			myLog("OneScan\n");
		else
			myLog("Not one\n");
	#endif
}

/*  geneSGList
 *
 *  Generate a subgoal list.
 */
sgList *
geneSGList(A_Expr *node, sgList * sg_list, List *fromClause)
{
	Node *lexpr;
	Node *rexpr;
	List *name;
	ColumnRef *lcref, *rcref;
	char *op;
	sgList *temp, *list = sg_list;
	subGoal *lnew, *rnew;
	bool new_sgList = true, lIsSame = false, rIsSame = false;

	/* Exception handling */
	if (node == NULL)
		return NULL;

	lexpr = node->lexpr;
	rexpr = node->rexpr;
	name = node->name;

	/* Recursively generate the list */
	if (name == NULL)
	{
		list = geneSGList((A_Expr *) lexpr, list, fromClause);
		list = geneSGList((A_Expr *) rexpr, list, fromClause);
	}
	/* The node is named */
	else
	{
		op = strVal(linitial(node->name));

		/* If the operation is "=" and both operands are column references, add
		 * it to the subgoal list.
		 */
		if (strcmp(op, "=") == 0 && nodeTag(lexpr) == T_ColumnRef && nodeTag(rexpr) == T_ColumnRef)
		{

			lcref = (ColumnRef *) lexpr;
			rcref = (ColumnRef *) rexpr;

			temp = list;

		   /* If the list is not NULL, check whether the column references already
		    * exist.
		    */
			while (temp != NULL)
			{
				/* The left column reference exists */
				if (isSameAttr(temp, lcref->fields))
					lIsSame = true;
				/* The right column reference exists */
				if (isSameAttr(temp, rcref->fields))
					rIsSame = true;

				if (lIsSame && !rIsSame)
					addNewSG(temp, rcref->fields, fromClause);
				else if (!lIsSame && rIsSame)
					addNewSG(temp, lcref->fields, fromClause);

				if (lIsSame || rIsSame)
				{
					new_sgList = false;
					break;
				}

				temp = temp->next;
			}

			/* If a new list is needed, create one. */
			if (new_sgList)
			{
				lnew = newSubGoal(lcref->fields, fromClause);
				rnew = newSubGoal(rcref->fields, fromClause);
				lnew->next = rnew;

				temp = palloc0(1 * sizeof(sgList));
				temp->head = lnew;
				temp->next = list;
				list = temp;
			}

		}
	}

	return list;
}

/*  isSameAttr
 *
 *  Return true if the column already exists in the subgoal list.
 */
static bool 
isSameAttr(sgList *list, List *fields)
{
	subGoal *temp = list->head;

	/* Loop the subgoal list */
	while (temp != NULL)
	{
		if (list_length(temp->fields) == list_length(fields))
		{
			ListCell *f1, *f2;

			forboth(f1, temp->fields, f2, fields)
			{	
				if(strcmp(strVal(lfirst(f1)), strVal(lfirst(f2))) != 0)
				{					
					goto next;
				}
			}

			return true;
		}
		else
		{
			if(strcmp(strVal(llast(temp->fields)), strVal(llast(fields))) != 0)
				goto next;
			else
				return true;
		}
		next:;
		temp = temp->next;
	}

	return false;
}

/*  addNewSG
 *
 *  Add a new subgoal to the subgoal list.
 */
static void 
addNewSG(sgList *list, List *fields, List *fromClause)
{
	subGoal *temp = list->head;
	subGoal *new = newSubGoal(fields, fromClause);

	while (true)
	{
		if (temp->next == NULL)
		{
			temp->next = new;
			break;
		}
		
		temp = temp->next;
	}
}

/*  newSubGoal
 *
 *  Generate a new subgoal.
 */
static subGoal *
newSubGoal(List *fields, List *fromClause)
{
	subGoal *new = palloc0(1 * sizeof(subGoal));

	new->fields = fields;
	new->relation = getRelation(&(new->fields), fromClause);
	new->next = NULL;

	return new;
}

/*  calSib
 *
 *  Calculate the number of siblings of a leaf node.
 */
void 
calSib(sigNode *node)
{
	sigNode *child = node->firstChild;
	sigNode *sibling;
	int count = 0;

	if (child != NULL)
	{
		if (child->isLeaf)
		{
			sibling = child->rightSibling;

			while (sibling != NULL)
			{
				count++;
				sibling = sibling->rightSibling;
			}

			child->varsToCombine = count;
		}
	}

	child = node->firstChild;
	
	/* Loop the siblings */
	while (child != NULL)
	{
		calSib(child);
		child = child->rightSibling;
	}
}

/*  calDomain
 *
 *  Calculate the length of a sub-signature
 */
void 
calDomain(sigNode *node)
{
	sigNode *child = node->firstChild;
	sigNode *sibling;
	int count = 0;

	if (child != NULL)
	{
		if (child->isLeaf)
		{
			sibling = child->rightSibling;

			/* Accumulate the counts for all siblings */
			while (sibling != NULL)
			{
				count += leafDescendentCount(sibling);
				sibling = sibling->rightSibling;
			}

			child->domain = count;
		}
	}

	child = node->firstChild;
	
	/* Calculate the domains for other children */
	while (child != NULL)
	{
		calDomain(child);
		child = child->rightSibling;
	}
}

/*  leafDescendentCount
 *
 *  Count the leaf descendants of a node.
 */
int 
leafDescendentCount(sigNode *node)
{
	sigNode *child;
	int count = 0;

	if (node->isLeaf)
		return 1;
	else
	{
		child = node->firstChild;

		while (child != NULL)
		{
			count += leafDescendentCount(child);
			child = child->rightSibling;
		}

		return count;
	}
}

/*  is1Scan
 *
 *  Return true if the signature has 1scan property.
 */
bool 
is1Scan(sigNode *node)
{

	sigNode *child;

	if (node->isLeaf)
		return true;

	child = node->firstChild;

	while (child != NULL)
	{
		if (!is1Scan(child))
			return false;

		child = child->rightSibling;
	}

	child = node->firstChild;

	if (child->isLeaf && !(child->withStar))
		return true;

	return false;
}

/*  fillLeafNode
 *
 *  Fill the leaf nodes of a signature tree in an array.
 */
int 
fillLeafNode(sigNode *node, sigNode **vars)
{
	sigNode *child;
	int count = 0;

	if (node->isLeaf)
	{
		*vars = node;
		return 1;
	}
	else
	{
		child = node->firstChild;

		while (child != NULL)
		{
			count += fillLeafNode(child, vars + count);
			child = child->rightSibling;
		}

		return count;
	}
}

/*  getVarOrder
 *
 *  Generate the variable order in sorting.
 */
List *
getVarOrder(sigNode *node, List *list)
{
	sigNode *child;
	List *result = list;

	if (node->isLeaf)
	{
		if (result == NULL)
			result = list_make1(node->sg->fields);
		else
			result = lappend(result, node->sg->fields);
	}
	else
	{
		child = node->firstChild;

		while (child != NULL)
		{
			result = getVarOrder(child, result);
			child = child->rightSibling;
		}
	}

	return result;
}

/*  drawSig
 *
 *  Print the signature.
 */
void 
drawSig(sigNode *root, int n)
{
	int i;
	char s[10] = "";
	sigNode *child = root->firstChild; 

	for (i = 0; i < n; i++)
		strcat(s, "\t");

	myLog(s);
	myLog("isLeaf: ");
	myLogb(root->isLeaf);
	nl(1);
	myLog(s);
	myLog("withStar: ");
	myLogb(root->withStar);
	nl(1);
	myLog(s);
	myLog("type: ");
	myLogi(root->type);
	nl(1);
	myLog(s);
	myLog("pos: ");
	myLogi(root->pos);
	nl(1);
	myLog(s);
	myLog("domain: ");
	myLogi(root->domain);
	nl(1);
	myLog(s);
	myLog("varsTo: ");
	myLogi(root->varsToCombine);
	nl(1);
	myLog(s);
	myLog("pt: ");
	myLogp(root->pt);
	nl(1);
	myLog(s);
	myLog("--------------------- ");
	nl(1);

	while (child != NULL)
	{
		drawSig(child, n + 1);
		child = child->rightSibling;
	}
}

/*  drawsgNode
 *
 *  Print the subgoal tree.
 */
void 
drawsgNode(sgTreeNode *root, int n)
{
	int i;
	char s[10] = "";
	sgTreeNode *child = root->firstChild;

	for (i = 0; i < n; i++)
		strcat(s, "\t");

	myLog(s);
	myLog("--------------------- ");
	nl(1);
	myLog(s);
	myLog("isLeaf: ");
	myLogb(root->isLeaf);
	nl(1);
	myLog(s);
	myLog("attrCount: ");
	myLogi(root->attrCount);
	nl(1);
	myLog(s);
	myLog("sglist: ");
	myLogp(root->sglist);
	nl(1);
	myLog(s);
	myLog("firstChild: ");
	myLogp(root->firstChild);
	nl(1);
	myLog(s);
	myLog("rightsibling: ");
	myLogp(root->rightSibling);
	nl(1);
	myLog(s);
	myLog("--------------------- ");
	nl(1);

	while (child != NULL)
	{
		drawsgNode(child, n + 1);
		child = child->rightSibling;
	}
}

/*  printsglist
 *
 *  Print the subgoal list.
 */
void 
printsglist(sgList *list)
{
	sgList *l = list;
	subGoal *sg;
	int i;

	while (l != NULL)
	{
		sg = l->head;
		myLog("A new List:\n");
		while (sg != NULL)
		{
			for (i = 0; i < list_length(sg->fields); i++)
			{
				myLog(strVal(list_nth(sg->fields, i)));
				myLog(".");
			}
			nl(1);
			sg = sg->next;
		}

		l = l->next;
	}
}

/*  IsPK
 *
 *  Return true if the attribute is the primary key of a relation.
 */
static bool 
IsPK(subGoal *sg)
{
	char *attr = strVal(llast(sg->fields));
	Node *node = sg->relation;
	RangeVar *rv;
	Relation rel;
	Relation catalog_relation;
	TupleDesc tuple_desc;
	SysScanDesc parent_scan;
	ScanKeyData parent_key;
	HeapTuple parent_tuple;
	bool result = false;

	/* Key does not exist in a subquery */
	if (!IsA(node, RangeVar))
		return false;

	/* Open the range variable from the catalog */
	rv = (RangeVar *) node;
	rel = relation_openrv(rv, NoLock);

	catalog_relation = heap_open(ConstraintRelationId, RowExclusiveLock);
	tuple_desc = RelationGetDescr(catalog_relation);

	/* Initialization of the scan for the key */
	ScanKeyInit(&parent_key, Anum_pg_constraint_conrelid,
			BTEqualStrategyNumber, F_OIDEQ, ObjectIdGetDatum(RelationGetRelid(rel)));

	parent_scan = systable_beginscan(catalog_relation, ConstraintRelidIndexId,
			true, SnapshotNow, 1, &parent_key);

	/* Scan for the key */
	while (HeapTupleIsValid(parent_tuple = systable_getnext(parent_scan)))
	{
		Form_pg_constraint parent_con = (Form_pg_constraint) GETSTRUCT(parent_tuple);

		/* Only care the constraints of keys */
		if (parent_con->contype == CONSTRAINT_PRIMARY)
		{
			Datum val;
			bool isnull;
			Datum *keys;
			int nKeys;
			int j;

			/* Get the tuple */
			val = SysCacheGetAttr(CONSTROID, parent_tuple,
					Anum_pg_constraint_conkey, &isnull);

			/* deconstruct the constraint into an array */
			deconstruct_array(DatumGetArrayTypeP(val), INT2OID, 2, true, 's', &keys, NULL, &nKeys);

			/* Loop all the keys */
			for (j = 0; j < nKeys; j++)
			{
				char *colName;
				
				/* Get the column name */
				colName = get_relid_attribute_name(parent_con->conrelid,
						DatumGetInt16(keys[j]));

				/* Find a match */
				if (strcmp(colName, attr) == 0)
					result = true;
			}
		}
	}

	/* Close the catalog scan */
	systable_endscan(parent_scan);
	heap_close(catalog_relation, RowExclusiveLock);

	relation_close(rel, NoLock);

	return result;
}

/*  rmNthLists
 *
 *  Remove the lists with specific index.
 */
static sgList *
rmNthLists(sgList *list, bool *rm, int n)
{
	int count = 0;
	int i;

	for (i = 0; i < n; i++)
	{
		if (*(rm + i))
		{
			list = rmNthList(list, i - count);
			count++;
		}
	}

	return list;
}

/*  rmNthList
 *
 *  Remove one list with specific index.
 */
static sgList *
rmNthList(sgList *list, int n)
{
	sgList *temp, *head;
	int i;
	temp = palloc0(1 * sizeof(sgList));
	head = temp;
	temp->next = list;

	for (i = 0; i <= n - 1; i++)
	{
		temp = temp->next;
	}

	temp->next = temp->next->next;

	temp = head->next;
	pfree(head);

	return temp;
}

/*  sameLists
 *
 *  Return true if two subgoal lists are the same.
 */
static bool 
sameLists(sgList *list)
{
	int i, j;
	int count = sgListCount(list);
	sgList *l1, *l2;

	if (count == 1)
		return true;

	for (i = 0; i < count - 1; i++)
	{
		l1 = getNthList(list, i);

		for (j = i + 1; j < count; j++)
		{
			l2 = getNthList(list, j);

			if (sgListcmp(l1, l2) != SG_EQUAL)
				return false;
		}
	}

	return true;
}

/*  getNthList
 *
 *  Return the list with specific index.
 */
static sgList *
getNthList(sgList *list, int n)
{
	sgList *temp;
	int count = -1;

	if (list == NULL)
		return NULL;

	temp = list;
	count++;

	while (n != count)
	{
		temp = temp->next;
		count++;
	}

	return temp;
}

/*  sgListcmp
 *
 *  Compare two subgoal list. There are 6 possible results:
 *
 * SG_EQUAL 
 * SG_GREAT
 * SG_LESS 
 * SG_DISJOINT
 * SG_INTERSECT	
 * SG_UNKNOWCMP 
 */
static int 
sgListcmp(sgList *l1, sgList *l2)
{
	int joint = 0;
	int c1 = sgCount(l1);
	int c2 = sgCount(l2);
	subGoal *temp = l1->head;

	while (temp != NULL)
	{
		if (sgInsgList(temp, l2))
			joint++;

		temp = temp->next;
	}

	if (joint == c1 && joint == c2)
		return SG_EQUAL;
	else if (joint == c1 && joint < c2)
		return SG_LESS;
	else if (joint == c2 && joint < c1)
		return SG_GREAT;
	else if (joint == 0)
		return SG_DISJOINT;
	else if (joint != c2 && joint != c1)
		return SG_INTERSECT;
	else
		return SG_UNKNOWCMP;
}

/*  sgInsgList
 *
 *  Return true if a subgoal is in a subgoal list.
 */
static bool 
sgInsgList(subGoal *sg, sgList *list)
{
	List *fields = sg->fields;
	subGoal *temp = list->head;
	int i;

	/* Loop the list */
	while (temp != NULL)
	{
		if (list_length(temp->fields) == list_length(fields))
		{
			for (i = 0; i < list_length(temp->fields) - 1; i++)
			{
				if (strcmp(strVal(list_nth(fields, i)), strVal(list_nth(temp->fields, i))) != 0)
					goto next;
			}

			return true;
		}
		
next:
		temp = temp->next;
	}

	return false;
}

/*  sgListCount
 *
 *  Return the number of subgoal lists.
 */
static int 
sgListCount(sgList *list)
{
	sgList *temp;
	int count = 0;

	if (list == NULL)
		return 0;

	temp = list;
	count++;

	while (true)
	{
		if (temp->next != NULL)
		{
			count++;
			temp = temp->next;
		}
		else
			break;
	}

	return count;
}

/*  sgCount
 *
 *  Return the number of subgoal in a subgoal list.
 */
static int 
sgCount(sgList *list)
{
	subGoal *temp;
	int count = 0;

	if (list == NULL)
		return 0;

	if (list->head == NULL)
		return 0;

	temp = list->head;
	count++;

	while (true)
	{
		if (temp->next != NULL)
		{
			count++;
			temp = temp->next;
		}
		else
			break;
	}

	return count;
}

/*  derivePos
 *
 *  Derive the positions of condition columns in the tuples of lineage.
 */
int 
derivePos(sigNode *node, int n)
{
	sigNode *child;
	int count = n;

	if (node->isLeaf)
	{
		node->pos = count;
		return count + 1;
	}
	else
	{
		child = node->firstChild;

		while (child != NULL)
		{
			count = derivePos(child, count);
			child = child->rightSibling;
		}

		return count;
	}
}

/*  buildSigTree
 *
 *  Build the signature tree.
 */
void 
buildSigTree(sgTreeNode *sgNode, sigNode *sig_node)
{
	subGoal *sg;
	sigNode *child = NULL, *lastChild = NULL;
	sgTreeNode *sgNodeChild = sgNode->firstChild;

	if (sgNode->sglist == NULL)
		return;

	sg = sgNode->sglist->head;

	/* create a sigNode for every subgoal that does not appear in the children of sgNode */
	while (sg != NULL)
	{
		/* If a subgoal does not appear in the children, create a node for it
		 * as the child of current node.
		 */
		if (!sgInChildSGLists(sg, sgNode))
		{
			child = palloc0(1 * sizeof(sigNode));
			child->isLeaf = true;
			child->withStar = true; 
			child->sg = sg;

			/* Primary key should be a subset of joining attributes */
			if (sgNode->attrCount == nattr(sg) || IsPK(sg)) 
				child->withStar = false;
		}

		if (sig_node->firstChild == NULL)
			sig_node->firstChild = child;

		if (lastChild != NULL)
		{
			lastChild->rightSibling = child;
		}

		lastChild = child;

		sg = sg->next;
	}

	/* Recursively build the tree */
	while (sgNodeChild != NULL)
	{
		child = palloc0(1 * sizeof(sigNode));

		if (sig_node->firstChild == NULL)
			sig_node->firstChild = child;

		if (lastChild != NULL)
			lastChild->rightSibling = child;

		lastChild = child;

		buildSigTree(sgNodeChild, child);

		sgNodeChild = sgNodeChild->rightSibling;
	}
}

/*  nattr
 *
 * Return the number of attributes of a relation. If the subgoal is not a table
 * in the catalog, return -1.
 */
static int 
nattr(subGoal *sg)
{
	int result = -1;
	Relation rel;

	switch (nodeTag(sg->relation))
	{
		case T_RangeVar:
			rel = relation_openrv((RangeVar *) (sg->relation), NoLock);
			result = RelationGetNumberOfAttributes(rel);
			relation_close(rel, NoLock);
			break;
		default:
			;
	}

	return result;
}

/* sgInChildSGLists
 *
 * Return true if a subgoal appears in the subgoal lists of child nodes.
 */
static bool 
sgInChildSGLists(subGoal *sg, sgTreeNode *sgNode)
{
	sgTreeNode *child = sgNode->firstChild;
	sgList *list;

	while (child != NULL)
	{
		list = child->sglist;

		if (sgInsgList(sg, list))
			return true;

		child = child->rightSibling;
	}

	return false;
}

/* buildSGTree
 *
 * Build the a subgoal tree.
 */
void 
buildSGTree(sgTreeNode *node, int parentSGListCount)
{

	sgList *disjointSet;
	sgList *list = node->sglist;
	sgTreeNode *child, *lastChild = NULL;

	if (sameLists(list))
	{
		node->isLeaf = true;
		node->attrCount = sgListCount(node->sglist) + parentSGListCount;
		return;
	}
	else
		node->isLeaf = false;

	node->sglist = findMax(list, &list);

	node->attrCount = sgListCount(node->sglist) + parentSGListCount;

	while (list != NULL)
	{ 
		disjointSet = findDisjoint(list, &list);

		child = palloc0(1 * sizeof(sgTreeNode));
		child->sglist = disjointSet;

		if (node->firstChild == NULL)
			node->firstChild = child;

		if (lastChild != NULL)
			lastChild->rightSibling = child;

		lastChild = child;
	}

	child = node->firstChild;

	while (child != NULL)
	{
		buildSGTree(child, node->attrCount);
		child = child->rightSibling;
	}
}

/* findDisjoint
 *
 * Find disjoint subgoal list. 
 */
static sgList *
findDisjoint(sgList *list, sgList **newList)
{
	int i;
	int count = sgListCount(list);
	sgList *first = list, *cmp;
	sgList *result, *temp;
	bool *rm = palloc0(count * sizeof(bool));

	result = palloc0(1 * sizeof(sgList));
	result->head = list->head;
	result->next = NULL;
	*rm = true;

	/* Loop the list */
	for (i = 1; i < count; i++)
	{
		cmp = getNthList(list, i);

		if (sgListcmp(first, cmp) != SG_DISJOINT)
		{
			temp = palloc0(1 * sizeof(sgList));
			temp->head = cmp->head;
			temp->next = result;
			result = temp;
			*(rm + i) = true;
		}

	}

	*newList = rmNthLists(list, rm, count);

	return result;
}

/* findMax
 *
 * Find the maximal attributes.
 */
static sgList *
findMax(sgList *list, sgList **newList)
{
	int i, j;
	int count = sgListCount(list);
	sgList *l1, *l2;
	bool isMaximal;
	sgList *result = NULL, *temp;
	bool *rm = palloc0(count * sizeof(bool));

	for (i = 0; i < count; i++)
	{
		l1 = getNthList(list, i);
		isMaximal = true;

		for (j = 0; j < count; j++)
		{
			l2 = getNthList(list, j);

			if (sgListcmp(l1, l2) == SG_LESS)
				isMaximal = false;

			if (j == (count - 1) && isMaximal)
			{
				temp = palloc0(1 * sizeof(sgList));
				temp->head = l1->head;
				temp->next = result;
				result = temp;
				*(rm + i) = true;
			}
		}
	}

	*newList = rmNthLists(list, rm, count);

	return result;
}

/* getRelation
 *
 * Return the relation where an attribute comes from.
 */
static Node *
getRelation(List **address, List *fromClause)
{
	List *fields = *address;
	ListCell *cell;
	Node *node = NULL;
	RangeVar *rv;
	Relation rel;
	TupleDesc tupdesc;
	int i;

	foreach(cell, fromClause)
	{
		node = lfirst(cell);

		/* We only deal with range variables */
		if(IsA(node, RangeVar))
		{
			rv = (RangeVar *) node;

			/* In the form of Relation.column */
			if(list_length(fields) == 2)
			{
				/* Use the alias if it's not NULL */
				if (rv->alias != NULL)
				{
					if (strcmp(rv->alias->aliasname, strVal(linitial(fields))) == 0)
						return node;
				}
				else
				{
					if (strcmp(rv->relname, strVal(linitial(fields))) == 0)
						return node;
				}
			}
			/* In the form of Schema.Relation.column */
			else if(list_length(fields) == 3)
			{
				if (strcmp(rv->schemaname, strVal(linitial(fields))) == 0
						&& strcmp(rv->relname, strVal(lsecond(fields))) == 0)
				return node;
			}
			/* In the form of Catalog.Schema.Relation.column */
			else if(list_length(fields) == 4)
			{
				if (strcmp(rv->catalogname, strVal(linitial(fields))) == 0
						&& strcmp(rv->schemaname, strVal(lsecond(fields))) == 0
						&& strcmp(rv->relname, strVal(lthird(fields))) == 0)
				return node;
			} else {
				rel = relation_openrv(rv, 1);
				relation_close(rel, 0);
				tupdesc = rel->rd_att;
	
				/* Loop all attributes in the relation. */
				for (i = 0; i < tupdesc->natts; i++)
				{
					if (strcmp(NameStr(tupdesc->attrs[i]->attname), strVal(llast(fields))) == 0)
					{
						if(rv->relname != NULL)
							*address = lcons(makeString(rv->relname), *address);
						if(rv->schemaname != NULL)
							*address = lcons(makeString(rv->schemaname), *address);
						if(rv->catalogname != NULL)
							*address = lcons(makeString(rv->catalogname), *address);
	
						return node;
					}
				}
	                }
		}
	}
	return NULL;
}

/* isHQ
 *
 * Return true if the query is hierarchical.
 */
bool 
isHQ(sgList *list)
{
	int i, j;
	int count = sgListCount(list);
	sgList *l1, *l2;

	for (i = 0; i < count - 1; i++)
	{
		l1 = getNthList(list, i);

		for (j = i + 1; j < count; j++)
		{
			l2 = getNthList(list, j);

			/* Decide the hierarchical property according to the definition */
			if (sgListcmp(l1, l2) == SG_INTERSECT)
				return false;
		}
	}

	return true;
}

/* copyFromList
 *
 * Copy the from clause in the select.
 */
static List *
copyFromList(List *list)
{
	ListCell *cell;
	Node *node, *node2;
	List *result = NULL;

	foreach(cell, list)
	{
		node = lfirst(cell);
		node2 = copyObject(node);
	
		if(result == NULL)
			result = list_make1(node2);
		else
			result = lappend(result, node2);
	}

	return result;
}

/* addNonJoinedRelation
 *
 * Add the non-join relations to the signature. 
 * For example, let query be 
 *        select * from a, b, c where a.id = b.id;
 * The relation c is added the signature at this stage. 
 */
sigNode *addNonJoinedRelation(sgTreeNode *sgRoot, sigNode *oldRoot,
		List *frlist)
{
	ListCell *cell;
	Node *node;
	sigNode *result;
	sigNode *signode;
	sigNode *firstnode = NULL, *lastnode = NULL;
	sigNode *oldFirstChild;

	/* Loop the from clause of the select */
	foreach(cell, frlist)
	{
		node = lfirst(cell);

		/* If the relation is not joined, add it the to subgoal tree */
		if(!relIsJoined(node, sgRoot))
		{
			signode = palloc0(1 * sizeof(sigNode));
			
			signode->isLeaf = true;
			signode->withStar = true;

			signode->sg = palloc0(1 * sizeof(subGoal));
			signode->sg->relation = node;
			signode->sg->fields = nodeGetFields(node);

			if(firstnode == NULL)
				firstnode = signode;

			if(lastnode != NULL)
				lastnode->rightSibling = signode;

			lastnode = signode;
		}
	}

	if(firstnode == NULL)
		return oldRoot;
	else {
		/* Get a new root for signature tree */
		if (sgRoot->attrCount != 0)
		{
			result = palloc0(1 * sizeof(sigNode));
			result->isLeaf = false;
			result->firstChild = oldRoot;
		}
		/* Continue to use the original root */
		else
		{ 
			result = oldRoot;
		}

		/* Update the list accordingly */
		oldFirstChild = result->firstChild;
		result->firstChild = firstnode;
		lastnode->rightSibling = oldFirstChild;

		return result;
	}
}

/* relIsJoined
 *
 * Return true if the relation is joined with any other relations.
 */
static bool 
relIsJoined(Node *rel, sgTreeNode *node)
{
	sgTreeNode *child;
	subGoal *sg;

	/* Recursively call this function */
	if (node->sglist == NULL)
	{
		child = node->firstChild;

		/* Loop the children of a node */		
		while (child != NULL)
		{
			if (relIsJoined(rel, child))
				return true;

			child = child->rightSibling;
		}
	}
	/* Base case */
	else
	{
		sg = node->sglist->head;
		
		/* Loop the subgoal list */
		while (sg != NULL)
		{
			if (sameRel(sg->relation, rel))
				return true;

			sg = sg->next;
		}
	}

	return false;
}

/* sameRel
 *
 * Return true if two relations are the same.
 */
static bool 
sameRel(Node *n1, Node *n2)
{
	RangeVar *rv1, *rv2;
	RangeSubselect *s1, *s2;

	/* Different types cannot have the same relations. */
	if (nodeTag(n1) != nodeTag(n2))
		return false;

	switch (nodeTag(n1))
	{
		case T_RangeVar:
			rv1 = (RangeVar *) n1;
			rv2 = (RangeVar *) n2;

			/* Compare two aliases. */
			if ((rv1->alias == NULL && rv2->alias != NULL) || (rv1->alias != NULL
					&& rv2->alias == NULL))
				return false;
			
			/* Compare two aliases. */
			if (rv1->alias != NULL && rv2->alias != NULL)
				return (strcmp(rv1->alias->aliasname, rv2->alias->aliasname) == 0);
			else
			{
				/* Catalog name is not NULL */
				if ((rv1->catalogname == NULL && rv2->catalogname != NULL)
					|| (rv1->catalogname != NULL && rv2->catalogname == NULL))
					return false;

				/* Schema name is not NULL */
				if ((rv1->schemaname == NULL && rv2->schemaname != NULL)
					|| (rv1->schemaname != NULL && rv2->schemaname == NULL))
					return false;

                                /* If catalog names are not NULL, compare
                                 * them */
                                if(rv1->catalogname != NULL && 
                                    strcmp(rv1->catalogname, rv2->catalogname))
                                  return false;

                                /* If schema names are not NULL, compare
                                 * them */
                                if(rv1->schemaname != NULL && 
                                    strcmp(rv1->schemaname, rv2->schemaname))
                                  return false;

				/* Compare three parts of the names */
				return (strcmp(rv1->relname, rv2->relname) == 0);
		}
		break;
		
	case T_RangeSubselect:
		s1 = (RangeSubselect *) n1;
		s2 = (RangeSubselect *) n2;

		return (strcmp(s1->alias->aliasname, s2->alias->aliasname) == 0);
		break;
		
	default:
		;
	}

	return false;
}
