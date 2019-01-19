/*-------------------------------------------------------------------------
 *
 * d-tree.c
 *	  	Implementation of the decomposition-tree algorithm for approximate confidence computation.
 *
 *
 * Copyright (c) 2009, MayBMS Development Group
 *
 *-------------------------------------------------------------------------
 */

#include "maybms/localcond.h"
#include "maybms/conf_comp.h"

/* Macros used in bitset operation */
#define BITSET_USED(nbits) \
	( ((nbits) + (BITSET_BITS - 1)) / BITSET_BITS )
	
#define BITSET_BITS \
	( CHAR_BIT * sizeof(size_t) )

/* Macros used to identify different situation after variable elimination */
#define SUBSET_VAR_RNG_IS_NULL 0

#define SUBSET_VAR_RNG_IS_EMPTY 1

#define SUBSET_VAR_RNG_SHOULD_UNION 2

//#define STATISTICS 1

/* The approach of confidence computation, true for relative and false for absolute */
bool is_relative = true;
char *appro_approach = NULL;

/* The error allowed in approximation */
prob appro_epsilon = 0.05;

/* Variables used in statistics collection */
int counter = 0;
int ind_counter = 0;
int subsumption_counter = 0;

/* A data structure for passing around the coefficients and constants used in the
 * upper and lower bound computation. 
 */
typedef struct
{
	float8 coefficient_upper;	/* Coefficient of the whole upper bound */
	float8 constant_upper;		/* Constant of the whole upper bound */	
	
	float8 coefficient_lower;	/* Coefficient of the whole lower bound */
	float8 constant_lower;		/* Constant of the whole lower bound */	
	
	float8 condition_coefficient_upper;	/* Coefficient of the whole upper bound in deciding whether to close a leave */
	float8 condition_constant_upper;	/* Constant of the whole upper bound in deciding whether to close a leave */
} bound_information;

/* A data structure for keeping a bucket. A bucket is a set of clauses among whom
 * no variables are shared. 
 */
typedef struct
{
	int capacity; 	/* The capacity of variables in the bucket */
	int count;		/* The current number of variables in the bucket */
	varType *vars;	/* The set of variables in the bucket */
	float8 prob;	/* The probability of clauses in the bucket */
} bucket_info;

/* A data structure for keeping the set of all buckets */
typedef struct
{
	int capacity;			/* The capacity of buckets */
	int count;				/* The current number of buckets */
	bucket_info *bucket_list;	/* The set of all buckets */
} buckets;

/* If the results of equations are not bigger than stopping_number, 
 * then the decomposition tree refinement can stop or a leave can be safely closed
 * (the equations vary in different cases). It is calculated once before
 * the tree construction and is never changed afterwords.
 */
float8 stopping_number;

/* This variable is set to true when an epsilon-approximation is reached */
bool has_satisfied_stopping_condition;

/* Local functions */

/* Functions used in quick sort */
static void quicksort(WSD **array, int left, int right);
static int partition(WSD **array, int left, int right);
static prob findMedianOfMedians(WSD **array, int left, int right);
static int findMedianIndex(WSD **array, int left, int right, int shift);
static void swap(WSD **array, int a, int b);

/* Heuristic for variable elimination */
static void compute_upper_and_lower_bounds(bitset *set, float8 *upper, float8 *lower, generalState *state);
static bool exists_in_bucket(WSD* wsd, bucket_info *bucket);
static void add_to_bucket(WSD* wsd, bucket_info *bucket);
static void add_var_to_bucket(int var, bucket_info *bucket);
static void add_new_bucket(WSD *wsd, buckets *all_buckets);

/* The major functions */
static void decomposition_tree_approximate(bitset* set, generalState *state, 
	float8 path_prob, float8 *lower, float8 *upper, 
	bound_information *bound_info, int latest_var_column);

static prob decomposition_tree_exact(bitset* set, generalState *state, 
	int latest_var_column);

/* Heuristic for variable elimination */
static worldTableEntry *choose_var_max_occur_same_column(bitset* set, generalState *state, int latest_var_col, int *new_var_col);

/* Functions used in bitset operation */
static void bitset_union_removing_subsumption(bitset *set1, bitset *set2);
static bitset* find_independent_split(bitset* set);
static bitset* find_wsds_with_var_rng(bitset* set, int var, int rng);
static bitset* find_wsds_without_var(bitset* set, int var);
static void dfs (bitset* set, bitset* set_to_construct, int idx);
static void reset_wsds_var_rng(bitset* set, int var, int rng);
static int wsd_ind (WSD* d1, WSD* d2);
static int compare_map (Map* m1, Map* m2);

/* compute_upper_and_lower_bounds
 *
 * Compute the probability upper and lower bounds for a set of clauses.
 * The heuristic used here is named "bucketing" in the paper.
 */
