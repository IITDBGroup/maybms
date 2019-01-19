
--Test for joins on uncertain quereis. The tuples with the same variables but
--different domain values should not be joined

create table r (a int, b int);

insert into r values (1,1),(1,2),(2,1),(2,2);

create table s as repair key a in r;

select s1.a as a1, s1.b as b1, s2.a as a2, s2.b as b2 from s s1, s s2;

select possible s1.a as a1, s1.b as b1, s2.a as a2, s1.b as b1, s2.b as b2 from s s1, s s2;

create table t as
select s1.a as a1, s1.b as b1, s2.a as a2, s2.b as b2 from s s1, s s2;

select a1, b1, a2, b2, s.a as a3, s.b as b3 from t, s;

select possible a1, b1, a2, b2, s.a as a3, s.b as b3 from t, s;

drop table r;
drop table s;
drop table t;
