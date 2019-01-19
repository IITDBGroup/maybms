/* A coin tossing example motivated by an example by J. Halpern; see also

   C.Koch, "Approximating Predicates and Expressive Queries on
            Probabilistic Databases", PODS 2008.

   A bucket contains two fair coins and one double-head coin.
   You pick one coin: the probability that it is fair is 2/3.
   You observe that an experiment of tossing the coin twice
   yields ``heads'' twice.  The posterior probability that
   the coin is fair given this evidence is 1/3.

   The example can be easily generalized to (further) types of
   biased coins, more tosses, and tosses that result in ``tails''.
*/

drop table Coins;
drop table Faces;
drop table Tosses;
drop table Evidence;
drop table CoinPicked;
drop table TossedTwice;
drop table EvidenceViolations;
drop table EvidenceViolationsP;
drop table Q;


/* Contents of the bucket. */
create table Coins (type varchar, cnt integer);
insert into  Coins values ('fair',    2);
insert into  Coins values ('2headed', 1);
/*
insert into  Coins values ('biased',  1);
*/
/* Should throw an error, these counts must be above 0. */

/* Description of the coin types fair and double-headed. */
create table Faces (type varchar, face varchar, fprob float);
insert into  Faces values ('fair',   'H', 0.5);
insert into  Faces values ('fair',   'T', 0.5);
insert into  Faces values ('2headed','H', 1);
insert into  Faces values ('biased', 'H', 0.8);
insert into  Faces values ('biased', 'T', 0.2);

/* We toss the chosen coin as often as there are distinct tuples in Tosses. */
create table Tosses (toss integer);
insert into  Tosses values (1);
insert into  Tosses values (2);
/*
insert into  Tosses values (3);
insert into  Tosses values (4);
*/

/* The outcome of each toss is ``heads''. */ 
create table Evidence as
select toss, cast('H' as varchar) as face from Tosses;

/*
insert into  Tosses values (5);
insert into  Evidence values (5, 'T');
*/



/* Above we only created a relational database. Below we will turn it
   into a probabilistic database.
*/
   


/* Pick a coin from a bucket of two fair coins and one
   double-headed coin.
*/
create table CoinPicked as
repair key dummy in (select 1 as dummy, type, cnt from Coins) weight by cnt;

select * from CoinPicked;
/*
 dummy |  type   | cnt | _v0  | _d0  |   _p0
-------+---------+-----+------+------+----------
     1 | 2headed |   1 |    1 | 2hea | 0.333333
     1 | fair    |   2 |    1 | fair | 0.666667
*/


/* Toss that coin twice. */
create table TossedTwice as
select R.type, toss, face
from   CoinPicked R,
       (
           repair key type, toss
           in (select * from Faces, Tosses)
           weight by fprob
       ) S0
where  R.type = S0.type;

select * from TossedTwice;
/*
  type   | toss | face | _v0  | _d0  |   _p0    | _v1  | _d1  | _p1
---------+------+------+------+------+----------+------+------+-----
 2headed |    1 | H    |    1 | 2hea | 0.333333 | 2he1 | H    |   1
 2headed |    2 | H    |    1 | 2hea | 0.333333 | 2he2 | H    |   1
 fair    |    1 | H    |    1 | fair | 0.666667 | fai1 | H    | 0.5
 fair    |    1 | T    |    1 | fair | 0.666667 | fai1 | T    | 0.5
 fair    |    2 | H    |    1 | fair | 0.666667 | fai2 | H    | 0.5
 fair    |    2 | T    |    1 | fair | 0.666667 | fai2 | T    | 0.5
*/


/* We observe that the outcome of the two tosses
   is twice heads (the "evidence").
   Compute, in each possible world, whether the outcome of the
   two coin tosses is different from the evidence.
   If so, output the type of the coin in that world.
*/
create table EvidenceViolations as
select S.type
from   TossedTwice S, Evidence E
where  S.toss = E.toss and S.face <> E.face;


select * from EvidenceViolations;
/*
 type | _v0  | _d0  |   _p0    | _v1  | _d1  | _p1
------+------+------+----------+------+------+-----
 fair |    1 | fair | 0.666667 | fai1 | T    | 0.5
 fair |    1 | fair | 0.666667 | fai2 | T    | 0.5
*/


/* Probability Pr[evidence is not twice heads, and coin type = t]
   that the evidence is not observed, for each coin type t.
*/
create table EvidenceViolationsP as
(
   (select type, conf() as P2 from EvidenceViolations group by type)
   union 
   (  /* the impossible coin types have probability 0 */
      (select type, 0 as p2 from Coins)
      except
      (select possible type, 0 as p2 from EvidenceViolations)
   )
);

select * from EvidenceViolationsP;
/*
  type   | p2
---------+-----
 2headed |   0
 fair    | 0.5         = .666667 * (1 - .5^2)
*/


/* The conditional probabilities
   Pr[Cointype = t | evidence is twice heads]
   for each coin type t.
*/
create table Q as
select Q1.type, cast((p1-p2)/(1-p3) as real) as p
from   (select type, conf() as p1 from TossedTwice group by type) Q1,
       EvidenceViolationsP Q2,
       (select conf() as p3 from EvidenceViolations) Q3
where  Q1.type = Q2.type;


select * from Q;
/*
  type   |    p
---------+----------
 2headed | 0.666667
 fair    | 0.333333
*/


/* maximum-a-posteriori */
select argmax(type, p) as map from Q;
/*
   map
---------
 2headed
*/


/* maximum likelihood */
select argmax(Q.type, Q.p/R.p) as mle
from   Q, (select type, conf() as P from CoinPicked group by type) R
where  Q.type = R.type;
/*
   mle
---------
 2headed
*/




