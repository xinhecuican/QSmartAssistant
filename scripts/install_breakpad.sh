#!/bin/bash
current_path=$(cd $(dirname $0); pwd)
parent_path=$(dirname ${current_path})
lib_path=${parent_path}/lib/breakpad
mkdir -p ${lib_path}

git clone https://github.com/google/breakpad.git
pushd breakpad
git clone git@github.com:linux-on-ibm-z/linux-syscall-support.git src/third_party/lss
./configure --prefix=${lib_path} && make -j`nproc`
if [ $? -ne 0 ]; then
	echo "make失败，请在scripts/breakpad下修改错误"
else
make install
popd
rm -rf breakpad
fi
