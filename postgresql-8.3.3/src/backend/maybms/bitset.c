/*-------------------------------------------------------------------------
 *
 * bitset.c
 *	  	Auxiliary functions for ws-tree algorithm.
 *
 *
 * Copyright (c) 2008, MayBMS Development Group
 *
 *-------------------------------------------------------------------------
 */
 
 
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "maybms/bitset.h"
#include "maybms/utils.h"

/* Local macros */
#define SEGFAULT() abort()

#define BITSET_BITS \
	( CHAR_BIT * sizeof(size_t) )

#define BITSET_MASK(pos) \
	( ((size_t)1) << ((pos) % BITSET_BITS) )

#define BITSET_WORD(set, pos) \
	( (set)->bits[(pos) / BITSET_BITS] )

#define BITSET_USED(nbits) \
	( ((nbits) + (BITSET_BITS - 1)) / BITSET_BITS )

/* bitset_init
 *
 * Initialization of a bitset 
 */
bitset *
bitset_init(size_t nbits) 
{
  bitset *set;
  set = (bitset*) palloc(sizeof(bitset));
  assert(set);

  set->bits = (size_t*) palloc(BITSET_USED(nbits)*sizeof(size_t));
  set->nbits = nbits;
  
  assert(set->bits);
  
  return set;
}

/* bitset_reset
 *
 * Reset a bitset 
 */
void 
bitset_reset(bitset *set) 
{
  memset(set->bits, (size_t)0, BITSET_USED(set->nbits) * sizeof(*set->bits));
}

/* bitset_set
 *
 * Set a bitset 
 */
void 
bitset_set(bitset *set) 
{
	size_t i;
  for ( i = 0; i < set->nbits; i++)
    bitset_set_bit(set,i);
}

/* bitset_count_set
 *
 * Count the set bit in a bitset 
 */
size_t 
bitset_count_set(bitset* set) 
{
  int count = 0;
  size_t i;
  for ( i = 0; i < set->nbits; i++)
    if (bitset_test_bit(set,i))
      count++;

  return count;
}

/* bitset_copy
 *
 * Copy a bitset to another 
 */
void 
bitset_copy(bitset *from, bitset *to) 
{
  /* assumes both from and to have the same bitset size */
  memcpy(to->bits, from->bits, BITSET_USED(to->nbits) * sizeof(*to->bits));
}

/* bitset_free
 *
 * Free a bitset 
 */
void 
bitset_free(bitset *set) 
{
  if (set) 
  {
    pfree(set->bits);
    pfree(set);
  }
}

/* bitset_clear_bit
 *
 * Clear a bit in a bitset
 */
void 
bitset_clear_bit(bitset *set, size_t pos) 
{
	if (pos >= set->nbits) 
	{
	    SEGFAULT();
	}

	BITSET_WORD(set, pos) &= ~BITSET_MASK(pos);
}

/* bitset_set_bit
 *
 * Set a bit in a bitset
 */
void 
bitset_set_bit(bitset *set, size_t pos) 
{
	if (pos >= set->nbits) 
	{
	    SEGFAULT();
	}

	BITSET_WORD(set, pos) |= BITSET_MASK(pos);
}

/* bitset_test_bit
 *
 * Test whether a bit is set in a bitset
 */
int 
bitset_test_bit(bitset *set, size_t pos) 
{
	if (pos >= set->nbits) 
	{
	    SEGFAULT();
	}

	return (BITSET_WORD(set, pos) & BITSET_MASK(pos)) != 0;
}

/* bitset_print
 *
 * Print a bitset to a file
 */
void 
bitset_print(bitset *set) 
{
	int i;

  if (!set)
    return;

 myLog("print bit set:\n");

  if (set)
    for ( i = 0; i < set->nbits; i++) 
      if (bitset_test_bit(set, i)){
		myLogi(i); myLog(",");
	}

	nl(1);nl(1);
}

/* bitset_union
 *
 * Union two sets
 */
void 
bitset_union(bitset *set1, bitset *set2)
{
	size_t i;
  if (set2 == NULL)
    return;

  for( i = 0; i < BITSET_USED(set1->nbits); i++)
    set1->bits[i] |= set2->bits[i];

}

/* bitset_complement
 *
 * Complement one set by another
 */
bitset *
bitset_complement(bitset *ref_set, bitset *to_complement)
{
	size_t i;
	bitset *res;
	
  if (!ref_set || !to_complement)
    return NULL;

  res = bitset_init(ref_set->nbits);
  bitset_reset(res);

  for ( i = 0; i < ref_set->nbits; i++)
    if (bitset_test_bit(ref_set,i) && !bitset_test_bit(to_complement,i))
      bitset_set_bit(res,i);

  return res;
}

/* bitset_subtract
 *
 * Assumes both sets have the same size
 */
void bitset_subtract(bitset *from_set, bitset *what)
{
	size_t i;
  for ( i = 0; i < from_set->nbits; i++)
    if (bitset_test_bit(what,i))
      bitset_clear_bit(from_set,i);
}

/* bitset_negate
 *
 * Negate a bitset
 */
void 
bitset_negate(bitset *ref_set, bitset *to_negate)
{
	size_t i;

  	for ( i = 0; i < ref_set->nbits; i++)
  	{
    	if (bitset_test_bit(ref_set,i))
    	{
      		if (bitset_test_bit(to_negate,i))
				bitset_clear_bit(to_negate,i);
      		else
				bitset_set_bit(to_negate,i);
		}
	}
}

/* bitset_first_bit_set
 *
 * return -1 if set is empty, or the pos of the first set bit otherwise
 */
int 
bitset_first_bit_set(bitset *set)
{
	int i = 0;
	
  if (!set) return -1;
  
  while (i < set->nbits && !bitset_test_bit(set, i))
    i++;
  if (i < set->nbits)
    return i;
    
  return -1;
}

/* bitset_test_singleton
 *
 * return -1 if set is not singleton, otherwise the pos of the set bit
 */
int 
bitset_test_singleton(bitset *set)
{
  int pos = -1;
  int i = 0;
  int how_many = 0;
  
  if (!set) return -1;

  while (i < set->nbits && how_many < 2)
    {
      if(bitset_test_bit(set, i)) {
	how_many++;
	pos = i;
      }
    i++;
    }
  if (how_many == 1)
    return pos;
  return -1;
}

/* bitset_test_empty
 *
 * Test whether a bitset is empty 
 */
int 
bitset_test_empty(bitset *set)
{
	size_t i = 0;
	
  if (!set) return 1;

  
  while (i < BITSET_USED(set->nbits) && set->bits[i] == (size_t)0)
    i++;

  return (i == BITSET_USED(set->nbits));
}


