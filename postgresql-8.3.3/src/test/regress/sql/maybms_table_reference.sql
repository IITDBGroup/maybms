
create table r (type varchar, p int);
insert into r values('a', 1), ('a', 2), ('b', 1), ('b', 2);

create table C as repair key type in r weight by p; 

--test for table reference
select d.type,conf() as P2 from C d group by d.type;

--bug reported by Christoph
select conf() as P2 from C group by type;

drop table r;
drop table C;
