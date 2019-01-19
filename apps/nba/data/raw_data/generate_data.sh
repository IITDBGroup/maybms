#!/bin/sh

# This script generates the data which can be imported directly into the database

./generate_participation.sh
./generate_skills.sh
./generate_player_info.sh
./generate_perf.sh
./generate_match.sh
./generate_name.sh
./generate_current_fitness.sh
./generate_fitness_transition.sh

