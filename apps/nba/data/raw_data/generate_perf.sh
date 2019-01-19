#!/bin/bash

# This script generates the performance-related data.

input_file=performance_raw.txt
output_file=Performance.txt

rm -f $output_file

cat $input_file | while read LINE
	do 
		nf=`echo $LINE | awk '{ print NF }'`
	
		if [ $nf -eq 2 ]
		then
					
			name=`echo $LINE | cut -d ' ' -f1-2`
					
		else
			
			date=`echo $LINE | cut -d' ' -f1-2`
	 
			opponent=`echo $LINE | cut -d' ' -f4`
		    
		    points=`echo $LINE | cut -d' ' -f27`    
		        
		    echo "$name|$date, 2009|$opponent|$points" >> $output_file
		
		fi
         
	done

mv $output_file ../$output_file
