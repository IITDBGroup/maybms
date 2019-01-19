/*-------------------------------------------------------------------------
 *
 * ws-tree.c
 *	  	Implementation of the ws-tree algorithm for exact confidence computation in conf().
 *
 *
 * Copyright (c) 2008, MayBMS Development Group
 *
 *-------------------------------------------------------------------------
 */

#include "maybms/localcond.h"
#include "maybms/conf_comp.h"

/* In case of variable elimination we can now use one of two
 * heuristics: minlog and minmax, as detailed in the paper.
 */
int WHICH_HEURISTIC = 0;

/* Local functions */

static bitset* find_independent_split(bitset* set);
static bitset* find_wsds_with_var_rng(bitset* set, int var, int rng);
static bitset* find_wsds_without_var(bitset* set, int var);
static void dfs (bitset* set, bitset* set_to_construct, int idx);
static void reset_wsds_var_rng(bitset* set, int var, int rng);
static int wsd_ind (WSD* d1, WSD* d2);
static int compare_map (Map* m1, Map* m2);
static worldTableEntry * choose_var_minlog(bitset* set, generalState *state );
static prob indve_compute_prob (bitset* set, generalState *state );

/* find_independent_split 
 *
 * Partition the given wsd set into 2 independent sets
 * first set is returned and contains dependent wsds.
 */
static bitset*
find_independent_split(bitset* set)
{
	int i = 0;
	bitset *subset;

  	if (bitset_test_empty(set))
    	return NULL;

  	subset  = bitset_init(NUM_WSDS);
  	bitset_reset(subset);

  	while (i < NUM_WSDS && !bitset_test_bit(set,i))
    	i++;

  	if (i == NUM_WSDS)
		elog(ERROR, "An invalid input of bitset!");

  	dfs(set,subset,i);
  
  	return subset;
}

static void
dfs (bitset* set, bitset* set_to_construct, int idx)
{ 
	int j;
  	bitset_set_bit(set_to_construct,idx);
  	for ( j=0; j<NUM_WSDS; j++)
    	if (bitset_test_bit(set,j) && !bitset_test_bit(set_to_construct,j) &&
    		!wsd_ind(S[idx],S[j]) && idx != j)
      	dfs(set,set_to_construct,j);
}

/* wsd_ind
 *
 * Return 0 if not independent and 1 if independent.
 */
static int 
wsd_ind (WSD* d1, WSD* d2)
{
  	int is_independent = 1;

  	int i=0;
  	
	while (i<WSD_LEN && is_independent)
    {
   		int j=0;
      
		while (j<WSD_LEN && compare_map(d1->data[i],d2->data[j]) == 0)
        	j++;
      	
		if (j < WSD_LEN)
        	is_independent = 0;

      	i++;
    }

  	return is_independent;
  
}

/*  compare_map
 *
 *  Return
 *  -2: equal 
 *  -1: mutex 
 *  0: different vars
 *  2: same vars, diff rng, both neg
 *  3: same vars, diff rng, diff neg (the positive one constrains the negative one)
 */
static int
compare_map (Map* m1, Map* m2)
{
	if (!m1 || !m2 || m1->rng == -1 || m2->rng == -1)
		return 0;

	if (m1->var == m2->var)
	{
		if (m1->rng == m2->rng)
    	{
      		if (m1->neg == m2->neg)
				return -2;
      		else
				return -1;
		}
    	else if (m1->neg != m2->neg)
			return 3;
  		else if (m1->neg && m2->neg)
	  		return 2;
		else 
	  		return -1;
  	}
  
  	return 0;
}

/* minlog_estimate
 *
 * Estimate = ln(e^s1+..+e^s_n), where si>0. si=size of partition for
 * an assignment of var that occurs in wsd set plus size of wsd subset
 * without var; there is an s_i for size of wsd subset without var.
 *
 * rec. formula for computing the estimate: 
 * f_k = f_{k-1}+ln(1+e^{sk-f_{k-1}}
 *
 * Find the variable with the best partitioning of the given wsd set
 */
