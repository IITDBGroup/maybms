--test for expanding stars

create table r(a int, b int);

create table r1 as repair key a in r; 

create table r2 as select a as c, b as d from (pick tuples from r) r0;

create table r3 as select a as d, b as e, c as f, d as g from r1, r2;

create table r4(h int);

select * from r1;

select * from r1, r2;

select * from r1, r4;

select r1.* from r1, r2;

select r2.*, r1.* from r1, r2, r3;

select r2.*, r1.*, r3.* from r1, r2, r3;

select r4.* from r1, r2, r3, r4;

select r1.a from r1, r2, r3;

select * from 
( select * from r1, r2 where r1.a = r2.c ) s1, ( select g from r1, r3, r4  ) s2;

select * from 
( select a as new_a, * from r1, r2 where r1.a = r2.c ) s1, ( select e as new_e from r1, r3  ) s2;

drop table r;
drop table r1;
drop table r2;
drop table r3;
drop table r4;
