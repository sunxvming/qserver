#!/bin/bash

XCODE=`xcode-select -print-path`
SDK=$XCODE/Platforms/iPhoneSimulator.platform/Developer
SDKBIN=$SDK/usr/bin/
SDKFLAG="-arch i386 -isysroot $SDK/SDKs/current"
make CC="gcc" HOST_CC="gcc -m32 -arch i386" CROSS=$SDKBIN TARGET_FLAGS="$SDKFLAG" TARGET_SYS=iOS XCFLAGS=-DLUAJIT_DISABLE_JIT
