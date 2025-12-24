#!/bin/bash
current_path=$(cd $(dirname $0); pwd)
parent_path=$(dirname ${current_path})

sudo apt install npm nodejs curl
sudo npm install -g pnpm@latest-10
git clone https://github.com/neteasecloudmusicapienhanced/api-enhanced.git ${parent_path}/Plugins/neteasemusic/NeteaseCloudMusicApi
cd ${parent_path}/Plugins/neteasemusic/NeteaseCloudMusicApi
pnpm i 