/*-------------------------------------------------------------------------
 *
 * aconf.c
 *        Implementation of approximate confidence computation with relative
 *	  approximation guarantees based on Monte Carlo Simulation.
 *
 *
 * INTERFACE ROUTINES:
 *
 *	  aconf_final()
 *	  aconf_accum<i>()
 *
 *
 * GLOBAL INTERFACE VARIABLES:
 *
 *        double epsilon
 *        double delta 
 *
 *
 * EXTERNAL GLOBAL VARIABLES USED:
 *
 *        WSD_LEN, NUM_WSDS, fcinfo, S[] (TODO: complete this list)
 *
 *
 * NOTES:
 *
 *	  To compute the confidence in a tuple of data values occurring
 *	  possibly in several tuples of a U-relation, we have to compute the
 *	  probability of the disjunction of the local conditions of all these
 *	  tuples. We have to eliminate duplicate tuples because we are
 *	  interested in the probability of the data tuples rather than some
 *	  abstract notion of tuple identity that is really an artifact of our
 *	  representation. That is, we have to compute the probability of a
 *	  DNF, i.e., the sum of the weights of the worlds identified with
 *	  valuations f of the random variables such that the DNF becomes true
 *	  under f. This problem is #P-complete. The result is not the sum of
 *	  the probabilities of the individual conjunctive local conditions,
 *	  because they may, intuitively, overlap.
 *
 *	  Confidence computation can be efficiently approximated by Monte
 *	  Carlo simulation. The technique is based on the Karp-Luby fully
 *	  polynomial-time randomized approximation scheme (FPRAS) for
 *	  counting the number of solutions to a DNF formula, which is
 *	  described in
 *
 *	  R. M. Karp and M. Luby. Monte-Carlo Algorithms for Enumeration and
 *	  Reliability Problems. In Proc. FOCS, pages 5664, 1983.
 *
 *	  R. M. Karp, M. Luby, and N. Madras. Monte-Carlo Approximation
 *	  Algorithms for Enumeration Problems. J. Algorithms, 10(3):429448,
 *	  1989.
 *
 *	  There is an efficiently computable unbiased estimator that in
 *	  expectation returns the probability p of a DNF of n clauses (i.e.,
 *	  the local condition tuples of a Boolean U-relation) such that
 *	  computing the average of a polynomial number of such Monte Carlo
 *	  steps (= calls to the Karp-Luby unbiased estimator) is an
 *	  (epsilon, delta)-approximation for the probability: If the average
 *	  p_hat is taken over at least
 *
 *	           3 * n * log(2/delta)/epsilon^2
 *
 *	  Monte Carlo steps, then Pr[|p - p_hat| >= epsilon * p] <= delta.
 *
 *	  The paper
 *
 *	  P. Dagum, R. M. Karp, M. Luby, and S. M. Ross. An Optimal Algorithm
 *	  for Monte Carlo Estimation. SIAM J. Comput., 29(5):14841496, 2000.
 *
 *	  improves upon this by determining smaller numbers (within a
 *	  constant factor from optimal) of necessary iterations to achieve
 *	  an (epsilon, delta)-approximation.
 *
 *	  The approximation algorithm implemented in this file is a
 *	  combination of the Karp-Luby unbiased estimator for DNF counting
 *	  in a modified version adapted for confidence computation in
 *	  probabilistic databases and the Dagum-Karp-Luby-Ross optimal
 *	  algorithm for Monte Carlo estimation. The latter is based on
 *	  sequential analysis and determines the number of invocations of
 *	  the Karp-Luby estimator needed to achieve the required bound by
 *	  running the estimator a small number of times to estimate its mean
 *	  and variance. We actually use the probabilistic variant of a version
 *	  of the Karp-Luby estimator described in the book
 *
 *	  V. V. Vazirani. Approximation Algorithms. Springer, 2001
 *
 *	  which computes fractional estimates that have smaller variance than
 *	  the zero-one estimates of the classical Karp-Luby estimator.
 *
 *	  The unbiased estimator in the Vazirani version is implemented in the
 *	  local function compute_estimator(). The Dagum-Karp-Luby-Ross
 *	  optimal Monte Carlo estimation algorithm is implemented in
 *	  AA_algorithm().
 *
 *
 * Copyright (c) 2008, MayBMS Development Group
 *
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include <math.h>
#include "maybms/localcond.h"
#include "maybms/conf_comp.h"


/* Global variables */
/* FIXME: eliminate these.
 * Can we improve encapsulation? These variables are set from
 * include/maybms/lineage.h . 
 */
