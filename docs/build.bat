@echo off

pushd "%~dp0"

mkdir build
cd build

cmake ..
cmake --build . --target doc --config Release

popd
