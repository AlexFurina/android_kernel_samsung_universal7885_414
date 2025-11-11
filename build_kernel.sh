#!/bin/bash

export CROSS_COMPILE=$(pwd)/toolchain/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android-
export CC=$(pwd)/toolchain/clang/host/linux-x86/clang-r353983c/bin/clang
export CLANG_TRIPLE=$(pwd)/toolchain/clang/host/linux-x86/clang-r353983c/bin/aarch64-linux-gnu-
export ARCH=arm64


export PLATFORM_VERSION=11
export ANDROID_MAJOR_VERSION=r
make -C $(pwd) O=$(pwd)/out KCFLAGS=-w  exynos7885-a10eudcm_defconfig
make -C $(pwd) O=$(pwd)/out KCFLAGS=-w  -j16

cp out/arch/arm64/boot/Image $(pwd)/arch/arm64/boot/Image