double epsilon;
double delta;


/* Local functions */
static int choose_with_distribution(int distrib_size, prob *distrib);
static int choose_with_distribution_2(worldTableEntry* entry);
static prob compute_estimator( generalState *state, prob* clause_bag_prob );
static prob AA_algorithm( generalState *state, prob* clause_bag_prob );


/* FIXME: choose_with_distribution[2]() is a naive and inefficient method
 * of sampling a clause resp. a variable assignment.
 */


/* choose_with_distribution: sample a clause
 *
 * Choose an index i from [0..distrib_size] with probability distrib[i]
 * This could be optimized by performing binary search.
 */
static int 
choose_with_distribution(int distrib_size, prob *distrib)
{
  int i;
  
  prob pick = (prob)rand(); /* in interval [0 .. RAND_MAX] */
  
  prob offset = 0;
  
  for(i = 0; i < distrib_size; i++)
  {
    offset += distrib[i] * (prob)RAND_MAX;	 
    if(pick <= offset) 
    	return i;
  }
  
  /* Due to the precision of float numbers problem, the code can reach here.
   * We know that pick is not bigger be offset. We return the index of the
   * last distribution.   
   */
  return distrib_size - 1;
}


/* choose_with_distribution_2: sample a variable assignment
 *
 * Similar to choose_with_distribution except that the input is a world table
 * entry.
 */
static int 
choose_with_distribution_2( worldTableEntry* entry )
{	
  	int i;
    
	prob pick = (prob)rand(); /* in interval [0 .. RAND_MAX] */
  	
  	prob offset = 0;

  	for(i = 0; i < entry->rng_entry_count; i++)
  	{
    		offset += ( entry->rng_entries + i )->p * (prob)RAND_MAX;	
    		 
    		if(pick <= offset) 
    			return (entry->rng_entries + i)->rng;
  	}  
  
	/* Due to the precision of float numbers problem, the code can reach
         * here.  We know that pick is not bigger be offset.  We return the
         * index of the last distribution.   
         */
  	return (entry->rng_entries + entry->rng_entry_count - 1)->rng;
}


/* compute_estimator
 *
 * Returns an unbiased estimator for p/nM, where
 * p is the probability of the DNF and nM is the bag sum of the clause weights.
 *
 * This is the version from the Vazirani book and converges more
 * quickly than the basic coverage algorithm from Karp-Luby-Madras.
 * It is similar to the algorithm from section 5 of that paper.
 */ 
static prob 
compute_estimator(generalState* state, prob* clause_bag_prob)
{
    int i = choose_with_distribution(NUM_WSDS, clause_bag_prob);
    
    int j, k;
	
    int c_tau = 0; /* count how many clauses are satisfied by tau */
    
    /* Pick one truth assignment tau from those of C[i] with probability
     * P(tau)/Sum_{tau' in C[i]} P(tau').
     */
    for(j = 0; j < state->wt_entry_count; j++)
    {
    	worldTableEntry *entry = state->wt_entries + j;
    	
      	int Cij = -1;
      	for(k = 0; k < WSD_LEN; k++) 
         	if(  S[i]->data[k]->var == entry->var ) 
         		Cij = S[i]->data[k]->rng;

		/* clause i does not determine value of x_j */
      	if(Cij == -1) 
      	{
            /* Randomly complete the truth assignment with the right weight. */
	    	entry->tau = choose_with_distribution_2( entry );
      	}
      	/* clause i determines value of x_j */
      	else 
      	{ 
      	    entry->tau = Cij;
      	}
    }
    
    /* Here is where the difference to the basic Karp-Luby estimator
     * starts. Rather than checking whether the chosen clause is the first
     * among those that satisfy the chosen possible world with respect to
     * some arbitrary fixed order of the clauses, and returning either
     * 1 or 0 based on this, we count the number of clauses that satisfy
     * the world and return the ratio of 1 to that count.
     */
    
    for(i = 0; i < NUM_WSDS; i++)
    {
      	bool clause_satisfied = true;
      	
      	for(k = 0; k < WSD_LEN; k++)
      	{
            if ( S[i]->data[k]->wt_entry->tau != S[i]->data[k]->rng )
            {
               clause_satisfied = false;
               break;
            }
        }
      	
      	if(clause_satisfied)
      	{
      	    c_tau++;
      	}
    }

    return (prob)1 / (prob)c_tau;
}