static void 
compute_upper_and_lower_bounds(bitset *set, float8 *upper, float8 *lower, generalState *state)
{
	float8 sum = 0;
	float8 max = 0;
	int i, j;
	buckets *all_buckets;
	MemoryContext oldcxt, currentcxt;
	
	/* If the bitset is NULL, set both bounds to zeros. */
	if (set == NULL)
	{
		*lower = 0;
		*upper = 0;
		return;
	}
	
	/* Allocate a new memory context. */
	currentcxt = AllocSetContextCreate( NULL, "CurrentContext", 
										ALLOCSET_DEFAULT_MINSIZE,
										ALLOCSET_DEFAULT_INITSIZE,
										ALLOCSET_DEFAULT_MAXSIZE);

	/* Allocate a new memory context. */
	oldcxt = MemoryContextSwitchTo(currentcxt);
	
	/* Initialization of the list of buckets */
	all_buckets = (buckets *) palloc0(sizeof(buckets));
	
	all_buckets->capacity = 10;
	
	all_buckets->count = 0;
	
	all_buckets->bucket_list = (bucket_info *) palloc0(sizeof(bucket_info) * all_buckets->capacity);
	
	/* Loop over all clauses */
	for (i = 0; i < NUM_WSDS; i++)
		/* If the bit of a clause is set, process it */
		if (bitset_test_bit(set,i))
		{
			bool bucket_is_found = false;
		
			/* Loop over all buckets */
			for (j = 0; j < all_buckets->count; j++)
			{
				/* Important: Do not write code like this: 
				 * Put bucket_list = all_buckets->bucket_list; outside the loop 
				 * and write bucket_info *bucket = bucket_list + j;
				 * This will cause the crash of system because all_buckets->bucket_list
				 * may change in the loop. 
				 */
				bucket_info *bucket = all_buckets->bucket_list + j;
			
				/* If a clause does not share any variable with clauses in a bucket,
				 * add it to the bucket.
				 */
				if (!exists_in_bucket(S[i], bucket))
				{
					add_to_bucket(S[i], bucket);
					
					bucket_is_found = true;
					
					break;
				}
			}
			
			/* If a clause share variables with clauses in all existing buckets
			 * create a new one for it.
			 */
			if (!bucket_is_found)
			{			
				add_new_bucket(S[i], all_buckets);
			}
		}
	
	/* Loop over all buckets */	
	for (i = 0; i < all_buckets->count; i++)
	{
		float8 p = (all_buckets->bucket_list + i)->prob;
		
		/* Keep the maximal probability of the buckets */	
		if (p > max)
			max = p;
		
		/* Calculate the sum of probabilities of all buckets */		
		sum += p;
	}
	
	/* Set the lower bound as the maximal probability of all buckets */	
	*lower = max;
	
	/* Set the upper bound as 1 or sum of probabilities of all buckets */	
	if (sum > 1)
		*upper = 1;
	else
		*upper = sum;	
		
	/* Switch back to the old memory context and delete the current one. */		
	MemoryContextSwitchTo(oldcxt);
	
	MemoryContextDelete(currentcxt);
}

/* exists_in_bucket
 *
 * Return true if a clause share any variables with clauses in a bucket.
 */
static bool
exists_in_bucket(WSD* wsd, bucket_info *bucket)
{
	int i, j;
	
	/* Loop over all variables in the bucket */	
	for(i = 0; i < bucket->count; i++)
	{
		varType var = *(bucket->vars + i); 
	
		/* Loop over all variables in the clause */
		for (j = 0; j < WSD_LEN; j++)
		{
			/* If a variable has not been eliminated and it is the same as 
			 * as a variable in the bucket, return true;
			 */
			if (wsd->data[j]->rng != -1 && wsd->data[j]->var == var)
			{
				return true;
			}
		}
	}
	
	/* If no match is found, return false */
	return false;
}

/* add_to_bucket
 *
 * Add all variables of a clause to a bucket.
 */
static void
add_to_bucket(WSD* wsd, bucket_info *bucket)
{
	int i;
	
	/* Loop over all variables in the clause */
	for (i = 0; i < WSD_LEN; i++)
	{
		/* If a variable has not been eliminated, add it to the bucket */
		if (wsd->data[i]->rng != -1)
		{
			add_var_to_bucket(wsd->data[i]->var, bucket);
		}
	}
	
	/* Update the probability of the bucket */
	bucket->prob = bucket->prob + wsd->prob - bucket->prob * wsd->prob;
}

/* add_var_to_bucket
 *
 * Add a variable to a bucket.
 */
static void
add_var_to_bucket(int var, bucket_info *bucket)
{		
	*(bucket->vars + bucket->count) = var;
	
	bucket->count++;	
	
	/* If the bucket is full, double its size */
	if (bucket->capacity == bucket->count)
	{	
		bucket->capacity = bucket->capacity * 2;
	
		bucket->vars = (varType *) repalloc(bucket->vars, bucket->capacity * sizeof(varType));
	}
}

/* add_var_to_bucket
 *
 * Add a variable to a bucket.
 */
static void
add_new_bucket(WSD *wsd, buckets *all_buckets)
{
	bucket_info *bucket;
	
	/* Initialization of a new bucket */
	bucket = all_buckets->bucket_list + all_buckets->count;
	
	bucket->capacity = WSD_LEN;
	
	bucket->count = 0;
	
	bucket->prob = 0;
	
	bucket->vars = (varType *) palloc0(sizeof(varType) * WSD_LEN);
	
	/* Add a clause to the new bucket */
	add_to_bucket(wsd, bucket);
	
	all_buckets->count++;

	/* If the bucket list is full, double its size */
	if (all_buckets->capacity == all_buckets->count)
	{	
		all_buckets->capacity = all_buckets->capacity * 2;
	
		all_buckets->bucket_list = (bucket_info *) repalloc(all_buckets->bucket_list, all_buckets->capacity * sizeof(bucket_info));
	}
}

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

/* choose_var_max_occur_same_column
 *
 * Find a variable in column latest_var_col with most occurrence. If all in this 
 * column has been eliminated, pick a variable with most occurrence in any column. 
 */
