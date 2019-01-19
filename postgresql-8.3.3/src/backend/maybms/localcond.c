/*-------------------------------------------------------------------------
 *
 * localcond.c
 *	  	Storing and accessing the lineage for world-set tree algorithm and 
 * Monte-Carlo simulations. Right now, these two confidence computation 
 * procedures main-memory-based and random acccess to lineage is required. 
 *
 * Copyright (c) 2008, MayBMS Development Group
 *
 *-------------------------------------------------------------------------
 */

#include "maybms/localcond.h"

/* Initial number of bits used for maksing */
#define INIT_NBITS 10

/* Initial size of the world table */
#define WTINITSIZE 1000

/* Initial size of the array storing domain values in each variable */
#define RNGENTRYSIZE 3

/* Initial size of a bucket storing pointers to the world table entries */
#define LIST_INIT_SIZE 30
#define KEY_NOT_FOUND -1

#define BUCKETISFULL(list) (list->count == list->size)
#define GETMASK(nbits) ((1 << nbits) - 1)
#define HASHFUNC(key, mask) (key & mask)

/* create_map
 *
 * Create a map with a variable, a range and a probability.
 */
Map * 
create_map (int var, int rng, int neg, prob prob)
{
  	Map* m      = (Map*) palloc (sizeof(Map));
  	m->var      = var;
  	m->rng      = rng;
  	m->neg      = neg;
  	m->prob     = prob;
  
  	return m;
}

/* advance
 *
 * Add a world set descriptor.
 */
void 
advance(int n, generalState *state, WSD* wsd)
{
	int i, j;

	/* Initialize S if it is NULL */
	if( S == NULL )
	{
		genStateInit( state ); 
	}

	/* Expand the size of S if it reaches its limit */
	if(NUM_WSDS >= current_size - 1)
	{
		current_size = current_size * 2;
		S = ( WSD ** ) repalloc( S, current_size * sizeof( WSD * ) );
	}

	/* Insert the world set descriptor */
	S[ NUM_WSDS ] = wsd;
	
	/* Loop every map in the world set descriptor */
	for (i = 0; i < n - 1; i++)
	{
		for( j = i + 1; j < n; j++ )
		{
			/* If two map share the same variable and same range, set one of them 
			 * to the reserved variable and range whose probability is 1. 
			 * This is necessary because the following calculation assumes that 
			 * there is no duplicate map.
			 */
			if( S[ NUM_WSDS ]->data[ i ]->var == S[ NUM_WSDS ]->data[ j ]->var )
			{
				S[ NUM_WSDS ]->data[ j ]->var = RESERVED_VAR;
				S[ NUM_WSDS ]->data[ j ]->rng = RNG_FOR_RESERVED_VAR;
				S[ NUM_WSDS ]->data[ j ]->prob = 1;
			}
		} 
	}

	/* Loop the maps */
	for( j = 0; j < n; j++ )
	{
		/* Calculate the probability of the world set descriptor */
		S[ NUM_WSDS ]->prob *= S[ NUM_WSDS ]->data[ j ]->prob;
		
		/* Update the local world table */
		updateWorldTable( state, S[ NUM_WSDS ]->data[ j ] ); 
	}

	/* Increase the counter */
	NUM_WSDS++;
}

/* getMissingRngs
 *
 * Calculate the probability for remaining attributes
 * For example, we have    (v,r,p)=(1,1,0.1),(1,2,0.2)
 * we should get           (v,r,p)=(1,1,0.1),(1,2,0.2),(1,3,0.7)
 */
void 
getMissingRngs(generalState *s)
{
	int i, j;
	worldTableEntry *wt_entry;
	rngEntry *rng_entry;
	prob sum = 0;
	rngType max = -1;

	/* Loop the local world table */
	for( i = 0; i < s-> wt_entry_count; i++ )
	{
		wt_entry = s->wt_entries + i;
		
		/* Set the first range value as the max */
		max = wt_entry->rng_entries->rng;
		
		sum = 0;

		/* Loop the range values */
		for( j = 0; j < wt_entry->rng_entry_count; j++ )
		{
			rng_entry = wt_entry->rng_entries + j;
			
			/* Sum up the probabilities */
			sum += rng_entry->p;
			
			/* Set the max */
			if( rng_entry->rng > max )
				max = rng_entry->rng;
		}

		/* Due to the precision of float4, the sum may be very close to 1 when 
		 * it's really 1. Therefore, we use an approximation here.
		 */
		if( sum - 1.0 > 0.01 )
		{
			elog( WARNING, "Sum: %f", sum );
			elog( ERROR, "The sum of probabilities for variable %d exceeds 1!", wt_entry->var );
		}
		/* When the sum is smaller than 1, complete the local world table */
		else if ( sum < 1.0 )
		{ 
			rngEntryInit( max + 1, 1 - sum, wt_entry );
		}
	}
}

