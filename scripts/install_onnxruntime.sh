#!/bin/bash
current_path=$(cd $(dirname $0); pwd)
parent_path=$(dirname ${current_path})
lib_path=${parent_path}/lib/onnxruntime
mkdir -p ${lib_path}

get_arch=`arch`
version=1.17.1
if [[ $get_arch =~ "x86_64" ]];then
    wget https://github.com/microsoft/onnxruntime/releases/download/v${version}/onnxruntime-linux-x64-${version}.tgz
    tar xzf onnxruntime-linux-x64-${version}.tgz
    pushd onnxruntime-linux-x64-${version}
    cp -r include/ ${lib_path}/
    cp -r lib/ ${lib_path}/
    popd
    rm -r onnxruntime-linux-x64-${version} onnxruntime-linux-x64-${version}.tgz
elif [[ $get_arch =~ "aarch64" ]];then
    wget https://github.com/microsoft/onnxruntime/releases/download/v${version}/onnxruntime-linux-aarch64-${version}.tgz
    tar xzf onnxruntime-linux-aarch64-${version}.tgz
    pushd onnxruntime-linux-aarch64-${version}
    mv include ${lib_path}/include
    mv lib ${lib_path}/lib
    popd
    rm -r onnxruntime-linux-aarch64-${version} onnxruntime-linux-aarch64-${version}.tgz
else
    git clone --recursive https://github.com/Microsoft/onnxruntime.git
    pushd onnxruntime
    pip install cmake
    ./build.sh --config RelWithDebInfo --build_shared_lib --parallel --compile_no_warning_as_error --skip_submodule_sync
    cd build/Linux/RelWithDebInfo
    mv libonnxruntime.so* ~/projects/lowpower-robot/lib/onnxruntime/lib/ 
    popd
    rm -rf onnxruntime
fi
