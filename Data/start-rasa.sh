#!/bin/bash
source ~/projects/lowpower-robot/lib/rasa/bin/activate
cd Data/rasa
if [ $# == 1 ]; then
    nohup rasa run --enable-api > log.txt 2>&1 &
elif [ $# == 2 ]; then
    nohup rasa run --enable-api -m $1 > log.txt 2>&1 &
fi
echo $! >| rasa.pid
