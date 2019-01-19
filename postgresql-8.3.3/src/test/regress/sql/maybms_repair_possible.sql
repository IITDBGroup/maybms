--test for the combination of possible and repair-key

create table r(a int, b int);

insert into r values (1,2), (1,3);

create table t as repair key(a) in r;

select * from t;

repair key (a) in (select possible a,b from t);

drop table r;
drop table t;
