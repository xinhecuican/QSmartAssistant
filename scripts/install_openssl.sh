#!/bin/bash
current_path=$(cd $(dirname $0); pwd)
parent_path=$(dirname ${current_path})
lib_path=${parent_path}/lib/openssl

wget https://www.openssl.org/source/openssl-1.1.1g.tar.gz
tar -xzf openssl-1.1.1g.tar.gz
rm openssl-1.1.1g.tar.gz
pushd openssl-1.1.1g
./config --prefix=${lib_path}
make -j `nproc`
make install
popd
rm -r openssl-1.1.1g
