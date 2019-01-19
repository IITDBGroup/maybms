/*-------------------------------------------------------------------------
 *
 * SPROUT.c
 *	  Implementation of confidence computation for hierarchical queries.
 *
 *
 * Copyright (c) 2008, MayBMS Development Group
 *
 *-------------------------------------------------------------------------
 */
/* Tuple-independent databases are databases in which every tuple is associated with an 
 * independent random variable. This model is simpler than U-relation used in 
 * world-set tree algorithms(ws-tree.c) and the karp-Luby simulation. The 
 * confidence computation for tuple-independent probabilistic databases is PTIME
 * or #P-complete as described in
 *
 * N. Dalvi and D. Suciu, The Dichotomy of Conjunctive Queries on Probabilistic
 * Structures. In Proc. PODS, 2007. 
 *
 * PTIME queries are also called hierarchical queries. We implement a confidence 
 * computation operator for hierarchical queries without self-joins in 
 * tuple-independent probabilistic databases. This technique is described in  
 *
 * D. Olteanu, J. Huang and C. Koch, SPROUT: Lazy vs. Eager Query Plans for 
 * Tuple-Independent Probabilistic Databases. In Proc. ICDE, 2009.
 * 
 * Before the confidence computation, the signature for the query is needed 
 * (see signature.c). Our implementation first performs aggregations to gain 1scan property in
 * the signature and then scan the lineage once to compute the probability on the
 * fly. The following is the sketch of the 1scan algorithm, the core of the 
 * confidence computation procedure. The implementation is much more complicated
 * than this.
 *
 * 1scan compute prob(1scanTree t)
 * {
 * 		prevSlot = NULL; fetch the first tuple into crtSlot;
 *
 *      foreach node n in t do 
 *		{
 *      	enable n, n.crtP = 0, n.allP = 0;
 *          n.index = index of its column in crtSlot; 
 *		}
 *
 *    	{Compare prevSlot and crtSlot, find leftmost unmatched column i;
 *     	 propagate prob(t, i, crtSlot);
 *       prevSlot = crtSlot; fetch the next tuple into crtSlot;
 *      } do while (prevSlot = NULL)
 *
 *      return t.root.allP;
 * }
 *
 * propagate prob(1scanTree n, int i, Slot crtSlot)
 * {
 * 		foreach child c of n do propagate prob(c, i, crtSlot);
 *
 *      if (n is enabled && n.index ≥ i)
 *      	if (n is a leaf node && n.index = i)
 *         		n.crtP = 1 - (1 - n.crtP) · (1 - crtSlot[n.index].prob);
 *    	else 
 *		{
 * 			foreach child c of n do n.crtP = n.crtP · c.allP;
 *
 *         	n.allP = 1 - (1 - n.crtP) · (1 - n.allP);
 *             
 *			if (n.index = i) 
 *			{
 *       		foreach descendant d of n do
 *					enable d; d.allP = 0; d.crtP = crtSlot[d.index].prob;
 *
 *             	n.crtP = crtSlot[n.index].prob; 
 *			}
 *         	else
 *        		disable n and all its descendants; 
 *		}
 * }
 *
 */ 
 
#include "postgres.h"
#include "fmgr.h"
#include "nodes/execnodes.h"
#include "maybms/bitset.h"
#include "maybms/conf_comp.h"

#define NOTAGGREGATED 0
#define AGGREGATED   1
#define HASHTABLESIZE 10000000

/* MACRO for accumulating new tuples */
#define accumulate( nargs ) \
	MemoryContext oldcxt; \
	AggState *aggState = ( AggState *) fcinfo->context; \
	int n = nargs, i = 0; \
	varType *vars;\
	prob *probs;\
	if ( groupcxt == NULL )\
	{ \
		groupcxt = AllocSetContextCreate( NULL, "GroupContext",  ALLOCSET_DEFAULT_MINSIZE, \
                                        	 ALLOCSET_DEFAULT_INITSIZE, ALLOCSET_DEFAULT_MAXSIZE);\
    }\
    oldcxt = MemoryContextSwitchTo( groupcxt ); \
	vars = palloc0( n * sizeof( varType ) ); \
	probs = palloc0( n * sizeof( prob ) ); \
	for( ; i < n ; i++ ){ \
		*( vars + i ) = PG_GETARG_INT32( 1 + i*2 ); \
		*( probs + i ) = PG_GETARG_FLOAT4( 2 + i*2 );	\
	} \
	advance( aggState, n, vars, probs ); \
	MemoryContextSwitchTo( oldcxt ); \
	PG_RETURN_DATUM( 1 );

/* variable entry in hash table */
typedef struct varEntry{
	varType var;
	probTableEntry *pte;
	struct varEntry *next;
} varEntry;

/* variable list used in hash table */
typedef struct varList{
	varEntry *head;
	varEntry *tail;
} varList;

/* The memory context for a group of duplicates */
MemoryContext groupcxt = NULL;