static worldTableEntry *
choose_var_minlog(bitset* set, generalState *state)
{
	int i;int j;
	int set_size = 0;
	int var = -1;
	float8 val;
	worldTableEntry *wt_entry, *result = NULL;
	rngEntry *rng_entry;

	/* Reset the count of every wsd */
	resetCount( state );

	/* Loop the world set descriptors and count the appearances of every range value  */
	for( i = 0; i < NUM_WSDS; i++)
    	if (bitset_test_bit(set, i)) 
		{
      		for ( j = 0; j < WSD_LEN; j++) 
			{
				/* If the range value is valid, increase its count */
				if( S[i]->data[j]->rng >= 0 )
				{
					S[i]->data[j]->rng_entry->count++;
				}
      		}
      		
      		set_size++;
    	}

	val = -1;

	/* Loop the local world table */
	for ( i = 0; i < state->wt_entry_count; i++)
	{
    	int how_many_diff_assignments = 0;
    	int how_many_assignments = 0;
		wt_entry = state->wt_entries + i;

		/* Loop the range entries of a single variable */
		for ( j = 0; j < wt_entry->rng_entry_count; j++)
		{
			rng_entry = wt_entry->rng_entries + j;

			if( rng_entry->count > 0) 
			{
				how_many_diff_assignments++;
				how_many_assignments += rng_entry->count;
      		}
		}

		/* Loop the range entries of a single variable */
    	if (how_many_diff_assignments != 0) 
		{
      		int t = set_size - how_many_assignments;
    		float8 new_val = 0.0;

			new_val = wt_entry->rng_entry_count * t;

			for ( j = 0; j < wt_entry->rng_entry_count; j++)
			{
				rng_entry = wt_entry->rng_entries + j;
				
				new_val += rng_entry->count;
			}
      
      		if (val > new_val || val == -1) 
			{
				val = new_val;
				var = wt_entry->var;
				result = wt_entry;
      		}
    	}
  	}

  	return result;
}

/* find_wsds_without_var
 *
 * Find the wsds that do not contain mapping of the given var
 */
static bitset*
find_wsds_without_var(bitset* set, int var)
{
  	bitset *subset  = NULL;
	int i;
 
 	/* Loop the world set descriptors */
  	for ( i = 0; i < NUM_WSDS; i++)
    	if (bitset_test_bit(set,i)) 
		{
      		int j = 0;
      		
			while (j < WSD_LEN && S[i]->data[j]->var != var)
				j++;

      		if (j == WSD_LEN) 
			{
				if (subset == NULL) 
				{
	  				subset = bitset_init(NUM_WSDS);
	  				bitset_reset(subset);
				}
	
				bitset_set_bit(subset,i);
      		}
    	}
  
  	return subset;
}

/* find_wsds_with_var_rng 
 *
 * Find the world set descriptors that contain mapping var->rng; also *temporarily* drop this mapping.
 */
static bitset*
find_wsds_with_var_rng(bitset* set, int var, int rng)
{
  	bitset *subset  = NULL;
  	int found_empty_wsd = 0;
	int i = 0;
	
  	while (i < NUM_WSDS && !found_empty_wsd) 
  	{
    	if (bitset_test_bit(set,i)) 
    	{
      		int j = 0;
      		
      		while (j < WSD_LEN && (S[i]->data[j]->var != var || S[i]->data[j]->rng != rng))
				j++;
				
      		if (j < WSD_LEN) 
      		{
				if (subset == NULL) 
				{
	  				subset = bitset_init(NUM_WSDS);
	  				bitset_reset(subset);
				}
	
			bitset_set_bit(subset,i);

			/* Mark the used choices of rng for that var. */
			S[i]->data[j]->rng = -1;
			S[i]->prob /= S[i]->data[j]->prob;

			/* Check if the remaining S[i] is empty, 
			 * in which case the prob of all wsds agreeing with (var,rng) is just S[i]->data[j]->prob
			 */
			if (S[i]->prob == 1.0)
	  			found_empty_wsd = 1;
      		}
    	}
    	
    	i++;
  	}
  
  	if (found_empty_wsd) 
  	{
  		reset_wsds_var_rng(subset, var, rng);
    	bitset_reset(subset);
  	}

  	return subset;
}

/* reset_wsds_var_rng
 *
 * The temporarily dropped mappings (see find_wsds_with_var_rng) are restored.
 */
static void
reset_wsds_var_rng(bitset* set, int var, int rng)
{
	int i; int j;

	/* Loop the world set descriptors */
  	for ( i = 0; i < NUM_WSDS; i++)
    	if (bitset_test_bit(set,i))
    		/* Restore the world set descriptors */
      		for ( j = 0; j < WSD_LEN; j++)
				if(S[i]->data[j]->var == var) 
				{
	  				S[i]->data[j]->rng = rng;
	  				S[i]->prob *= S[i]->data[j]->prob;
				}
  
}

/* indve_compute_prob
 *
 * Compute the probability of a given set of wsds using independent
 * partitioning and variable elimination; set is the subset of wsd
 * S set for which to compute probability
 */
