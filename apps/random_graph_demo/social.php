<html>
<head>
<title>MayBMS: Social networks</title>
<meta http-equiv="content-type" content="text/html; charset=utf-8" />
<link rel="stylesheet" type="text/css" href="style2.css" media="screen" />

<script type="text/javascript">
	function validate_load_form(theForm) {
		if (theForm.fname.value == '') {
	    	alert("Please provide a file to load.\n");
	    	return false;
	  	}
	  	return true;
	}
	
	function validate_query_form(theForm) {
		if (theForm.method[1].checked &&
				(isNaN(theForm.epsilon1.value) || theForm.epsilon1.value == '')) {
			alert("Please provide valid epsilon for aconf.\n");
	    	return false;
		}
		if (theForm.method[2].checked &&
				(isNaN(theForm.epsilon2.value) || theForm.epsilon2.value == '')) {
			alert("Please provide valid epsilon for rconf.\n");
	    	return false;
		}
		if (theForm.method[1].checked &&
				(isNaN(theForm.delta.value) || theForm.delta.value <= 0 || theForm.delta.value >= 1)) {
	    	alert("delta must be in (0,1).\n");
	    	return false;
	  	}
		if (theForm.method[1].checked &&
				(isNaN(theForm.epsilon1.value) || theForm.epsilon1.value <= 0 || theForm.epsilon1.value >= 1)) {
	    	alert("epsilon must be in (0,1).\n");
	    	return false;
	  	}
		if (theForm.method[2].checked &&
				(isNaN(theForm.epsilon2.value) || theForm.epsilon2.value <= 0 || theForm.epsilon2.value >= 1)) {
	    	alert("epsilon must be in (0,1).\n");
	    	return false;
	  	}		
		// alert("query type" + theForm.query.value + ", node:" + theForm.node_name.value);
		if (theForm.query[4].checked && theForm.node_name.value == '') {
	    	alert("Please provide a start node for the query.\n");
	    	return false;
	  	}
	  	return true;
	}
</script>

</head>
<body>

<div align="center">
<h1><a href="social.php"><img src="img/maybms_logo.jpg" alt="MayBMS" /></a></h1>
<h1>MayBMS: <a href="index.php">Random graphs</a> and Social networks</h1>
</div>


<?php
// change the connection string with the server, db name and username
$conn_string = "host=localhost dbname=postgres user=lantova";
$query_type = array(0 => "Existence of a triangle",
1 => "Existence of a four-clique",
2 => "Existence of a path of length three",
3 => "Pairs of nodes within 4 degrees of separation",
4 => "Nodes within two degrees of separation from node",
5 => "Nodes connected to at least three other nodes with probability >= 0.8",
6 => "Nodes that are not directly connected but share at least two neighbors",
7 => "All triangles with probability >= 0.8");

function init_connection($conn_string) {
	$conn = pg_connect($conn_string);
	return $conn;
}

function cleanup_tables($conn) {
	// drop tables and supress any possible errors
	$result = @pg_query($conn, "drop table node cascade;");
	$result = @pg_query($conn, "drop table edge cascade;");
	$result = @pg_query($conn, "drop table edge2 cascade;");
	$result = @pg_query($conn, "drop table no_edge cascade;");
	$result = @pg_query($conn, "drop table total_order cascade;");
	$result = @pg_query($conn, "drop table edges_temp cascade;");
	$result = @pg_query($conn, "drop table clauses0 cascade;");
	$result = @pg_query($conn, "drop table clauses1 cascade;");
	$result = @pg_query($conn, "drop table D cascade;");
	$result = @pg_query($conn, "drop table W cascade;");
	$result = @pg_query($conn, "drop sequence cid_seq;");
	@pg_query($conn, "drop table total_order_not_null");
}

