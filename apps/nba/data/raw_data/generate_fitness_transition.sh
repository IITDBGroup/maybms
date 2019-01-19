#!/bin/bash

# This script randomly generates fitness transition matrices for all players.

input_file=names.txt
output_file=FitnessTransition.txt

rm -f $output_file

cat $input_file | while read LINE
	do
		name=`echo $LINE | cut -d' ' -f1-2`
	
		counter=0
		
		while [ $counter -lt 18 ]
		do
			choice=$(($counter % 18))
	
			case "$choice" in
			'0')
				echo "$name|N|Fit for Match|Fit for Match|1" >> $output_file
				;;
			'1')
				echo "$name|N|Fit for Match|Seriously Injured|0" >> $output_file
				;;
			'2')
				echo "$name|N|Fit for Match|Slightly Injured|0" >> $output_file
				;;
			'3')
				prob=$(($RANDOM % 10))
				prob=$(($prob + 1))			
				prob=$(echo "scale=2; $prob / 100" | bc)
			
				echo "$name|N|Seriously Injured|Fit for Match|$prob" >> $output_file
				;;
			'4')
				prob2=$(($RANDOM % 50))
				prob2=$(($prob2 + 1))			
				prob2=$(echo "scale=2; $prob2 / 100" | bc)
				
				echo "$name|N|Seriously Injured|Seriously Injured|$prob2" >> $output_file
				;;
			'5')
				prob3=$(echo "scale=2; 1 - $prob - $prob2" | bc)
			
				echo "$name|N|Seriously Injured|Slightly Injured|$prob3" >> $output_file
				;;	
			'6')
				prob=$(($RANDOM % 60))
				prob=$(($prob + 11))			
				prob=$(echo "scale=2; $prob / 100" | bc)
			
				echo "$name|N|Slightly Injured|Fit for Match|$prob" >> $output_file
				;;
			'7')				
				echo "$name|N|Slightly Injured|Seriously Injured|0" >> $output_file
				;;
			'8')
				prob3=$(echo "scale=2; 1 - $prob" | bc)
			
				echo "$name|N|Slightly Injured|Slightly Injured|$prob3" >> $output_file
				;;	
			'9')
				prob=$(($RANDOM % 5))
				prob=$(($prob + 1))			
				prob=$(echo "scale=2; $prob / 100" | bc)
				
				echo "$name|Y|Fit for Match|Seriously Injured|$prob" >> $output_file
				;;
			'10')
				prob2=$(($RANDOM % 15))
				prob2=$(($prob2 + 6))			
				prob2=$(echo "scale=2; $prob2 / 100" | bc)

				echo "$name|Y|Fit for Match|Slightly Injured|$prob2" >> $output_file
				;;
			'11')
				prob3=$(echo "scale=2; 1 - $prob - $prob2" | bc)
			
				echo "$name|Y|Fit for Match|Fit for Match|$prob3" >> $output_file
				;;
			'12')
				echo "$name|Y|Seriously Injured|Fit for Match|0" >> $output_file
				;;
			'13')
				echo "$name|Y|Seriously Injured|Seriously Injured|1" >> $output_file
				;;
			'14')
				echo "$name|Y|Seriously Injured|Slightly Injured|0" >> $output_file
				;;
			'15')
				echo "$name|Y|Slightly Injured|Fit for Match|0" >> $output_file
				;;
			'16')
				prob=$(($RANDOM % 30))
				prob=$(($prob + 1))			
				prob=$(echo "scale=2; $prob / 100" | bc)
				
				echo "$name|Y|Slightly Injured|Seriously Injured|$prob" >> $output_file
				;;
			'17')
				prob2=$(echo "scale=2; 1 - $prob" | bc)
			
				echo "$name|Y|Slightly Injured|Slightly Injured|$prob2" >> $output_file
				;;
			esac
		
			counter=$(($counter + 1))
		
		done

	done

mv $output_file ../$output_file
