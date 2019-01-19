/*-------------------------------------------------------------------------
 *
 * signature.h
 *	  Structures and prototypes used in rewrite.h.
 *
 *
 * Copyright (c) 2008, MayBMS Development Group
 *
 *-------------------------------------------------------------------------
 */
 
#include "fmgr.h"
#include "nodes/parsenodes.h"
#include "utils/memutils.h"

#define VARNAME "_v"
#define PROBNAME "_p"
#define DOMAINNAME "_d"

#define VARTYPENAME "int4"
#define PROBTYPENAME "float4"
#define DOMAINTYPENAME "int4"

#define FIRSTVNAME "_v0"

typedef int32 	varType;
typedef float4 	prob;
typedef int32 	rngType;

typedef struct subGoal{
	List *fields;
	Node *relation;
	struct subGoal *next;
}subGoal;

typedef struct sgList{
	subGoal *head;
	struct sgList *next;
}sgList;

typedef struct sgTreeNode{
	bool isLeaf;
	int attrCount;
	sgList *sglist;
	struct sgTreeNode *firstChild;
	struct sgTreeNode *rightSibling;
}sgTreeNode;

typedef struct probTableEntry{
	varType repre;
	prob probability;
	struct probTableEntry *next;
} probTableEntry;

typedef struct probTable{
	probTableEntry *head;
	probTableEntry *tail;
} probTable;

typedef struct readySum{
	prob sum;
	struct readySum *next;
} readySum;

typedef struct sigNode{
	bool isLeaf;

	// the attributes above are only for leaf node
	bool withStar;
	char *rel;
	subGoal *sg;
	struct sigNode *firstChild;
	struct sigNode *rightSibling;

	int type; // ORDINARY or MERGED
	int pos; // pos of the variable in the lineage
	int domain; // if domain > 0, the var is the head of a subsignature
	int varsToCombine; // if domain > 0, this is the number of the variable to be combined for probability computation
	probTable *pt;
	
} sigNode;

sgTreeNode *sgTreeRoot;
sigNode *sigTreeRoot;
bool isOneScan;
List *relList;

MemoryContext signaturecxt;

/* Functions related to processing query trees */
extern Node *process( Node *parsetree );

/* Functions related to storing condtion columns */
extern List *GetStoredConColumns(Node *parsetree);

extern bool is1Scan( sigNode *node );
extern void buildSigTree( sgTreeNode *sgNode, sigNode *sigNode );
extern int derivePos( sigNode *node, int n );
extern void calSib( sigNode *node );
extern void calDomain( sigNode *node );
extern int leafDescendentCount( sigNode *node );
extern void drawSig( sigNode *root, int n );
extern void drawsgNode( sgTreeNode *root, int n );
extern void printsglist( sgList *list );
extern int fillLeafNode( sigNode *node, sigNode **vars );
extern sigNode *addNonJoinedRelation( sgTreeNode *sgRoot, sigNode *oldRoot, List *frlist );
