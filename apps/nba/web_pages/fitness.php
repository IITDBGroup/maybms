<html>
 	<head>
  		<title>Fitness Prediction</title>
  		<link rel="stylesheet" type="text/css" href="style1.css" media="screen" />  
 	</head>
 	<body>
 
<?php

// Connect the database 
$dbconn = pg_connect("host=localhost port=5432 dbname=nba");

// The names of the team and current player
$name = $_GET['player'];
$team = $_GET['team'];

// Get the fitness information for the current player
$query = "select fitness from Fitness where player='$name'";
$result = pg_query($query) or die('Query failed: ' . pg_last_error());
$line = pg_fetch_array($result, null, PGSQL_ASSOC);
$current_fit = $line['fitness'];

echo
	"<div>
			<table>
				<tr>
					<td width='800'><h5><a href='main.html'>Main Page</a> > <a href='manage.php?team=$team'>Team Page</a> > <a href='player.php?player=$name&team=$team'>Player Page</a><h5></td>
					<td><h5>powered by:</h5></td>
					<td><h5><img src='img/maybms_logo.jpg' width='70' height='30'/></h5></td>
				</tr>
			</table>
	</div>";

echo "<div class='node sticky'>";

echo 	"<table>
			<tr>
				<td width='250'><a href='player.php?player=$name&team=$team'> <h4>Player: <b>" . $name . "</b><h4></a></td>
				<td><h4>Team: <b><a href='manage.php?team=$team'>$team</a><b><h4></td>
				<td>&nbsp&nbsp<a href='manage.php?team=$team'><img src='img/logos/$team.gif'/></a></td>
			</tr>
			<tr>
				<td colspan=2><h4>Current Fitness: <b>$current_fit </b></h4></td>
			</tr>
		</table><br>";
?>

<?php
// This function fetches all entries in a transition matrix
function output_transition($result)
{
	while ($line = pg_fetch_array($result, null, PGSQL_NUM)) 
	{
		if ($line[0] == "Fit for Match" && $line[1] == "Fit for Match")
			$fit_fit = $line[2];
		else if ($line[0] == "Fit for Match" && $line[1] == "Seriously Injured")
			$fit_se = $line[2];
		else if ($line[0] == "Fit for Match" && $line[1] == "Slightly Injured")
			$fit_sl = $line[2];
		else if ($line[0] == "Seriously Injured" && $line[1] == "Fit for Match")
			$se_fit = $line[2];
		else if ($line[0] == "Seriously Injured" && $line[1] == "Seriously Injured")
			$se_se = $line[2];
		else if ($line[0] == "Seriously Injured" && $line[1] == "Slightly Injured")
			$se_sl = $line[2];
		else if ($line[0] == "Slightly Injured" && $line[1] == "Fit for Match")
			$sl_fit = $line[2];
		else if ($line[0] == "Slightly Injured" && $line[1] == "Seriously Injured")
			$sl_se = $line[2];
		else if ($line[0] == "Slightly Injured" && $line[1] == "Slightly Injured")
			$sl_sl = $line[2];
	}

	// Print the transition matrix as a table
	echo 	"<tr>
				<td>Fit for Match</td>
				<td><center>$fit_fit</td>
				<td><center>$fit_se</td>
				<td><center>$fit_sl</td>
			</tr>
			<tr>
				<td>Seriously Injured</td>
				<td><center>$se_fit</td>
				<td><center>$se_se</td>
				<td><center>$se_sl</td>
			</tr>
			<tr>
				<td>Slightly Injured</td>
				<td><center>$sl_fit</td>
				<td><center>$sl_se</td>
				<td><center>$sl_sl</td>
			</tr>";

	echo "</table>";
}

// This function exams whether a game is checked in the last page
function checked($array, $game)
{
	for ($i=0; $i<count($array); $i++)
	{
		if ($game == $array[$i])
			return True;
	} 
	
	return False;
} 

// Get the transition matrix for a non-match day
$query = "select InitState, FinalState, p from FitnessTransition where player='$name' and play_a_match='N'";
$result = pg_query($query) or die('Query failed: ' . pg_last_error());

echo 	"<div class='node sticky'>
			<table>
				<tr>
					<td align='center'>";

echo "<b>Fitness Matrix for a Non-Match Day (A player tends to get fit):<b><br>";
echo "<table border=1>
		<tr>
			<td>&nbsp</td>
			<td>Fit for Match</td>
			<td>Seriously Injured</td>
			<td>Slightly Injured</td>
		</tr>";

output_transition($result);

echo "</td><td align='center' width='500'>";

// Get the transition matrix for a match day
$query = "select InitState, FinalState, p from FitnessTransition where player='$name' and play_a_match='Y'";
$result = pg_query($query) or die('Query failed: ' . pg_last_error());

echo "<b>Fitness Matrix for a Match Day (A player tends to get injured):</b><br>";
echo "<table border=1>
		<tr>
			<td>&nbsp</td>
			<td>Fit for Match</td>
			<td>Seriously Injured</td>
			<td>Slightly Injured</td>
		</tr>";

output_transition($result);

echo "</td></tr></table></div>";

// Create a temporary relation representing a match-day transition with repair-key
$query = "create table one_step_with_match as
repair key InitState in (select InitState, FinalState, p from FitnessTransition where player = '$name' and play_a_match = 'Y') weight by p";

$result = pg_query($query) or die('Query failed: ' . pg_last_error());

// Create a temporary relation representing a non-match-day transition with repair-key
$query = "create table one_step_without_match as
repair key InitState in (select InitState, FinalState, p from FitnessTransition where player = '$name' and play_a_match = 'N') weight by p";

