--test for multiple conf in a query

create table r (type varchar, p int);
insert into r values('a', 1), ('a', 2), ('b', 1), ('b', 2);

create table C as repair key type in r weight by p; 

select type, conf() from C group by type;

select type, conf(), conf() from C group by type;

select type, conf(), aconf(.5,.5) from C group by type;

select conf(), conf() from C;

drop table r;
drop table C;