/* local utility functions */
static sigNode *get1stLeafChild( sigNode *node );
static void storeLineage( lineageTable *lineage, int n, varType *vars, prob *probs );
static void addVarProb( varprob *vp, lineageTable *lineage );
static void resetLineageCursor( lineageTable *lineage );
static void	advance( AggState *aggState, int n, varType *vars, prob *probs );
static prob indeEventConjunc( prob a, prob b );
static bool isAValidVar( sigNode *info, varType var );
static prob lookup( sigNode *info, varType var, prob probability );
static int hashFunction( varType var );
static void insertIntoProbTable( probTable *pt, probTableEntry *entry );
static void insertIntoVarList( varList *list, varEntry *entry );
static prob getProduct( sigNode *info, prob p, readySum **plist );
static readySum *insertIntoReadySumList( readySum *head, prob sum );
static probTableEntry *getPTEFromHT( varList *ht, varType var );
static void newVarEntry( varType var, probTableEntry *pte, varList *ht );

/* local function for the scheduler */
static void schedule( sigNode *node, lineageTable *lineage );
static void simpleAggregation( sigNode **vars, int NoOfVars, lineageTable *lineage );
static void complexAggregation( sigNode **vars, int NoOfVars, lineageTable *lineage );
static varprob *getNextTuple( lineageTable *lineage );
static prob oneScan( stateData *state, sigNode *root, lineageTable *lineage );

/* local functions for tuple processing */
static void processTuple( stateData *s, prob *blockProb, prob *finalProb );
static void getStateAndLineage( AggState *aggState, stateData **state, lineageTable **lineage );

/* local functions for testing */
void print_probTable(probTable *pt);

/*  conf_accum0
 *
 *  Transition function for conf with no arguments
 */
Datum 
conf_accum0(PG_FUNCTION_ARGS)
{
	PG_RETURN_FLOAT4( 1 );
}

/* conf_accum1
 *
 * Transition function for conf with only one uncertain relation.
 * This is similar to the normal aggregates in PostgreSQL.
 */
Datum 
conf_accum1(PG_FUNCTION_ARGS)
{
	prob a = PG_GETARG_FLOAT4( 0 );
	prob b = PG_GETARG_FLOAT4( 2 ); 

	prob result = a+b-a*b;

	PG_RETURN_FLOAT4( result );
}

/*  conf_accum2
 *
 *  Transition function for conf with 2 pairs of condition columns.
 */
Datum 
conf_accum2(PG_FUNCTION_ARGS)
{
	accumulate( 2 )
}

/*  conf_accum3
 *
 *  Transition function for conf with 3 pairs of condition columns.
 */
Datum 
conf_accum3(PG_FUNCTION_ARGS)
{
	accumulate( 3 )
}

/*  conf_accum4
 *
 *  Transition function for conf with 4 pairs of condition columns.
 */
Datum 
conf_accum4(PG_FUNCTION_ARGS)
{
	accumulate( 4 )
}

/*  conf_accum5
 *
 *  Transition function for conf with 5 pairs of condition columns.
 */
Datum 
conf_accum5(PG_FUNCTION_ARGS)
{
	accumulate( 5 )
}

/*  conf_accum6
 *
 *  Transition function for conf with 6 pairs of condition columns.
 */
Datum 
conf_accum6(PG_FUNCTION_ARGS)
{
	accumulate( 6 )
}

/*  conf_accum7
 *
 *  Transition function for conf with 7 pairs of condition columns.
 */
Datum 
conf_accum7(PG_FUNCTION_ARGS)
{
	accumulate( 7 )
}

/*  conf_accum8
 *
 *  Transition function for conf with 8 pairs of condition columns.
 */
Datum 
conf_accum8(PG_FUNCTION_ARGS)
{
	accumulate( 8 )
}

/*  conf_accum9
 *
 *  Transition function for conf with 9 pairs of condition columns.
 */
Datum 
conf_accum9(PG_FUNCTION_ARGS)
{
	accumulate( 9 )
}

/*  conf_accum10
 *
 *  Transition function for conf with 10 pairs of condition columns.
 */
Datum 
conf_accum10(PG_FUNCTION_ARGS)
{
	accumulate( 10 )
}

/*  conf_final 
 *
 *  Final function for confidence computation.
 */
