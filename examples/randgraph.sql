/* Creates an undirected random graph in which all possible graphs
   have the same probability. Essentially, we are emulating a
   tuple-independent database.

   Note that there are 2^{n*(n-1)/2} possible worlds for RANDGRAPH(n),
   i.e. a random graph of n nodes.


   If each edge is independently in the random graph with probability 0.5,
   then these are the probabilities or tiangles, 4-cliques, and 2-paths in
   some random graphs:

   Pr[triangle in RANDGRAPH(1)] = 0     (obvious)
   Pr[triangle in RANDGRAPH(2)] = 0     (obvious)
   Pr[triangle in RANDGRAPH(3)] = 1/8   (obvious)
   Pr[triangle in RANDGRAPH(4)] = 23/64 (verified by CK)
   Pr[triangle in RANDGRAPH(5)] = 0.621094 ~ 636/1024
   Pr[triangle in RANDGRAPH(6)] = 0.823334 ~ 26979/32768
   Pr[triangle in RANDGRAPH(7)] = 0.936342
   Pr[triangle in RANDGRAPH(8)] = 0.982557

   Pr[4-clique in RANDGRAPH(1)] = 0    (obvious)
   Pr[4-clique in RANDGRAPH(2)] = 0    (obvious)
   Pr[4-clique in RANDGRAPH(3)] = 0    (obvious)
   Pr[4-clique in RANDGRAPH(4)] = 1/64 (obvious)
   Pr[4-clique in RANDGRAPH(5)] = .0644531 ~ 66/1024
   Pr[4-clique in RANDGRAPH(6)] = .156921 ~ 5142/32768
   Pr[4-clique in RANDGRAPH(7)] = .291135
   aconf(.005,.005)[4-clique in RANDGRAPH(8)] ~ .450481
   aconf(.005,.005)[4-clique in RANDGRAPH(9)] ~ .614472
   aconf(.005,.005)[4-clique in RANDGRAPH(10)] ~ .757663
   aconf(.005,.005)[4-clique in RANDGRAPH(11)] ~ .863336
   aconf(.005,.005)[4-clique in RANDGRAPH(12)] ~ .932774
   aconf(.005,.005)[4-clique in RANDGRAPH(15)] ~ .995135 (2^105 p.w.)

   Pr[path of length 2 in RANDGRAPH(1)] = 0     (obvious)
   Pr[path of length 2 in RANDGRAPH(2)] = 0     (obvious)
   Pr[path of length 2 in RANDGRAPH(3)] = 3/8   (obvious)
   Pr[path of length 2 in RANDGRAPH(4)] = 49/64 (verified by CK)
*/


drop table node;
drop table inout;
drop table total_order;
drop table to_subset;
drop table edge0;
drop table no_edge0;
drop table edge;
drop table no_edge;

create table node (n integer);
insert into  node values (1);
insert into  node values (2);
insert into  node values (3);
insert into  node values (4);
/*
insert into  node values (5);
insert into  node values (6);
insert into  node values (7);
insert into  node values (8);
insert into  node values (9);
insert into  node values (10);
insert into  node values (11);
insert into  node values (12);
insert into  node values (13);
insert into  node values (14);
insert into  node values (15);
insert into  node values (16);
insert into  node values (17);
insert into  node values (18);
insert into  node values (19);
insert into  node values (20);
insert into  node values (21);
insert into  node values (22);
insert into  node values (23);
insert into  node values (24);
insert into  node values (25);
insert into  node values (26);
insert into  node values (27);
insert into  node values (28);
insert into  node values (29);
insert into  node values (30);
insert into  node values (31);
insert into  node values (32);
insert into  node values (33);
insert into  node values (34);
insert into  node values (35);
insert into  node values (36);
insert into  node values (37);
insert into  node values (38);
insert into  node values (39);
insert into  node values (40);
*/


/* Here we specify the probability that an edge is in the graph.
   Note: The explanation above assumes that p is 0.5.
*/
create table inout (bit integer, p float);
insert into  inout values (1, 0.5); /* probability that edge is in the graph */
insert into  inout values (0, 0.5); /* probability that edge is missing */

create table total_order as
(
   select n1.n as u, n2.n as v
   from node n1, node n2
   where n1.n < n2.n
);


/* This table represents all subsets of the total order over node as possible
   worlds. We use the same probability -- from inout -- for each edge, but
   in principle we could just as well have a different (independent)
   probability for each edge.
*/
create table to_subset as
(
   repair key u,v
   in (select * from total_order, inout)
   weight by p
);


create table edge0    as (select u,v from to_subset where bit=1);
create table no_edge0 as (select u,v from to_subset where bit=0);

create table edge     as (select *              from edge0);
insert into  edge        (select v as u, u as v from edge0);

create table no_edge  as (select *              from no_edge0);
insert into  no_edge     (select v as u, u as v from no_edge0);


/*
select conf() as triangle_prob
from   edge e1, edge e2, edge e3
where  e1.v = e2.u and e2.v = e3.u and e3.v=e1.u
and    e1.u <> e2.u and e1.u <> e3.u and e2.u <> e3.u;
*/

/* A more efficient implementation: We use edge0 -- a subset of the
   total order -- and approximate the probability value using
   aconf(epsilon, delta).
*/
select aconf(.001,.000001) as triangle_prob
from   edge0 e1, edge0 e2, edge0 e3
where  e1.v = e2.u and e2.v = e3.v and e1.u = e3.u
and    e1.u < e2.u and e2.u < e3.v;


/*
select conf() as fourclique_prob
from   edge e1, edge e2, edge e3, edge e4, edge e5, edge e6
where  e1.v = e2.u and e2.v = e3.u and e3.v = e4.u and e4.v = e1.u
and    e1.u = e5.u and e5.v = e3.u and e2.u = e6.u and e6.v = e4.u
and    e1.u <> e2.u and e1.u <> e3.u and e1.u <> e4.u
and    e2.u <> e3.u and e2.u <> e4.u and e3.u <> e4.u;
*/

/* A more efficient implementation. */
select aconf(.5,.1) as fourclique_prob
from   edge e1, edge e2, edge e3, edge e4, edge e5, edge e6
where  e1.v = e2.u and e2.v = e3.u and e1.u = e4.u and e4.v = e2.v
and    e5.u = e2.u and e5.v = e3.v and e6.u = e1.u and e6.v = e3.v
and    e1.u < e2.u and e2.u < e3.u and e3.u < e3.v;



/* The probability that there is a path of length 2. We use the no_edge
   relation here.
*/
/*
select conf() as path2_prob
from   node n1, node n2, node n3,
       edge e12, edge e23, no_edge e13
where  n1.n <> n2.n and n1.n <> n3.n and n2.n <> n3.n
and    n1.n = e12.u and e12.v = n2.n
and    n2.n = e23.u and e23.v = n3.n
and    n1.n = e13.u and e13.v = n3.n;
*/

/* Again a more efficient implementation: */
/*
select aconf(.05,.05) as path2_prob
from   edge e1, edge e2, no_edge e3
where  e1.v = e2.u and e2.v = e3.u and e3.v = e1.u
and    e1.u <> e2.u and e1.u <> e3.u and e2.u <> e3.u;
*/


