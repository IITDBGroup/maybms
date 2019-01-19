--test for esum

create table s (a int, b int);
insert into s values (1,1), (1,2),(1,3),(2,1),(2,2),(2,3);

create table r as repair key a in s weight by b;

select a, esum( b ) from r group by a; 

select esum( b ) from r; 

select a, ecount( b ) from r group by a; 

select ecount( b ) from r; 

--test for ecount without arguments, the results should be the same as those 
--with any other argument

select a, ecount() from r group by a; 

select ecount() from r; 

drop table r;
drop table s;