static prob
indve_compute_prob(bitset* set, generalState *state )
{
  	prob p_left = 0.0;
  	prob p_right = 0.0;
  	int i;
  	bitset *subset;
  	int pos;
  	int var;		
	prob p_without_var = -1;
	prob cur_prob = 0.0;
    worldTableEntry *wt_entry;
	rngEntry *rng_entry;
	bitset* subset_without_var;
	bitset* subset_var_rng;

	/* Return 0 if the set if empty */
  	if (bitset_test_empty(set))
  	{
    	return 0.0;
	}

  	/* Find subset of S independent of the rest (subset repr. dependent wsds) */
  	subset = find_independent_split(set);
 
  	/* Process the left subset containing dependent wsds */      
  
  	pos = bitset_test_singleton(subset);  

	/* Special case of 1 wsd */
	if (pos != -1) 
    	p_left = S[pos]->prob;
    /* Subset contains more than one wsd */
 	else 
 	{		
		wt_entry = choose_var_minlog(subset, state);

		/* All vars are used in the wsd set */
		if (wt_entry == NULL)
		{ 
			return 1.0;
   		}

		var = wt_entry->var;

		subset_without_var = find_wsds_without_var(subset, var);

		rng_entry = wt_entry->rng_entries;

		/* Loop the range values for a single variable */
		for ( i = 0; i < wt_entry->rng_entry_count; i++) 
		{
			cur_prob = ( rng_entry + i )->p;
			subset_var_rng = find_wsds_with_var_rng(subset, var, ( rng_entry + i )->rng );

		  	if (subset_var_rng == NULL) 
		  	{
				if (p_without_var == -1)
				{
		  			p_without_var = indve_compute_prob (subset_without_var, state);
				}
				
				cur_prob *= p_without_var;
		  	}
		  	else 
		  	{
				if (bitset_test_empty(subset_var_rng))
					;
				else 
				{
		  			bitset_union(subset_var_rng, subset_without_var);
					cur_prob *= indve_compute_prob (subset_var_rng, state);
					reset_wsds_var_rng(subset_var_rng, var, ( rng_entry + i )->rng );
				}
		  	}

			if (subset_var_rng)
				bitset_free(subset_var_rng);

			p_left += cur_prob;

			/* Stop early */
		  	if ((p_left == 1.0) && (wt_entry->rng_entry_count == 1)) 
		  	{
		  		
				if (subset_without_var)
					bitset_free(subset_without_var);
				
				bitset_free(subset);
						  	
				return 1.0;
		  	}
    	}
    	
		if (subset_without_var)
			bitset_free(subset_without_var);
  	}

	/* Stop early */
  	if (p_left == 1.0) 
  	{
  		bitset_free(subset);
  		
    	return 1.0;
  	}

  	/* Process recursively the right subset */

  	bitset_negate(set,subset);
  	p_right = indve_compute_prob(subset, state );
  	
  	bitset_free(subset);

  	/* Combine the probabilities of left and right subsets */

  	return p_left + p_right - p_left * p_right;
}


/* Following are transition functions for ws-tree algorithm.
 * They are actually only dummies and the task is done in the MACRO accum
 */

/* TODO: We can use arrays to store the condition columns in the future. */

/* conf_accum1_ge
 *
 * Transition function for exact confidence computation with 1 triple of 
 * condition columns 
 */
Datum 
conf_accum1_ge(PG_FUNCTION_ARGS)
{
	accum( 1 , 0 )
}

/* conf_accum2_ge
 *
 * Transition function for exact confidence computation with 2 triples of 
 * condition columns 
 */
Datum 
conf_accum2_ge(PG_FUNCTION_ARGS)
{
	accum( 2 , 0 )
}

/* conf_accum3_ge
 *
 * Transition function for exact confidence computation with 3 triples of 
 * condition columns 
 */
Datum 
conf_accum3_ge(PG_FUNCTION_ARGS)
{
	accum( 3 , 0 )
}

/* conf_accum4_ge
 *
 * Transition function for exact confidence computation with 4 triples of 
 * condition columns 
 */
Datum 
conf_accum4_ge(PG_FUNCTION_ARGS)
{
	accum( 4 , 0 )
}

/* conf_accum5_ge
 *
 * Transition function for exact confidence computation with 5 triples of 
 * condition columns 
 */
Datum 
conf_accum5_ge(PG_FUNCTION_ARGS)
{
	accum( 5 , 0 )
}

/* conf_accum6_ge
 *
 * Transition function for exact confidence computation with 6 triples of 
 * condition columns 
 */
Datum 
conf_accum6_ge(PG_FUNCTION_ARGS)
{
	accum( 6 , 0 )
}

/* conf_accum7_ge
 *
 * Transition function for exact confidence computation with 7 triples of 
 * condition columns 
 */
