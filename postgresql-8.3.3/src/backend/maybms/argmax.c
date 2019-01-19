/*-------------------------------------------------------------------------
 *
 * argmax.c
 *	  Implementation of argmax().
 *
 * Currently, argmax is implemented as an aggregate function. This requires that
 * a argmax with new input type are registered and implemented before using it. 
 * In addition, only one value is returned by one argmax. If more returned columns
 * are required, the input should be spread to several argmax.
 *
 *
 * Copyright (c) 2008, MayBMS Development Group
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "fmgr.h"
#include "maybms/argmax.h"
#include "nodes/execnodes.h"

/* Local functions.  */
static argmaxState *get_argmax_state(AggState *aggState);
static int float4_cmp_internal(float4 a, float4 b);
static int float8_cmp_internal(float8 a, float8 b);

/* An argmax macro implementation with second variable as a float.
 * The float value can not be simply compared by using ">" or "<",
 * because of special case NAN.
 */
#define argmax_float(type1, gettype1, type1Return, type2, type2MAX, gettype2, type2comp) \
	argmaxState *state = get_argmax_state( ( AggState *) fcinfo->context ); \
	type2 		arg1 = state->type2MAX; \
	type2      	arg2 = gettype2(2); \
	type1		result; \
	if (type2comp(arg1,arg2) > 0) \
	{ \
		result = gettype1(0); \
		state->type2MAX = arg1; \
	} \
	else \
	{ \
		result = gettype1(1); \
		state->type2MAX = arg2; \
	} \
	\
	type1Return(result);

/* An argmax macro implementation with second variable as an integer. */	
#define argmax_int(type1, gettype1, type1Return, type2, type2MAX, gettype2) \
	argmaxState *state = get_argmax_state( ( AggState *) fcinfo->context ); \
	type2 		arg1 = state->type2MAX; \
	type2      	arg2 = gettype2(2); \
	type1		result; \
	if (arg1>arg2) \
	{ \
		result = gettype1(0); \
		state->type2MAX = arg1; \
	} \
	else \
	{ \
		result = gettype1(1); \
		state->type2MAX = arg2; \
	} \
	\
	type1Return(result);

/* argmax_int4_int2_accum
 *
 * This is the argmax function for int4 and int2.
 */
Datum 
argmax_int4_int2_accum(PG_FUNCTION_ARGS)
{	
	argmax_int(int32, PG_GETARG_INT32, PG_RETURN_INT32, 
		int16, int16MAX, PG_GETARG_INT16)
}

/* argmax_int4_int4_accum
 *
 * This is the argmax function for int4 and int4.
 */
Datum 
argmax_int4_int4_accum(PG_FUNCTION_ARGS)
{	
	argmax_int(int32, PG_GETARG_INT32, PG_RETURN_INT32, 
		int32, int32MAX, PG_GETARG_INT32)
}

/* argmax_int4_int8_accum
 *
 * This is the argmax function for int4 and int8.
 */
Datum 
argmax_int4_int8_accum(PG_FUNCTION_ARGS)
{	
	argmax_int(int32, PG_GETARG_INT32, PG_RETURN_INT32, 
		int64, int64MAX, PG_GETARG_INT64)
}

/* argmax_int4_float4_accum
 *
 * This is the argmax function for int4 and float4.
 */
Datum 
argmax_int4_float4_accum(PG_FUNCTION_ARGS)
{
	argmax_float(int32, PG_GETARG_INT32, PG_RETURN_INT32, 
		float4, float4MAX, PG_GETARG_FLOAT4, float4_cmp_internal)
}

/* argmax_int4_float8_accum
 *
 * This is the argmax function for int4 and float8.
 */
Datum 
argmax_int4_float8_accum(PG_FUNCTION_ARGS)
{
	argmax_float(int32, PG_GETARG_INT32, PG_RETURN_INT32, 
		float8, float8MAX, PG_GETARG_FLOAT8, float8_cmp_internal)
}

/* argmax_varchar_int2_accum
 *
 * This is the argmax function for varchar and int2.
 */
Datum 
argmax_varchar_int2_accum(PG_FUNCTION_ARGS)
{	
	argmax_int(Datum, PG_GETARG_DATUM, PG_RETURN_VARCHAR_P, 
		int16, int16MAX, PG_GETARG_INT16)
}

/* argmax_varchar_int4_accum
 *
 * This is the argmax function for varchar and int4.
 */