static worldTableEntry *
choose_var_max_occur_same_column(bitset* set, generalState *state, int latest_var_col, int *new_var_col)
{
	int i;
	int j;
	float8 same_column_max = -1;
	float8 other_columns_max = -1;
	worldTableEntry *same_column_max_wt_entry = NULL;
	worldTableEntry *other_columns_max_wt_entry = NULL;

	/* Clear the counters for all varibles */
	for ( i = 0; i < state->wt_entry_count; i++)
	{
		(state->wt_entries + i)->occur_count = 0;
  	}	

	/* Loop over all clauses and count the occurrence of every variable */
	for( i = 0; i < NUM_WSDS; i++)
    	if (bitset_test_bit(set, i)) 
		{
      		for ( j = 0; j < WSD_LEN; j++) 
			{
				/* If a variable has not been eliminated, increase its occurrence */
				if( S[i]->data[j]->rng >= 0 )
				{
					S[i]->data[j]->wt_entry->occur_count++;
					
					if (j == latest_var_col)
					{
						if (S[i]->data[j]->wt_entry->occur_count > same_column_max)
						{
							same_column_max_wt_entry = S[i]->data[j]->wt_entry;
							same_column_max = S[i]->data[j]->wt_entry->occur_count;
							*new_var_col = j;
						}	
					}
					else if (same_column_max == -1)
					{
						if (S[i]->data[j]->wt_entry->occur_count > other_columns_max)
						{
							other_columns_max_wt_entry = S[i]->data[j]->wt_entry;
							other_columns_max = S[i]->data[j]->wt_entry->occur_count;
							*new_var_col = j;
						}							
					}					
				}
      		}
    	}

	if (same_column_max != -1)
	{
		return same_column_max_wt_entry;
	}
	else
		return other_columns_max_wt_entry;
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
      		int j;
      		
      		for(j = 0; j < WSD_LEN; j++)
      		{
      			if (S[i]->data[j]->var == var && S[i]->data[j]->rng == rng)
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

/* quicksort
 *
 * Quicksort the array.
 */
static void 
quicksort(WSD **array, int left, int right)
{
	int index;

    if(left >= right)
        return;
 
    index = partition(array, left, right);
    quicksort(array, left, index - 1);
    quicksort(array, index + 1, right);
}
 
/* partition
 * 
 * Partition the array into two halves and return the index about which the 
 * array is partitioned.
 */
static int 
partition(WSD **array, int left, int right)
{
	int pivotIndex, index, i;
	prob pivotValue;

    /* Makes the leftmost element a good pivot, specifically the median of medians */ 	
    findMedianOfMedians(array, left, right);

    pivotIndex = left; 
    index = left;
    
 	pivotValue = array[pivotIndex]->prob;
     
    swap(array, pivotIndex, right);
    
    for(i = left; i < right; i++)
    {
        if(array[i]->prob > pivotValue)
        {
            swap(array, i, index);
            index += 1;
        }
    }
    
    swap(array, right, index);
 
    return index;
}

/* findMedianOfMedians
 * 
 * Computes the median of each group of 5 elements and stores it as the first 
 * element of the group. Recursively does this till there is only one group and 
 * hence only one Median.
 */ 
static prob 
findMedianOfMedians(WSD **array, int left, int right)
{
	int i, shift = 1;
	
    if(left == right)
        return array[left]->prob;
     
    while(shift <= (right - left))
    {
        for(i = left; i <= right; i+=shift*5)
        {
            int endIndex = (i + shift*5 - 1 < right) ? i + shift*5 - 1 : right;
            int medianIndex = findMedianIndex(array, i, endIndex, shift);
 
            swap(array, i, medianIndex);
        }
        shift *= 5;
    }
 
    return array[left]->prob;
}
 
/* findMedianIndex
 * 
 * Find the index of the Median of the elements of array that occur at every 
 * "shift" positions.
 */ 
static int 
findMedianIndex(WSD **array, int left, int right, int shift)
{
    int i, groups = (right - left)/shift + 1, k = left + groups/2*shift;

    for(i = left; i <= k; i+= shift)
    {
        int minIndex = i,  j;
        prob minValue = array[minIndex]->prob;
        
        for(j = i; j <= right; j+=shift)
            if(array[j]->prob > minValue)
            {
                minIndex = j;
                minValue = array[minIndex]->prob;
            }
        swap(array, i, minIndex);
    }
 
    return k;
}
 
/* swap
 * 
 * Swap the positions of two world set descriptors.
 */ 
static void 
swap(WSD **array, int a, int b)
{
    WSD *tmp = array[a];
    array[a] = array[b];
    array[b] = tmp;
}

/* bitset_union_removing_subsumption
 *
 * Union two bitsets and remove the subsumed clauses.
 * CNF A is subsumed by CNF B if A implies B. 
 */
static void 
bitset_union_removing_subsumption(bitset *set1, bitset *set2)
{
	int i, j, k, h;
	
	bitset *temp_set;
	
	if (set2 == NULL)
	{
		return;
	}

  	/* Copy the second bitset to a temporary one because modifications are needed */
  	temp_set = bitset_init(NUM_WSDS);  
        	
	bitset_copy(set2, temp_set);

	/* Loop over all clauses in the first bitset */
	for (i = 0; i < NUM_WSDS; i++)
		/* Pick a valid clause in the first bitset */
		if (bitset_test_bit(set1,i))
		{	
			/* Loop over all clauses in the temporary bitset */
			for (j = 0; j < NUM_WSDS; j++)
			{
				/* Pick a valid clause in the second bitset */
				if (bitset_test_bit(temp_set,j))
				{
					bool vars_found = true;
				
					/* Loop over all variables in the first clause */
					for (k = 0; k < WSD_LEN; k++)
					{
						if (S[i]->data[k]->rng != -1)
						{
							bool var_found = false;
						
							/* Loop over all variables in the second clause */
							for (h = 0; h < WSD_LEN; h++)
							{
								if (S[i]->data[k]->var == S[j]->data[h]->var)
								{
									var_found = true;
									break;
								}
							}
							
							/* If a variable in the second clause is not found
							 * in the first clause, it is sure that the second clause
							 * does not imply the first clause.
							 */
							if (!var_found)
							{
								vars_found = false;
								break;
							}
						}
					}
					
					/* If all variables in the first clause are found in the 
					 * second clause, then clear the bit for the second clause */
					if (vars_found)
					{
						bitset_clear_bit(temp_set,j);
						#ifdef STATISTICS
						subsumption_counter++;
						#endif
					}
				}
			}
		}	

	/* Perform the union operation. */
	for( i = 0; i < BITSET_USED(set1->nbits); i++)
		set1->bits[i] |= temp_set->bits[i];

	/* Free the temporary bitset */
	bitset_free(temp_set);
}

/* decomposition_tree_approximate
 * 
 * This is the major function for approximate confidence computation with 
 * decomposition tree.
 */
static void
decomposition_tree_approximate (bitset* set, generalState *state, 
	float8 path_prob, float8 *lower, float8 *upper, 
	bound_information *bound_info, int latest_var_column)
{
	int pos;
	int var = -1;
	int i;
	int j;
	bitset* subset_without_var = NULL;
	bitset* subset;
	worldTableEntry *wt_entry;
	rngEntry *rng_entry;
	bound_information next_bound_info;

	float8 p_left_lower = 0;
	float8 p_left_upper = 0;
	float8 p_right_lower = 0;
	float8 p_right_upper = 0;	
	float8 whole_upper;
	float8 whole_lower;
	float8 condition_whole_upper;

	/* If the set is empty, return 0s as the bounds. */		
	if (bitset_test_empty(set))
	{
  		*lower = 0;
  		*upper = 0;		
		return;
	}

	/* Find a subset of clauses which do not share variables with the rest of the clauses */	
	subset = find_independent_split(set);
	
	/* Get the bitset of the rest of the clauses so that their upper and lower bounds can be computed. */
	bitset_negate(set, subset);
	
	/* Compute the bounds of the rest of the clauses. */
	compute_upper_and_lower_bounds(subset, &p_right_upper, &p_right_lower, state);
	
	#ifdef STATISTICS
	counter++;
	
	if (p_right_upper != 0)
	{
		ind_counter++;
	}		
	#endif
	
	/* Get back the bitset of the subset. */
	bitset_negate(set, subset);
	
	/* Singleton test */
	pos = bitset_test_singleton(subset);

 	/* Special case of 1 clause: the bounds are the probability of the clause */
  	if (pos != -1)
  	{
  		p_left_lower = S[pos]->prob;
  		
  		p_left_upper = S[pos]->prob;
  	}
  	/* Cases with more than 1 clause */
  	else
  	{
  		int new_var_column;
  		
  		/* Choose a variable with most occurrence from the same column of last eliminated variable */
		wt_entry = choose_var_max_occur_same_column(subset, state, 
			latest_var_column, &new_var_column);
		
		/* If the variable is NULL, return the bounds as 1s. */
		if (wt_entry == NULL)
		{
  			*lower = 1;
  			
  			*upper = 1;
  			
  			bitset_free(subset);
  			
  			return;
   		}
   		
   		/* The variable in the returned world table entry */
		var = wt_entry->var;

		/* All range values of the variable */
		rng_entry = wt_entry->rng_entries;

		/* The bitset of clauses which do not contain the eliminated variables */
		subset_without_var = find_wsds_without_var(subset, var);     		

		/* The bounds for clauses where a range value of the variable is chose */
		float8 upper_bounds[wt_entry->rng_entry_count];
		float8 lower_bounds[wt_entry->rng_entry_count];

		/* The states of clauses after the variable is eliminated */
		int state_of_subset_var_rng[wt_entry->rng_entry_count];

		float8 upper_bound_without_var = -1;
		float8 lower_bound_without_var = -1;  
		
		int rng_count = wt_entry->rng_entry_count;
		
		/* Loop over all range values for the first round to compute the upper 
		 * and lower bounds for each range value.  
		 */
		for (i = 0; i < rng_count; i++)
		{
			/* The probability of the range value */
			float8 cur_prob = (rng_entry + i)->p;
	
			/* The bitset of clauses where the range value of the variable appears */
			bitset* subset_var_rng = find_wsds_with_var_rng(subset, var, (rng_entry + i)->rng);
		
			/* No clauses contain the range value of the variable */
			if (subset_var_rng == NULL) 
			{
				/* Compute the upper and lower bounds if they are not yet computed */
				if (upper_bound_without_var == -1)
				{
					compute_upper_and_lower_bounds(
						subset_without_var, 
						&upper_bound_without_var, 
						&lower_bound_without_var, 
						state);				
				}
			
				/* Set the bounds */
				upper_bounds[i] = cur_prob * upper_bound_without_var;
				lower_bounds[i] = cur_prob * lower_bound_without_var;
		
				/* Set the state of clauses for the range value */
				state_of_subset_var_rng[i] = SUBSET_VAR_RNG_IS_NULL;
			}
			else 
			{	
				/* One clause has exhausted all its variables due to variable elimination */	
				if (bitset_test_empty(subset_var_rng))
				{
					/* The bounds are simply the probability of the range value */
					upper_bounds[i] = cur_prob;
					lower_bounds[i] = cur_prob;

					/* Set the state of clauses for the range value */
					state_of_subset_var_rng[i] = SUBSET_VAR_RNG_IS_EMPTY;
				
					#ifdef STATISTICS
					subsumption_counter += bitset_count_set(set);
					#endif			
				}
				/* Other cases */	
				else 
				{
					/* Get the union of bitset of clauses that contain the range 
					 * values and that do not contain the variable. 
					 */
					bitset_union(subset_var_rng, subset_without_var);

					/* Compute the bounds */
					compute_upper_and_lower_bounds(
						subset_var_rng, 
						&upper_bounds[i], 
						&lower_bounds[i], 
						state);		
			
					/* Take into account the probability of the range value */
					upper_bounds[i] *= cur_prob;
					
					lower_bounds[i] *= cur_prob;
			
					/* Reset the bitset */
					reset_wsds_var_rng(subset_var_rng, var, (rng_entry + i)->rng);
				
					/* Set the state of clauses for the range value */
					state_of_subset_var_rng[i] = SUBSET_VAR_RNG_SHOULD_UNION;
				}
			}
			
			/* Free local bitsets */
			if (subset_var_rng)
				bitset_free(subset_var_rng);
		}

		/* Loop over all range values for the second round to decide whether to 
		 * refine or close a leave. 
		 */
		for (i = 0; i < rng_count; i++)
		{
			/* The sum of upper bounds of all range values */		
			float8 aggregate_upper = 0;				
			
			/* The sum of lower bounds of all range values */
			float8 aggregate_lower = 0;				
			
			/* The sum of upper bounds of all range values used in deciding whether to close a leave.
	 		 * It is not the same as aggregate_upper.
			 */
			float8 condition_aggregate_upper = 0;	
			
			/* The probability of the range value */
			float8 cur_prob;						
			
			/* The sum of upper bounds of all range values except for the current one */
			float8 aggregate_upper_without_itself;	
			
			/* The sum of lower bounds of all range values except for the current one */
			float8 aggregate_lower_without_itself;	
			
			/* Compute the sums of bounds */
			for (j = 0; j < rng_count; j++)
			{
				aggregate_upper += upper_bounds[j];
				aggregate_lower += lower_bounds[j];
			}
		
			/* Compute the sums of upper bounds used in deciding whether to close a leave. */
			for (j = 0; j < rng_count; j++)
			{
				/* Use the upper bounds of iterated range values */
				if (j < i)
					condition_aggregate_upper += upper_bounds[j];
				/* Use the lower bounds of uniterated range values */
				/* Please refer to the page for the reason. */	
				else if (j > i)
					condition_aggregate_upper += lower_bounds[j];
			}		

			/* Compute the upper bound of the whole decomposition tree */
			whole_upper = bound_info->coefficient_upper * (aggregate_upper + 
				p_right_upper - aggregate_upper * p_right_upper) + 
				bound_info->constant_upper;

			/* Compute the lower bound of the whole decomposition tree */
			whole_lower = bound_info->coefficient_lower * (aggregate_lower + 
				p_right_lower - aggregate_lower * p_right_lower) + 
				bound_info->constant_lower;

			/* Compute the upper bound of the whole decomposition tree used in deciding whether to close a leave */
			condition_whole_upper = bound_info->condition_coefficient_upper * 
				(condition_aggregate_upper + p_right_lower - 
				condition_aggregate_upper * p_right_lower) + 
				bound_info->condition_constant_upper;

			/* Relative cases */
			if (is_relative)
			{
				/* Test the stopping condition */
				if ((whole_upper - whole_lower) / whole_lower <= stopping_number)
				{
					has_satisfied_stopping_condition = true;
					break;
				}	
		
				/* Test whether we can close an open leave */
				if ((condition_whole_upper - whole_lower) / whole_lower <= stopping_number)
				{
					/* Decide whether we should close the leave */
					if (((upper_bounds[i] - lower_bounds[i]) * path_prob) / whole_lower <= 0.001 * stopping_number)
					{					
						continue;				
					}
				}
			}
			/* Absolute cases */
			else
			{
				/* Test the stopping condition */
				if ((whole_upper - whole_lower) <= stopping_number)
				{
					has_satisfied_stopping_condition = true;
					break;
				}	
		
				/* Test whether we can close an open leave */
				if ((condition_whole_upper - whole_lower) <= stopping_number )
				{
					/* Decide whether we should close the leave */
					if (((upper_bounds[i] - lower_bounds[i]) * path_prob) <= 0.001 * stopping_number)
					{	
						continue;				
					}
				}		
			}

			/* The code below prepares the necessary information to refine a leave */
			/* TODO: More detailed explanation of coefficients and constants below is needed */

			/* Get the probability of range value */		
			cur_prob = (rng_entry + i)->p;

			aggregate_upper_without_itself = aggregate_upper - upper_bounds[i];

			/* Compute the coefficient to be passed down in calculating the upper bound */
			next_bound_info.coefficient_upper = bound_info->coefficient_upper * 
				(1 - p_right_upper) * cur_prob;
			
			/* Compute the constant to be passed down in calculating the upper bound */
			next_bound_info.constant_upper = bound_info->constant_upper + 
				bound_info->coefficient_upper * 
				(p_right_upper + aggregate_upper_without_itself - 
				p_right_upper * aggregate_upper_without_itself);			
		
			aggregate_lower_without_itself = aggregate_lower - lower_bounds[i];

			/* Compute the coefficient to be passed down in calculating the lower bound */
			next_bound_info.coefficient_lower = bound_info->coefficient_lower 
				* (1 - p_right_lower) * cur_prob;
			
			/* Compute the constant to be passed down in calculating the lower bound */
			next_bound_info.constant_lower = bound_info->constant_lower + 
				bound_info->coefficient_lower * 
				(p_right_lower + aggregate_lower_without_itself - 
				p_right_lower * aggregate_lower_without_itself);	
			
			/* Compute the coefficient to be passed down in calculating the upper bound for deciding whether to close a leave */
			next_bound_info.condition_coefficient_upper = next_bound_info.coefficient_lower;
			
			/* Compute the constant to be passed down in calculating the upper bound for deciding whether to close a leave */
			next_bound_info.condition_constant_upper = 
				bound_info->condition_constant_upper + 
				bound_info->condition_coefficient_upper * 
				(p_right_lower + condition_aggregate_upper - 
				p_right_lower * condition_aggregate_upper);
			
			/* No clauses contain the range value of the variable */
			if (state_of_subset_var_rng[i] == SUBSET_VAR_RNG_IS_NULL) 
			{
				/* Refine the leave */				
				decomposition_tree_approximate(subset_without_var, state, 
					path_prob * cur_prob, &lower_bounds[i], &upper_bounds[i], 
					&next_bound_info, latest_var_column);
				
				/* Update the bounds with probability of the range value */			
				lower_bounds[i] *= cur_prob;
				
				upper_bounds[i]	*= cur_prob;	
			}
			else 
			{
				/* One clause has exhausted all its variables due to variable elimination */	
				if (state_of_subset_var_rng[i] == SUBSET_VAR_RNG_IS_EMPTY)
				{
					/* No actions are needed, because refinement cannot narrow 
					 * the gap between lower and upper bounds. 
					 */
				}
				/* Other cases */	
				else 
				{
					/* Compute the bitset of clauses containing the range value */				
					bitset* subset_var_rng = find_wsds_with_var_rng(subset, var, (rng_entry + i)->rng);

					/* Get the union of bitset of clauses that contain the range 
					 * values and that do not contain the variable. Subsumed clases
					 * are removed at the same time.
					 */						
					bitset_union_removing_subsumption(subset_var_rng, subset_without_var);
				
					/* Refine the leave */
					decomposition_tree_approximate(subset_var_rng, state, 
						path_prob * cur_prob, &lower_bounds[i], &upper_bounds[i], 
						&next_bound_info, latest_var_column);		
			
					/* Reset the bitset */
					reset_wsds_var_rng(subset_var_rng, var, (rng_entry + i)->rng);

					/* Free the local bitset */
					bitset_free(subset_var_rng);
					
					/* Update the bounds with probability of the range value */		
					lower_bounds[i] *= cur_prob;
					
					upper_bounds[i]	*= cur_prob;	
				}
			}
			
			/* If the an epsilon-refinement has been reached, stop the iteration */
			if (has_satisfied_stopping_condition)
				break;
		}  		  		

		/* Sum up the bounds for all range values */
		for (j = 0; j < rng_count; j++)
		{
		 	p_left_upper += upper_bounds[j];
		 	
			p_left_lower += lower_bounds[j];
		}	
  	} 

	/* Compute the upper and lower bounds of the whole decomposition tree */
	whole_upper = bound_info->coefficient_upper * 
		(p_left_upper + p_right_upper - p_left_upper * p_right_upper) 
		+ bound_info->constant_upper;
	
	whole_lower = bound_info->coefficient_lower * 
		(p_left_lower + p_right_lower - p_left_lower * p_right_lower) 
		+ bound_info->constant_lower;

	/* Relative cases */
	if (is_relative)
	{
		/* Test the stopping condition */
		if ((whole_upper - whole_lower) / whole_lower <= stopping_number)
		{
			has_satisfied_stopping_condition = true;
		}	
	}
	/* Absolute cases */
	else
	{
		/* Test the stopping condition */
		if ((whole_upper - whole_lower) <= stopping_number)
		{
			has_satisfied_stopping_condition = true;
		}	
	}

	/* If an epsilon-approximation has not been reached and the right partition 
	 * is not NULL, proceed to the right partition. 
	 */
	if (!has_satisfied_stopping_condition && p_right_lower != 0)
	{
		/* The code below prepares the necessary information to refine a leave */
		/* TODO: More detailed explanation of coefficients and constants below is needed */
	
		/* Compute the coefficient to be passed down in calculating the upper bound */
		next_bound_info.coefficient_upper = bound_info->coefficient_upper * (1 - p_left_upper);
		
		/* Compute the constant to be passed down in calculating the upper bound */	
		next_bound_info.constant_upper = bound_info->constant_upper + bound_info->coefficient_upper * p_left_upper;			

		/* Compute the coefficient to be passed down in calculating the lower bound */
		next_bound_info.coefficient_lower = bound_info->coefficient_lower * (1 - p_left_lower);
		
		/* Compute the constant to be passed down in calculating the lower bound */	
		next_bound_info.constant_lower = bound_info->constant_lower + bound_info->coefficient_lower * p_left_lower;			
	
		/* Compute the coefficient to be passed down in calculating the upper bound for deciding whether to close a leave */
		next_bound_info.condition_coefficient_upper = next_bound_info.coefficient_upper;
		
		/* Compute the constant to be passed down in calculating the upper bound for deciding whether to close a leave */	
		next_bound_info.condition_constant_upper = next_bound_info.constant_upper;
		
		/* Compute the bitset for the right partition */
		bitset_negate(set, subset);
		
		/* Refine the right partition */
		decomposition_tree_approximate(subset, state, path_prob, &p_right_lower, 
			&p_right_upper, &next_bound_info, latest_var_column);
	}	
	
	/* Compute the upper and lower bounds of the node */
	*lower = p_left_lower + p_right_lower - p_left_lower * p_right_lower;
	
	*upper = p_left_upper + p_right_upper - p_left_upper * p_right_upper;

	/* Free local bitsets */
	if (subset_without_var)
		bitset_free(subset_without_var);	

	bitset_free(subset);
}


/* decomposition_tree_exact
 * 
 * This is the major function for exact confidence computation with 
 * decomposition tree. This is function is called only when epsilon is 0.
 */
static prob
decomposition_tree_exact(bitset* set, generalState *state, int latest_var_column)
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
	bitset* subset_without_var = NULL;
	bitset* subset_var_rng;

	/* Return 0 if the set if empty */
  	if (bitset_test_empty(set))
  	{
    	return 0.0;
	}

  	/* Find subset of S independent of the rest (subset repr. dependent wsds) */
  	subset = find_independent_split(set);
 
	#ifdef STATISTICS
	counter++;
	
	bitset_negate(set,subset);
	
	if (subset != NULL && !bitset_test_empty(subset))
	{
		ind_counter++;
	}
	
	bitset_negate(set,subset);		
	#endif
 
    /* Process the left subset containing dependent wsds */       
  
  	pos = bitset_test_singleton(subset);  

	/* Special case of 1 wsd */
	if (pos != -1) 
    	p_left = S[pos]->prob;
    /* Subset contains more than one wsd */
 	else 
 	{				
		int new_var_column;

		wt_entry = choose_var_max_occur_same_column(subset, state, latest_var_column, &new_var_column);

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
		  			p_without_var = decomposition_tree_exact (subset_without_var, state, new_var_column);
				}
				
				cur_prob *= p_without_var;
		  	}
		  	else 
		  	{
				if (bitset_test_empty(subset_var_rng))
				{
					#ifdef STATISTICS
					subsumption_counter += bitset_count_set(set);
					#endif				
				}
				else 
				{
					bitset_union_removing_subsumption(subset_var_rng, subset_without_var);
					cur_prob *= decomposition_tree_exact (subset_var_rng, state, new_var_column);
					reset_wsds_var_rng(subset_var_rng, var, ( rng_entry + i )->rng );
				}
		  	}

			p_left += cur_prob;

			/* Stop early */
		  	if ((p_left == 1.0) && (wt_entry->rng_entry_count == 1)) 
		  	{
				return 1.0;
		  	}
		  	
		  	if (subset_var_rng != NULL)
    			bitset_free(subset_var_rng);
    	}
  	}

	/* Stop early */
  	if (p_left == 1.0) 
  	{
    	return 1.0;
  	}

	if (subset_without_var != NULL)
		bitset_free(subset_without_var);

  	/* Process recursively the right subset */

  	bitset_negate(set,subset);
  	p_right = decomposition_tree_exact(subset, state, latest_var_column);	

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
conf_appro_accum1_ge(PG_FUNCTION_ARGS)
{
	conf_appro_accum( 1 )
}

