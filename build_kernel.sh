#!/bin/bash

export CROSS_COMPILE=$HOME/compiler/bin/aarch64-linux-android-
export ARCH=arm64

export PLATFORM_VERSION=11
export ANDROID_MAJOR_VERSION=r
make -C $(pwd) O=$(pwd)/out KCFLAGS=-w  exynos9810_defconfig
make -C $(pwd) O=$(pwd)/out KCFLAGS=-w  -j16