Datum 
conf_final(PG_FUNCTION_ARGS)
{
	AggState *aggState = ( AggState *) fcinfo->context;
	stateData *state;
	lineageTable *lineage;
	prob result = 1	;
	MemoryContext oldcxt;
	bool onescan = isOneScan;

	/* If the group context is NULL, return 0 */	
	if( groupcxt == NULL )
		PG_RETURN_FLOAT4(0);
	
	/* Switch to the group context */	
	oldcxt = MemoryContextSwitchTo( groupcxt ); 

	/* Get the current state of confidence computation */	
	getStateAndLineage( aggState, &state, &lineage );

	/* Process a NULL tuple to close the last partition */
	if ( onescan )
	{
		processTuple( state, NULL, &result );

		state->counter = 0;
	}
	/* Aggregate the variable columns to gain 1scan property and compute the 
	 * confidence with 1scan property.
	 */
	else
	{
		/* Aggregate the variable columns to remove stars */
		schedule( sigTreeRoot, lineage );

		/* computer the result with 1scan property  */
		result = oneScan( state, sigTreeRoot, lineage );

		/* Switch to signature context */
		MemoryContextSwitchTo( signaturecxt ); 

		/* Rebuild the signature */
		sigTreeRoot = palloc0( 1 * sizeof( sigNode ) );
		buildSigTree( sgTreeRoot, sigTreeRoot );
		sigTreeRoot = addNonJoinedRelation( sgTreeRoot, sigTreeRoot, relList ); 	
	
		derivePos( sigTreeRoot, 0 );
		calSib( sigTreeRoot );
		calDomain( sigTreeRoot );
		
		/* Switch to group context */
		MemoryContextSwitchTo( groupcxt ); 

		/* Reset the pointers in the lineage */
		lineage->head = NULL;
		lineage->tail = NULL;
		lineage->cursor = NULL;
	}
	
	/* Switch to the old context */
	MemoryContextSwitchTo( oldcxt );
	
	/* Delete the group context */
	MemoryContextDelete( groupcxt );
	groupcxt = NULL;   

	/* Return the result */
	PG_RETURN_FLOAT4( result );
}

/* schedule
 *
 * Schedule aggregations to gain 1scan property. 
 */
static void 
schedule( sigNode *node, lineageTable *lineage )
{
	sigNode **vars;
	int totalAttr;
	sigNode *child = node->firstChild;
	sigNode *firstDesc;

	/* Do nothing if the signature has 1scan property. */
	if ( is1Scan( node ) )
		return;

	/* Gain 1scan property in every child. */
	while( child != NULL )
	{
		if ( !is1Scan( child ) )
			schedule( child, lineage );

		child = child->rightSibling;
	}
	
	/* Return if the signature has 1scan property now */
	if ( is1Scan( node ) )
		return;

	/* Remove the star for the first child */
	child = node->firstChild;

	/* Use simple aggregation to gain 1scan property */
	if ( child->isLeaf )
	{
		vars = palloc0( 2 * sizeof( sigNode * ) );
		*vars = child;
		*(vars+1) = get1stLeafChild( child->rightSibling ); 
		simpleAggregation( vars, 2, lineage );
	}
	/* Use complex aggregation to gain 1scan property */
	else
	{
		totalAttr = leafDescendentCount( child ) + 1;
		vars = palloc0( totalAttr * sizeof( sigNode * ) );
		fillLeafNode( child, vars );
		*( vars + totalAttr - 1 ) = get1stLeafChild( child->rightSibling ); 
		complexAggregation( vars, totalAttr, lineage );
		
		firstDesc = get1stLeafChild( child );
		child->pos = firstDesc->pos;
	    child->type = firstDesc->type; 
	    child->pt  = firstDesc->pt;
		child->domain = firstDesc->domain;	
		child->varsToCombine = firstDesc->domain;
	}

	/* Set the relating field in the first child */
	child->isLeaf = true;
	child->withStar = false;
	child->type = AGGREGATED;

	calSib( node );
	calDomain( node );
}

/* get1stLeafChild
 *
 * Get the left most leaf descendant.
 */
static sigNode *
get1stLeafChild( sigNode *node )
{
	if ( node->isLeaf )
		return node;
	else
		return get1stLeafChild( node->firstChild );
}

/* storeLineage
 *
 * Store the lineage if the signature does not have 1scan property.
 */
static void 
storeLineage( lineageTable *lineage, int n, varType *vars, prob *probs )
{
	varprob *vp = palloc0( 1 * sizeof( varprob ) );
	vp->vars = vars;
	vp->probs = probs;
	
	addVarProb( vp, lineage ); 
}

/* storeLineage
 *
 * Add a tuple to the lineage.
 * TODO: Use the list structure in Postgres to replace this.
 */
static void 
addVarProb( varprob *vp, lineageTable *lineage )
{
	if ( lineage->head == NULL ){
		lineage->head = vp;
		lineage->tail = vp;
		lineage->cursor = vp;
	}
	else{
		lineage->tail->next = vp;
		lineage->tail = vp;
	}
}

/* getStateAndLineage
 *
 * Get the pointer of state and lineage.
 * TODO: This function is not necessary anymore, because hashing is not used in
 * duplicate elimination due to the sorting in the rewriting.
 */
static void 
getStateAndLineage( AggState *aggState, stateData **state, lineageTable **lineage )
{
	if (((Agg *) aggState->ss.ps.plan)->aggstrategy == AGG_HASHED){

		*state = aggState->currentEntry->state;
		*lineage = aggState->currentEntry->lineage;
	}	
	else{
		*state = aggState->state;
		*lineage = aggState->lineage;		
	}
}

