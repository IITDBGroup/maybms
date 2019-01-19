--test of pick-tuples

create table r(a int, b real);
insert into r values (1,2),(1,3),(2,-1),(2,0);

--syntax check
pick tuples from r independently;

pick tuples from r independently with probability 1;

pick tuples from r with probability 1;

--raise an error for p > 1
pick tuples from r with probability b;

drop table r;
create table r(a int, b real);
insert into r values (1,0.5),(1,0.4),(2,-1),(2,0);

--raise an error for p < 0
pick tuples from r with probability b;

--silently remove tuples with probability 0
pick tuples from r with probability (b+1)/10;

drop table r;


