--test for argmax(varchar, float4) 
drop table r;
create table r( a int, b varchar, c real );

insert into r values( 1, 'b1', 0.03 );
insert into r values( 1, 'b2', 0.04 );
insert into r values( 1, 'b1', 0.01 );
insert into r values( 1, 'b3', 0.09 );
insert into r values( 1, 'b4', 0.02 );
insert into r values( 2, 'b1', 0.04 );
insert into r values( 2, 'b2', 0.02 );
insert into r values( 2, 'b3', 0.01 );
insert into r values( 2, 'b5', 0.02 );

select a, argmax( b, c ) from r group by a;
select argmax( b, c ) from r;

select a, argmax( b, c / 0.2 ) from r group by a;
select argmax( b, c / 0.2 ) from r;

--test for argmax(varchar, smallint) 
drop table r;
create table r( a int, b varchar, c smallint );

insert into r values( 1, 'b1', 3 );
insert into r values( 1, 'b2', 4 );
insert into r values( 1, 'b1', 1 );
insert into r values( 1, 'b3', 9 );
insert into r values( 1, 'b4', 2 );
insert into r values( 2, 'b1', 4 );
insert into r values( 2, 'b2', 2 );
insert into r values( 2, 'b3', 1 );
insert into r values( 2, 'b5', 2 );

select a, argmax( b, c ) from r group by a;
select argmax( b, c ) from r;

--test for argmax(varchar, integer) 
drop table r;
create table r( a int, b varchar, c integer );

insert into r values( 1, 'b1', 3 );
insert into r values( 1, 'b2', 4 );
insert into r values( 1, 'b1', 1 );
insert into r values( 1, 'b3', 9 );
insert into r values( 1, 'b4', 2 );
insert into r values( 2, 'b1', 4 );
insert into r values( 2, 'b2', 2 );
insert into r values( 2, 'b3', 1 );
insert into r values( 2, 'b5', 2 );

select a, argmax( b, c ) from r group by a;
select argmax( b, c ) from r;

--test for argmax(varchar, bigint) 
drop table r;
create table r( a int, b varchar, c bigint );

insert into r values( 1, 'b1', 3 );
insert into r values( 1, 'b2', 4 );
insert into r values( 1, 'b1', 1 );
insert into r values( 1, 'b3', 9 );
insert into r values( 1, 'b4', 2 );
insert into r values( 2, 'b1', 4 );
insert into r values( 2, 'b2', 2 );
insert into r values( 2, 'b3', 1 );
insert into r values( 2, 'b5', 2 );

select a, argmax( b, c ) from r group by a;
select argmax( b, c ) from r;

drop table r;

