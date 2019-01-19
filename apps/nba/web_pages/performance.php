<?php
// Get the number of matches selected by the user
if ( $_POST["number"] == NULL)
	$select_number = 1;
else	
	$select_number = $_POST["number"];
?>

<html>
 	<head>
  		<title>Performance Prediction</title>
  		<link rel="stylesheet" type="text/css" href="style1.css" media="screen" />  
 	</head>
 	
 	<body>
 
 <?php
// Connect the database
$dbconn = pg_connect("host=localhost port=5432 dbname=nba");

// The names of the team and current player
$name = $_GET['player'];
$team = $_GET['team'];

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
				<td align='center' width='350'>";

echo " <h3>Player:  <b><a href='player.php?player=$name&team=$team'>" . $name . "</a></b><h3><br><br><br>";

echo 	"<table>
			<tr>
				<td><h4>Team:  <b><a href='manage.php?team=$team'>$team</a><b><h4></td>
			</tr>
			<tr>
				<td align='center'>&nbsp<br><a href='manage.php?team=$team'><img src='img/logos/$team.gif'/></a></td>
			</tr>
		</table><br><br>";
				
echo	" Recent Performance:</td><td>";

// Fetch all performance-related information of the player
$query = "select match_date, match, points from Performance where player='$name'";
$result = pg_query($query) or die('Query failed: ' . pg_last_error());

// The number of the matches
$row_count = pg_num_rows($result);

echo 	"<table border=1>
			<tr>
				<td><center>Date</td>
				<td><center>Opponent</td>
				<td><center>Points</td>
			</tr>";

// Print all performance-related information of the player
while ($line = pg_fetch_array($result, null, PGSQL_NUM)) 
{
	echo 	"<tr>
				<td><center>$line[0]</td>
				<td><center>$line[1]</td>
				<td><center>$line[2]</td><tr>";
}

echo "</table><td/></tr></table><br>";

echo 	"<div class='node sticky'>
			<form name='myForm' method='post' action=$PHP_SELF>";

echo "Predict the performance of the next match with the last <select name='number'>:";

$count = 1;

while ( $count <= $row_count )
{
	echo "<option>$count</option>";
	$count++;
} 

echo "</select> games.";

echo "     <input type='submit' name='update' value='Submit'>";

echo "</form>";

// Restart the sequence with an appropriate number
$query = "alter sequence serial restart with $select_number";

$result = pg_query($query) or die('Query failed: ' . pg_last_error());

// Create a temporary table for recent performance with weights 
$query = 	"create table RecentPerformance as
				select *, nextval('serial') as p 
				from 
					(select * from Performance 
					where player = '$name' 
					order by match_date desc 
					limit $select_number) R";
					
$result = pg_query($query) or die('Query failed: ' . pg_last_error());

// Calculate the expected sum of recent performance
$query = 	"select esum(points) 
			from
				(repair key dummy in 
				(select 1 as dummy, points, p 
					from RecentPerformance) 
				weight by p) R";

$result = pg_query($query) or die('Query failed: ' . pg_last_error());

$line = pg_fetch_array($result, null, PGSQL_NUM);

echo "Predicted performance with last <b>$select_number</b> games: <b>$line[0] points</b></div>";

// Drop the temporary relation
$query = "drop table RecentPerformance";

$result = pg_query($query) or die('Query failed: ' . pg_last_error());

pg_close($dbconn);
?>

		</div>
		
	</body>
</html>
