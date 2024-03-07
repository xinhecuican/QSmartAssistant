#!/bin/bash
current_path=$(cd $(dirname $0); pwd)
parent_path=$(dirname ${current_path})
lib_path=${parent_path}/lib/snowboy

wget http://downloads.sourceforge.net/swig/swig-3.0.10.tar.gz
sudo apt-get install libpcre3 libpcre3-dev
tar -xvzf swig-3.0.10.tar.gz
pushd swig-3.0.10
./configure --prefix=/usr                  \
        --without-clisp                    \
        --without-maximum-compile-warnings &&
make -j `nproc`
make install &&
install -v -m755 -d /usr/share/doc/swig-3.0.10 &&
cp -v -R Doc/* /usr/share/doc/swig-3.0.10
popd
rm -rf swig-3.0.10 swig-3.0.10.tar.gz
sudo apt-get install libatlas-base-dev

git clone https://github.com/Kitt-AI/snowboy.git
mkdir -p ${lib_path}
mkdir -p ${lib_path}/include
mkdir -p ${lib_path}/lib
pushd snowboy
mv include/snowboy-detect.h ${lib_path}/include
mv resoures/common.res ${parent_path}/Data
libname=libsnowboy-detect.a
get_arch=`arch`
if grep -q "Raspberry Pi" /sys/firmware/devicetree/base/model; then
    mv lib/rpi/${libname} ${lib_path}/lib
elif [[ $get_arch =~ "x86_64" ]];then
    mv lib/ubuntu64/${libname} ${lib_path}/lib
elif [[ $get_arch =~ "aarch64" ]];then
    mv lib/aarch64-ubuntu1604/${libname} ${lib_path}/lib
else
    echo "unknown arch"
fi

popd
rm -rf snowboy