/* AA_algorithm 
 *
 * from Dagum, Karp, Luby, Ross, "An Optimal Algorithm for
 * Monte Carlo Estimation".

 * IMPORTANT NOTE / BUG-FIX:
 * Inside this function, some floating point numbers have to be of
 * high precision: (at least) float8, rather than prob, which may be of
 * type float4. This is particularly true for the S variables in all three
 * steps, since these sum up very small numbers over many iterations
 * to result in a very large number. Once S has reached a certain size,
 * the lack of precision of the numbers may lead to substantial errors
 * when making small increments by adding results from compute_estimator().
 *
 * For example, consider computing the probability of a triangle in a random
 * graph of 6 nodes (with iid edges with probability 0.5). Here,
 * for aconf(epsilon = 0.0005, delta = 0.00001) and float8 variables
 * everywhere,
 *
 * upsilon = 140278389.814750  upsilon2 = 34504004.176039
 * Step1: epsilon1 = 0.022361  delta1 = 0.000003
          upsilon_sra = 76452.103887  upsilon1_sra = 78162.624940
 *        N = 236963  S = 78162.711737  mu_hat = 0.329852
 * Step2: N = 52303.000000  S = 3016.064210  rho_hat = 0.057665
 * Step3: N = 18287160.984889  S = 6021712.739108  S/N = 0.329286 nM = 2.5
 *
 *  triangle_prob
 * ---------------
 *       0.823216
 * (1 row)
 *
 * while for float4 variables everywhere,
 *
 * upsilon = 140278384.000000  upsilon2 = 34504004.000000
 * Step1:  epsilon1 = 0.022361  delta1 = 0.000003
 *         upsilon_sra = 76452.101562  upsilon1_sra = 78162.625000
 *         N = 237419  S = 78162.674479  mu_hat = 0.329218
 * Step2:  N = 52403.000000  S = 3026.940918  rho_hat = 0.057763
 * Step3:  N = 18388656.000000  S = 5820828.750000  S/N = 0.316545
 *
 *  triangle_prob
 * ---------------
 *       0.791361
 * (1 row)
 *
 * These numbers are of course the outcome of a randomized computation
 * and will vary between runs.
 *
 * The second result (0.791361) is significantly wrong: the correct result is
 * Pr[triangle in RANDGRAPH(6)] = 0.823334 ~ 26979/32768.
 *
 * CAUTION: Using float8 is not a complete solution: It would be better to
 * add up the S values hierarchically, so that the gap between the values
 * added never reaches extremes.
 *
 */
static prob 
AA_algorithm(generalState *state, prob* clause_bag_prob)
{
	const float8 e = 2.718281828459;

   	const float8 upsilon  = 4.0 * (e - 2.0) * log(2 / delta)
                            / (epsilon * epsilon);

   	const float8 upsilon2 = 2.0 * (1.0 + sqrt(epsilon))
                         * (1.0 + 2.0 * sqrt(epsilon))
                         * (1.0 + log(1.5))/log(2/delta) * upsilon;

	double mu_hat;
 	double rho_hat;


   	/* Step 1: stopping rule algorithm */
	{
		const float8 epsilon_sra =
			(0.5 < sqrt(epsilon)) ? 0.5 : sqrt(epsilon);
		const float8 delta_sra = delta/3;
		const float8 upsilon_sra  = 4.0 * (e - 2.0)
                       * log(2.0 / delta_sra)
                       / (epsilon_sra * epsilon_sra);
                         
		const float8 upsilon1_sra = 1.0 + (1.0 + epsilon_sra)
		                        * upsilon_sra;

		int    N = 0;
		float8 S = 0;

		while(S < upsilon1_sra)
		{
		   N++;
		   S += compute_estimator( state, clause_bag_prob );
		}

		mu_hat = upsilon1_sra / (float8)N;
	}
 
   	/* Step 2: */
	{
   		const float8 N = ceil(upsilon2 * epsilon / mu_hat);
 	  	float8 S = 0;
		int i;
   
   		for(i = 1; i <= N; i++)
   		{
      		   prob kl2 = compute_estimator( state, clause_bag_prob )
		            - compute_estimator( state, clause_bag_prob );
      		   S += kl2 * kl2 / 2.0;
   		}
   
 	  	rho_hat = (S / N > epsilon * mu_hat) ?
			  (S / N) : (epsilon * mu_hat);
	}

   	/* Step 3: */
	{
		const float8 N = upsilon2 * rho_hat / (mu_hat * mu_hat);
 	  	float8 S = 0;
		int i;

   		for(i = 0; i < N; i++)
		   S += compute_estimator( state, clause_bag_prob );

   		return (prob)(S / N);
	}
}


