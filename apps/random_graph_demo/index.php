<html>
<head>
	<title>Random graphs</title>
	<meta http-equiv="content-type" content="text/html; charset=utf-8" /> 
    <link rel="stylesheet" type="text/css" href="style2.css" media="screen" /> 
    
    <script type="text/javascript">
		function validate_generate_form(theForm) {
			if (theForm.nodes.value == '' || isNaN(theForm.nodes.value)
					|| theForm.nodes.value <= 0) {
		    	alert("Please provide a valid number of nodes.\n");
		    	return false;
		  	}
		  	
			if (theForm.rand_prob.checked == false &&
					(theForm.prob.value == '' || isNaN(theForm.prob.value)					
					|| theForm.prob.value <= 0 || theForm.prob.value > 1)) {
		    	alert("Please provide a valid probability for the edges.\n");
		    	return false;
		  	}

			if (theForm.delete_prob.value == '' || isNaN(theForm.delete_prob.value)
					|| theForm.delete_prob.value < 0 || theForm.delete_prob.value > 1) {
		    	alert("Please provide a valid delete probability.\n");
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
		  	return true;
		}
	</script>
	
</head>
<body>

<div align="center">
<h1><a href="index.php"><img src="img/maybms_logo.jpg" alt="MayBMS" /></a></h1> 
<h1>MayBMS: Random graphs and <a href="social.php">Social networks</a></h1> 
</div> 

 
<?php
	// change the connection string with the server, db name and username
	$conn_string = "host=localhost dbname=postgres user=lantova";
	$query_types = array(0 => "triangle", 1 => "four-clique",
						 2 => "path of length three");
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
	}
	
	function generate_graph($conn, $num_nodes, $edge_prob, $random_prob,
							$delete_prob) {
		cleanup_tables($conn);
		pg_query($conn, "create table node (n integer);") or die('Query failed: ' . pg_last_error());
		
		// create nodes
		pg_prepare($conn, "nodes_q", "insert into node select $1");
		for ($i = 1; $i <= $num_nodes; $i++) {
			pg_execute($conn, "nodes_q", array($i));
		}
		
		// create edges
		$edges_query = "create table total_order as";
		if ($random_prob)
			$edges_query .= " select n1.n as u, n2.n as v,1 as bit,random() as p
							  from node n1, node n2 where n1.n < n2.n;";			
		else
			$edges_query .= " select n1.n as u,n2.n as v,1 as bit,$edge_prob as p
							from node n1, node n2 where n1.n < n2.n;";
			
		$edges_query .= "insert into total_order
						 select u,v,0 as bit, 1-p from total_order where 1-p>0;";
		pg_query($conn, $edges_query);
		// delete random edges
		if ($delete_prob > 0) {
			pg_query($conn, "delete from total_order where random()<$delete_prob;");
		}
		
		$edges_query = "create table edges_temp as ";
		$edges_query .= "repair key u,v in total_order weight by p;";		 
		pg_query($conn, $edges_query);
		
		$edges_query = "create table edge as select u,v from edges_temp
						where bit=1";
		$edges_query .= " union all select v,u from edges_temp where bit=1;";
		pg_query($conn, $edges_query);
		
		$edges_query = "create table no_edge as select u,v from edges_temp
						where bit=0";
		$edges_query .= " union all select v,u from edges_temp where bit=0;";
		pg_query($conn, $edges_query);
		
		$edges_query = "create table edge2 as repair key u in (select n as u,n as v from node);";
		pg_query($conn, $edges_query);
	
		$edges_query = "insert into edge2 select * from edge;";
		pg_query($conn, $edges_query);
		
	}
	
	function run_aconf($conn, $query, $epsilon, $delta) {
		// TODO: add more types of queries
		if ($query == 0) {
			// triangle
			$aconf_query = "select aconf($epsilon,$delta) as triangle_prob";
			$aconf_query .= " from  edge e1, edge e2, edge e3";
			$aconf_query .= " where e1.v = e2.u and e2.v = e3.v and e1.u = e3.u
								and e1.u < e2.u and e2.u < e3.v;";
			$result = pg_query($conn, $aconf_query);
			// return result aconf
			return pg_fetch_result($result, 0, 0);
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
			// return result aconf
			return pg_fetch_result($result, 0, 0);
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
			// return result aconf
			return pg_fetch_result($result, 0, 0);
		}
	}
	
	function run_rconf($conn, $query, $epsilon) {
		// TODO: add more types of queries
		if ($query == 0) {
			// triangle
			$aconf_query = "select conf('R',$epsilon) as triangle_prob";
			$aconf_query .= " from  edge e1, edge e2, edge e3";
			$aconf_query .= " where e1.v = e2.u and e2.v = e3.v and e1.u = e3.u
								and e1.u < e2.u and e2.u < e3.v;";
			$result = pg_query($conn, $aconf_query);
			// return result aconf
			return pg_fetch_result($result, 0, 0);
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
			// return result aconf
			return pg_fetch_result($result, 0, 0);
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
			// return result aconf
			return pg_fetch_result($result, 0, 0);
		}
	}
	
	function run_conf($conn, $query) {
		// TODO: add more types of queries
		if ($query == 0) {
			// triangle
			$conf_query = "select conf() as triangle_prob";
			$conf_query .= " from  edge e1, edge e2, edge e3";
			$conf_query .= " where  e1.v = e2.u and e2.v = e3.v and e1.u = e3.u
			and e1.u < e2.u and e2.u < e3.v;";
			$result = pg_query($conn, $conf_query);
			// return result conf
			return pg_fetch_result($result, 0, 0);
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
			return pg_fetch_result($result, 0, 0);
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
			// return result conf
			return pg_fetch_result($result, 0, 0);
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
?>

<div id="main-wrapper"> 
<div id="main" class="clear-block"> 


<!-- -------------------------------------------
 Random graph generation
 ------------------------------------------- -->

<div class="node sticky"> 
<h1>Random graph generation</h1>

<?php
	$conn = init_connection($conn_string);
	if (isset($_GET['g']) && isset($_GET['nodes']) && $_GET['nodes'] != "")
		$num_nodes = $_GET['nodes'];
	else {
		$num_nodes = get_num_nodes($conn);
		if ($num_nodes == -1) {
			$num_nodes = 10;
		}
	}
	if (isset($_GET['g']) && isset($_GET['prob']) && $_GET['prob'] != "")
		$edge_prob =  $_GET['prob'];
	else {
		$edge_prob = get_edge_prob($conn);
		if ($edge_prob == -1)
			$rand_prob = true;
		else
			$rand_prob = false;
	}
	if (isset($_GET['g'])) {
		$rand_prob = ($_GET['rand_prob'] == 'on') ? true : false;		
	}
	if (isset($_GET['g']) && isset($_GET['delete_prob']) && $_GET['delete_prob'] != "")
		$delete_prob =  $_GET['delete_prob'];
	else {
		$delete_prob =  0;
	}
	
	if ($rand_prob) $checked = "checked";
	else $checked = "";
	
	
	
	// TODO: check whether values are admissible

	$n = array(3 => "", 4 => "", 5 => "", 6 => "", 7 => "", 8 => "",
			9 => "", 10 => "", 11 => "", 12 => "", 13 => "", 14 => "");
	$n[$num_nodes] = "selected";
	echo "<form name=\"generation\" action=\"index.php\" method=\"get\"
			onsubmit=\"return validate_generate_form(this)\">
		 <table>
		 	<tr>
		 		<td><label>Number of nodes:</label></td>
			 	<td><input type=\"text\" size=\"4\" name=\"nodes\" value=\"$num_nodes\"/></td>
		 		</td>
			 </tr>
			 <tr>
			 	<td>Edge probability:</td>
			 	<td><input type=\"text\" size=\"4\" name=\"prob\" value=\"$edge_prob\"/></td>
		 	</tr>
		 	<tr>
		 		<td><input type=\"checkbox\" name=\"rand_prob\" $checked/></td>
		 		<td>Randomize edge probability</td> 		
		 	</tr>
			 <tr>
			 	<td>Delete random edges (probability):</td>
			 	<td><input type=\"text\" size=\"4\" name=\"delete_prob\" value=\"$delete_prob\"/></td>
		 	</tr>
		 	<tr>
		 		<td><input type=\"submit\" value=\"Update graph\"/></td>
		 	</tr>
		 </table>
		 <input type=\"hidden\" name=\"g\"></input>	
		</form>";
?>

<div class="node sticky">
<!-- -------------------------------------------
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
	
 	$q = array(0 => "", 1 => "", 2 => "");
	$q[$query] = "selected";
 	
	$m = array(0 => "", 1 => "", 2 => "");
	$m[$method] = "checked";
	
	echo "<form name=\"query\" action=\"index.php\" method=\"get\" onsubmit=\"return validate_query_form (this)\";>
		 <table>
		 	<tr>
		 		<td><label>Query:</label></td>
		 		<td>
			 	<select name=\"query\">
			 		<option value=\"0\" $q[0]>Triangle</option>
			 		<option value=\"1\" $q[1]>Four-clique</option>
			 		<option value=\"2\" $q[2]>Path of length 3</option>
			 	</select>
			 	</td>
			 </tr>
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
		 		<td><input type=\"submit\"  value=\"Run query\"/></td>
		 	</tr>
		 </table>
		 <input type=\"hidden\" name=\"q\"></input>	
		</form>";
?>
</div>
<!-- -------------------------------------------
 Display result
 ------------------------------------------- -->
 <div class="node sticky">
 <h1>Result</h1>
<?php
	if (isset($_GET['g'])) {	// generating random graph
		$conn = init_connection($conn_string);
		// TODO: check whether values are admissible
		generate_graph($conn, $num_nodes, $edge_prob, $rand_prob, $delete_prob);
		echo "<table>
				<tr><td>Generated the following graph:</td></tr>
				<tr><td>Number of nodes:</td><td>$num_nodes</td>
				</tr>";
		$num_edges = get_num_edges($conn);
		echo	"<tr><td>Number of edges:</td><td>$num_edges</td></tr>";
		$edge_prob = get_edge_prob($conn);
		if ($edge_prob == -1)
			echo	"<tr><td>Edge probability:</td><td>random</td></tr>";
		else
			echo	"<tr><td>Edge probability:</td><td>$edge_prob</td></tr>";
		echo "</table>";
		//echo "<hr>";
	}
	else if (isset($_GET['q'])) {	// querying
		$conn = init_connection($conn_string);
		// run query and display result
		echo "<table>";
		echo "<tr><td><label>Query:</label></td>
				  <td><label>$query_types[$query]</label></td></tr>";
					
		if ($method == 1) {
			// run aconf
			// run several refinements steps
			$curr_eps = 1;
			do {
				$curr_eps *= 0.5;
				if ($curr_eps < $epsilon1)
					$curr_eps = $epsilon1;
				$a_start = gettimeofday(true);
				$aconf = run_aconf($conn, $query, $curr_eps, $delta);
				$a_end = gettimeofday(true);
				$a_time = ($a_end - $a_start);
				
				// display result
				echo	
					"<tr>
						<td><label>aconf(eps=$curr_eps, delta=$delta):</label></td>
						<td><label>$aconf</label></td>
					</tr>
					<tr>
						<td><label>Time:</label></td>
						<td><label>$a_time sec.</label></td>
					</tr>";
				flush();
				ob_flush();
			}
			while ($curr_eps > $epsilon1) ;			
		}
	if ($method == 2) {
			// run rconf
			// run several refinements steps
			$curr_eps = 1;
			do {
				$curr_eps *= 0.5;
				if ($curr_eps < $epsilon2)
					$curr_eps = $epsilon2;
				$a_start = gettimeofday(true);
				$aconf = run_rconf($conn, $query, $curr_eps);
				$a_end = gettimeofday(true);
				$a_time = ($a_end - $a_start);
				
				// display result
				echo	
					"<tr>
						<td><label>rconf(eps=$curr_eps):</label></td>
						<td><label>$aconf</label></td>
					</tr>
					<tr>
						<td><label>Time:</label></td>
						<td><label>$a_time sec.</label></td>
					</tr>";
				flush();
				ob_flush();
			}
			while ($curr_eps > $epsilon2) ;			
		}
		if ($method == 0) {
			// run conf
			$c_start = gettimeofday(true);
			$conf = run_conf($conn, $query);
			$c_end = gettimeofday(true);
			$c_time = ($c_end - $c_start);
			
			// display result
			echo	
				"<tr>
					<td><label>Confidence (exact):</label></td>
					<td><label>$conf</label></td>
				</tr>
				<tr>
					<td><label>Time:</label></td>
					<td><label>$c_time sec.</label></td>
				</tr>";
		}		 
		echo "</table>";		
	}
?>
</div>

<?php

// Free resultset
@pg_free_result($result);
// Closing connection
@pg_close($conn);
?>

</div>
</div>
 </body>
</html>