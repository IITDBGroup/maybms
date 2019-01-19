/* An example demonstrating expected sums. */

drop table sales;
drop table R;


/* A table of potential sales transactions, with an associated degree
   of belief that the transaction will happen, expressed as a probability.
*/
create table sales (transact_id int, amount float, prob float);
insert into  sales values (1, 5000, .7);
insert into  sales values (2, 2000, .4);


/* Expected sales using linearity of expectation. */
select sum(amount * prob) as expected_sales from sales;
/*
 expected_sales
----------------
           4300
(1 row)
*/


/* create tuple-independent table */
create table R as
pick tuples from sales independently with probability prob;


select * from R;
/*
 transact_id | amount | prob | _v0 | _d0 | _p0 
-------------+--------+------+-----+-----+-----
           1 |   5000 |  0.7 |  x1 |   1 | 0.7
           2 |   2000 |  0.4 |  x2 |   1 | 0.4

(2 rows)
*/


select esum(amount) as expected_sales from R;
/*
 expected_sales
----------------
           4300
(1 row)
*/

