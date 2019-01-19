--test for altering uncertain table
create table s(a int, b int);

create table r as repair key a in s;

alter table r add column c int;

alter table r drop column a;

alter table r drop column _v0;

alter table r drop column _d0;

alter table r drop column _p0;

drop table r;
drop table s;



