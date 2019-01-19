--test for constant column in union

create table Census_SSN_0           (tid integer, ssn integer, p float);

create table Census_SSN as
   repair key (tid) in Census_SSN_0 weight by p;

create table FD_Violations as
select S1.ssn
from   Census_SSN S1, Census_SSN S2
where  S1.tid <> S2.tid and S1.ssn = S2.ssn; 

/* Bug: There is an error if you remove "as p2" below (twice).
   That's ok, but the error message is inappropriate:
   "ERROR:  DISTINCT ON position 0 is not in select list"
*/
(select ssn, 0 from Census_SSN_0)
except
(select possible ssn, 0 from FD_Violations);

drop table Census_SSN;
drop table Census_SSN_0;
drop table FD_Violations;
