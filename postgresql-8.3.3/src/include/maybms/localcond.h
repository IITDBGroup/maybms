/*-------------------------------------------------------------------------
 *
 * lineage.h
 *	  	Storing and accessing the lineage for ws-tree-based algorithm and aconf().
 *
 *
 * Copyright (c) 2008, MayBMS Development Group
 *
 *-------------------------------------------------------------------------
 */
 
#include "math.h"
#include <stdlib.h>
#include <time.h>

#include "postgres.h"
#include "fmgr.h"
#include "maybms/bitset.h"
#include "nodes/execnodes.h"
#include "maybms/conf_comp.h"
#include "nodes/pg_list.h"
#include "utils/memutils.h"

#define RESERVED_VAR 0
#define RNG_FOR_RESERVED_VAR 1
#define RNG_FOR_RESERVED_VAR_NEGATIVE 0

#define conf_appro_accum( n ) \
	MemoryContext oldcxt; \
	int j = 0; \
	generalState *state = ( ( AggState *) fcinfo->context )->genstate; \
	WSD *wsd; \
	VarChar *source = PG_GETARG_VARCHAR_PP( 1 ); \
	appro_approach = VARDATA_ANY(source); \
	appro_epsilon = PG_GETARG_FLOAT4( 2 ); \
	if ( groupcxt == NULL )\
	{ \
		groupcxt = AllocSetContextCreate( NULL, "GroupContext",  ALLOCSET_DEFAULT_MINSIZE, \
                                        	 ALLOCSET_DEFAULT_INITSIZE, ALLOCSET_DEFAULT_MAXSIZE);\
    }\
    oldcxt = MemoryContextSwitchTo( groupcxt ); \
    wsd = (WSD*) palloc(sizeof(WSD));	\
	WSD_LEN = n; \
    wsd->data = (Map**)palloc(WSD_LEN*sizeof(Map*)); \
    wsd->prob = 1.0; \
	for( j = 0; j < WSD_LEN; j++ ){ \
		wsd->data[ j ] = \
		create_map ( PG_GETARG_INT32( 2 + 1 + j*3 ), PG_GETARG_INT32( 2 + 2 + j*3 ), 0, PG_GETARG_FLOAT4( 2 + 3 + j*3 ) ); \
	} \
	advance( WSD_LEN, state, wsd  ); \
	MemoryContextSwitchTo( oldcxt ); \
	PG_RETURN_DATUM( 1 ); 

/* This macro should always be in sync with MACRO aconf_accum except two differences:
 * 1. epsilon and delta is set in aconf_accum.
 * 2. The first two arguments in aconf_accum are epsilon and delta.
 *
 * To be less erro prone, aconf_accum should call accum. However, this will cause 
 * the warnings from the compiler. 
 */
#define accum( n, narg ) \
	MemoryContext oldcxt; \
	int j = 0; \
	generalState *state = ( ( AggState *) fcinfo->context )->genstate; \
	WSD *wsd; \
	if ( groupcxt == NULL )\
	{ \
		groupcxt = AllocSetContextCreate( NULL, "GroupContext",  ALLOCSET_DEFAULT_MINSIZE, \
                                        	 ALLOCSET_DEFAULT_INITSIZE, ALLOCSET_DEFAULT_MAXSIZE);\
    }\
    oldcxt = MemoryContextSwitchTo( groupcxt ); \
    wsd = (WSD*) palloc(sizeof(WSD));	\
	WSD_LEN = n; \
    wsd->data = (Map**)palloc(WSD_LEN*sizeof(Map*)); \
    wsd->prob = 1.0; \
	for( j = 0; j < WSD_LEN; j++ ){ \
		wsd->data[ j ] = \
		create_map ( PG_GETARG_INT32( narg + 1 + j*3 ), PG_GETARG_INT32( narg + 2 + j*3 ), 0, PG_GETARG_FLOAT4( narg + 3 + j*3 ) ); \
	} \
	advance( WSD_LEN, state, wsd  ); \
	MemoryContextSwitchTo( oldcxt ); \
	PG_RETURN_DATUM( 1 ); 

