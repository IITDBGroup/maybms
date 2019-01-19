<?php
?>

<html>
 	<head>
  		<title>Player</title>
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
					<td width='800'><h5><a href='main.html'>Main Page</a> > <a href='manage.php?team=$team'>Team Page</a><h5></td>
					<td><h5>powered by:</h5></td>
					<td><h5><img src='img/maybms_logo.jpg' width='70' height='30'/></h5></td>
				</tr>
			</table>
	</div>";

echo "<div class='node sticky' align='center'>";

echo 	"<table>
			<tr>
				<td align='center' width='250'> <h4>Player: <b>" . $name . "</b><h4></td>
				<td><h4>Team: <b><a href='manage.php?team=$team'>$team</a><b><h4></td>
				<td>&nbsp&nbsp<a href='manage.php?team=$team'><img src='img/logos/$team.gif'/></a></td>
			</tr>
		</table>";
?>

<div class='node sticky' align='center'>

<table border="0" cellpadding="0" cellspacing="0">
	<tr>
		<td width="300">
			<?php
			echo "<img border='0' src='img/players/$team/$name.jpg'>";
			?>	
		</td>
		<td width="230">
			<div >
				<h3 >Player Information</h3>
			<dl>

			<?php
			// Fetch the player information 
			$query = "SELECT dob, height, weight FROM PlayerInfo P where P.player = '$name' and P.team='$team'";

			$result = pg_query($query) or die('Query failed: ' . pg_last_error());

			$line = pg_fetch_array($result, null, PGSQL_NUM);

			$dob = $line[0];
			$height = $line[1];
			$weight = $line[2];

			echo 		"<table width='200'>
							<tr><td>Born:</td><tr>
							<tr><td align='center'>$dob</td><tr>
							<tr><td>Height:</td><tr>
							<tr><td align='center'>$height</td><tr>
							<tr><td>Weight:</td><tr>
							<tr><td align='center'>$weight lbs.</td><tr>
						</table>";
			?>
		</td>
		
		<td width="219">
<?php

echo "<div class='node sticky'><ul><li><a href='fitness.php?player=$name&team=$team'>Fitness Prediction</a> <br><br>";
echo "<li><a href='performance.php?player=$name&team=$team'>Performance Prediction</a> </ul></div>";

// Free resultset
pg_free_result($result);

// Closing connection
pg_close($dbconn);
?>
		
		</td>
		
	</tr>
</table><br>

</div>

</div>

 	</body>
</html>
