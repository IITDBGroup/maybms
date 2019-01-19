<html>
 	<head>
  		<title>Team Management</title>
  		<link rel="stylesheet" type="text/css" href="style1.css" media="screen" />  	
 	</head>
 	<body>
 
	<div>
			<table>
				<tr>
					<td width='800'><a href="main.html"><h5>Main Page</h5></a></td>
					<td><h5>powered by:</h5></td>
					<td><h5><img src="img/maybms_logo.jpg" alt="MayBMS" width="70" height="30"/></h5></td>
				</tr>
			</table>
	</div>

 <?php
// This function exams whether a player is checked in the last page 
function checked($array, $player)
{
	for ($i=0; $i<count($array); $i++)
	{
		if ($player == $array[$i])
			return True;
	} 
	
	return False;
} 

// Return the probability of a skill from the answer 
function match($array, $name, $column, $desired_column)
{
	for ($i = 0; $i < count($array); $i++)
	{
		if ($name == $array[$i][$column])
			return $array[$i][$desired_column];
	} 
	
	return 0;
} 

// Connect the database  
$dbconn = pg_connect("host=localhost port=5432 dbname=nba");

// The names of the team
$team = $_GET['team'];

echo "<div class='node sticky'>";

echo 	"<center><table>
			<tr>
				<td><a href='manage.php?team=$team'><img src='img/logos/$team.gif'/></a>&nbsp&nbsp</td>
				<td><h4>Team Management:  <b>$team</b></h2></td>
			</tr>
		</table></center><br>";

// Select the skill, the participtation rate and the salary of a player
$query = 	"select S.player, has, skill, rate, salary 
			from Skills S, Participation P, Salary SA 
			where S.player = P.player and S.team='$team' and S.player = SA.player 
			order by salary desc, player, skill";

$result = pg_query($query) or die('Query failed: ' . pg_last_error());

echo "<form name='myForm' method='post' action=$PHP_SELF>";

echo "The following table shows the skill availabilities* in case that some of the players can not play the future games due to injuries, bans or other reasons.<br><br>";

echo "<div class='node sticky' align='center'>";

echo 	"<table border=1>
			<tr align='center'>
				<td rowspan=2>Remove</td>
				<td rowspan=2>Player</td>
				<td colspan=5><center>Skills</td>
				<td rowspan=2>Participation Rate**</td>
				<td rowspan=2>Salary($)</td>
			</tr>
			<tr align='center'>
				<td>Blocking</td>
				<td>Passing</td>
				<td>Rebound</td>
				<td>Shooting</td>
				<td>Stealing</td>
			</tr>";

$prev_player;
$prev_player_rate;
$prev_player_salary;

// Loop over all players
while ($line = pg_fetch_array($result, null, PGSQL_NUM)) 
{
	if ($prev_player != $line[0] && $prev_player != NULL)
	{
		echo 	"<td><center>$prev_player_rate</td>
				<td align=right>$prev_player_salary</td>
				</tr>";
	}

	if ($prev_player != $line[0] )
	{
		$has_been_checked = checked($_POST['remove'], $line[0]);
	
		if ($has_been_checked)
			echo "<tr>
					<td><center><input type='checkbox' name='remove[]' value='$line[0]' 'CHECKED'></td>";
		else
			echo "<tr>
					<td><center><input type='checkbox' name='remove[]' value='$line[0]'></td>";
		
		echo "<td><a href='player.php?player=$line[0]&team=$team'>$line[0]</a></td>";
	}

	echo "<td><center>$line[1]</td>";
	
	$prev_player = $line[0];
	$prev_player_rate = $line[3];
	$prev_player_salary = $line[4];
}

echo 	"<td><center>$prev_player_rate</td><td align=right>$prev_player_salary</td>
		</tr>";

// This is the inequality condition used in the query of confidence computation 
$inequality;

// If a player row is checked, add the player to the inequatlity condition
for ($i=0; $i<count($_POST['remove']); $i++)
{
	$player = $_POST['remove'][$i];
	$inequality = $inequality . " AND P.player <> '$player' ";
} 

// Compute the probability for each skill with only unchecked players
$query = 	"select skill, conf() from
			Skills P, Participation PA
			where P.team='$team' and P.player = PA.player AND has = 'Y' " . $inequality . " group by skill";

$result = pg_query($query) or die('Query failed: ' . pg_last_error());

$arr = pg_fetch_all($result);

// Fetch the probabilities for all skills
$blocking = match($arr, 'Blocking', 'skill', 'conf');
$passing = match($arr, 'Passing', 'skill', 'conf');
$rebound = match($arr, 'Rebound', 'skill', 'conf');
$shooting = match($arr, 'Shooting', 'skill', 'conf');
$stealing = match($arr, 'Stealing', 'skill', 'conf');

echo 	"<tr>
			<td><input type='submit' name='update' value='Update'></td>
			<td>Skill Availability</td>
			<td><b>$blocking</td>
			<td><b>$passing</td>
			<td><b>$rebound</td>
			<td><b>$shooting</td>
			<td><b>$stealing</td>
			<td>&nbsp</td>
			<td>&nbsp</td>
		</tr>";

echo "</table>";

echo "</form>";

echo "</div>";

// Free resultset
pg_free_result($result);

// Closing connection
pg_close($dbconn);
?>

*: Skill availability is the likelihood that team possesses a certain skill in the following games. It's between 0 and 1. <br>
**: Participation Rate = the number of games the player participated / the total number of games of a team. A higher value indicates that the skills of the player are more available to the team. <br>

	</div>

 	</body>
</html>

