#!/bin/bash
source $1
cd ~/.config/QSmartAssistant/Data/rasa
if [ $# != 2 ]; then
    nohup rasa run --enable-api > log.txt 2>&1 &
else 
    nohup rasa run --enable-api -m $2 > log.txt 2>&1 &
fi
echo $! >| rasa.pid