function load_sn_graph($conn, $fname) {
	cleanup_tables($conn);
	pg_query($conn, "create table node(node text);");
	pg_query($conn, "create table total_order(u text, v text, p float, bit int default 1);");

	// copy edge data from file
	pg_query($conn, "copy total_order(u,v,p) from '$fname' with null as '';");
	pg_query($conn, "insert into total_order select u,v,1-p,0 as bit
						 from total_order where v is not null and 1-p>0;");
	
	pg_query($conn, "insert into node select u from total_order
						 union select v from total_order;");
	pg_query($conn, "insert into total_order select n1.node as u,n2.node as v,1 as p,0 as bit
						 from node n1, node n2 
						 where n1.node < n2.node and
						 		(n1.node, n2.node) not in (select u,v from total_order);");
	// create U-relations
	pg_query($conn, "create table total_order_not_null as
						 select * from total_order where v is not null;");

	$edges_query = "create table edges_temp as ";
	$edges_query .= "repair key u,v in (select u,v,p,bit from total_order_not_null) weight by p;";
	pg_query($conn, $edges_query);

	pg_query($conn, "drop table total_order_not_null");

	$edges_query = "create table edge as select u,v from edges_temp
						where bit=1";
	$edges_query .= " union all select v,u from edges_temp where bit=1;";
	pg_query($conn, $edges_query);

	$edges_query = "create table edge2 as repair key u in (select node as u,node as v from node);";
	pg_query($conn, $edges_query);
	
	$edges_query = "insert into edge2 select * from edge;";
	pg_query($conn, $edges_query);
	
	$edges_query = "create table no_edge as select u,v from edges_temp
						where bit=0";
	$edges_query .= " union all select v,u from edges_temp where bit=0;";
	pg_query($conn, $edges_query);

	return true;
}

function run_aconf($conn, $query, $args, $epsilon, $delta) {
	// TODO: add more types of queries
	if ($query == 0) {
		// triangle
		$aconf_query = "select aconf($epsilon,$delta) as triangle_prob";
		$aconf_query .= " from  edge e1,edge e2,edge e3
				   			where  e1.v = e2.u and e2.v = e3.u and e1.u = e3.v
							and e1.u < e2.u and e2.u < e3.u;";		
		$result = pg_query($conn, $aconf_query);
		// return result
		// return pg_fetch_result($result, 0, 0);
		return $result;
	}
	else if ($query == 1) {
		// four-clique
		$aconf_query = "select aconf($epsilon,$delta) as fourclique_prob";
		$aconf_query .= " from  edge e1,edge e2,edge e3,edge e4,edge e5,edge e6";
		$aconf_query .= " where  e1.v = e2.u and e2.v = e3.u and e1.u = e4.u
								and e4.v = e2.v";
		$aconf_query .= " and e5.u = e2.u and e5.v = e3.v and e6.u = e1.u
								and e6.v = e3.v";
		$aconf_query .= " and e1.u < e2.u and e2.u < e3.u and e3.u < e3.v;";
		$result = pg_query($conn, $aconf_query);
		// return pg_fetch_result($result, 0, 0);
		return $result;
	}
	else if ($query == 2) {
		// path of length 3
		$aconf_query = "select aconf($epsilon,$delta) as path_prob";
		$aconf_query .= " from  edge e1,edge e2,edge e3,
									no_edge ne4,no_edge ne5,no_edge ne6";
		$aconf_query .= " where e1.v = e2.u and e2.v = e3.u
								and ne4.u = e1.u and ne4.v = e2.v";
		$aconf_query .= " and ne5.u = e1.u and ne5.v = e3.v and ne6.u = e2.u
								and ne6.v = e3.v;";
		$result = pg_query($conn, $aconf_query);
			
		// return pg_fetch_result($result, 0, 0);
		return $result;
	}
	else if ($query == 3) {
		// pairs of nodes within 4 degrees of separation
		$aconf_query = "select e1.u,e4.v,aconf($epsilon,$delta) as aconf";
		$aconf_query .= " from edge2 e1,edge2 e2,edge2 e3, edge2 e4
							 where e1.v = e2.u and e2.v = e3.u and e3.v = e4.u and e1.u < e4.v
							group by e1.u,e4.v";
		return pg_query($conn, $aconf_query);
	}
	else if ($query == 4) {
		// Nodes within two degrees of separation from given node
		$n = $args['node_name'];
		$aconf_query = "select e2.v, aconf($epsilon, $delta)
							from edge e1, edge e2
							where e1.u = '$n' and e1.v = e2.u and e1.u <> e2.v
							group by e2.v;";
		return pg_query($conn, $aconf_query);
	}
	else if ($query == 5) {
		// Nodes connected to at least 3 other nodes with probability >= 0.8
		$aconf_query = "select * from (
							select e1.u, aconf($epsilon,$delta) as aconf
							from edge e1, edge e2, edge e3
							where e1.u = e2.u and e2.u = e3.u
									and e1.v < e2.v and e2.v < e3.v
							group by e1.u) r
							where aconf >= 0.8;";
		return pg_query($conn, $aconf_query);
	}
	else if ($query == 6) {
		// Nodes that are not directly connected but share at least 2 neighbors
		$aconf_query = "select e1.u,e2.v, aconf($epsilon, $delta) as aconf
							from edge e1, edge e2, edge e3, edge e4, no_edge ne
							where e1.u = e3.u and e2.v = e4.v and e1.v = e2.u and e3.v = e4.u and e1.v < e3.v
							and e1.u = ne.u and e2.v = ne.v and e1.u <> e2.v
							group by e1.u,e2.v;";
		return pg_query($conn, $aconf_query);
	}
	else if ($query == 7) {
		// All triangles with probability >= 0.8
		$aconf_query = "select * from (
							select e1.u, e1.v, e2.v as w, aconf($epsilon,$delta) as aconf
							from  edge e1, edge e2, edge e3
							where e1.v = e2.u and e2.v = e3.v and e1.u = e3.u
										and e1.u < e2.u and e2.u < e3.v
							group by e1.u, e1.v, e2.v) r
							where aconf > 0.8;";
		return pg_query($conn, $aconf_query);
	}
}

function run_rconf($conn, $query, $args, $epsilon) {
	// TODO: add more types of queries
	if ($query == 0) {
		// triangle
		$aconf_query = "select conf('R',$epsilon) as triangle_prob";		
		$aconf_query .= " from  edge e1,edge e2,edge e3
				   			where  e1.v = e2.u and e2.v = e3.u and e1.u = e3.v
							and e1.u < e2.u and e2.u < e3.u;";		
		$result = pg_query($conn, $aconf_query);
		// return result
		// return pg_fetch_result($result, 0, 0);
		return $result;
	}
	else if ($query == 1) {
		// four-clique
		$aconf_query = "select conf('R',$epsilon) as fourclique_prob";
		$aconf_query .= " from  edge e1,edge e2,edge e3,edge e4,edge e5,edge e6";
		$aconf_query .= " where  e1.v = e2.u and e2.v = e3.u and e1.u = e4.u
								and e4.v = e2.v";
		$aconf_query .= " and e5.u = e2.u and e5.v = e3.v and e6.u = e1.u
								and e6.v = e3.v";
		$aconf_query .= " and e1.u < e2.u and e2.u < e3.u and e3.u < e3.v;";
		$result = pg_query($conn, $aconf_query);
		// return pg_fetch_result($result, 0, 0);
		return $result;
	}
	else if ($query == 2) {
		// path of length 3
		$aconf_query = "select conf('R',$epsilon) as path_prob";
		$aconf_query .= " from  edge e1,edge e2,edge e3,
									no_edge ne4,no_edge ne5,no_edge ne6";
		$aconf_query .= " where e1.v = e2.u and e2.v = e3.u
								and ne4.u = e1.u and ne4.v = e2.v";
		$aconf_query .= " and ne5.u = e1.u and ne5.v = e3.v and ne6.u = e2.u
								and ne6.v = e3.v;";
		$result = pg_query($conn, $aconf_query);
			
		// return pg_fetch_result($result, 0, 0);
		return $result;
	}
	else if ($query == 3) {
		// pairs of nodes within 4 degrees of separation
		$aconf_query = "select e1.u,e4.v,conf('R',$epsilon) as aconf";
		$aconf_query .= " from edge2 e1,edge2 e2,edge2 e3, edge2 e4
							 where e1.v = e2.u and e2.v = e3.u and e3.v = e4.u and e1.u < e4.v
							group by e1.u,e4.v";
		return pg_query($conn, $aconf_query);
	}
	else if ($query == 4) {
		// Nodes within two degrees of separation from given node
		$n = $args['node_name'];
		$aconf_query = "select e2.v, conf('R',$epsilon)
							from edge e1, edge e2
							where e1.u = '$n' and e1.v = e2.u and e1.u <> e2.v
							group by e2.v;";
		return pg_query($conn, $aconf_query);
	}
	else if ($query == 5) {
		// Nodes connected to at least 3 other nodes with probability >= 0.8
		$aconf_query = "select * from (
							select e1.u, conf('R',$epsilon) as aconf
							from edge e1, edge e2, edge e3
							where e1.u = e2.u and e2.u = e3.u
									and e1.v < e2.v and e2.v < e3.v
							group by e1.u) r
							where aconf >= 0.8;";
		return pg_query($conn, $aconf_query);
	}
	else if ($query == 6) {
		// Nodes that are not directly connected but share at least 2 neighbors
		$aconf_query = "select e1.u,e2.v, conf('R',$epsilon) as aconf
							from edge e1, edge e2, edge e3, edge e4, no_edge ne
							where e1.u = e3.u and e2.v = e4.v and e1.v = e2.u and e3.v = e4.u and e1.v < e3.v
							and e1.u = ne.u and e2.v = ne.v and e1.u <> e2.v
							group by e1.u,e2.v;";
		return pg_query($conn, $aconf_query);
	}
	else if ($query == 7) {
		// All triangles with probability >= 0.8
		$aconf_query = "select * from (
							select e1.u, e1.v, e2.v as w, conf('R',$epsilon) as aconf
							from  edge e1, edge e2, edge e3
							where e1.v = e2.u and e2.v = e3.v and e1.u = e3.u
										and e1.u < e2.u and e2.u < e3.v
							group by e1.u, e1.v, e2.v) r
							where aconf > 0.8;";
		return pg_query($conn, $aconf_query);
	}
}

