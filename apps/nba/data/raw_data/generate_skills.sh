#!/bin/bash

# This script generates the skill information for all players.

# These are the thresholds for all skills. If the skill performance of a player 
# passes the threshold for the skill, then the player is considered to have own 
# the skill. 
# For example, the threshold for for skill rebound is 3. If a player gets 2.1 
# rebounds per game, then he is considered not to own skill rebound. If a player 
# gets 3.1 rebounds per game, then he is considered to own skill rebound.     
block_threshold=1
pass_threshold=2
rebound_threshold=3
shoot_threshold=0.45
steal_threshold=0.50

counter=1
players_per_team=12

input_file=skills_raw.txt
output_file=Skills.txt

rm -f $output_file

cat $input_file | while read LINE
	do 
		if [ $(($counter % $(( $players_per_team + 1 )) )) -eq 1 ]
		then
			nf=`echo $LINE | awk '{ print NF }'` 
			
			team=`echo $LINE | cut -d ' ' -f1-$nf`
					
		else
			
			name=`echo $LINE | cut -d' ' -f1-2`
	 
			#Add the blocking skill 
			block=`echo $LINE | cut -d' ' -f14`
		        
			if [ $(echo "$block >= $block_threshold"|bc) -eq 1 ]
			then
				has_block="Y"
			else
				has_block="N"
			fi

			#Add the passing skill 
			pass=`echo $LINE | cut -d' ' -f12`
		        
			if [ $(echo "$pass >= $pass_threshold"|bc) -eq 1 ]
			then
				has_pass="Y"
			else
				has_pass="N"
			fi         

			#Add the rebound skill 
			rebound=`echo $LINE | cut -d' ' -f11`
		        
			if [ $(echo "$rebound >= $rebound_threshold"|bc) -eq 1 ]
			then
				has_rebound="Y"
			else
				has_rebound="N"
			fi                
		    
			#Add the shooting skill 
			shoot=`echo $LINE | cut -d' ' -f6`
		        
			if [ $(echo "$shoot >= $shoot_threshold"|bc) -eq 1 ]
			then
				has_shoot="Y"
			else
				has_shoot="N"
			fi          
		        
			#Add the stealing skill 
			steal=`echo $LINE | cut -d' ' -f6`
		        
			if [ $(echo "$steal >= $steal_threshold"|bc) -eq 1 ]
			then
				has_steal="Y"
			else
				has_steal="N"
			fi    
		    
		    #the following code randomly remove or add a skill to a player
		    
		    random=10
		    
		    if [ $has_block == "Y" -a $has_pass == "Y" -a $has_rebound == "Y" -a $has_shoot == "Y" -a $has_steal == "Y" ]	
		    	then
		    		random=$(($RANDOM % 5))
		    		new="N"	
		    fi

		    if [ $has_block == "N" -a $has_pass == "N" -a $has_rebound == "N" -a $has_shoot == "N" -a $has_steal == "N" ]	
		    	then
		    		random=$(($RANDOM % 5))
		    		new="Y"
		    fi

			case "$random" in
			'0')
				has_block=$new
			;;
			'1')
				has_pass=$new			
			;;
			'2')
				has_rebound=$new		
			;;
			'3')
				has_shoot=$new		
			;;
			'4')
				has_steal=$new		
			;;
			esac
		        
		    echo "$team|$name|Shooting|$has_shoot" >> $output_file
			echo "$team|$name|Passing|$has_pass"	>> $output_file
			echo "$team|$name|Rebound|$has_rebound"	>> $output_file
			echo "$team|$name|Blocking|$has_block"	>> $output_file
			echo "$team|$name|Stealing|$has_steal"	>> $output_file
		
		fi

		counter=$(( $counter + 1))
         
	done

mv $output_file ../$output_file

