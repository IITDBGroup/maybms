
/* A medical example representing for assigning diagnosis based on symptoms.

   Given is a database listing diseases and their accompanying symptoms. For
   each disease the database contains possible treatments that can be
   administered by a physician.
   Each treatment can be effective up to a certain degree, if no improvement is
   observed in the patient, this can indicate either an incorrect diagnosis,
   or that the treatment has no effect on the patient for other reasons.

   Assume a patient complains from a set of symptoms. Based on initial
   observations of the symptoms, select the most probable diagnosis and start
   treatment for it. Then, reconsider the diagnosis based on the outcomes of
   the treatment.

*/

drop table Diseases;
drop table Diagnosis;
drop table Treatment_0;
drop table Treatment_1;
drop table Treatment;
drop table Treatment_results;
drop table Treatment_violations;
drop table Q;


create table Diseases (disease varchar, symptom varchar, freq float);

/* for each disease, list the symptoms of that disease together with the
frequency with which they occur */
insert into Diseases values ('migraine' , 'nausea'     , 0.4);
insert into Diseases values ('migraine' , 'headache'   , 0.5);
insert into Diseases values ('migraine' , 'unknown'    , 0.1);
insert into Diseases values ('gastritis', 'nausea'     , 0.2);
insert into Diseases values ('gastritis', 'indigestion', 0.6);
insert into Diseases values ('gastritis', 'unknown'    , 0.2);


/* create the possible diagnosis, assuming each symptom determines one
 possible disease */
create table Diagnosis as
select disease, symptom
from (repair key symptom in Diseases weight by freq) r;


select * from Diagnosis;
/*

  disease  |   symptom   | _v0 | _d0 |   _p0    
-----------+-------------+-----+-----+----------
 migraine  | headache    |  30 |  48 |        1
 gastritis | indigestion |  31 |  46 |        1
 migraine  | nausea      |  32 |  45 | 0.666667
 gastritis | nausea      |  32 |  44 | 0.333333
 migraine  | unknown     |  29 |  43 | 0.333333
 gastritis | unknown     |  29 |  47 | 0.666667
(6 rows)

 */


/* Suppose the patient suffers from nausea. What are the possible diagnoses,
ranked by confidence */
select   disease, conf()
from     Diagnosis
where    symptom = 'nausea'
group by disease
order by conf desc;

/*

  disease  |   conf   
-----------+----------
 migraine  | 0.666667
 gastritis | 0.333333
(2 rows)

*/


/* Treatment options for the diseases together with their effectiveness.
For example migraine can be treated with paracetamol with 60% effectiveness.
*/
create table Treatment_0 (disease varchar, treat varchar, effectiveness float);
insert into  Treatment_0 values ('migraine',  'paracetamol', 0.6);
insert into  Treatment_0 values ('gastritis', 'pca',         0.7);

create table Treatment_1 as 
(
   select disease, treat, cast(true as boolean) as result, effectiveness
   from Treatment_0
   union
   select disease, treat, cast(false as boolean) as result,
          1-effectiveness as effectiveness from Treatment_0
);


select * from Treatment_1;
/*
  disease  |    treat    | result | effectiveness 
-----------+-------------+--------+---------------
 gastritis | pca         | f      |           0.3
 gastritis | pca         | t      |           0.7
 migraine  | paracetamol | f      |           0.4
 migraine  | paracetamol | t      |           0.6
(4 rows)

*/


/* possible outcomes of treatment */
create table Treatment as
select disease, treat, result from
(
   repair key disease, treat in Treatment_1
   weight by effectiveness
) p;


select * from Treatment;
/*

  disease  |    treat    | result | _v0 | _d0 | _p0 
-----------+-------------+--------+-----+-----+-----
 gastritis | pca         | f      |  33 |  51 | 0.3
 gastritis | pca         | t      |  33 |  52 | 0.7
 migraine  | paracetamol | f      |  34 |  50 | 0.4
 migraine  | paracetamol | t      |  34 |  49 | 0.6
(4 rows)

*/


/* The patient is treated paracetamol, but no improvement is observed */
create table Treatment_results (treat varchar, result  boolean);
insert into  Treatment_results values ('paracetamol', 'f');


create table Treatment_violations as
select t1.disease, t1.treat
from   Treatment t1, Treatment_results t2
where  t1.treat = t2.treat and t1.result = t2.result;


select * from Treatment_violations;
/*

 disease  |    treat    | _v0 | _d0 | _p0 
----------+-------------+-----+-----+-----
 migraine | paracetamol |  34 |  50 | 0.4
(1 row)

*/


/*
Compute the possible diseases with their confidence,
given the results of the treatment.
*/
create table Q as
select Q1.disease, cast((p1-p2)/(1-p3) as real) as p
from   (select disease, conf() as p1 from Diagnosis
        where symptom='nausea' group by disease) Q1,
       (
          (
             select disease, conf() as P2 from Treatment_violations
             group by disease
          )
          union 
          (
             (select disease, 0 as p2 from Diseases)
             except
             (select possible disease, 0 as p2 from Treatment_Violations)
          )
       ) Q2,
       (select conf() as p3 from Treatment_Violations) Q3
where  Q1.disease = Q2.disease;


select * from Q
order by p desc;
/*

  disease  |    p     
-----------+----------
 gastritis | 0.555556
 migraine  | 0.444444
(2 rows)

*/