function run_conf($conn, $query, $args) {
	// TODO: add more types of queries
	if ($query == 0) {
		// triangle
		$conf_query = "select conf() as triangle_prob";
		$conf_query .= " from  edge e1, edge e2, edge e3";
		$conf_query .= " where  e1.v = e2.u and e2.v = e3.v and e1.u = e3.u
			and e1.u < e2.u and e2.u < e3.v;";
		$result = pg_query($conn, $conf_query);
		// return result conf
		// return pg_fetch_result($result, 0, 0);
		return $result;
	}
	else if ($query == 1) {
		// four-clique
		$conf_query = "select conf() as fourclique_prob";
		$conf_query .= " from edge e1,edge e2,edge e3,edge e4,edge e5,edge e6";
		$conf_query .= " where e1.v = e2.u and e2.v = e3.u and e1.u = e4.u
								and e4.v = e2.v";
		$conf_query .= " and e5.u = e2.u and e5.v = e3.v and e6.u = e1.u
							 and e6.v = e3.v";
		$conf_query .= " and e1.u < e2.u and e2.u < e3.u and e3.u < e3.v;";
		$result = pg_query($conn, $conf_query);
		// return result aconf
		// return pg_fetch_result($result, 0, 0);
		return $result;
	}
	else if ($query == 2) {
		// path of length 3
		$conf_query = "select conf() as path_prob";
		$conf_query .= " from  edge e1,edge e2,edge e3,
									no_edge ne4,no_edge ne5,no_edge ne6";
		$conf_query .= " where e1.v = e2.u and e2.v = e3.u
								and ne4.u = e1.u and ne4.v = e2.v";
		$conf_query .= " and ne5.u = e1.u and ne5.v = e3.v and ne6.u = e2.u
								and ne6.v = e3.v;";
		$result = pg_query($conn, $conf_query);
		// return pg_fetch_result($result, 0, 0);
		return $result;
	}
	else if ($query == 3) {
		// pairs of nodes within 4 degrees of separation
		$conf_query = "select e1.u,e4.v,conf() as aconf";
		$conf_query .= " from edge2 e1,edge2 e2,edge2 e3, edge2 e4
							 where e1.v = e2.u and e2.v = e3.u and e3.v = e4.u and e1.u < e4.v
							group by e1.u,e4.v";
		return pg_query($conn, $conf_query);
	}
	else if ($query == 4) {
		// Nodes within two degrees of separation from given node
		$n = $args['node_name'];
		$conf_query = "select e2.v, conf()
							from edge e1, edge e2
							where e1.u = '$n' and e1.v = e2.u and e1.u <> e2.v
							group by e2.v;";
		return pg_query($conn, $conf_query);
	}
	else if ($query == 5) {
		$conf_query = "select * from (
							select e1.u, conf() as conf
							from edge e1, edge e2, edge e3
							where e1.u = e2.u and e2.u = e3.u
									and e1.v < e2.v and e2.v < e3.v
							group by e1.u) r
							where conf >= 0.8;";
		return pg_query($conn, $conf_query);
	}
	else if ($query == 6) {
		$conf_query = "select e1.u,e2.v, conf() as conf
							from edge e1, edge e2, edge e3, edge e4, no_edge ne
							where e1.u = e3.u and e2.v = e4.v and e1.v = e2.u and e3.v = e4.u and e1.v < e3.v
							and e1.u = ne.u and e2.v = ne.v and e1.u <> e2.v
							group by e1.u,e2.v;";
		return pg_query($conn, $conf_query);
	}
	else if ($query == 7) {
		$conf_query = "select * from (
							select e1.u, e1.v, e2.v as w, conf() as conf
							from  edge e1, edge e2, edge e3
							where e1.v = e2.u and e2.v = e3.v and e1.u = e3.u
										and e1.u < e2.u and e2.u < e3.v
							group by e1.u, e1.v, e2.v) r
							where conf > 0.8;";
		return pg_query($conn, $conf_query);
	}
}