/* advance
 *
 * Process one tuple of lineage.
 */
static void	
advance(AggState *aggState, int n, varType *vars, prob *probs)
{
	stateData *state;
	lineageTable *lineage;	
	bool onescan = isOneScan; 

	getStateAndLineage( aggState, &state, &lineage );

	/* Accumulate the tuple of lineage to the confidence if 1scan property
	 * is present.
	 */
	if( onescan )
	{
		/* Initialize the state with the signature information */
		if( state->NoOfVars == 0 )
		{
			int totalAttr = leafDescendentCount( sigTreeRoot );
			sigNode **sig = palloc0( totalAttr * sizeof( sigNode * ) );
			fillLeafNode( sigTreeRoot, sig );

			state->vars = sig;
			state->NoOfVars = n; 
		}

		/* Set the condition columns for confidence computation */
		state->curTuple = vars;
		state->curTupleProb = probs;

		/* Update the confidence with the current tuple */
		processTuple( state, NULL, NULL );
	}
	/* Store the current tuple of lineage if 1scan property is not present */
	else
	{
		storeLineage( lineage, n, vars, probs );
	}
}

/* simpleAggregation
 *
 * Aggregate a variable column to remove star. 
 * For example, aggregate column A in signature (A*B*)* to gain 1scan property
 * in (AB*)*.
 */
static void 
simpleAggregation( sigNode **vars, int NoOfVars, lineageTable *lineage )
{

	varType preTuple[NoOfVars];	
	varType curTuple[NoOfVars];
	prob curTupleProb[NoOfVars];
	int counter = 0;
	probTable *pt = palloc0( 1 * sizeof(probTable) );
	int i;
	int pos[NoOfVars];
	varList *hashTable0 = (varList *) palloc0( HASHTABLESIZE * sizeof(varList) ); 
	varList *hashTable1 = (varList *) palloc0( HASHTABLESIZE * sizeof(varList) ); 
	bool newX, newY;
	probTableEntry *pte;
	varprob *tuple = NULL;

	/* Set the positions of all variables */
	for(i=0; i<NoOfVars ;i++ )
	{
		pos[i] = vars[i]->pos;
	}

	/* Loop the lineage */
	for(;;)
	{
		counter++; 

		/* Fetch the next tuple */
		tuple = getNextTuple( lineage );

		/* Set the probability table to the variable node */
		if ( tuple == NULL )
		{
			vars[0]->pt = pt;
			pfree( hashTable0 );
			pfree( hashTable1 );
			break;
		}

		/* Update current tuple  */
		for( i=0; i< NoOfVars ;i++ )
		{
			curTuple[i] = *(tuple->vars + pos[i]);
			curTupleProb[i] = *(tuple->probs + pos[i] );
		}

		/* Flag for indicating whether new variable has been encountered  */
		newX = false;
		newY = false;

		/* Flag for indicating whether new variable has been encountered */
		if ( counter == 1 )
		{
			newX = true;
			newY = true;
		}
		
		/* Set newX to true if the first variable column is new */
		if ( curTuple[0] != preTuple[0] 
			 && isAValidVar( vars[0], curTuple[0] )  
			 && ( getPTEFromHT( hashTable0, curTuple[0] ) == NULL ) )
		{
			newX = true;
		}

		/* Set newY to true if the second variable column is new */
		if ( curTuple[1] != preTuple[1] 
			 && isAValidVar( vars[1], curTuple[1] ) 
			 && ( getPTEFromHT( hashTable1, curTuple[1] ) == NULL ) )
		{
			newY = true;
		}		

		/* If both variables are new, create new entries for the them in the 
		 * corresponding hash tables.
		 */
		if ( newX  && newY )
		{
			pte = palloc0( 1 * sizeof(probTableEntry) );
			pte->repre = curTuple[0];
			pte->probability = lookup( vars[0], curTuple[0], curTupleProb[0] );
			insertIntoProbTable( pt, pte );

			newVarEntry( curTuple[1], pte, hashTable1 );
			newVarEntry( curTuple[0], pte, hashTable0 );
		}
		/* If only the first variable is new, create a new entry in the first
		 * hash tables and update the probability of the corresponding partitions.
		 */
		else if ( newX )
		{
			pte = getPTEFromHT( hashTable1, curTuple[1] );
			newVarEntry( curTuple[0], pte, hashTable0 );

			pte->probability = indeEventConjunc( pte->probability, 
				lookup( vars[0], curTuple[0], curTupleProb[0] ) );
		}
		/* If only the second variable is new, create a new entry in the second
		 * hash tables.
		 */
		else if ( newY )
		{
			pte = getPTEFromHT( hashTable0, curTuple[0] );
			newVarEntry( curTuple[1], pte, hashTable1 );			
		}

		/* Update the preTuple with curTuple */
		memcpy( preTuple, curTuple, sizeof(varType) * NoOfVars );
	}

	/* Reset the cursor in the lineage */
	resetLineageCursor( lineage );
}

