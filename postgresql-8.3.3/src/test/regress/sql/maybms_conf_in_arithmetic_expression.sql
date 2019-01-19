--test for multiple conf in a query

create table r (type varchar, p int);
insert into r values('a', 1), ('a', 2), ('b', 1), ('b', 2);

create table C as repair key type in r weight by p; 

--This tree queries should work after Lyublena disable the check for it
select conf() + 1 as p from C;

select conf()/8 as p from C;

select (conf()+1)/7 as p from C;
--

select cast( (conf()+1)/7 as real ) as p from C;

select cast( 9+(conf()+1)/7 as real ) as p from C;

select cast( abs(0-(conf()+1)/7) as real ) as p from C;

drop table r;
drop table C;
