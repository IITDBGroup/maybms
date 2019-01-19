--test of combination of maybms constructs

create table r(a int, b int);
insert into r values(1,2), (1,3), (2,4);

create table s as
repair key a in r;
select * from s;

--output an error message for aconf without parameters
select aconf() from s;

--output an error message for esum and ecount without parameters
select a, esum() from s group by a;

--combination of repair-key and confidence computation operators
repair key p in (select a, conf() as p from s group by a);

repair key p in (select a, esum(b) as p from s group by a);

repair key p in (select a, ecount(b) as p from s group by a);

repair key p in (select tconf() as p from s);

repair key p in (select aconf(0.1,0.1) as p from s);

repair key p in (select a, conf() as p from s group by a);

--combination of pick-tuples and confidence computation operators
pick tuples from (select a, conf() as p from s group by a);

pick tuples from (select a, esum(b) as p from s group by a);

pick tuples from (select a, ecount(b) as p from s group by a);

pick tuples from (select tconf() as p from s);

pick tuples from (select aconf(0.1,0.1) as p from s);

pick tuples from (select a, conf() as p from s group by a);

--combination of possible and confidence computation operators
select possible a, conf() as p from s group by a;

select possible a, esum(b) as p from s group by a;

select possible a, ecount(b) as p from s group by a;

select possible tconf() as p from s;

select possible a, aconf() as p from s group by a;

--combination of repair-key and possible
repair key a in (select possible a from r);

--combination of pick-tuples and possible
pick tuples from (select possible a from r);

--combination of repair-key and repair-key
repair key a in (repair key a in r);

--combination of repair-key and repair-key
pick tuples from (pick tuples from r);

--combination of repair-key and pick-tuples
pick tuples from (repair key a in r);

--combination of repair-key and pick-tuples
repair key a in (pick tuples from r);

drop table r;
drop table s;