/* conf_accum2_ge
 *
 * Transition function for exact confidence computation with 2 triples of 
 * condition columns 
 */
Datum 
conf_appro_accum2_ge(PG_FUNCTION_ARGS)
{
	conf_appro_accum( 2 )
}

/* conf_accum3_ge
 *
 * Transition function for exact confidence computation with 3 triples of 
 * condition columns 
 */
Datum 
conf_appro_accum3_ge(PG_FUNCTION_ARGS)
{
	conf_appro_accum( 3  )
}

/* conf_accum4_ge
 *
 * Transition function for exact confidence computation with 4 triples of 
 * condition columns 
 */
Datum 
conf_appro_accum4_ge(PG_FUNCTION_ARGS)
{
	conf_appro_accum( 4 )
}

/* conf_accum5_ge
 *
 * Transition function for exact confidence computation with 5 triples of 
 * condition columns 
 */
Datum 
conf_appro_accum5_ge(PG_FUNCTION_ARGS)
{
	conf_appro_accum( 5 )
}

/* conf_accum6_ge
 *
 * Transition function for exact confidence computation with 6 triples of 
 * condition columns 
 */
Datum 
conf_appro_accum6_ge(PG_FUNCTION_ARGS)
{
	conf_appro_accum( 6 )
}

/* conf_accum7_ge
 *
 * Transition function for exact confidence computation with 7 triples of 
 * condition columns 
 */
