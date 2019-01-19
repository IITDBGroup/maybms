#!/bin/bash

# This script generates match information.

input_file=match_raw.txt
output_file=Matches.txt

rm -f $output_file

cat $input_file | while read LINE
	do 
		nf=`echo $LINE | awk '{ print NF }'`
	
		if [ $nf -eq 2 -o $nf -eq 3 ]
		then
					
			name=`echo $LINE | cut -d ' ' -f1-$nf`
					
		else
			
			if [ $nf -eq 6 ]
			then
				opponent=`echo $LINE | cut -d' ' -f3-4`
			else
				opponent=`echo $LINE | cut -d' ' -f3-5`
			fi
		        
		    echo "$name|$opponent" >> $output_file
		
		fi
         
	done

mv $output_file ../$output_file
