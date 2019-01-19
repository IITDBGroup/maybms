-- test for direct manipulation on condition columns 

create table s (a int, b int);
insert into s values (1,1), (1,2),(1,3),(2,1),(2,2),(2,3);

create table r as repair key a in s weight by b;

--insert condition columns
insert into r values (1,1,4,2,0.7);

--insert condition columns
insert into r values (1,1);
select * from r;

--modify condition columns
update r set _v0=4 where a = 1;

update r set _d0=4 where a = 1;

update r set _p0=4 where a = 1;

drop table r;
drop table s;


