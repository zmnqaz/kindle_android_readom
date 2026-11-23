# SPDX-License-Identifier: Apache-2.0
# Kindle Paperwhite 4 (10th generation, i.MX6SLL). DRAFT, step 1 of
# porting/android.md: the PW3 product as it is, under the PW4's name, to have
# the whole tree build for pw4 before the hardware-specific parts are
# replaced one by one (touch, Wi-Fi, PMIC/battery, backlight, the e-ink ABI of
# the 4.1 kernel, rc files, sepolicy).
# The PW4's own versions of files the PW3 product also installs. They come
# before the inheritance: of two copies to one destination the first is kept.
PRODUCT_COPY_FILES += \
    device/amazon/pw4/rootdir/init.pw4.usb.rc:root/init.pw3.usb.rc \
    device/amazon/pw4/hardware/init.pw4.usb.modules.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.pw3.usb.modules.rc \
    device/amazon/pw4/hardware/init.pw4.hardware.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.pw3.hardware.rc
# The kernel modules of the PW4 kernel build (not in this repository; staged
# by porting/stage-pw4-hardware.py).
-include vendor/amazon/pw4-hardware/pw4-hardware.mk

$(call inherit-product, device/amazon/pw3/pw3.mk)

PRODUCT_NAME := pw4
PRODUCT_DEVICE := pw4
PRODUCT_BRAND := PW4LocalPort
PRODUCT_MODEL := Kindle Paperwhite 4 development port
PRODUCT_MANUFACTURER := Amazon
