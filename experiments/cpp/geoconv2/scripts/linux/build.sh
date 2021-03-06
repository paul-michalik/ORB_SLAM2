#!/bin/bash

ScriptDir=$(realpath $(dirname $0))
ProjectDir=$(realpath ${ScriptDir}/../..)
cd ${ProjectDir}

Platform=`uname -m`
Toolset=gcc.`gcc -dumpversion`
Buildtype=Release
cores=$(nproc)

if [ ! -z "$1" ] 
then
    Platform="$1"
fi

if [ ! -z "$2" ] 
then
    Toolset="$2"
fi

if [ ! -z "$3" ] 
then
    Buildtype="$3"
fi

BuildDir="products/cmake.make.linux.${Platform}.${Toolset}.${Buildtype}"

mkdir -p "${BuildDir}"

cmake ${ProjectDir} \
    -B${BuildDir} \
    -DCMAKE_BUILD_TYPE=${Buildtype} \
    -DBUILD_TESTING=ON \
    -DCMAKE_INSTALL_PREFIX=/usr/local

cmake --build ${BuildDir} -- -j${cores}
