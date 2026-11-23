#!/bin/bash
# Runs inside pw4_aosp81:18.04. Compile test of Readom's PW3 product on native x86_64.
set -eo pipefail
cd ${AOSP:?}
export LC_ALL=C USER=u
export PW3_BUILD_GOROOT=${GOROOT_1108:?}
export JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64
export PATH="$JAVA_HOME/bin:$PATH"
export JACK_SERVER_VM_ARGUMENTS="-Dfile.encoding=UTF-8 -XX:+TieredCompilation -Xmx8g"
export ANDROID_DATA=${AOSP:?}/out/host-android-data; mkdir -p $ANDROID_DATA
source build/envsetup.sh
lunch pw3-userdebug
J=${JOBS:-16}
make -j$J init adbd pw3-testfile-init pw3-init-check pw3-usb-check sepolicy
make -j$J systemimage
rm -f out/target/product/pw3/ramdisk.img
make -j$J ramdisk
