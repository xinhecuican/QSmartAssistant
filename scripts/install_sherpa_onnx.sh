#!/bin/bash
current_path=$(cd $(dirname $0); pwd)
parent_path=$(dirname ${current_path})
lib_path=${parent_path}/lib/sherpa_onnx

git clone https://github.com/k2-fsa/sherpa-onnx.git
pushd sherpa-onnx
mkdir build-shared
cd build-shared

cmake \
  -DSHERPA_ONNX_ENABLE_C_API=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON \
  -DCMAKE_INSTALL_PREFIX=/tmp/sherpa-onnx/shared \
  ..

make -j `nproc`
make install

mkdir -p ${lib_path}

mv -f /tmp/sherpa-onnx/shared/include ${lib_path}
mv -f /tmp/sherpa-onnx/shared/lib ${lib_path}
popd
rm -rf sherpa-onnx
