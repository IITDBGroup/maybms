/* A very simple example involving Boolean queries, random variables,
   marginalization, and conditional probabilities.
*/

/*
We start by creating a simple table using SQL commands.
The table encodes that we see rain and a wet ground with probability 0.4,
no rain but a wet ground with probability 0.1, and
no rain and a dry ground with probability 0.5.
*/
create table R (Dummy varchar, Weather varchar, Ground varchar, P float);
insert into R values ('dummy',    'rain', 'wet', 0.4);
insert into R values ('dummy', 'no rain', 'wet', 0.1);
insert into R values ('dummy', 'no rain', 'dry', 0.5);

select * from R;
/*
 dummy | weather | ground |  p
-------+---------+--------+-----
 dummy | rain    | wet    | 0.4
 dummy | no rain | wet    | 0.1
 dummy | no rain | dry    | 0.5
(3 rows)
*/

/*
The following statement creates a table S with exactly one of the three
tuples, chosen with probability P.
*/
create table S as repair key Dummy in R weight by P;

/*
There are at least two natural interpretations of this example,
one using random variables and one using a possible worlds semantics.

(1) We can think of R as a table specifying the joint probability distribution
of two discrete random variables Weather (with values ``rain'' and ``no rain'')
and Ground (with values ``wet'' and ``dry'').

(2) Alternatively, there are three possible worlds, in which the values of
Weather and Ground are given by the three tuples of R.
Relation S computes these three possible worlds, which are represented in the
database.

Using the conf() operation, we can compute the probabilities Pr[Ground='wet']
and Pr[Weather='rain' and Ground='wet'] as follows.
*/

create table Wet as
select conf() as P from S where Ground = 'wet';

select * from Wet;
/*
  p
-----
 0.5
(1 row)
*/

create table Rain_and_Wet as
select conf() as P from S where Weather = 'rain' and Ground = 'wet';

select * from Rain_and_Wet;
/*
  p
-----
 0.4
(1 row)
*/

/*
Finally, we compute the conditional probability
Pr[Weather='rain' | Ground='wet'] as the ratio
Pr[Weather='rain' and Ground='wet'] / Pr[Ground='wet'].
*/
select R1.P/R2.P as Rain_if_Wet from Rain_and_Wet R1, Wet R2;
/*
 rain_if_wet
-------------
         0.8
(1 row)
*/


/*
Note that conf() is an aggregate. We can compute the marginal probability
table for random variable Ground as follows.
*/
select Ground, conf() from S group by Ground;
/*
 ground | conf
--------+------
 dry    |  0.5
 wet    |  0.5
(2 rows)
*/


/* Similarly we can compute a table for the conditional confidence values
   Pr[Weather=w | Ground=g] as follows.
*/
select W_and_G.Weather, G.Ground, W_and_G.P/G.P as Weather_given_Ground
from
   (
      select Weather, Ground, conf() as P from S
      group by Weather, Ground
   ) W_and_G,
   (
      select Ground, conf() as P from S group by Ground
   ) G
where W_and_G.Ground = G.Ground;
/*
 weather | ground | weather_given_ground
---------+--------+----------------------
 no rain | dry    |                    1
 no rain | wet    |                  0.2
 rain    | wet    |                  0.8
(3 rows)
*/


/* Note that the query

   select Weather, Ground, conf() as P from S
   group by Weather, Ground;

   only returns a tuple for Weather-Ground value pairs whose probability
   is greater than zero. Thus, the conditional probability
   Pr[Weather='rain' | Ground='dry'] = 0 is not shown. To change that,
   we can proceed as follows.
*/
select W_and_G.Weather, G.Ground, W_and_G.P/G.P as Weather_given_Ground
from
   (
      (
         select Weather, Ground, conf() as P from S
         group by Weather, Ground
      )
      union
      (
         (
            select R1.Weather, R2.Ground, 0 as P
            from   R R1, R R2
         )
         except
         (
            select possible Weather, Ground, 0 as P from S
         )
      )
   ) W_and_G,
   (
      select Ground, conf() as P from S group by Ground
   ) G
where W_and_G.Ground = G.Ground;
/*
 weather | ground | weather_given_ground
---------+--------+----------------------
 no rain | dry    |                    1
 no rain | wet    |                  0.2
 rain    | dry    |                    0
 rain    | wet    |                  0.8
(4 rows)
*/

/* The keyword "possible" is syntactic sugar. The subquery

   select possible Weather, Ground, 0 as P from S;

   is equivalent to

   select Weather, Ground, 0 as P
   from
   (
      select Weather, Ground, conf() as P0
      from S
      group by Weather, Ground
   ) Q
   where P0 > 0;

   However, the where-condition is always satisfied and can be left out.
*/

drop table R;
drop table S;
drop table Wet;
drop table Rain_and_Wet;