Datum 
conf_appro_accum7_ge(PG_FUNCTION_ARGS)
{
	conf_appro_accum( 7 )
}

/* conf_accum8_ge
 *
 * Transition function for exact confidence computation with 8 triples of 
 * condition columns 
 */
Datum 
conf_appro_accum8_ge(PG_FUNCTION_ARGS)
{
	conf_appro_accum( 8 )
}

/* conf_accum9_ge
 *
 * Transition function for exact confidence computation with 9 triples of 
 * condition columns 
 */
Datum 
conf_appro_accum9_ge(PG_FUNCTION_ARGS)
{
	conf_appro_accum( 9 )
}

/* conf_accum10_ge
 *
 * Transition function for exact confidence computation with 10 triples of 
 * condition columns 
 */
Datum 
conf_appro_accum10_ge(PG_FUNCTION_ARGS)
{
	conf_appro_accum( 10 )
}

/* conf_final_ge
 *
 * The final function for confidence computation of decomposition tree.
 */
Datum 
conf_appro_final_ge(PG_FUNCTION_ARGS)
{
	generalState *state = ( ( AggState *) fcinfo->context )->genstate;
	prob result = 0;
	bitset* set;
	MemoryContext oldcxt;
	bound_information bound_info;
	
	float8 lower = 0;
	float8 upper = 0;
	
	#ifdef STATISTICS

	int no_of_posssible_worlds = 1;
	int i;
	FILE * fp = fopen( "report.txt", "a" );
	
	#endif
	
	/* Return 0 if there is no tuple */
	if (groupcxt == NULL)
		PG_RETURN_FLOAT4(0);	

	/* Check the validity of the input */	
	if (strncmp(appro_approach, "R", 1) == 0)
		is_relative = true;
	else if (strncmp(appro_approach, "A", 1) == 0)
		is_relative = false;
	else
	{
		/* Delete the memory context for the group of duplicates */
		MemoryContextDelete( groupcxt );
	
		/* Set the world-set-descriptor-related global variables to NULL */
		NUM_WSDS = 0;
		S = NULL;
		groupcxt = NULL;  		
	
		elog(ERROR, "The approximation approach can only be 'R' (relative approximation) or 'A' (absolute approximation).");
	}
	
	/* Switch to the group context */
	oldcxt = MemoryContextSwitchTo( groupcxt ); 

	/* Complete the local world table */
	getMissingRngs( state ); 

	/* Compute the entry pointers of all clauses */
	computeEntryPointers(state);
  	
  	/* bitset related operation */
  	set = bitset_init(NUM_WSDS);  

  	bitset_set(set);            

	/* If epsilon is larger than 0, call the approximate approach */
	if (appro_epsilon > 0)
	{
		/* Set the stopping number used to decide whether an epsilon approximation is reached */
		if (is_relative)
	 		stopping_number = 2 * appro_epsilon / (1 - appro_epsilon);
		else
			stopping_number = 2 * appro_epsilon;

		has_satisfied_stopping_condition = false;

		/* Prepare the coefficients and constants for efficient upper and lower bound computation */	
		bound_info.coefficient_upper = 1;
		bound_info.constant_upper = 0;
	
		bound_info.coefficient_lower = 1;
		bound_info.constant_lower = 0;
	
		bound_info.condition_coefficient_upper = 1;
		bound_info.condition_constant_upper = 0;
    
    	/* Statistics initialization */
    	ind_counter = 0;
    	counter = 0;
    	subsumption_counter = 0;
    
    	/* Quicksort the clauses according to their probabilities */
    	quicksort(S, 0, NUM_WSDS - 1);
	
		/* Compute the upper and lower bounds before any node is constructed */
		compute_upper_and_lower_bounds(set, &upper, &lower, state);

		/* Relative case */
		if (is_relative)
		{
			/* Test the stopping condition */
			if ((upper - lower) / lower <= stopping_number)
				has_satisfied_stopping_condition = true;
		}
		/* Absolute case */
		else
		{
			/* Test the stopping condition */
			if ((upper - lower) <= stopping_number)
				has_satisfied_stopping_condition = true;
		}

		/* If an epsilon approximation is not reached, construct the decomposition tree */
		if (!has_satisfied_stopping_condition)
		{
			lower = 0;
			upper = 0;
			
			decomposition_tree_approximate(set, state, 1, &lower, &upper, &bound_info, -1);
		}
		
		/* Relative case */
		if (is_relative)
		{
			result = (upper * (1- appro_epsilon) + lower * (1 + appro_epsilon)) / 2;
		}
		/* Absolute case */
		else
		{
			result = (upper + lower) / 2; 
		}		
	}
	/* If epsilon is 0, call the exact confidence computation */
	else
	{
		/* As above, test whether a 0-approximation has been reached before the tree construction */
		compute_upper_and_lower_bounds(set, &upper, &lower, state);
		
		/* Call the exact confidence computation */
		if (upper - lower > 0)
		{
			result = decomposition_tree_exact(set, state, -1);
		}
		/* Stop early */
		else
			result = upper;	
	}
	
	#ifdef STATISTICS
	
	for (i = 0; i < state->wt_entry_count; i++)
	{
		no_of_posssible_worlds *= (state->wt_entries + i)->rng_entry_count;
	}	
	
	fprintf(fp, "---------stats for 1 set of duplicates:\n");
	
	fprintf(fp, "#clauses: %d\n", NUM_WSDS);
	
	fprintf(fp, "#variables in a clause:%d\n", WSD_LEN);
	
	fprintf(fp, "#variables:%d\n", state->wt_entry_count);
	
	fprintf(fp, "#nodes:%d\n", counter);	
	
	fprintf(fp, "#possible worlds:%d\n", no_of_posssible_worlds);
	
	fprintf(fp, "#independent partitions:%d\n", ind_counter);			
	
	fprintf(fp, "#subsumed claused:%d\n", subsumption_counter);		
	
	fprintf(fp, "Upper bound:%f\n", upper);
	
	fprintf(fp, "Lower bound:%f\n", lower);

	fclose(fp);

	#endif	
	
	/* Switch back to the old context */
	MemoryContextSwitchTo( oldcxt );
	MemoryContextDelete( groupcxt );
	
	/* Set the world-set-descriptor-related global variables to NULL */
	NUM_WSDS = 0;
	S = NULL;
	groupcxt = NULL;      

	/* Return the result */
	PG_RETURN_FLOAT4(result);	
}
