# SPDX-License-Identifier: Apache-2.0
# Kindle Paperwhite 4 (10th generation, i.MX6SLL). DRAFT, step 1 of
# porting/android.md: the PW3 product as it is, under the PW4's name, to have
# the whole tree build for pw4 before the hardware-specific parts are
# replaced one by one (touch, Wi-Fi, PMIC/battery, backlight, the e-ink ABI of
# the 4.1 kernel, rc files, sepolicy).
$(call inherit-product, device/amazon/pw3/pw3.mk)

PRODUCT_NAME := pw4
PRODUCT_DEVICE := pw4
PRODUCT_BRAND := PW4LocalPort
PRODUCT_MODEL := Kindle Paperwhite 4 development port
PRODUCT_MANUFACTURER := Amazon
