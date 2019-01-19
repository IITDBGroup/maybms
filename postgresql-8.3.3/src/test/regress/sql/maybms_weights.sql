--test for weights in repair-key

create table r(a int, b int, c int, w int);

insert into r values (1,1,1,0); /* This should not be selected */
insert into r values (2,1,1,1);
insert into r values (2,1,2,1);
insert into r values (3,1,1,0); /* This should not be selected */
insert into r values (1,1,2,2);

repair key a,b in r weight by w;

drop table r;

create table r(a int, b int, c int, w real);

insert into r values (1,1,1,0.0); /* This should not be selected */
insert into r values (1,1,2,0.5);
insert into r values (2,1,1,0);   /* This should not be selected */
insert into r values (1,1,1,1);
insert into r values (1,1,1,0.000); /* This should not be selected */
insert into r values (1,1,1,-0.000); /* This should not be selected */

repair key a,b in r weight by w;

drop table r;

create table r(a int, b int, c int, w real);

insert into r values (1,1,1,0.1);
insert into r values (1,1,2,0.5);
insert into r values (2,1,1,0);
insert into r values (1,1,1,-1); /* This will cause an error message */
insert into r values (1,1,1,0.000);
insert into r values (1,1,1,-0.000);

repair key a,b in r weight by w;

drop table r;
