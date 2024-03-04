#!/bin/bash
source ~/projects/lowpower-robot/lib/rasa/bin/activate
cd ~/.config/lowpower_robot/Data/rasa
if [ $# != 1 ]; then
    nohup rasa run --enable-api > log.txt 2>&1 &
else 
    nohup rasa run --enable-api -m $1 > log.txt 2>&1 &
fi
echo $! >| rasa.pid
