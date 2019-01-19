#!/bin/bash

# This script fetches all the player names in the raw data and place them in a file.

counter=1
players_per_team=12

input_file=skills_raw.txt
output_file=names.txt

rm -f $output_file

cat $input_file | while read LINE
	do 
		if [ $(($counter % $(( $players_per_team + 1 )) )) -eq 1 ]
		then
			nf=`echo $LINE | awk '{ print NF }'` 
			
			team=`echo $LINE | cut -d ' ' -f1-$nf`
					
		else
			
			name=`echo $LINE | cut -d' ' -f1-2`
	
		    echo "$name" >> $output_file

		fi

		counter=$(( $counter + 1))
         
	done


