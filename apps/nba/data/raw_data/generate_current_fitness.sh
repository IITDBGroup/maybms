#!/bin/bash

# This script randomly generates current fintness information for all players.

input_file=names.txt
output_file=Fitness.txt

rm -f $output_file

cat $input_file | while read LINE
	do 
		name=`echo $LINE | cut -d' ' -f1-2`
		
		random=$(($RANDOM % 10))

			case "$random" in
			[0-6])
				fitness="Fit for Match"
			;;
			'9')
				fitness="Seriously Injured"
			;;
			[7-8])
				fitness="Slightly Injured"
			;;
			esac
	
		echo "$name|$fitness" >> $output_file

	done

mv $output_file ../$output_file