/* resetLineageCursor
 *
 * Reset the cursor in the lineage
 */
static void 
resetLineageCursor(lineageTable *lineage)
{
	lineage->cursor = lineage->head;
}

/* complexAggregation
 *
 * Aggregate all descendants in a child node to remove its star to gain 1scan
 * property. 
 * For example, aggregate columns A and B in signature (AB*)*(CD*)* to get a
 * new signature A(CD*)*.
 */
static void 
complexAggregation( sigNode **vars, int NoOfVars, lineageTable *lineage )
{
	int i,j;
	prob product;
	prob sum[NoOfVars];
	prob tempSum[NoOfVars];
	varType preTuple[NoOfVars];	
	varType curTuple[NoOfVars];
	prob curTupleProb[NoOfVars];
	int counter = 0;
	probTableEntry *pte;
	int pos[NoOfVars];
	readySum *readySumList;
	int posY = NoOfVars - 1;
	int cursor = NoOfVars - 2;
	varprob *tuple = NULL;
	probTable *pt = palloc0( 1 * sizeof(probTable) );
	bool newX = false, newY, updatePTE, calculating;
	varList *hashTable0 = ( varList * ) palloc0( HASHTABLESIZE * sizeof(varList) ); 
	varList *hashTable1 = ( varList * ) palloc0( HASHTABLESIZE * sizeof(varList) ); 
	
	/* Set the positions of all involved variable columns */
	for( i=0; i<NoOfVars ;i++ )
	{
		pos[i] = vars[i]->pos;
	}

	/* Loop the lineage */
	for(;;)
	{
		counter++; 

		/* Fetch the next tuple */
		tuple = getNextTuple( lineage );

		/* If the tuple is NULL, close the last partition */
		if (tuple == NULL)
		{
			if (counter == 1)
				break;

			goto insert;

finish:;
			/* Attach the probability table to the variable node */
			vars[0]->pt = pt; 
			pfree( hashTable0 );
			pfree( hashTable1 );
			break;
		}

		/* Update the current tuple */
		for( i=0; i< NoOfVars ;i++ )
		{
			curTuple[i] = tuple->vars[pos[i]];
			curTupleProb[i] = tuple->probs[pos[i]];
		}

		/* Flags for indicating whether new variable has been encountered  */
		newX = false;
		newY = false;

		/* If this is the first tuple, both flags are set to true */
		if ( counter == 1 )
		{
			newX = true;
			newY = true;
		}
		
		/* Set newX to true if the first variable is new */
		if ( curTuple[0] != preTuple[0] 
			 && isAValidVar( vars[0], curTuple[0] )  
			 && ( getPTEFromHT( hashTable0, curTuple[0] ) == NULL ) )
		{
			newX = true;
		}

		/* Set newY to true if the second variable is new */
		if ( curTuple[posY] != preTuple[posY] 
			 && isAValidVar( vars[posY], curTuple[posY] ) 
			 && ( getPTEFromHT( hashTable1, curTuple[posY] ) == NULL ) )
		{
			newY = true;
		}		

		/* If both variables are new, create new entries for the them in the 
		 * corresponding hash tables.
		 */
		if ( newX  && newY )
		{
			pte = palloc0( 1 * sizeof(probTableEntry) );
			pte->repre = curTuple[0];
			pte->probability = 0;
			insertIntoProbTable( pt, pte );

			newVarEntry( curTuple[posY], pte, hashTable1 );
			newVarEntry( curTuple[0], pte, hashTable0 );
		}
		/* If only the first variable is new, create a new entry in the first
		 * hash tables.
		 */
		else if ( newX )
		{
			pte = getPTEFromHT( hashTable1, curTuple[posY] );
			newVarEntry( curTuple[0], pte, hashTable0 );
		}
		/* If only the second variable is new, create a new entry in the second
		 * hash tables.
		 */
		else if ( newY )
		{
			pte = getPTEFromHT( hashTable0, curTuple[0] );
			newVarEntry( curTuple[posY], pte, hashTable1 );			
		}

		/* Whether to update the probability table entry */
		updatePTE = false;

		/* If this is the first tuple, initialize the probaiblities for each 
		 * variable columns.
		 */
		if ( counter == 1 )
		{
			calculating = true;

			for( i=0; i < posY; i++ ){
				sum[i] = lookup(vars[i],curTuple[i],curTupleProb[i]);
				tempSum[i] = 0;
			}

			memcpy( preTuple, curTuple, sizeof(varType) * NoOfVars );
		}
		/* If the new first variable is different from the previous, update the
		 * corresponding flag.
		 */
		else if( curTuple[0] != preTuple[0] )
		{
			/* If the new variable is valid, update the corresponding 
			 * probability table entry.
			 */
			if ( calculating )
			{
				updatePTE = true;
				goto insert;
			}

			if ( newX )
			{
				calculating = true;
			}
			else
			{
				calculating = false;
			}
		}

		/* Update the probabilities for the changing variables */
		if ( calculating )
		{
			/* Compare the current tuple and the last tuple */
			for( i = 0; i <= cursor; i++ )
			{
				/* If two variables are different and the new one is valid, update the probability */
				if( curTuple[i] != preTuple[i] && isAValidVar( vars[i], curTuple[i] ) )
				{
					/* If the change is on the cursor column, 
					 * only update the probaiblity of this column.
					 */
					if(i == cursor)
					{
						sum[i]=indeEventConjunc(sum[i],lookup( vars[i], curTuple[i],curTupleProb[i] ) );
					}
					/* Otherwise, the update involves the other columns */
					else{
insert:;				
						/* Close the current partition of first variable column */	
						if (tuple == NULL || updatePTE)
							i = 0;

						/* Loop from the cursor column to first column of changes */
						for( j = cursor; j > i ; j-- )
						{
							/* Put the final probability in a list for propagation */
							if ( vars[j]->domain == 0 )
							{
								readySumList = insertIntoReadySumList( readySumList, sum[j] );
							}
							/* Propagate the probabilities and put the result into a list */
							else
							{
								product = getProduct( vars[j], sum[j], &readySumList );
								tempSum[j] = indeEventConjunc( tempSum[j], product );
								readySumList = insertIntoReadySumList( readySumList, tempSum[j] );
								tempSum[j] = 0;
							}
						}
						
						/* Update the fields of first column of change */						
						if ( vars[i]->domain > 0 || NoOfVars == 1 )
						{
							/* Update the probability */
							if ( i != 0 )
							{
								product = getProduct( vars[i], sum[i], &readySumList );
								tempSum[i] = indeEventConjunc( tempSum[i], product );
							}
							/* If this is the representative column,
							 * update the corresponding probability of the entry
							 * in probability table.
							 */
							else
							{
								pte = getPTEFromHT( hashTable0, preTuple[0] );
								pte->probability = indeEventConjunc( pte->probability,  
									getProduct( vars[0], sum[0], &readySumList ) );
							}

							if ( tuple == NULL )
								goto finish;

							/* Update the cursor */
							cursor = i + vars[i]->domain;

							/* Update the probability of all descendant columns */
							for( j=i; j<=cursor; j++ )
							{
								sum[j] = lookup(vars[j],curTuple[j],curTupleProb[j]);
							}
						}
						/* When the domain of representative column is 0 */
						else
						{
							sum[i]= indeEventConjunc( sum[i],lookup( vars[i], curTuple[i], curTupleProb[i] ) );
							cursor = i;
						}
						
					}

					break;
				}
			}
		}

		/* If the representative column is new, update all the fields of 
		 * descendant columns.
		 */
		if ( newX )
		{
			for( i=0; i < posY; i++ )
			{
				sum[i] = lookup(vars[i],curTuple[i],curTupleProb[i]);
				tempSum[i] = 0;
			}
		}

		/* Set the current tuple as the previous tuple */
		memcpy( preTuple, curTuple, sizeof(varType) * NoOfVars );

	}	

	/* Reset the cursor in lineage */
	resetLineageCursor( lineage );
}