// get number of nodes of the graph in the database
function get_num_nodes($conn) {
	$query = "select count(*) as num_nodes from node;";
	$result = @pg_query($conn, $query);
	// return number of nodes
	if (!$result) {
		return -1;
	}
	return pg_fetch_result($result, 0, 0);
}

// get number of edges of the graph in the database
function get_num_edges($conn) {
	$query = "select count(*) as num_edges from
					(select possible u,v from edge) r;";
	$result = @pg_query($conn, $query);
	if (!$result)
	return -1;
	// return number of edges
	return pg_fetch_result($result, 0, 0);
}

// get the edge probability of the graph in the database, or -1 if random
function get_edge_prob($conn) {
	$query = "select max(p), count(*) from
					(select possible _p0 as p from edge) r;";
	$result = @pg_query($conn, $query);
	// return number of edges
	if (!$result || pg_fetch_result($result, 0, 1) > 1)
	// random probability
	return -1;
	return pg_fetch_result($result, 0, 0);
}

// retrieve the nodes stored in the database and return them as an array
function get_nodes($conn) {
	$res = @pg_query($conn, "select distinct node from node;");
	if (!$res)
	return false;
	return pg_fetch_all_columns($res, 0);
}
?>

<div id="main-wrapper">
<div id="main" class="clear-block"><!-- -------------------------------------------
 Random graph generation
 ------------------------------------------- -->

