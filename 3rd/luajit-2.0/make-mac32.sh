#!/bin/bash

XCODE=`xcode-select -print-path`
SDK=$XCODE/Platforms/MacOSX.platform/Developer

SDKFLAG="-g -arch i386 -isysroot $SDK/SDKs/MacOSX10.11.sdk"
make CC="llvm-gcc -g -O0" HOST_CC="llvm-gcc -g -m32 -O0 " TARGET_FLAGS="$SDKFLAG" TARGET_SYS=Darwin

#SDKFLAG="-g -arch x86_64 -isysroot $SDK/SDKs/MacOSX10.11.sdk"
#make CC="llvm-gcc -g -O0" HOST_CC="llvm-gcc -g -O0 " TARGET_FLAGS="$SDKFLAG" TARGET_SYS=Darwin
