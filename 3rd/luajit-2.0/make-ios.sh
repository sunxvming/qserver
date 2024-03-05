#!/bin/bash

XCODE=`xcode-select -print-path`
SDK=$XCODE/Platforms/iPhoneOS.platform/Developer
#SDKBIN=$SDK/usr/bin/
#SDKFLAG="-arch armv7 -isysroot $SDK/SDKs/current"
SDKBIN=/usr/bin/

## armv7
SDKFLAG="-arch armv7 -miphoneos-version-min=5.0 -isysroot $SDK/SDKs/iPhoneOS.sdk"
make CC="llvm-gcc" HOST_CC="llvm-gcc -m32 -arch i686" CROSS=$SDKBIN TARGET_FLAGS="$SDKFLAG" TARGET_SYS=iOS

## arm64
#SDKFLAG="-arch arm64 -miphoneos-version-min=5.0 -isysroot $SDK/SDKs/iPhoneOS.sdk"
#make CC="llvm-gcc" HOST_CC="llvm-gcc" CROSS=$SDKBIN TARGET_FLAGS="$SDKFLAG" TARGET_SYS=iOS