<div class="node sticky">
<h1>Load social network graph</h1>

<?php
$conn = @init_connection($conn_string);
echo "<form enctype=\"multipart/form-data\" name=\"social_network\"
			action=\"social.php\" method=\"post\" 
			onsubmit=\"return validate_load_form(this)\">
		 <table>
		 <input type=\"hidden\" name=\"MAX_FILE_SIZE\" value=\"300000\" />
		 <tr>
		 		<td>Choose file:</td>
		 		<td><input type=\"file\" size=\"40\" name=\"fname\"/>
		 	</tr>
		 	<tr>
		 		<td><input type=\"submit\" value=\"Load graph\"/></td>
		 	</tr>
		 </table>		 
		 <input type=\"hidden\" name=\"sn\"></input>	
		</form>";
?></div>

<div class="node sticky"><!-- -------------------------------------------
 Query options
 ------------------------------------------- -->
<h1>Query options</h1>
<?php

if (isset($_GET['query']) && $_GET['query'] != "")
$query = $_GET['query'];
else $query = 0;

if (isset($_GET['method']) && $_GET['method'] != "")
$method =  $_GET['method'];
else $method = 1;

if (isset($_GET['epsilon1']) && $_GET['epsilon1'] != "")
$epsilon1 =  $_GET['epsilon1'];
else $epsilon1 = 0.05;

