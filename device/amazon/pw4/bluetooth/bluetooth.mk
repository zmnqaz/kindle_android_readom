# SPDX-License-Identifier: Apache-2.0
# PW4 Bluetooth: A2DP source to headphones only (BCM43430A1 on UART3 =
# /dev/ttymxc2, powered by the kernel's bt_pwr_ctrl rfkill switch, kernel
# branch bt-prep). DRAFT: included by pw4.mk before it inherits pw3.mk, so the
# PW4 copies of files the PW3 product also installs win (first copy kept).
#
# Packages:
#  - Bluetooth (packages/apps/Bluetooth) brings bluetooth.default, the
#    Fluoride stack, through LOCAL_REQUIRED_MODULES (its Android.mk:31).
#  - android.hardware.bluetooth@1.0-impl/-service: the HIDL HAL; the impl
#    dlopen()s libbt-vendor.so (hardware/interfaces/bluetooth/1.0/default/
#    vendor_interface.cc:30, 192). The service's own rc is installed by its
#    LOCAL_INIT_RC (same dir, Android.mk:22).
#  - libbt-vendor: hardware/broadcom/libbt, built only with
#    BOARD_HAVE_BLUETOOTH_BCM set (its Android.mk:3), see BoardConfig.mk.
#  - audio.a2dp.default: system/bt/audio_a2dp_hw (Android.bp:13), loaded in
#    audioserver for the "a2dp" module of audio_policy_configuration.xml.
PRODUCT_PACKAGES += \
    Bluetooth \
    libbt-vendor \
    android.hardware.bluetooth@1.0-impl \
    android.hardware.bluetooth@1.0-service \
    audio.a2dp.default

PW4_BT := device/amazon/pw4/bluetooth
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.bluetooth.xml:system/etc/permissions/android.hardware.bluetooth.xml \
    $(PW4_BT)/bt_vendor.conf:system/etc/bluetooth/bt_vendor.conf \
    $(PW4_BT)/init.pw4.bluetooth.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.pw4.bluetooth.rc \
    $(PW4_BT)/ueventd.rc:$(TARGET_COPY_OUT_VENDOR)/ueventd.rc \
    device/amazon/pw4/audio/audio_policy_configuration.xml:system/vendor/etc/audio_policy_configuration.xml \
    device/amazon/pw4/audio/a2dp_audio_policy_configuration.xml:system/vendor/etc/a2dp_audio_policy_configuration.xml

# Decoders for the reader's audio (PLAN.md section 3, step 0): Android only
# copies these in sdk_base.mk:95-100; MediaCodecsXmlParser looks in
# /odm/etc, /vendor/etc, /etc (MediaCodecsXmlParser.h:35-39). Not needed
# for A2DP itself; keep only if step 0 shows MediaCodecList is empty.
PRODUCT_COPY_FILES += \
    device/amazon/pw4/media/media_codecs.xml:system/vendor/etc/media_codecs.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_audio.xml:system/vendor/etc/media_codecs_google_audio.xml

# Only A2DP source (and GATT, which the enable path goes through); the rest
# of the profiles stay compiled but are not started.
DEVICE_PACKAGE_OVERLAYS += device/amazon/pw4/overlay

# The BD address: until a ro.bt.bdaddr_path / ro.boot.btmacaddr source from
# idme bt_mac_addr exists (PLAN.md 2.2), the HAL generates and persists a
# random one (hardware/interfaces/bluetooth/1.0/default/bluetooth_address.cc).
