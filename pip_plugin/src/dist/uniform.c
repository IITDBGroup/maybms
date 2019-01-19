#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>
#include "postgres.h"			/* general Postgres declarations */
#include "fmgr.h"
#include "libpq/pqformat.h"		/* needed for send/recv functions */
#include "executor/executor.h"
#include "catalog/pg_type.h"  

#include "pip.h"

void    pip_uniform_init (pip_var *var, HeapTupleHeader params);
float8  pip_uniform_gen  (pip_var *var, int64 seed);
float8  pip_uniform_pdf  (pip_var *var, float8 point);
float8  pip_uniform_cdf  (pip_var *var, float8 point);
float8  pip_uniform_icdf (pip_var *var, float8 point);
int     pip_uniform_in   (pip_var *var, char *str);
int     pip_uniform_out  (pip_var *var, int len, char *str);

DECLARE_PIP_DISTRIBUTION(uniform) = {
  .name = "Uniform",
  .size = sizeof(float8) * 2,
  .init= &pip_uniform_init,
  .gen = &pip_uniform_gen,
  .pdf = &pip_uniform_pdf,
  .cdf = &pip_uniform_cdf,
  .icdf= &pip_uniform_icdf,
  .in  = &pip_uniform_in,
  .out = &pip_uniform_out,
  .joint= false
};



void    pip_uniform_init (pip_var *var, HeapTupleHeader params)
{
  ((float8 *)var->group_state)[0] = dist_param_float8(params, 0, 0.0);
  ((float8 *)var->group_state)[1] = dist_param_float8(params, 1, 1.0);
  if(((float8 *)var->group_state)[0] > ((float8 *)var->group_state)[1]){
    float8 tmp = ((float8 *)var->group_state)[1];
    ((float8 *)var->group_state)[1] = ((float8 *)var->group_state)[0];
    ((float8 *)var->group_state)[0] = tmp;
  }
}
float8  pip_uniform_gen  (pip_var *var, int64 seed)
{
  double rng = pip_prng_float(&seed);
  double ret = ((float8 *)var->group_state)[0] + rng * ( ((float8 *)var->group_state)[1] - ((float8 *)var->group_state)[0] );
//  elog(NOTICE, "Generating Uniform: [%lf, %lf](%lf) = %lf", ((float8 *)var->group_state)[0], ((float8 *)var->group_state)[1], rng, ret);
  return ret;
}
float8  pip_uniform_pdf  (pip_var *var, float8 point)
{
  if( (point >= ((float8 *)var->group_state)[1]) || (point <= ((float8 *)var->group_state)[0]) ) { 
    return 0;
  }
  return 1.0 / ( ((float8 *)var->group_state)[1] - ((float8 *)var->group_state)[0] );
}
float8  pip_uniform_cdf  (pip_var *var, float8 point)
{
  if (point >= ((float8 *)var->group_state)[1]) { 
    return 1.0;
  } else if (point < ((float8 *)var->group_state)[0]) {
    return 0.0;
  } else {
    return (point - ((float8 *)var->group_state)[0]) / (((float8 *)var->group_state)[1] - ((float8 *)var->group_state)[0]);
  }
}
float8  pip_uniform_icdf (pip_var *var, float8 point)
{
  return point * (((float8 *)var->group_state)[1] - ((float8 *)var->group_state)[0]) + ((float8 *)var->group_state)[0];
}
int pip_uniform_in   (pip_var *var, char *str)
{
  double low,high;
  int c;
  
  sscanf(str, "(%lf:%lf)%n", &low, &high, &c);
  
  ((float8 *)var->group_state)[0] = low;
  ((float8 *)var->group_state)[1] = high;
  
  if(((float8 *)var->group_state)[0] > ((float8 *)var->group_state)[1]){
    float8 tmp = ((float8 *)var->group_state)[1];
    ((float8 *)var->group_state)[1] = ((float8 *)var->group_state)[0];
    ((float8 *)var->group_state)[0] = tmp;
  }
  
  return c;
}
int pip_uniform_out  (pip_var *var, int len, char *str)
{
  return snprintf(str, len, "(%lf:%lf)", (double)((float8 *)var->group_state)[0], (double)((float8 *)var->group_state)[1]);
}




