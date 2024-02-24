#!/bin/bash
source /home/lizilin/venv/rasa/bin/activate
cd Data/rasa
nohup rasa run --enable-api > log.txt 2>&1 &
echo $! >| rasa.pid
