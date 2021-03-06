/* A number of identical temperature sensors measure (with independent error)
   the same temperature. The measurements cannot be read out individually
   but only as certain aggregates.
   What is the posterior for the temperature given the evidence in
   the form of such aggregate measurements?

   This is of course a very simplistic example, but it demonstrates
   how interesting correlation structures can be generated by queries
   from classical relational databases.
*/


drop table Temperature;
drop table Sensors;
drop table Delta_t;
drop table Evidence;
drop table T;
drop table Q;
drop table EvidenceViolations;


/* sensor ids of a number of temperature sensors, all of the same kind */
create table Sensors (sid integer);
insert into  Sensors values (1);
insert into  Sensors values (2);
insert into  Sensors values (3);

create table Delta_t (delta integer, p float);
insert into  Delta_t values (-1, .25);
insert into  Delta_t values ( 0, .50);
insert into  Delta_t values ( 1, .25);


/* temperature prior: uniformly distributed. */
create table Temperature (t float);
insert into  Temperature values (6.5);
insert into  Temperature values (7.0);
insert into  Temperature values (7.5);
insert into  Temperature values (8.0);
insert into  Temperature values (8.5);


/* We only see aggregate sensor readings si + sj = v,
   i.e. the sum of the values read by sensors si and sj is v.
   We cannot observe the sensor readings individually.
*/
create table Evidence (si integer, sj integer, v real);
insert into  Evidence values (1, 2, 14.0);
insert into  Evidence values (2, 3, 15.0);



create table T as
(
   repair key dum in (select 1 as dum, t from Temperature)
);

create table Q as
(
   repair key sid in (select * from Sensors, Delta_t)
   weight by p
);


create table EvidenceViolations as
select t
from   Q Q1, Q Q2, Evidence E, T
where  Q1.sid = E.si and Q2.sid = E.sj
and    Q1.delta + Q2.delta + 2 * T.t <> v;


select t, conf() as Prior from T group by t;
/*
  t  | prior
-----+-------
 6.5 |   0.2
   7 |   0.2
 7.5 |   0.2
   8 |   0.2
 8.5 |   0.2
*/

select S1.t, cast((P1-P2)/(1-P3) as real) as Posterior
from (select t, conf() as P1 from T group by t) S1,
     (select t, conf() as P2 from EvidenceViolations group by t) S2,
     (select conf() as P3 from EvidenceViolations) S3
where S1.t = S2.t;
/*
  t  | posterior
-----+-----------
 6.5 |     0.125
   7 |     0.375
 7.5 |     0.375
   8 |     0.125
 8.5 |         0
*/


