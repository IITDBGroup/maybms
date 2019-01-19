/*-------------------------------------------------------------------------
 *
 * conf_comp.h
 *        Prototypes for argmax.
 *
 *
 * Copyright (c) 2008, MayBMS Development Group
 *
 *-------------------------------------------------------------------------
 */

/* warning function for negative weight */
extern Datum test_negative(PG_FUNCTION_ARGS);
extern Datum test_from_0_to_1(PG_FUNCTION_ARGS);
 
/* varchar */
extern Datum argmax_varchar_float4_accum(PG_FUNCTION_ARGS);
extern Datum argmax_varchar_float8_accum(PG_FUNCTION_ARGS);
extern Datum argmax_varchar_int8_accum(PG_FUNCTION_ARGS);
extern Datum argmax_varchar_int4_accum(PG_FUNCTION_ARGS);
extern Datum argmax_varchar_int2_accum(PG_FUNCTION_ARGS);

/* int4 */
extern Datum argmax_int4_float4_accum(PG_FUNCTION_ARGS);
extern Datum argmax_int4_float8_accum(PG_FUNCTION_ARGS);
extern Datum argmax_int4_int8_accum(PG_FUNCTION_ARGS);
extern Datum argmax_int4_int4_accum(PG_FUNCTION_ARGS);
extern Datum argmax_int4_int2_accum(PG_FUNCTION_ARGS);

