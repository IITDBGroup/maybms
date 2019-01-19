create sequence serial increment -1 maxvalue 10000;

create table PlayerInfo( team varchar, player varchar, dob varchar, height varchar, weight varchar );

create table ParticipationRate( team varchar, player varchar, total real, participation real );

create table Performance( player varchar, match_date date, match varchar, points int );

create table Skills( team varchar, player varchar, skill varchar, has varchar );

create table Salary( player varchar, salary int );

create table FitnessTransition( player varchar, play_a_match varchar, InitState varchar, FinalState varchar, P real );

create table Matches( team1 varchar, team2 varchar );

create table Fitness( player varchar, fitness varchar );