/* computeEntryPointers
 *
 * Compute the entry ponters in the clauses with the offsets.
 */
void
computeEntryPointers(generalState *s)
{
	Map *map;	
	int i, k;
	
	/* Loop over all clauses */
    for(i = 0; i < NUM_WSDS; i++)
    {
    	/* Loop over all variables */
      	for(k = 0; k < WSD_LEN; k++)
      	{
      		map = S[i]->data[k];
      	
      		map->wt_entry = s->wt_entries + map->wt_offset;
      		
      		map->rng_entry = map->wt_entry->rng_entries + map->rng_offset;
        }
    }
}


/* updateWorldTable
 *
 * Insert a map to the local world table.
 */
void 
updateWorldTable(generalState *s, Map *map)
{
	int index = HASHFUNC(map->var, s->mask);
	worldTableEntry *wt_entry;
	rngEntry *rng_entry;
	int i, wt_entry_index;

	/* Step 1: Find the entry for the variable in the local world table */

	/* If the entry of the world table is empty, create a new one. */
	if (s->HT[index] == NULL)
	{
		/* Create a new entry for the variable */        
		wt_entry_index = wtEntryInit( map->var, s );
		
		/* Create a list and add the entry */
		create_bucket(s, index);
		
		mlappend(s->HT[index], map->var, wt_entry_index);
	}
	/* If there exists a list for the hash table entry, find a match in the list 
	 * or create an entry in the list.
	 */
	else
	{     
		wt_entry_index = search(s->HT[index], map->var);

		/* If a match is not found, create an entry for the variable */
		if (wt_entry_index == KEY_NOT_FOUND)
		{ 
			wt_entry_index = wtEntryInit(map->var, s);
			
			/* NOTES: We can not use index instead of HASHFUNC(map->var, s->mask) 
			 * because index is static and may be invalid after the table is expanded
			 */
			while (BUCKETISFULL(s->HT[HASHFUNC(map->var, s->mask)]))
			{	
				if (s->HT[HASHFUNC(map->var, s->mask)]->nbits == s->nbits)
					expand_hashtable(s);
				
				distribute_elements(s, map->var, HASHFUNC(map->var, s->mask));
			}
			
			mlappend(s->HT[HASHFUNC(map->var, s->mask)], map->var, wt_entry_index);
		}
	}
	
	/* Get the world table entry */
	wt_entry = s->wt_entries + wt_entry_index;  	

	/* Set the world table entry in the clause */
	map->wt_offset = wt_entry_index;

	/* Step 2: Find the entry for the range value in the local world table entry */
	for(i = 0; i < wt_entry->rng_entry_count; i++)
	{
		rng_entry = wt_entry->rng_entries + i;
		
		if (rng_entry->rng == map->rng)
		{
			/* Set the index of the range entry */
			map->rng_offset = i;
			return;
		}
	}
	
	/* Set the index of the range entry after initializing a new one */
	map->rng_offset = rngEntryInit(map->rng, map->prob, wt_entry);
}

/* rngEntryInit
 *
 * Initialize a range entry with a specified range and probability in a world 
 * table entry.
 */
int 
rngEntryInit( rngType rng, prob p, worldTableEntry *wt_entry )
{
	/* Get the next empty entry and assign the range and probability */
	rngEntry *rng_entry = wt_entry->rng_entries + wt_entry->rng_entry_count; 
	rng_entry->p = p;
	rng_entry->rng = rng;
	
	/* Increase the counter for world table entry */
	wt_entry->rng_entry_count++;

	/* If the limit of the range entries is reached, increase the capacity by 2 */
	if(wt_entry->rng_entry_count == wt_entry->rng_entry_max)
	{
		wt_entry->rng_entry_max = wt_entry->rng_entry_max * 2;
		wt_entry->rng_entries = 
			( rngEntry * ) repalloc( wt_entry->rng_entries , wt_entry->rng_entry_max * sizeof( rngEntry ) );		
	}

	/* Return the index of the entry */
	return ( wt_entry->rng_entry_count - 1 );
}

