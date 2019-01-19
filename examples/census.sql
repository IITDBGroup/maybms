/* Vertical decompositioning to represent a relation
   Census(TID, SSN, MaritalStatus, Name)

   This example is used in the MayBMS overview paper
   C.Koch, "MayBMS: A System for Managing Large Uncertain and
            Probabilistic Databases".
*/

drop table Census_SSN;
drop table Census_SSN_0;
drop table Census_MaritalStatus;
drop table Census_MaritalStatus_0;
drop table Census_Name;
drop table FD_Violations;
drop table FD_Violations_by_ssn;

create table Census_SSN_0           (tid integer, ssn integer, p float);
create table Census_MaritalStatus_0 (tid integer, m integer,   p float);
create table Census_Name            (tid integer, n varchar);

insert into Census_SSN_0 values (1, 185, .4);
insert into Census_SSN_0 values (1, 785, .6);
insert into Census_SSN_0 values (2, 185, .7);
insert into Census_SSN_0 values (2, 186, .3);

insert into Census_MaritalStatus_0 values (1, 1, .8);
insert into Census_MaritalStatus_0 values (1, 2, .2);
insert into Census_MaritalStatus_0 values (2, 1, .25);
insert into Census_MaritalStatus_0 values (2, 2, .25);
insert into Census_MaritalStatus_0 values (2, 3, .25);
insert into Census_MaritalStatus_0 values (2, 4, .25);

insert into Census_Name values (1, 'Smith');
insert into Census_Name values (2, 'Brown');

create table Census_SSN as
   repair key tid in Census_SSN_0 weight by p;

select * from Census_SSN;
/*
 tid | ssn |  p  | _v0 | _d0 | _p0
-----+-----+-----+-----+-----+-----
   1 | 185 | 0.4 |  s1 | 185 | 0.4
   1 | 785 | 0.6 |  s1 | 785 | 0.6
   2 | 185 | 0.7 |  s2 | 185 | 0.7
   2 | 186 | 0.3 |  s2 | 186 | 0.3
*/


create table Census_MaritalStatus as
   repair key tid in Census_MaritalStatus_0 weight by p;

select * from Census_MaritalStatus;
/*
 tid | m |  p   | _v0 | _d0 | _p0
-----+---+------+-----+-----+------
   1 | 1 |  0.8 |  m1 |   1 |  0.8
   1 | 2 |  0.2 |  m1 |   2 |  0.2
   2 | 1 | 0.25 |  m2 |   1 | 0.25
   2 | 2 | 0.25 |  m2 |   2 | 0.25
   2 | 3 | 0.25 |  m2 |   3 | 0.25
   2 | 4 | 0.25 |  m2 |   4 | 0.25
*/


/* full census table
select S.tid, ssn, m, n
from   Census_SSN S, Census_MaritalStatus M, Census_Name N
where  S.tid = M.tid and M.tid = N.tid;
*/


/* violations of fd ssn->tid */
create table FD_Violations as
select S1.ssn
from   Census_SSN S1, Census_SSN S2
where  S1.tid < S2.tid and S1.ssn = S2.ssn; 

select * from FD_Violations;
/*
 ssn | _v0 | _d0 | _p0 | _v1 | _d1 | _p1
-----+-----+-----+-----+-----+-----+-----
 185 |  s1 | 185 | 0.4 |  s2 | 185 | 0.7
*/


select ssn, conf() as prior
from   Census_SSN
group by ssn;
/*
 ssn | prior
-----+-------
 185 |  0.82
 186 |   0.3
 785 |   0.6
*/


/* For each SSN, the sum of the weights of those worlds in
   which the SSN occurs and an FD is violated.
*/
create table FD_Violations_by_ssn as
(
   select S.ssn, conf() as p
   from FD_Violations V,
        Census_SSN S
   group by S.ssn
);


/* This is a demonstration of data cleaning using an integrity constraint,
   here the fd ssn->tid.
*/
select Q1.ssn, p1, p2, p3, cast((p1-p2)/(1-p3) as real) as posterior
from
   (
      select ssn, conf() as p1
      from   Census_SSN
      group by ssn
   ) Q1,
   (
      (select ssn, p as p2 from FD_Violations_by_ssn)
      union
      (
         (select ssn, 0 as p2 from Census_SSN_0)
         except
         (select possible ssn, 0 as p2 from FD_Violations_by_ssn)
      )
   ) Q2,
   (
      select conf() as p3
      from   FD_Violations
   ) Q3
where Q1.ssn = Q2.ssn;
/*
 ssn |  p1  |  p2  |  p3  | posterior
-----+------+------+------+-----------
 185 | 0.82 | 0.28 | 0.28 |      0.75
 186 |  0.3 |    0 | 0.28 |  0.416667
 785 |  0.6 |    0 | 0.28 |  0.833333
*/


