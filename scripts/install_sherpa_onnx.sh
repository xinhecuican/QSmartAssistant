#!/bin/bash
current_path=$(cd $(dirname $0); pwd)
parent_path=$(dirname ${current_path})
lib_path=${parent_path}/lib/sherpa_onnx
onnx_path=${parent_path}/lib/onnxruntime

# Check if onnxruntime library exists, if not, install it
if [ ! -f "${onnx_path}/lib/libonnxruntime.so" ]; then
    echo "${onnx_path}/lib/libonnxruntime.so not found, install it first"
    ./install_onnxruntime.sh
fi

git clone https://github.com/k2-fsa/sherpa-onnx.git
pushd sherpa-onnx
git checkout 94a040e396d5b612de1b54f173434e3489c357b4
mkdir build-shared
cd build-shared

SHERPA_ONNXRUNTIME_INCLUDE_DIR=${onnx_path}/include \
SHERPA_ONNXRUNTIME_LIB_DIR=${onnx_path}/lib \
cmake \
  -DSHERPA_ONNX_ENABLE_C_API=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON \
  -DCMAKE_INSTALL_PREFIX=/tmp/sherpa-onnx/shared \
  ..

make -j `nproc`
make install

mkdir -p ${lib_path}

chmod +x /tmp/sherpa-onnx/shared/lib/libonnxruntime.so
chmod +x /tmp/sherpa-onnx/shared/lib/libsherpa-onnx-c-api.so
mv -f /tmp/sherpa-onnx/shared/include ${lib_path}
mv -f /tmp/sherpa-onnx/shared/lib ${lib_path}
popd
rm -rf sherpa-onnx

pushd ../Data

wget https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-paraformer-zh-2024-03-09.tar.bz2
tar xvf sherpa-onnx-paraformer-zh-2024-03-09.tar.bz2
rm sherpa-onnx-paraformer-zh-2024-03-09.tar.bz2

wget https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/matcha-icefall-zh-baker.tar.bz2
tar xvf matcha-icefall-zh-baker.tar.bz2
rm matcha-icefall-zh-baker.tar.bz2

popd