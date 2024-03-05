#!/bin/bash

XCODE=`xcode-select -print-path`
SDK=$XCODE/Platforms/MacOSX.platform/Developer

make clean

make CC="gcc -m64 -arch x86_64" all
cp src/libluajit.a ../../fancy-engine/Dependencies/luajit/lib64/libluajit-mac.a
cp src/libluajit.a ../../fancy-3d/Dependencies/libluajit-mac.a

#make CC="gcc -m32 -arch i386" all
#cp src/libluajit.a ../../fancy-engine/Dependencies/luajit/lib/libluajit-mac.a
