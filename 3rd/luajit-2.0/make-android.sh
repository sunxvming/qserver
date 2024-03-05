#!/bin/bash

NDK=~/android-ndk-r7-crystax-5.beta3
NDKBIN=$NDK/toolchains/arm-linux-androideabi-4.6.3/prebuilt/linux-x86/bin/arm-linux-androideabi-
NDKFLAG="-march=armv7-a -mfloat-abi=softfp -mfpu=neon -Wl,--fix-cortex-a8 --sysroot $NDK/platforms/android-14/arch-arm"
make CC="gcc" HOST_CC="gcc -m32" CROSS=$NDKBIN TARGET_FLAGS="$NDKFLAG" TARGET_SYS=android
