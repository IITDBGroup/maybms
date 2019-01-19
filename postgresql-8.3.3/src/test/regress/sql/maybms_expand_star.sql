--test for expanding star with functions and repair-key

create table s (a int);

create table r as repair key a in s;

select * from r;

select * from (select tconf() from r) s;

repair key (tconf) in (select * from (select tconf() from r) s);

repair key (a) in (select * from (select tconf() from r) s);

repair key (a) in (select * from (select a, tconf() from r) s);

drop table r;
drop table s;