/* getNextTuple
 *
 * Fetch the next tuple from the lineage.
 */
static varprob *
getNextTuple( lineageTable *lineage )
{
	varprob *result = lineage->cursor;

	if( result != NULL )
		lineage->cursor = lineage->cursor->next;

	return result; 
}

/* getProduct
 *
 * Propagate the probabilities of descendant columns.
 */
static prob 
getProduct(sigNode *info, prob p, readySum **plist)
{
	int i;
	prob result = p;	

	for( i=0; i < info->varsToCombine; i++ )
	{
		result = result * (*plist)->sum;
		*plist = (*plist)->next;
	}

	return result;
}

/* insertIntoReadySumList
 *
 * Insert the a ready probability into the list.
 */
static readySum *
insertIntoReadySumList( readySum *head, prob sum )
{
	readySum *s = palloc0( 1 * sizeof(readySum) );
	s->sum = sum;
	s->next = head;
	return s;
}

/* insertIntoVarList
 *
 * Insert a variable into a hash table entry.
 */
static void 
insertIntoVarList(varList *list, varEntry *entry)
{
	if ( list->head == NULL )
	{
		list->head = entry;
		list->tail = entry;
	}
	else
	{
		list->tail->next = entry;
		list->tail = entry;
	}
}

/* insertIntoProbTable
 *
 * Insert a representative and its probability to probability table.
 */
static void 
insertIntoProbTable( probTable *pt, probTableEntry *entry )
{
	if ( pt->head == NULL )
	{
		pt->head = entry;
		pt->tail = entry;
	}
	else
	{
		pt->tail->next = entry;
		pt->tail = entry;
	}
}

