#!/bin/bash
current_path=$(cd $(dirname $0); pwd)
parent_path=$(dirname ${current_path})
lib_path=${parent_path}/lib/silerovad
mkdir -p ${lib_path}/lib

git clone --recursive https://github.com/Microsoft/onnxruntime.git
pushd onnxruntime
pip install cmake
./build.sh --config RelWithDebInfo --build_shared_lib --parallel --compile_no_warning_as_error --skip_submodule_sync
cd build/Linux/RelWithDebInfo
mv libonnxruntime.so* ~/projects/lowpower-robot/lib/silerovad/lib/ 
popd
rm -rf onnxruntime