/* wtEntryInit
 *
 * Initialize entry in the local world table with a specified variable
 */
int 
wtEntryInit(varType v, generalState *s)
{
	/* Get the next empty entry*/
	worldTableEntry *wt_entry = ( s->wt_entries ) + ( s->wt_entry_count );
	
	/* Assign the variable */
	wt_entry->var = v;
	
	/* Initialize other fields */
	wt_entry->rng_entries = ( rngEntry * ) palloc0( RNGENTRYSIZE * sizeof( rngEntry ) );
	wt_entry->rng_entry_count = 0;
	wt_entry->rng_entry_max = RNGENTRYSIZE;
	
	/* Increase the counter in the local world table */
	s->wt_entry_count++;

	/* If the limit of the variable entries is reached, increase the capacity by 2 */
	if( s->wt_entry_count == s->wt_entry_max )
	{
		s->wt_entry_max = s->wt_entry_max * 2;
		s->wt_entries = 
			(worldTableEntry *) repalloc(s->wt_entries, s->wt_entry_max * sizeof(worldTableEntry));
	}

	/* Return the index of the entry */
	return ( s->wt_entry_count - 1 );
}

/* genStateInit
 *
 * Initialize the state structure.
 */
void 
genStateInit(generalState *state)
{
	NUM_WSDS = 0;
	current_size = 100;
	S = ( WSD ** ) palloc0(current_size * sizeof(WSD *));

	state->nbits = INIT_NBITS;
	state->mask = GETMASK(INIT_NBITS);
	state->HT = ( mList ** ) palloc0((state->mask + 1) * sizeof(mList *));
		
	state->wt_entries = ( worldTableEntry * ) palloc0( WTINITSIZE * sizeof( worldTableEntry ) );
	state->wt_entry_max = WTINITSIZE;
	state->wt_entry_count = 0;
}

/* resetCount
 *
 * Reset the counter of every range value in a local world table.
 */
void 
resetCount(generalState *state)
{
	worldTableEntry *wt_entry;
	int i, j;

	/* Loop the local world table */
	for ( i = 0; i < state->wt_entry_count; i++)
	{
		wt_entry = state->wt_entries + i;
		
		/* Loop the range values of a variable */
		for (j = 0; j < wt_entry->rng_entry_count; j++)
		{
			( wt_entry->rng_entries + j )->count = 0;
		}
	}
}

/* printState
 *
 * Print the world table.
 */
void 
printState( generalState *state )
{
	worldTableEntry *wt_entry;
	int i, j;

	myLog("World Table:\n");

	for ( i = 0; i < state->wt_entry_count; i++)
	{
		wt_entry = state->wt_entries + i;
		
		for ( j = 0; j < wt_entry->rng_entry_count; j++)
		{
			myLogi( wt_entry->var ); myLogi( ( wt_entry->rng_entries + j )->rng );
			myLogf( ( wt_entry->rng_entries + j )->p ); nl(1);
		}
	}

	nl(1);
}

/* printWSD
 *
 * Print all world set descriptors with a bitset.
 */
void 
printWSD(bitset* set)
{
	int i, j;
	myLog("Print WSDs:\n");

	if( set == NULL ){
		myLog("set is null!\n"); 
		return;
	}

	for ( i = 0; i < NUM_WSDS; i++)
	{	
		if (bitset_test_bit(set, i)) 
		{
			for ( j = 0; j < WSD_LEN; j++)
			{
				myLogi( S[i]->data[j]->var ); 
				myLogi( S[i]->data[j]->rng );
				myLogf( S[i]->data[j]->prob ); 
				myLog("\t");
			}
			myLog("prob");  myLogf( S[i]->prob ); nl(1);
		}
	}
	
	nl(1);
}

/* printWSD2
 *
 * Print all world set descriptors.
 */

void 
printWSD2(int a)
{
	int i, j;
	myLog("Print WSDs:\n");

	for ( i = 0; i < NUM_WSDS; i++)
	{	
		for ( j = 0; j < WSD_LEN; j++)
		{
			myLogi( S[i]->data[j]->var ); 
			myLogi( S[i]->data[j]->rng );
			myLogf( S[i]->data[j]->prob ); 
			myLog("\t");
		}
		
		myLog("prob");  myLogf( S[i]->prob ); nl(1);
	}
	
	nl(1);
}


/* printBucket
 *
 * Print all the buckets in the local world table.
 */
