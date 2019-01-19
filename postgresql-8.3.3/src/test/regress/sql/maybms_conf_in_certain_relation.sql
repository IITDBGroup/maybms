--test of error messges of confidence computation on top of certain relation 

create table r(a int, b int);

select conf() from r;

select tconf() from r;

select aconf(.1,.1) from r;

select ecount() from r;

select esum(a) from r;

select a, conf() from r group by a;

select a, tconf() from r;

select a, aconf() from r group by a;

select a, ecount() from r group by a;

select a, esum(b) from r group by a;

drop table r;

create table r(a int, b int);
insert into r values (1,1), (1,2), (2,1), (2,2);

create table s as repair key a in r;

select conf() from( select conf() from s ) as s;

select tconf() from( select tconf() from s ) as s;

select aconf(.1,.1) from( select aconf(.1,.1) from s ) as s;

select ecount() from( select ecount() from s ) as s;

select esum(esum) from( select esum(a) from s ) as s;

drop table r;
drop table s;
