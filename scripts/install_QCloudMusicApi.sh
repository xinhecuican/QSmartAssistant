current_path=$(cd $(dirname $0); pwd)
parent_path=$(dirname ${current_path})
lib_path=${parent_path}/lib/netease_music

git clone https://github.com/xinhecuican/QCloudMusicApi.git
pushd QCloudMusicApi
git submodule update --init --recursive
mkdir build
cd build
cmake ..
make -j `nproc`
make install
rm -r ${lib_path}/lib
mv bin ${lib_path}/lib
cp lib/lib* ${lib_path}/lib/
rm -r ${lib_path}/include
mv include ${lib_path}/include

popd
rm -rf QCloudMusicApi
