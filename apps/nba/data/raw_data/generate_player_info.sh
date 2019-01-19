#!/bin/bash

input_file=player_info_raw.txt
output_file=PlayerInfo.txt

rm -f $output_file

cat $input_file | while read LINE
	do 
		nf=`echo $LINE | awk '{ print NF }'`
	
		if [ $nf -eq 2 -o $nf -eq 3 ]
		then
					
			team=`echo $LINE | cut -d ' ' -f1-$nf`
					
		else
			
			name=`echo $LINE | cut -d' ' -f2-3`
	 
			born=`echo $LINE | cut -d' ' -f7`
		    
		    ht=`echo $LINE | cut -d' ' -f5`    
		    
		    wt=`echo $LINE | cut -d' ' -f6`   
		        
		    echo "$team|$name|$born|$ht|$wt" >> $output_file
		
		fi
         
	done

mv $output_file ../$output_file

