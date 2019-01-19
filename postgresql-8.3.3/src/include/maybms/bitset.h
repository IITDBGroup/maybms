/*-------------------------------------------------------------------------
 *
 * bitset.h
 *	  	Function declarations for bitset.c. 
 *
 *
 * Copyright (c) 2008, MayBMS Development Group
 *
 *-------------------------------------------------------------------------
 */

#ifndef _BITSET_H_
#define _BITSET_H_

#include <stddef.h>


typedef struct {
	size_t *bits;
	size_t nbits;
} bitset;

bitset *bitset_init(size_t nbits);
void bitset_union(bitset *set1, bitset *set2);
void bitset_subtract(bitset *from_set, bitset *what);
void bitset_negate(bitset *ref_set, bitset *to_negate);
bitset *bitset_complement(bitset *ref_set, bitset *to_complement);
size_t bitset_count_set(bitset* set);
void bitset_reset(bitset *set);
void bitset_set(bitset *set);
void bitset_copy(bitset *from, bitset *to);
void bitset_free(bitset *set);
void bitset_print(bitset *set);

void bitset_clear_bit(bitset *set, size_t pos);
void bitset_set_bit(bitset *set, size_t pos);
int bitset_test_bit(bitset *set, size_t pos);
int bitset_test_singleton(bitset *set);
int bitset_first_bit_set(bitset *set);
int bitset_test_empty(bitset *set);

#endif
