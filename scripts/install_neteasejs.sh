#!/bin/bash
current_path=$(cd $(dirname $0); pwd)
parent_path=$(dirname ${current_path})

git submodule update --init --recursive
sudo apt install npm nodejs curl
cd ${parent_path}/Plugins/neteasemusic/NeteaseCloudMusicApi
npm install