Datum 
conf_accum7_ge(PG_FUNCTION_ARGS)
{
	accum( 7 , 0 )
}

/* conf_accum8_ge
 *
 * Transition function for exact confidence computation with 8 triples of 
 * condition columns 
 */
Datum 
conf_accum8_ge(PG_FUNCTION_ARGS)
{
	accum( 8 , 0 )
}

/* conf_accum9_ge
 *
 * Transition function for exact confidence computation with 9 triples of 
 * condition columns 
 */
Datum 
conf_accum9_ge(PG_FUNCTION_ARGS)
{
	accum( 9 , 0 )
}

/* conf_accum10_ge
 *
 * Transition function for exact confidence computation with 10 triples of 
 * condition columns 
 */
Datum 
conf_accum10_ge(PG_FUNCTION_ARGS)
{
	accum( 10 , 0 )
}

/* conf_accum11_ge
 *
 * Transition function for exact confidence computation with 11 triples of 
 * condition columns 
 */
Datum 
conf_accum11_ge(PG_FUNCTION_ARGS)
{
	accum( 11 , 0 )
}

/* conf_accum12_ge
 *
 * Transition function for exact confidence computation with 12 triples of 
 * condition columns 
 */
Datum 
conf_accum12_ge(PG_FUNCTION_ARGS)
{
	accum( 12 , 0 )
}

/* conf_accum13_ge
 *
 * Transition function for exact confidence computation with 13 triples of 
 * condition columns 
 */
Datum 
conf_accum13_ge(PG_FUNCTION_ARGS)
{
	accum( 13 , 0 )
}

/* conf_accum14_ge
 *
 * Transition function for exact confidence computation with 14 triples of 
 * condition columns 
 */
Datum 
conf_accum14_ge(PG_FUNCTION_ARGS)
{
	accum( 14 , 0 )
}

/* conf_accum15_ge
 *
 * Transition function for exact confidence computation with 15 triples of 
 * condition columns 
 */
Datum 
conf_accum15_ge(PG_FUNCTION_ARGS)
{
	accum( 15 , 0 )
}

/* conf_accum16_ge
 *
 * Transition function for exact confidence computation with 16 triples of 
 * condition columns 
 */
Datum 
conf_accum16_ge(PG_FUNCTION_ARGS)
{
	accum( 16 , 0 )
}

/* conf_accum17_ge
 *
 * Transition function for exact confidence computation with 17 triples of 
 * condition columns 
 */
Datum 
conf_accum17_ge(PG_FUNCTION_ARGS)
{
	accum( 17 , 0 )
}

/* conf_accum18_ge
 *
 * Transition function for exact confidence computation with 18 triples of 
 * condition columns 
 */
Datum 
conf_accum18_ge(PG_FUNCTION_ARGS)
{
	accum( 18 , 0 )
}

/* conf_accum19_ge
 *
 * Transition function for exact confidence computation with 19 triples of 
 * condition columns 
 */
Datum 
conf_accum19_ge(PG_FUNCTION_ARGS)
{
	accum( 19 , 0 )
}

/* conf_accum20_ge
 *
 * Transition function for exact confidence computation with 20 triples of 
 * condition columns 
 */
Datum 
conf_accum20_ge(PG_FUNCTION_ARGS)
{
	accum( 20 , 0 )
}

/* conf_final_ge
 *
 * The final function for exact confidence computation.
 */
Datum 
conf_final_ge(PG_FUNCTION_ARGS)
{
	generalState *state = ( ( AggState *) fcinfo->context )->genstate;
	prob result = 0;
	bitset* set;
	MemoryContext oldcxt;
	
	/* Return 0 if there is no tuple */
	if (groupcxt == NULL)
		PG_RETURN_FLOAT4(0);	
	
	/* Switch to the group context */
	oldcxt = MemoryContextSwitchTo( groupcxt ); 

	/* Complete the local world table */
	getMissingRngs( state ); 

	/* Compute the entry pointers of all clauses */
	computeEntryPointers(state);
  	
  	/* bitset related operation */
  	set = bitset_init(NUM_WSDS);  
  	bitset_set(set);             

	/* Compute the probability */
    result = indve_compute_prob(set, state ); 
	
	/* Switch back to the old context */
	MemoryContextSwitchTo( oldcxt );
	MemoryContextDelete( groupcxt );
	
	/* Set the world-set-descriptor-related global variables to NULL */
	NUM_WSDS = 0;
	S = NULL;
	groupcxt = NULL;      

	/* Return the result */
	PG_RETURN_FLOAT4( result );	
}