/* aconf_accum0 
 *
 * Transition function for approximation of confidence computation 
 * involving 1 uncertain relations. Do nothing.
 */
Datum 
aconf_accum0(PG_FUNCTION_ARGS)
{
	PG_RETURN_INT32(1);	
}

/* aconf_accum1 
 *
 * Transition function for approximation of confidence computation 
 * involving 1 uncertain relations.
 */
Datum 
aconf_accum1(PG_FUNCTION_ARGS)
{
	aconf_accum(1)
}

/* aconf_accum2 
 *
 * Transition function for approximation of confidence computation 
 * involving 2 uncertain relations.
 */
Datum 
aconf_accum2(PG_FUNCTION_ARGS)
{
	aconf_accum(2)
}

/* aconf_accum3 
 *
 * Transition function for approximation of confidence computation 
 * involving 3 uncertain relations.
 */
Datum 
aconf_accum3(PG_FUNCTION_ARGS)
{
	aconf_accum(3)
}

/* aconf_accum4 
 *
 * Transition function for approximation of confidence computation 
 * involving 4 uncertain relations.
 */
Datum 
aconf_accum4(PG_FUNCTION_ARGS)
{
	aconf_accum(4)
}

/* aconf_accum5 
 *
 * Transition function for approximation of confidence computation 
 * involving 5 uncertain relations.
 */
Datum 
aconf_accum5(PG_FUNCTION_ARGS)
{
	aconf_accum(5)
}

/* aconf_accum6 
 *
 * Transition function for approximation of confidence computation 
 * involving 6 uncertain relations.
 */
Datum 
aconf_accum6(PG_FUNCTION_ARGS)
{
	aconf_accum(6)
}

/* aconf_accum7 
 *
 * Transition function for approximation of confidence computation 
 * involving 7 uncertain relations.
 */
Datum 
aconf_accum7(PG_FUNCTION_ARGS)
{
	aconf_accum(7)
}

/* aconf_accum8 
 *
 * Transition function for approximation of confidence computation 
 * involving 8 uncertain relations.
 */
Datum 
aconf_accum8(PG_FUNCTION_ARGS)
{
	aconf_accum(8)
}

/* aconf_accum9 
 *
 * Transition function for approximation of confidence computation 
 * involving 9 uncertain relations.
 */
Datum 
aconf_accum9(PG_FUNCTION_ARGS)
{
	aconf_accum(9)
}

/* aconf_accum10 
 *
 * Transition function for approximation of confidence computation 
 * involving 10 uncertain relations.
 */
Datum 
aconf_accum10(PG_FUNCTION_ARGS)
{
	aconf_accum(10)
}


/* aconf_final 
 *
 * Final function for approximation of confidence computation. 
 */
Datum 
aconf_final(PG_FUNCTION_ARGS)
{	
	prob nM = 0;
	prob *clause_bag_prob; 	/* clause_prob / nM */
	generalState *state = ( ( AggState *) fcinfo->context )->genstate;
	prob result = 0;
	MemoryContext oldcxt; 
        int i;
	
	/* If there is no tuple, return probability 0.  */
	if (groupcxt == NULL)	
		PG_RETURN_FLOAT4(0);	
	
	/* Switch to the right context. */	
	oldcxt = MemoryContextSwitchTo( groupcxt ); 

	/* Complete the missing range values for all variables */
	getMissingRngs( state ); 
	
	/* Compute the entry pointers of all clauses */
	computeEntryPointers(state);
	
	/* Calculate the bag sum nM and the bag ratios of the confidences of
     * all clauses.
     */

	/* The corresponding deallocation happens when freeing the current
	 * memory context in aconf_final()
	 */
	clause_bag_prob = ( prob * ) palloc( NUM_WSDS * sizeof( prob ) )  ;
	
	for( i = 0; i < NUM_WSDS; i++ )
	{
		nM += S[i]->prob;
	}
	
	for( i = 0; i < NUM_WSDS; i++ )
	{
		clause_bag_prob[ i ] = S[i]->prob / nM;
	}

	/* Confidence approximation */
	result = AA_algorithm( state, clause_bag_prob ) * nM; 

	/* Switch to the old context */
	MemoryContextSwitchTo( oldcxt );
	
	/* Delete the context for the current group of duplicates */
	MemoryContextDelete( groupcxt );
	
	/* Set the relevant variables to NULL */
	NUM_WSDS = 0;
	S = NULL;
	groupcxt = NULL;

	PG_RETURN_FLOAT4( result );	
}