$result = pg_query($query) or die('Query failed: ' . pg_last_error());

$query = "select team2 from Matches M where M.team1 = '$team'";
$result = pg_query($query) or die('Query failed: ' . pg_last_error());

echo "<div class='node sticky'><form name='myForm' method='post' action=$PHP_SELF>";

echo "<h4><b>Fitness Prediction:</b><br></h4>";

echo "<table border=1>
		<tr>
			<td rowspan=2>Play</td>
			<td rowspan=2><center>Match</td>
			<td colspan=3 align=center><b>Fitness before the Match</b></td>
		</tr>
		<tr>
			<td>Fit for Match</td>
			<td>Seriously Injured</td>
			<td>Slightly Injured</td>
		</tr>";


$count = 0; 	// Count the games 
$fit;			// The probability that a player is fit before a match
$serious;		// The probability that a player is seriously injured before a match
$slight;		// The probability that a player is slightly injured before a match
$step;			// A match-day transition or a non-match-day transition
$previous_row_is_checked;	// If the previous row is checked, then the previous day is a match-day; otherwise, it is a non-match day

$rows = pg_num_rows($result); 	// The number of matches

// Loop over all matches
while ($line = pg_fetch_array($result, null, PGSQL_NUM)) {
	$count++;

	$has_been_checked = checked($_POST['games'], $line[0]);
	
	if ($has_been_checked)
		echo 	"<tr>
					<td><center><input type='checkbox' name='games[]' value='$line[0]' 'CHECKED'></td>";
	else
		echo 	"<tr>
					<td><center><input type='checkbox' name='games[]' value='$line[0]'></td>";

	// If the previous day is a match-day, use the match-day transition;
	// otherwise, use the non-match-day transition
	if ($previous_row_is_checked)
		$step = "one_step_with_match";
	else
		$step = "one_step_without_match";
		
	$previous_row_is_checked = $has_been_checked;
	
	echo "<td>$team $line[0]</td>";

	// If this is the first match, use the current fitness information. No 
	// random walk is needed
	if ($count == 1)
	{
		if ($current_fit == "Fit for Match")
			echo 	"<td><center>1</td>
					<td><center>0</td>
					<td><center>0</td>
					</tr>";
		else if ($current_fit == "Seriously Injured")
			echo 	"<td><center>0</td>
					<td><center>1</td>
					<td><center>0</td>
					</tr>";
		else
			echo 	"<td><center>0</td>
					<td><center>0</td>
					<td><center>1</td>
					</tr>";
	
		continue;
	}
	// The second match
	else if ($count == 2)
	{	
		// Compute the probabilities of different states on top of a one-step random walk
		$FT_query = "select FinalState as State, conf() as p from $step where InitState = '$current_fit' group by FinalState";
					
		$FT_result = pg_query($FT_query) or die('Query failed: ' . pg_last_error());
		
		// Create a temporary relation representing the transition matrix of a one-step random walk
		$FT_query = "create table current_fitness as
		select InitState, FinalState, conf() as p from $step group by InitState, FinalState";		
					
		pg_query($FT_query) or die('Query failed: ' . pg_last_error());
	}
	// Other matches
	else
	{
		// Compute the probabilities of different states on top of a more-than-one-step random walk	
		$FT_query = "select S.FinalState, conf() as p 
					from $step S, ( repair key InitState in current_fitness weight by p ) R 
					where R.FinalState = S.InitState and R.InitState = '$current_fit' group by S.FinalState";
					
		$FT_result = pg_query($FT_query) or die('Query failed: ' . pg_last_error());

		// Create a temporary relation representing the transition matrix of 
		// random walks from the first match to current match
		$FT_query = "create table current_fitness_temp as 
					select R.InitState, S.FinalState, conf() as p 
					from $step S, ( repair key InitState in current_fitness weight by p ) R 
					where R.FinalState = S.InitState group by R.InitState, S.FinalState;";

		// Rename the temporary ralation
		$FT_query .= "drop table current_fitness;";		

		$FT_query .= "alter table current_fitness_temp rename to current_fitness";
					
		pg_query($FT_query) or die('Query failed: ' . pg_last_error());			
	}
	
	// Initialize all probabilities to 0s
	$fit = 0;
	$serious = 0;
	$slight = 0;
	
	// Store the probabilities to the proper variables
	while ($FT_line = pg_fetch_array($FT_result, null, PGSQL_NUM)) 
	{
		if ($FT_line[0] == "Fit for Match")
			$fit = $FT_line[1];
		else if ($FT_line[0] == "Seriously Injured")
			$serious = $FT_line[1];
		else
			$slight = $FT_line[1];
	}
	
	// Print the probabilites
	echo 	"<td><center>$fit</td>
			<td><center>$serious</td>
			<td><center>$slight</td>
			</tr>";
	
	// Delete the temporary random walk relation if all the matches have been iterated
	if ($count == $rows)
	{
		$FT_query = "drop table current_fitness";
					
		$FT_result = pg_query($FT_query) or die('Query failed: ' . pg_last_error());				
	}	
}

echo "</table>";

echo "<br><input type='submit' name='update' value='Update'>";

echo "</form></div>";

// Delete the temporary one-step random walk relations
$query = "drop table one_step_with_match";

$result = pg_query($query) or die('Query failed: ' . pg_last_error());

$query = "drop table one_step_without_match";

$result = pg_query($query) or die('Query failed: ' . pg_last_error());


// Free resultset
pg_free_result($result);

// Closing connection
pg_close($dbconn);
?>

 	</body>
</html>
