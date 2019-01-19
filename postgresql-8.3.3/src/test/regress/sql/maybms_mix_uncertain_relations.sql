--test of mix of two kinds of uncertain relations

create table r(a int, b int);
insert into r values (1,2),(1,3),(2,-1),(2,0);

--pick-tuples
create table s1 as pick tuples from r;

--repair-key
create table s2 as repair key a in r;

select conf() from s1, s2;

select conf() from 
( pick tuples from r ) as s1, ( repair key a in r ) as s2;

drop table r;
drop table s1;
drop table s2;