/* This macro should always be in sync with MACRO aconf_accum except two differences:
 * 1. epsilon and delta is set in aconf_accum.
 * 2. The first two arguments in aconf_accum are epsilon and delta.
 *
 * To be less erro prone, aconf_accum should call accum. However, this will cause 
 * the warnings from the compiler. 
 */
#define aconf_accum( n ) \
	MemoryContext oldcxt; \
	int j = 0; \
	generalState *state = ( ( AggState *) fcinfo->context )->genstate; \
	WSD *wsd; \
	epsilon = PG_GETARG_FLOAT4( 1 ); \
	delta = PG_GETARG_FLOAT4( 2 ); \
	if ( groupcxt == NULL )\
	{ \
		groupcxt = AllocSetContextCreate( NULL, "GroupContext",  ALLOCSET_DEFAULT_MINSIZE, \
                                        	 ALLOCSET_DEFAULT_INITSIZE, ALLOCSET_DEFAULT_MAXSIZE);\
    }\
    oldcxt = MemoryContextSwitchTo( groupcxt ); \
    wsd = (WSD*) palloc(sizeof(WSD));	\
	WSD_LEN = n; \
    wsd->data = (Map**)palloc(WSD_LEN*sizeof(Map*)); \
    wsd->prob = 1.0; \
	for( j = 0; j < WSD_LEN; j++ ){ \
		wsd->data[ j ] = \
		create_map ( PG_GETARG_INT32( 2 + 1 + j*3 ), PG_GETARG_INT32( 2 + 2 + j*3 ), 0, PG_GETARG_FLOAT4( 2 + 3 + j*3 ) ); \
	} \
	advance( WSD_LEN, state, wsd  ); \
	MemoryContextSwitchTo( oldcxt ); \
	PG_RETURN_DATUM( 1 ); 


/* number of input world-set descriptors */
int NUM_WSDS;
int current_size;

/* size of input world-set descriptors */
int WSD_LEN;

/*
 * Map = var assignment var->rng with probability prob. we also keep
 * reference to the world-set descriptor (wsd_host) using this
 * mapping. neg is only used in case of mutex computation to
 * succinctly represent as not(var->rng) all other assignments of
 * variable var.
 */
typedef struct 
{
  	int var;
  	int rng;
  	int neg;
  	prob prob; /* of var->rng; if neg=1, then the real prob is 1-prob. */
	int wt_offset;
	worldTableEntry *wt_entry; /* this is computed with wt_offset */
	int rng_offset;
	rngEntry *rng_entry; /* this is computed with rng_offset */
} Map;

/* 
 * A world-set decriptor is an array of assignments of different vars.
 * its probability is the product of prob of its var assignments.
 */
typedef struct
{
  Map** data;
  prob prob;
} WSD;

WSD** S;

// The memory context for a group of duplicates
MemoryContext groupcxt;

extern Map* create_map (int var, int rng, int neg, prob prob );
extern void genStateInit( generalState *state );
extern void updateWorldTable( generalState *s, Map *map );
extern int wtEntryInit( varType v, generalState *s );
extern int rngEntryInit( rngType rng, prob p, worldTableEntry *wt_entry );
extern void getMissingRngs( generalState *s );
extern void resetCount( generalState *s );
extern void resetTau( generalState *s );
extern void advance(int n, generalState *state, WSD* wsd);
extern void computeEntryPointers(generalState *s);

extern void printState( generalState *state );
extern void printBucket( generalState *state );
extern void printWSD(bitset* set);
extern void printWSD2(int a);

extern mList* mlist_make(int nbits);
extern void mlappend(mList* list, varType key, int index);
extern int search(mList *list, varType key);
extern void expand_hashtable(generalState *s);
extern void distribute_elements(generalState *s, varType key, int index);
extern mList *create_bucket(generalState *s, int index);
extern void update_buckets(generalState *s, int index, mList *list);


