#include <stdio.h>
#include <stdlib.h>

#include "postgres.h"
#include "fmgr.h"
#include "executor/executor.h"
#include "executor/spi.h"

#include "pip.h"

extern bool pip_use_sampling_groups;

Datum   pip_debug_use_sampling_groups   (PG_FUNCTION_ARGS)
{
  pip_use_sampling_groups = PG_GETARG_BOOL(0);
  
  PG_RETURN_NULL();
}