/* hashFunction
 *
 * Return a hash value.
 */
static int 
hashFunction( varType var )
{
	return var % HASHTABLESIZE;
}

/* getPTEFromHT
 *
 * Return a variable entry from the probability table.
 */
static probTableEntry *
getPTEFromHT(varList *ht, varType var)
{
	varEntry *entry = ht[hashFunction(var)].head;

	while( entry != NULL )
	{
		if ( entry->var == var )
		{
			return entry->pte;
		}
		
		entry = entry->next;
	}

	return NULL;
}

/* newVarEntry
 *
 * Create an entry for a variable and put it into the corresponding hash table
 * entry.
 */
static void 
newVarEntry( varType var, probTableEntry *pte, varList *ht )
{
	varEntry *ve = palloc0( 1 * sizeof(varEntry) );
	ve->var = var;
	ve->pte = pte;
	insertIntoVarList( &(ht[hashFunction(var)]), ve );
}

/* isAValidVar
 *
 * Return true if the variable is valid.
 * After simple or complex aggregations for 1scan property, the variables expect
 * the representatives are not useful anymore. If a variable is not a 
 * representative then it is not valid.
 */
static bool 
isAValidVar( sigNode *info, varType var )
{
	/* If the column is not aggregated, the variable is valid */
	if ( info->type == NOTAGGREGATED )
		return true;
	/* The column is aggregated */
	else
	{
		/* If the probability table is NULL, the variable is not valid */
		if( info->pt->head == NULL )
			return false;

		return (info->pt->head->repre == var);
	}
}

/* lookup
 *
 * Return the probability of a variable. This could be a representative of a 
 * partition and then the probability of the partition is returned.
 */
static prob 
lookup( sigNode *info, varType var, prob probability )
{
	prob result;
	probTableEntry *temp;

	/* If the column is not aggregated, return its probability */
	if ( info->type == NOTAGGREGATED )
		return probability;
	/* The column is aggregated */
	else
	{
		/* Sanity check */
		if( info->pt->head == NULL )
		{
			myLog( "******************************info->pt->head == NULL\n" );
		}
		if( info->pt->head->repre != var )
		{
			myLog( "******************************info->pt->head->repre != var\n" );
		}

		/* Return the probability of first variable */
		result = info->pt->head->probability;
		temp = info->pt->head;
		info->pt->head = temp->next;
		
		return result;
	}
}

/* indeEventConjunc
 *
 * Calculate the probability of the conjunction two independent events.
 */
static prob 
indeEventConjunc( prob a, prob b )
{
	return (1 - (1-a)*(1-b));
}

/* oneScan
 *
 * Compute the confidence for a signature with 1scan property.
 */
static prob 
oneScan(stateData *state, sigNode *root, lineageTable *lineage)
{
	int totalAttr = leafDescendentCount( root ), i;
	int pos[ totalAttr ];
	prob result = 1;	
	varType *curTuple;
	prob *curTupleProb;
	varprob *tuple;

	/* Retrieve signature information into an array */
	sigNode **sig = palloc0( totalAttr * sizeof( sigNode * ) );
	fillLeafNode( root, sig );

	/* Initialize the state */
	state->counter = 0;
	state->vars = sig;
	state->NoOfVars = totalAttr;

	/* Retrieve the positions of columns involved */
	for( i=0; i < totalAttr; i++ )
	{
		pos[i] = sig[i]->pos;
	}

	/* Loop the lineage */
	for(;;)
	{       
		/* Fetch the next tuple */
		tuple = getNextTuple( lineage ); 

		/* Allocate space for the tuple */
		curTuple = palloc0( state->NoOfVars * sizeof( varType ) );
		curTupleProb = palloc0( state->NoOfVars * sizeof( prob ) );

		/* If the end of lineage is reached, close the last partition */
		if( tuple == NULL )
		{  
			processTuple( state, NULL, &result );

			break;
		}

		/* Set the current tuple and the probabilities in it */
		for( i=0; i < totalAttr; i++ )
		{
			*(curTuple+i) = *(tuple->vars + pos[i]); 
			*(curTupleProb+i) = *(tuple->probs + pos[i]);
		}

  		state->curTuple = curTuple;
		state->curTupleProb = curTupleProb;

		/* Process the tuple */
		processTuple( state, NULL, NULL );
	}

	return result;
}

/* processTuple
 *
 * Update the probabilities with one tuple of lineage.
 * This function is called if the signature has 1scan property.
 */