void 
printBucket(generalState *state)
{
	int i = 0, j;	
	worldTableEntry *wt_entry;

	elog(WARNING, "Print buckets");
	
	for (i = 0; i < GETMASK(state->nbits) + 1; i++)
	{
		if( state->HT[i] != NULL )
		{
			myLog( "entries: " );
			
			for( j=0; j < state->HT[i]->count; j++ )
			{
				wt_entry = state->wt_entries + state->HT[i]->indices[j];
				myLogi( wt_entry->var );
			}
		}
		else
			myLog( "empty bucket" );

		nl(1);
	}
}

/* mlist_make
 *
 * Make a bucket with n-bit mask.
 */
mList* 
mlist_make(int nbits)
{
	mList *result = (mList *) palloc0(sizeof(mList));
	
	result->size = LIST_INIT_SIZE;
	
	result->nbits = nbits;
	
	result->mask = GETMASK(nbits);

	result->keys = (varType *) palloc0(result->size * sizeof(varType));
	
	result->indices = (int *) palloc0(result->size * sizeof(int));
	
	result->count = 0;

	return result;	
}

/* mlappend
 *
 * Append an integer to a list.
 */
void
mlappend(mList *list, varType key, int index)
{
	list->keys[ list->count ] = key;
	list->indices[ list->count ] = index;
	list->count++;
}

/* search
 *
 * Search a key value
 */
int 
search(mList *list, varType key)
{
	int i;
	
	for(i = 0; i < list->count; i++)
	{
		if (list->keys[i] == key)
			return list->indices[i];
	}
	
	return KEY_NOT_FOUND;
}

/* expand_hashtable
 *
 * Expand the hash table.
 */
void
expand_hashtable(generalState *s)
{
	int size;

	/* Increase the number of bits for mask */
	s->nbits = s->nbits + 1;
	
	/* Calculate the mask */
	s->mask = GETMASK(s->nbits);
	
	/* Size of expanded hash table */
	size = s->mask + 1;
	
	/* Expand the hash table */
	s->HT = (mList **) repalloc(s->HT, sizeof(mList *) * size);
	
	/* Copy the old entries to the new entries */
	memcpy(s->HT + (size / 2), s->HT, sizeof(mList *) * size / 2); 
}

/* distribute_elements
 *
 * Redistribute the elements to two buckets if a bucket is too full.
 */
void
distribute_elements(generalState *s, varType key, int index)
{
	/* The full list */
	mList *old_list = s->HT[index];
	
	/* The two new lists */
	mList *list1 = mlist_make(old_list->nbits + 1);
	mList *list2 = mlist_make(old_list->nbits + 1);
	
	int i;
	/* The index of the first list */
	int low_index = HASHFUNC(key, old_list->mask);
	
	/* The index of the second list */
	int high_index = low_index + (1 << old_list->nbits);
	
	/* Update the buckets associated with the old list */
	update_buckets(s, low_index, list1);
	update_buckets(s, high_index, list2);
	
	/* Loop over the elements in the old list and redistribute them according 
	 * to the new mask.
	 */
	for (i = 0; i < old_list->size; i++)
		mlappend(s->HT[HASHFUNC(old_list->keys[i], list1->mask)], 
				old_list->keys[i], 
				old_list->indices[i]);		
				
	pfree(old_list);
}

/* create_bucket
 *
 * Create a list.
 */
mList *
create_bucket(generalState *s, int index)
{
	/* Create a list with INIT_NBITS-bit mask */
	mList *result = mlist_make(INIT_NBITS);
	
	update_buckets(s, index, result);
	
	return result;	
}

/* update_buckets
 *
 * Update the buckets with a new list.
 *
 * If the current number of bits of the hash table is bigger than the initial
 * one, we must associate this list to all other relevant buckets.
 *
 * For example, INIT_NBITS is 1 and the current nbits of the hash table is 2.
 * Then the new list for bucket[0] should also be associated with bucket[2]. 
 */
void
update_buckets(generalState *s, int index, mList *list)
{
	/* The difference of nbits between the hash table and the list  */
	int dif = s->nbits - list->nbits;
	
	/* The distance between buckets to be updated   */
	int block_size = GETMASK(list->nbits) + 1;
	
	/* The number of such buckets  */
	int block_count = GETMASK(dif) + 1;
	int i;
	
	/* Update the buckets associated with this list  */
	for (i = 0; i < block_count; i++)
		s->HT[index + i * block_size] = list;			
}

