/* The company buying example from

   L.Antova, C.Koch, and D.Olteanu, "From Complete to Incomplete Information
                and Back", Proc. SIGMOD 2007

   with group-worlds-by replaced by conditional probability computation.

   The following problem is captured by this example. Given a relational
   database storing information about employees working in companies and
   having certain skills. The question is the following: If we buy
   exactly one of the companies, and exactly one employee leaves the
   company we buy, which skills do we gain for certain?
*/

drop table CompanyEmployee;
drop table EmployeeSkill;
drop table Choices;
drop table RemainingEmployees;
drop table SkillGained;


create table CompanyEmployee (cid varchar, eid varchar);
insert into  CompanyEmployee values ('Google', 'Larry');
insert into  CompanyEmployee values ('Google', 'Sergey');
insert into  CompanyEmployee values ('Yahoo',  'Raghu');
insert into  CompanyEmployee values ('Yahoo',  'Chris');
insert into  CompanyEmployee values ('Yahoo',  'Phil');

create table EmployeeSkill (eid varchar, skill varchar);
insert into  EmployeeSkill values ('Larry',  'Web');
insert into  EmployeeSkill values ('Sergey', 'Web');
insert into  EmployeeSkill values ('Sergey', 'Segway');
insert into  EmployeeSkill values ('Raghu',  'DB');
insert into  EmployeeSkill values ('Raghu',  'Web');
insert into  EmployeeSkill values ('Chris',  'Search');
insert into  EmployeeSkill values ('Phil',   'DB');


/* We indicate no weights for the alternatives, so we assume a uniform
   distribution.
*/
create table Choices as
repair key dummy in (select 1 as dummy, * from CompanyEmployee);

select * from Choices;
/*
This creates five possible worlds, each containing one choice of a company
and an employee from that company. That is, each world contains precisely
one tuple from CompanyEmployee. Each of the five worlds has probability 1/5.

MayBMS U-relation representation of the query result:
 dummy |  cid   |  eid   | _v0 | _d0 | _p0
-------+--------+--------+-----+-----+-----
     1 | Google | Larry  |   v |   1 | 0.2
     1 | Google | Sergey |   v |   2 | 0.2
     1 | Yahoo  | Raghu  |   v |   3 | 0.2
     1 | Yahoo  | Chris  |   v |   4 | 0.2
     1 | Yahoo  | Phil   |   v |   5 | 0.2
*/


create table RemainingEmployees as
select CE.cid, CE.eid
from   CompanyEmployee CE, Choices C
where  C.cid = CE.cid and C.eid <> CE.eid;

select * from RemainingEmployees;
/*
RemainingEmployees stores, in each possible world, the employees of the
chosen company except for the chosen employee, i.e., the employee that
leaves the company.

MayBMS U-relation representation of the query result:
  cid   |  eid   | _v0 | _d0 | _p0
--------+--------+-----+-----+-----
 Google | Sergey |   v |   1 | 0.2    <-- emp. selected: Larry
 Google | Larry  |   v |   2 | 0.2    <-- emp. selected: Sergey
 Yahoo  | Chris  |   v |   3 | 0.2    <-- emp. selected: Raghu
 Yahoo  | Phil   |   v |   3 | 0.2 
 Yahoo  | Raghu  |   v |   4 | 0.2    <-- emp. selected: Chris
 Yahoo  | Phil   |   v |   4 | 0.2
 Yahoo  | Raghu  |   v |   5 | 0.2    <-- emp. selected: Phil
 Yahoo  | Chris  |   v |   5 | 0.2
*/


/* This query computes, for each company cid and each skill, the conditional
   probability p that the skill is gained by buying company cid
   under the assumption that a random employee leaves.
*/
create table SkillGained as
select Q1.cid, Q1.skill, p1, p2, p1/p2 as p
from
   (
      select   R.cid, S.skill, conf() as p1
      from     RemainingEmployees R, EmployeeSkill S
      where    R.eid = S.eid
      group by R.cid, S.skill
   ) Q1,
   (
      select   cid, conf() as p2
      from     RemainingEmployees
      group by cid
   ) Q2
where Q1.cid = Q2.cid;

select * from SkillGained;
/*
MayBMS U-relation representation of the query result:
  cid   | skill  | p1  | p2  |    p
--------+--------+-----+-----+----------
 Google | Segway | 0.2 | 0.4 |      0.5
 Google | Web    | 0.4 | 0.4 |        1
 Yahoo  | DB     | 0.6 | 0.6 |        1
 Yahoo  | Search | 0.4 | 0.6 | 0.666667
 Yahoo  | Web    | 0.4 | 0.6 | 0.666667
*/


select cid, skill from SkillGained where p = 1;
/*
  cid   | skill
--------+-------
 Google | Web
 Yahoo  | DB
*/


