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
    device/amazon/pw4/hardware/init.pw4.hardware.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.pw3.hardware.rc \
    device/amazon/pw4/input/bd71827-power.kl:system/usr/keylayout/bd71827-power.kl \
    device/amazon/pw4/input/goodix-ts.idc:system/usr/idc/goodix-ts.idc
# Bluetooth: A2DP source to headphones (bluetooth/bluetooth.mk). Before the
# inheritance, for its audio_policy_configuration.xml and overlay to win.
$(call inherit-product, device/amazon/pw4/bluetooth/bluetooth.mk)
# The kernel modules of the PW4 kernel build (not in this repository; staged
# by porting/stage-pw4-hardware.py).
-include vendor/amazon/pw4-hardware/pw4-hardware.mk

$(call inherit-product, device/amazon/pw3/pw3.mk)

PRODUCT_NAME := pw4
PRODUCT_DEVICE := pw4
# As the PW3 (pw3.mk): a release is "Readom" on the model's own name, which
# Windows shows for the MTP device; test builds keep the names the test
# device's logged-in apps have known.
ifeq ($(TARGET_BUILD_VARIANT),user)
PRODUCT_BRAND := Readom
PRODUCT_MODEL := Kindle Paperwhite 4
else
PRODUCT_BRAND := PW4LocalPort
PRODUCT_MODEL := Kindle Paperwhite 4 development port
endif
PRODUCT_MANUFACTURER := Amazon

# Wi-Fi off at screen off (frameworks/opt/net/wifi patch 0005): bcmdhd is
# power-cycled across a suspend and returns as a new wiphy, which wificond does
# not survive. Off before autosuspend, on at screen on, as the stock Kindle.
PRODUCT_PROPERTY_OVERRIDES += ro.pw3.wifi_idle_ms=0