if (isset($_GET['epsilon2']) && $_GET['epsilon2'] != "")
$epsilon2 =  $_GET['epsilon2'];
else $epsilon2 = 0.05;

if (isset($_GET['delta']) && $_GET['delta'] != "")
$delta =  $_GET['delta'];
else $delta = 0.05;


if (isset($_GET['node_name']) && $_GET['node_name'] != "")
$node_name =  $_GET['node_name'];

if (isset($_GET['refine_aconf']) || !isset($_GET['q'])) {
	$refine_aconf = true;
}
$q = array(0 => "", 1 => "", 2 => "", 3 => "", 4 => "", 5 => "", 6 => "", 7 => "");
$q[$query] = "checked";

$m = array(0 => "", 1 => "", 2 => "");
$m[$method] = "checked";

if ($refine_aconf)
$checked = "checked";
else $checked = "";

$nodes = get_nodes($conn);

echo "<form name=\"query\" action=\"social.php\" method=\"get\" onsubmit=\"return validate_query_form (this)\";>
		 <table>
		 	<tr>
		 		<td><label>Query:</label></td>
		 		<td>
			 		<input type=\"radio\" name=\"query\" value=\"0\" $q[0]>
			 		$query_type[0]
			 	</td>
			</tr>
			<tr><td/>
			 	<td>
			 		<input type=\"radio\" name=\"query\" value=\"1\" $q[1]>
			 		$query_type[1]
			 	<td>
			</tr>
			<!--
			<tr><td/>
			 	<td>
			 		<input type=\"radio\" name=\"query\" value=\"2\" $q[2]>
			 		$query_type[2]
			 	</td>
			 </tr>
			 -->			 	 
			<tr><td/>
			 	<td>
			 		<input type=\"radio\" name=\"query\" value=\"3\" $q[3]>
			 		$query_type[3]
			 	</td>
			 </tr>
			<tr><td/>
			 	<td>
			 		<input type=\"radio\" name=\"query\" value=\"4\" $q[4]>
			 		$query_type[4] ";
			 		//<input type=\"text\" name=\"node_name\" size=\"5\" value=\"$node_name\"/>

			 		echo	 	"<select name=\"node_name\">";
			 		foreach($nodes as $i => $node) {
			 			echo "<option value=\"$node\">$node</option>";
			 		}
			 		echo 		"</select>
			 	</td>
			 </tr>			 
			 <tr>
			 	<td/>
			 	<td>
			 		<input type=\"radio\" name=\"query\" value=\"5\" $q[5]>
			 		$query_type[5]
			 	</td>
			 </tr>
			 <tr>
			 	<td/>
			 	<td>
			 		<input type=\"radio\" name=\"query\" value=\"6\" $q[6]>
			 		$query_type[6]
			 	</td>
			 </tr>
			 <tr>
			 	<td/>
			 	<td>
			 		<input type=\"radio\" name=\"query\" value=\"7\" $q[7]>
			 		$query_type[7]
			 	</td>
			 </tr>
			 
			 </table>";
			echo "<table
		 	<tr>
		 		<td><label>Method:</label></td>
		 		<td>
		 		<input type=\"radio\" name=\"method\" value=\"0\" $m[0]>conf (exact confidence)
		 		</td>
		 	</tr>
		 	<tr><td/>
		 		<td>		 	
		 		<input type=\"radio\" name=\"method\" value=\"1\" $m[1]>aconf (Karb-Luby approximation)
		 		</td>
		 		<td>epsilon:</td>
			 	<td><input type=\"text\" size=\"4\" name=\"epsilon1\" value=\"$epsilon1\"/></td>
			 	<td>delta:</td>
			 	<td><input type=\"text\" size=\"4\" name=\"delta\" value=\"$delta\"/></td>
		 	</tr>
		 	<tr><td/>
		 		<td>
		 		<input type=\"radio\" name=\"method\" value=\"2\" $m[2]>rconf (heuristic confidence with fixed relative error)
			 	</td>
			 	<td>epsilon:</td>
			 	<td><input type=\"text\" size=\"4\" name=\"epsilon2\" value=\"$epsilon2\"/></td>
			 </tr>
			 <tr>			 	
			 	<td/>
			 	<td><input type=\"checkbox\" name=\"refine_aconf\" $checked/>
		 		Refine aconf computation</td>
		 	</tr>
			 <tr>
		 		<td><input type=\"submit\"  value=\"Run query\"/></td>
		 	</tr>
		 </table>
		 <input type=\"hidden\" name=\"q\"></input>	
		</form>";
			 		?></div>
