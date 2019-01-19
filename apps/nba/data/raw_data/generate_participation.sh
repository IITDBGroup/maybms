#!/bin/bash

# This script generates the participation information for all players.

counter=1
players_per_team=12

input_file=skills_raw.txt
output_file=Participation.txt

rm -f $output_file

cat $input_file | while read LINE
	do 
		if [ $(($counter % $(( $players_per_team + 1 )) )) -eq 1 ]
		then
			nf=`echo $LINE | awk '{ print NF }'` 
			
			team=`echo $LINE | cut -d ' ' -f1-$nf`
					
		else
			
			name=`echo $LINE | cut -d' ' -f1-2`
	  
			participate=`echo $LINE | cut -d' ' -f3`
		        
		    echo "$team|$name|110|$participate" >> $output_file
		
		fi
        
        counter=$(( $counter + 1))
         
	done

mv $output_file ../$output_file
