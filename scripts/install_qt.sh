#!/bin/bash
sudo apt install qtbase5-dev qtchooser qt5-qmake qtbase5-dev-tools
sudo apt install qtmultimedia5-dev
sudo apt install qtcreator
sudo apt-get install libqt5multimedia5-plugins
sudo apt install qt5-doc

sudo apt install qt6-base-dev qt6-multimedia-dev qt6-base-dev-tools qt6-documentation-tools libqt6multimedia6
git clone https://github.com/qt/qtmqtt.git
pushd qtmqtt
git checkout 6.8.1
mkdir build && cd build
qt-configure-module ..
cmake --build .
cmake --install . --verbose
popd
rm -rf qtmqtt