Datum 
argmax_varchar_int4_accum(PG_FUNCTION_ARGS)
{	
	argmax_int(Datum, PG_GETARG_DATUM, PG_RETURN_VARCHAR_P, 
		int32, int32MAX, PG_GETARG_INT32)
}

/* argmax_varchar_int8_accum
 *
 * This is the argmax function for varchar and int8.
 */
Datum 
argmax_varchar_int8_accum(PG_FUNCTION_ARGS)
{	
	argmax_int(Datum, PG_GETARG_DATUM, PG_RETURN_VARCHAR_P, 
		int64, int64MAX, PG_GETARG_INT64)
}

/* argmax_varchar_float4_accum
 *
 * This is the argmax function for varchar and float4.
 */
Datum 
argmax_varchar_float4_accum(PG_FUNCTION_ARGS)
{
	argmax_float(Datum, PG_GETARG_DATUM, PG_RETURN_VARCHAR_P, 
		float4, float4MAX, PG_GETARG_FLOAT4, float4_cmp_internal)
}

/* argmax_varchar_float8_accum
 *
 * This is the argmax function for varchar and float8.
 */
Datum 
argmax_varchar_float8_accum(PG_FUNCTION_ARGS)
{
	argmax_float(Datum, PG_GETARG_DATUM, PG_RETURN_VARCHAR_P, 
		float8, float8MAX, PG_GETARG_FLOAT8, float8_cmp_internal)
}

/* test_negative
 *
 * This will produce an error if the input is negative. 
 */
Datum 
test_negative(PG_FUNCTION_ARGS)
{
	float4      weight = PG_GETARG_FLOAT4(0);
	
	if (float4_cmp_internal(weight, 0.0) < 0)
	{
		elog(ERROR,"Negative weight:%f. Weight must have a non-negative value", weight);
	}		

	PG_RETURN_FLOAT4(weight);
}

/* test_negative
 *
 * This will produce an error if the input is negative. 
 */
Datum 
test_from_0_to_1(PG_FUNCTION_ARGS)
{
	float4      weight = PG_GETARG_FLOAT4(0);
	
	//elog(WARNING,"%f", weight);
	
	if (float4_cmp_internal(weight, 0.0) < 0 || float4_cmp_internal(weight, 1.0) > 0)
	{
		elog(ERROR,"Invalid probability:%f. The probability must be from (0,1]. ", weight);
	}		

	PG_RETURN_FLOAT4(weight);
}

/* get_argmax_state
 *
 * Get argmaxState from the aggState.
 */
static argmaxState *
get_argmax_state(AggState *aggState)
{

	if (((Agg *) aggState->ss.ps.plan)->aggstrategy == AGG_HASHED)
		return aggState->currentEntry->argmax;
	else	
		return aggState->argmax;
}

/* float4_cmp_internal
 *
 * Compare the values of two float4.
 * If a > b, return 1;
 * If a = b, return 0;
 * If a < b, return -1;
 */
static int
float4_cmp_internal(float4 a, float4 b)
{
	/*
	 * We consider all NANs to be equal and larger than any non-NAN. This is
	 * somewhat arbitrary; the important thing is to have a consistent sort
	 * order.
	 */
	if (isnan(a))
	{
		if (isnan(b))
			return 0;			/* NAN = NAN */
		else
			return 1;			/* NAN > non-NAN */
	}
	else if (isnan(b))
	{
		return -1;				/* non-NAN < NAN */
	}
	else
	{
		if (a > b)
			return 1;
		else if (a < b)
			return -1;
		else
			return 0;
	}
}

/* float8_cmp_internal
 *
 * Compare the values of two float8.
 * If a > b, return 1;
 * If a = b, return 0;
 * If a < b, return -1;
 */
static int
float8_cmp_internal(float8 a, float8 b)
{
	/*
	 * We consider all NANs to be equal and larger than any non-NAN. This is
	 * somewhat arbitrary; the important thing is to have a consistent sort
	 * order.
	 */
	if (isnan(a))
	{
		if (isnan(b))
			return 0;			/* NAN = NAN */
		else
			return 1;			/* NAN > non-NAN */
	}
	else if (isnan(b))
	{
		return -1;				/* non-NAN < NAN */
	}
	else
	{
		if (a > b)
			return 1;
		else if (a < b)
			return -1;
		else
			return 0;
	}
}