<!-- -------------------------------------------
 Display result
 ------------------------------------------- -->
<div class="node sticky">
<h1>Result</h1>
			 		<?php
			 		if (isset($_POST['sn'])) {	// load social network
			 			$start = gettimeofday(true);
			 			$conn = @init_connection($conn_string);
			 			if (!$conn) {
			 				echo "Error: Could not establish connection to MayBMS server.";
			 				return;
			 			}
			 			// move file from temp location to uploads location
			 			$uploaddir = '/var/www/social/uploads/';
			 			$fname = $uploaddir . basename($_FILES['fname']['name']);
			 			if (move_uploaded_file($_FILES['fname']['tmp_name'], $fname)) {
			 				$res = load_sn_graph($conn, $fname);
			 			}
			 			else {
			 				// TODO: error uploading file
			 				echo "Error uploading file";
			 			}
			 			$end = gettimeofday(true);
			 			$time = ($end - $start);
			 			echo "<table>";
			 			if ($res) {
			 				echo "<tr><td>Loaded the following graph:</td></tr>";
			 				$sn_num_nodes = get_num_nodes($conn);
			 				$sn_num_edges = get_num_edges($conn);
			 				if ($sn_num_nodes > 0 && $sn_num_edges) {
			 					echo "<tr><td>Number of nodes:</td><td>$sn_num_nodes</td>
						</tr>";
			 					echo	"<tr><td>Number of edges:</td><td>$sn_num_edges</td></tr>";
			 				}
			 				else {
			 					echo "<tr><td>error loading graph</td></tr>";
			 				}
			 			}
			 			else {
			 				echo "<tr><td>error loading graph</td></tr>";
			 			}
			 			echo "<tr><td>Time:</td><td>$time</td></tr>";
			 			echo "</table>";
			 		}
			 		else if (isset($_GET['q'])) {	// querying
			 			$conn = @init_connection($conn_string);
			 			if ($conn == FALSE) {
			 				echo "Error: Could not establish connection to MayBMS server.";
			 				return;
			 			}
			 			// run query and display result
			 			echo "<table>";
			 			echo "<tr><td><label>Query:</label></td>
				  <td><label>$query_type[$query]</label></td></tr>";
			 			$args = array(node_name => $node_name);

			 			if ($method == 1) {
			 				// run aconf
			 				// run several refinements steps
			 				if ($refine_aconf)
			 				$curr_eps = 1;
			 				else
			 				$curr_eps = 2 * $epsilon1;
			 				do {
			 					$curr_eps *= 0.5;
			 					if ($curr_eps < $epsilon1)
			 					$curr_eps = $epsilon1;
			 					$a_start = gettimeofday(true);
			 					$result = @run_aconf($conn, $query, $args, $curr_eps, $delta);
			 					if (!$result) {
			 						echo "<tr><td colspan=2>Error executing query:</td></tr>";
			 						echo "<tr><td colspan=2>";
			 						echo pg_last_error($conn);
			 						echo "</td></tr></table>";
			 						return;
			 					}
			 					// display result
			 					echo "<p><label>aconf(eps=$curr_eps, delta=$delta)</label></p>";			 					

			 					$n_attr = pg_num_fields($result);
			 					echo "<table class=\"rel\"><thead><tr>";
			 					for ($j = 0; $j < $n_attr; ++$j) {
			 						$attr = pg_field_name($result, $j);
			 						echo "<td><b>$attr</b></td>";
			 					}
			 					echo "</tr></thead>";

			 					echo "<tbody>";
			 					while($row = pg_fetch_array($result)) {
			 						echo "<tr>";
			 						for ($j = 0; $j < $n_attr; ++$j) {
	      				$value = $row[$j];
	      				echo "<td>$value</td>";
			 						}
			 						echo "</tr>";
			 					}

			 					echo "</tbody></table>";

			 					$a_end = gettimeofday(true);
			 					$a_time = ($a_end - $a_start);

			 					echo "<p><label>Time: </label><label>$a_time sec.</label></p>";
			 					echo "<hr>";
			 					flush();
			 					ob_flush();
			 				}
			 				while ($curr_eps > $epsilon1) ;
			 			}
			 			if ($method == 2) {
			 				// run heuristic aconf
			 				// run several refinements steps
			 				if ($refine_aconf)
			 				$curr_eps = 1;
			 				else
			 				$curr_eps = 2 * $epsilon2;
			 				do {
			 					$curr_eps *= 0.5;
			 					if ($curr_eps < $epsilon2)
			 					$curr_eps = $epsilon2;
			 					$a_start = gettimeofday(true);
			 					$result = @run_rconf($conn, $query, $args, $curr_eps);
			 					if (!$result) {
			 						echo "<tr><td colspan=2>Error executing query:</td></tr>";
			 						echo "<tr><td colspan=2>";
			 						echo pg_last_error($conn);
			 						echo "</td></tr></table>";
			 						return;
			 					}
			 					// display result
			 					// echo "<p><label>aconf(eps=$curr_eps, delta=$delta)</label></p>";
			 					echo "<p><label>rconf(eps=$curr_eps)</label></p>";

			 					$n_attr = pg_num_fields($result);
			 					echo "<table class=\"rel\"><thead><tr>";
			 					for ($j = 0; $j < $n_attr; ++$j) {
			 						$attr = pg_field_name($result, $j);
			 						echo "<td><b>$attr</b></td>";
			 					}
			 					echo "</tr></thead>";

			 					echo "<tbody>";
			 					while($row = pg_fetch_array($result)) {
			 						echo "<tr>";
			 						for ($j = 0; $j < $n_attr; ++$j) {
	      				$value = $row[$j];
	      				echo "<td>$value</td>";
			 						}
			 						echo "</tr>";
			 					}

			 					echo "</tbody></table>";

			 					$a_end = gettimeofday(true);
			 					$a_time = ($a_end - $a_start);

			 					echo "<p><label>Time: </label><label>$a_time sec.</label></p>";
			 					echo "<hr>";
			 					flush();
			 					ob_flush();
			 				}
			 				while ($curr_eps > $epsilon2) ;
			 			}
			 			
			 			if ($method == 0) {
			 				// run conf
			 				$c_start = gettimeofday(true);
			 				$result = @run_conf($conn, $query, $args);
			 				if (!$result) {
			 					echo "<tr><td colspan=2>Error executing query:</td></tr>";
			 					echo "<tr><td colspan=2>";
			 					echo pg_last_error($conn);
			 					echo "</td></tr></table>";
			 					return;
			 				}

			 				// display result
			 				$n_attr = pg_num_fields($result);
			 				echo "<table class=\"rel\"><thead><tr>";
			 				for ($j = 0; $j < $n_attr; ++$j) {
			 					$attr = pg_field_name($result, $j);
			 					echo "<td><b>$attr</b></td>";
			 				}
			 				echo "</tr></thead>";
			 				echo "<tbody>";
			 				while($row = pg_fetch_array($result)) {
			 					echo "<tr>";
			 					for ($j = 0; $j < $n_attr; ++$j) {
			 						$value = $row[$j];
			 						echo "<td>$value</td>";
			 					}
			 					echo "</tr>";
			 				}
			 				$c_end = gettimeofday(true);
			 				$c_time = ($c_end - $c_start);
			 					
			 				echo "</tbody></table>";
			 				echo "<p><label>Time: </label><label>$c_time sec.</label></p>";
			 				echo "<hr>";
			 				flush();
			 				ob_flush();
			 			}
			 		}
			 		?></div>

<?php

// Free resultset
@pg_free_result($result);
// Closing connection
@pg_close($conn);
?></div>
</div>
</body>
</html>
