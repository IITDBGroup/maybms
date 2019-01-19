#!/bin/sh

# Please replace '/home/jiewen/paper/db/bin' with the directory where the 
# binaries of the postgres component are installed
bin_dir=/home/jiewen/paper/db/bin

#create a database
$bin_dir/dropdb nba

$bin_dir/createdb nba

#import data 
$bin_dir/psql -d nba -f populate.sql

$bin_dir/psql -d nba -c "COPY PlayerInfo FROM '$PWD/PlayerInfo.txt' WITH DELIMITER AS '|'"

$bin_dir/psql -d nba -c "COPY ParticipationRate FROM '$PWD/Participation.txt' WITH DELIMITER AS '|'"

$bin_dir/psql -d nba -c "alter table ParticipationRate add rate real"

$bin_dir/psql -d nba -c "update ParticipationRate set rate = participation / total"

$bin_dir/psql -d nba -c "create table Participation as pick tuples from ParticipationRate with probability rate"

$bin_dir/psql -d nba -c "COPY Performance FROM '$PWD/Performance.txt' WITH DELIMITER AS '|'"

$bin_dir/psql -d nba -c "COPY Skills FROM '$PWD/Skills.txt' WITH DELIMITER AS '|'"

$bin_dir/psql -d nba -c "COPY Salary FROM '$PWD/Salary.txt' WITH DELIMITER AS '|'"

$bin_dir/psql -d nba -c "COPY FitnessTransition FROM '$PWD/FitnessTransition.txt' WITH DELIMITER AS '|'"

$bin_dir/psql -d nba -c "COPY Matches FROM '$PWD/Matches.txt' WITH DELIMITER AS '|'"

$bin_dir/psql -d nba -c "COPY Fitness FROM '$PWD/Fitness.txt' WITH DELIMITER AS '|'"