static void 
processTuple( stateData *s, prob *blockProb, prob *finalProb )
{ 
	int i, j;
	prob product;

	/* Final probability is required */
	if(finalProb != NULL)
	{
		/* Close the last partition if it is still open */
		if (s->calculating)
		{
			i = 0;
			goto finish;
		}
		/* If the last partition is closed, return the probability of the first
		 * coloumn.
		 */
		else
		{
			*finalProb = s->tempSum[0];
			return;
		}
	}

	/* Increase the counter */
	s->counter++; 
	
	/* Initialize the states */
	if ( s->counter == 1 )
	{  
		s->sum = palloc0( s->NoOfVars * sizeof( prob ) );
		s->tempSum = palloc0( s->NoOfVars * sizeof( prob ) );
		s->preTuple = palloc0( s->NoOfVars * sizeof( varType ) );
		s->readySumList = NULL;

newBlock:
		
		for( i = 0; i< s->NoOfVars; i++ )
		{ 
			s->sum[i] = lookup( s->vars[i], s->curTuple[i], s->curTupleProb[i] );
		}
		
		s->calculating = true;

		s->cursor = s->NoOfVars - 1;

		memcpy( s->preTuple, s->curTuple, sizeof(varType) * s->NoOfVars );
	
		return;
	}
	
	/* When a new block appears, we have 4 situations:
	 * (1) A valid block, 	calculating			: continue and initialize the new block 
	 * (2) A valid block, 	not calculating 	: initialization of new block and return
	 * (3) An invalid block, calculating 		: finish the old block
	 * (4) An invalid block, not calculating	: return
	 */
	 	
	/* A new block appears */
	if (s->curTuple[0] != s->preTuple[0])
	{
		if (isAValidVar(s->vars[0], s->curTuple[0]))
		{
			/* Situation (1) */
			if (s->calculating)
				s->calculating = true;
			/* Situation (2) */
			else
			 	goto newBlock;
		}
		else
		{
			/* Situation (3) */
			if (s->calculating)
			{
				s->calculating = false;
				i = 0;
				goto finish;
			}
			/* Situation (4) */
			else
				return;
		}
	}
	/* Still in the same block */
	else
	{
		if (!s->calculating)
			return;
	}

	/* Loop all columns before the cursor column */
	for( i = 0; i <= s->cursor; i++ )
	{
		/* Two variables in a column are not the same and the new one is valid. */ 
		if(s->curTuple[i] != s->preTuple[i] && isAValidVar(s->vars[i], s->curTuple[i]))
		{
			/* If the change is on the cursor column, 
			 * only update the probaiblity of this column.
			 */
			if( i == s->cursor )
			{
				s->sum[i]= indeEventConjunc(s->sum[i],
					lookup(s->vars[i], s->curTuple[i], s->curTupleProb[i]));
			}
			/* Otherwise, the update involves the other columns */
			else{					
finish:;
				/* Loop from the cursor column to first column of changes */
				for( j = s->cursor; j > i ; j-- )
				{
					/* Put the final probability in a list for propagation */
					if ( s->vars[j]->domain == 0 )
					{
						s->readySumList = insertIntoReadySumList( s->readySumList, s->sum[j] );
					}
					else
					{
						/* Propagate the probabilities and put the result into a list */
						prob product = getProduct( s->vars[j], s->sum[j], &(s->readySumList) );
						s->tempSum[j] = indeEventConjunc( s->tempSum[j], product );
						
						s->readySumList = insertIntoReadySumList( s->readySumList, s->tempSum[j] );
						s->tempSum[j] = 0;
					}
				}

				/* Update the fields of first column of change */
				if ( s->vars[i]->domain > 0 || s->NoOfVars == 1 )
				{
					product = getProduct( s->vars[i], s->sum[i], &(s->readySumList) );
					s->tempSum[i] = indeEventConjunc( s->tempSum[i], product );
					
					/* Return the final probability if required */
					if( finalProb != NULL )
					{
						*finalProb = s->tempSum[0];
						return;
					}

					/* Update the cursor column */
					s->cursor = i + s->vars[i]->domain;

					/* Reset the probability of the descendant columns */
					if (s->calculating)
					{
						for( j=i; j<=s->cursor; j++ )
						{
							s->sum[j] = lookup( s->vars[j], s->curTuple[j], s->curTupleProb[j] );
						}
					}

					/* Return the probability of a block if required */
					if(i == 0 && blockProb != NULL)
						*blockProb = product;
				}
				/* The node does not have descendants */
				else{
					s->sum[i]= indeEventConjunc( s->sum[i],
						lookup(s->vars[i], s->curTuple[i], s->curTupleProb[i]));
					
					/* Update the cursor column */	
					s->cursor = i;
				}			
			}

			break;
		}
	}

	/* Set the previous tuple as the current tuple */
	memcpy( s->preTuple, s->curTuple, sizeof( varType ) * s->NoOfVars );
}

/* print_probTable
 *
 * Print the probability table.
 */
void
print_probTable(probTable *pt)
{
	probTableEntry 	*entry = pt->head;
	
	while(entry != NULL)
	{
		myLogi(entry->repre); myLogf(entry->probability); nl(1);
		entry = entry->next;
	}	
}


