/*-------------------------------------------------------------------------
 *
 * tupleconf.c
 *	  Implementation of tconf().
 *
 *
 * Copyright (c) 2008, MayBMS Development Group
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "fmgr.h"
#include "maybms/localcond.h"

/* Calculates the product of a series of probabilities  */
#define product(n) \
		int i = 0; \
		prob result = 1; \
		for(; i < n; i++) \
			result = result * PG_GETARG_FLOAT4(i); \
		PG_RETURN_FLOAT4(result);

/* Calculates the probabilities of a tuple coming from a join of urelations. 
 * Right now, if two triples of condition columns share the variable and domain,
 * one of the triples are set the a reserved variable and domain, and its 
 * probabilities becomes 1. If two triple contradicts each other, the probability of
 * one of them is set to 0.
 */
#define product_ge(n) \
	MemoryContext oldcxt; \
	int i, j; \
	prob result = 1.0; \
	WSD *wsd; \
	groupcxt = AllocSetContextCreate( NULL, "GroupContext",  ALLOCSET_DEFAULT_MINSIZE, \
                                        	 ALLOCSET_DEFAULT_INITSIZE, ALLOCSET_DEFAULT_MAXSIZE);\
    oldcxt = MemoryContextSwitchTo( groupcxt ); \
	wsd = (WSD*) palloc(sizeof(WSD));	\
    wsd->data = (Map**) palloc( n * sizeof(Map*) ); \
	for( j = 0; j < n; j++ ){ \
		wsd->data[ j ] = \
		create_map ( PG_GETARG_INT32( j*3 ), PG_GETARG_INT32( 1 + j*3 ), 0, PG_GETARG_FLOAT4( 2 + j*3 ) ); \
	} \
	for( i = 0; i < n - 1; i++ ) \
	{ \
		for( j = i + 1; j < n; j++ ) \
		{ \
			if ( wsd->data[ i ]->var == wsd->data[ j ]->var ) \
			{ \
				if (wsd->data[ i ]->rng == wsd->data[ j ]->rng) \
				{ \
					wsd->data[ j ]->var = RESERVED_VAR; \
					wsd->data[ j ]->rng = RNG_FOR_RESERVED_VAR; \
					wsd->data[ j ]->prob = 1; \
				} \
				else \
				{ \
					wsd->data[ j ]->var = RESERVED_VAR; \
					wsd->data[ j ]->rng = RNG_FOR_RESERVED_VAR_NEGATIVE; \
					wsd->data[ j ]->prob = 0; \
				} \
			} \
		} \
	} \
	for(i = 0; i < n; i++){ \
		result *= wsd->data[i]->prob; \
	} \
	MemoryContextSwitchTo(oldcxt); \
	MemoryContextDelete(groupcxt); \
	groupcxt = NULL; \
	\
	PG_RETURN_FLOAT4(result);
	
/* product0
 *
 * This is for tconf involving no uncertain relations.
 * It simply returns 1. 
 */	
Datum 
product0(PG_FUNCTION_ARGS)
{
	PG_RETURN_FLOAT4(1);
}

/* The following are the functions for tconf involving only urelations.
 * Currently, up to 10 triples of condition columns are allowed. 
 */
	 
/* product1_ge
 *
 * This is the function for tuples with only one triple of condition 
 * columns. It simply returns the probability. 
 */	
Datum 
product1_ge(PG_FUNCTION_ARGS)
{
	PG_RETURN_FLOAT4(PG_GETARG_FLOAT4(2));
}

/* product2_ge 
 *
 * Calculate tconf involving 2 triples 
 */
Datum 
product2_ge(PG_FUNCTION_ARGS)
{
	product_ge(2)
}

/* product3_ge 
 *
 * Calculate tconf involving 3 triples 
 */
Datum 
product3_ge(PG_FUNCTION_ARGS)
{
	product_ge(3)
}

/* product4_ge 
 *
 * Calculate tconf involving 4 triples 
 */
Datum 
product4_ge(PG_FUNCTION_ARGS)
{
	product_ge(4)
}

/* product5_ge 
 *
 * Calculate tconf involving 5 triples 
 */
Datum 
product5_ge(PG_FUNCTION_ARGS)
{
	product_ge(5)
}

/* product6_ge 
 *
 * Calculate tconf involving 6 triples 
 */
Datum 
product6_ge(PG_FUNCTION_ARGS)
{
	product_ge(6)
}

/* product7_ge 
 *
 * Calculate tconf involving 7 triples 
 */
Datum 
product7_ge(PG_FUNCTION_ARGS)
{
	product_ge(7)
}

/* product8_ge 
 *
 * Calculate tconf involving 8 triples 
 */
Datum 
product8_ge(PG_FUNCTION_ARGS)
{
	product_ge(8)
}

/* product9_ge 
 *
 * Calculate tconf involving 9 triples 
 */
Datum 
product9_ge(PG_FUNCTION_ARGS)
{
	product_ge(9)
}

/* product10_ge 
 *
 * Calculate tconf involving 10 triples 
 */
Datum 
product10_ge(PG_FUNCTION_ARGS)
{
	product_ge(10)
}

/* The following are the functions for tconf involving only tuple-independent 
 * probabilistic relations. Currently, up to 10 relations are allowed. 
 */

/* product1
 *
 * This is the function for tuples with only one tuple-independent relations.  
 * It simply returns the probability. 
 */	
Datum 
product1(PG_FUNCTION_ARGS)
{
	PG_RETURN_FLOAT4(PG_GETARG_FLOAT4(0));
}

/* product2
 *
 * Calculate tconf involving 2 tuple-independent relations. 
 */
Datum 
product2(PG_FUNCTION_ARGS)
{
	product(2)
}

/* product3
 *
 * Calculate tconf involving 3 tuple-independent relations. 
 */
Datum 
product3(PG_FUNCTION_ARGS)
{
	product(3)
}

/* product4
 *
 * Calculate tconf involving 4 tuple-independent relations. 
 */
Datum 
product4(PG_FUNCTION_ARGS)
{
	product(4)
}

/* product5
 *
 * Calculate tconf involving 5 tuple-independent relations. 
 */
Datum 
product5(PG_FUNCTION_ARGS)
{
	product(5)
}

/* product6
 *
 * Calculate tconf involving 6 tuple-independent relations. 
 */
Datum 
product6(PG_FUNCTION_ARGS)
{
	product(6)
}

/* product7
 *
 * Calculate tconf involving 7 tuple-independent relations. 
 */
Datum 
product7(PG_FUNCTION_ARGS)
{
	product(7)
}

/* product8
 *
 * Calculate tconf involving 8 tuple-independent relations. 
 */
Datum 
product8(PG_FUNCTION_ARGS)
{
	product(8)
}

/* product9
 *
 * Calculate tconf involving 9 tuple-independent relations. 
 */
Datum 
product9(PG_FUNCTION_ARGS)
{
	product(9)
}

/* product10
 *
 * Calculate tconf involving 10 tuple-independent relations. 
 */
Datum 
product10(PG_FUNCTION_ARGS)
{
	product(10)
}


