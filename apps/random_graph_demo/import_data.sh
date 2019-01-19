#!/bin/sh

# Please replace '/home/lantova/maybms/bin/' with the directory where the 
# binaries of maybms are installed, and set user and db to the appropriate
# user and database names
psql=/home/lantova/maybms/bin/psql
user=lantova
db=postgres

# file with edge data
fname=/home/lantova/Documents/Programs/maybms/apps/random_graph_demo/datasets/karate.txt

# clean old tables
$psql -U $user -d $db -c "drop table node cascade;"
$psql -U $user -d $db -c "drop table edge cascade";
$psql -U $user -d $db -c "drop table edge2 cascade";
$psql -U $user -d $db -c "drop table no_edge cascade";
$psql -U $user -d $db -c "drop table total_order cascade";
$psql -U $user -d $db -c "drop table edges_temp cascade";
$psql -U $user -d $db -c "drop table clauses0 cascade";
$psql -U $user -d $db -c "drop table clauses1 cascade";
$psql -U $user -d $db -c "drop table D cascade";
$psql -U $user -d $db -c "drop table W cascade";
$psql -U $user -d $db -c "drop sequence cid_seq";
$psql -U $user -d $db -c "drop table total_order_not_null"

# create edge relation
$psql -U $user -d $db -c "create table node(node text)";
$psql -U $user -d $db -c "create table total_order(u text, v text, p float, bit int default 1);"

# copy edge data from file
$psql -U $user -d $db -c "copy total_order(u,v,p) from '$fname' with null as '';"
$psql -U $user -d $db -c "insert into total_order select u,v,1-p,0 as bit
						 from total_order where v is not null and 1-p>0;"


$psql -U $user -d $db -c "insert into node select u from total_order union select v from total_order;"
$psql -U $user -d $db -c "insert into total_order select n1.node as u,n2.node as v,1 as p,0 as bit
						 from node n1, node n2 
						 where n1.node < n2.node and
						 		(n1.node, n2.node) not in (select u,v from total_order);"
# create U-relations
$psql -U $user -d $db -c "create table total_order_not_null as
						 select * from total_order where v is not null;"
$psql -U $user -d $db -c "create table edges_temp as
						  repair key u,v in (select u,v,p,bit from total_order_not_null)
						  weight by p;"

$psql -U $user -d $db -c "drop table total_order_not_null;"

$psql -U $user -d $db -c "create table edge as select u,v from edges_temp
						where bit=1
						union all select v,u from edges_temp where bit=1;"

# edge2 is the edge relation that contains the all self loops
$psql -U $user -d $db -c "create table edge2 as repair key u in
						(select node as u,node as v from node);";
	
$psql -U $user -d $db -c "insert into edge2 select * from edge;"

$psql -U $user -d $db -c "create table no_edge as select u,v from edges_temp
						where bit=0
						union all select v,u from edges_temp where bit=0;"

