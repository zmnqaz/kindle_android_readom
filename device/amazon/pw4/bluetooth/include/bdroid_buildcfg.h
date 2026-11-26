/*
 * PW4 Fluoride build configuration (BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR,
 * build/make/core/soong_config.mk:90 -> system/bt/build/fluoride.go:50-58;
 * included first by system/bt/include/bt_target.h:32-34).
 *
 * Kept small on purpose: switching protocols off at compile time
 * (HID_HOST_INCLUDED, PAN_INCLUDED, BTM_SCO_INCLUDED ...) is not well tested
 * in 8.1 and breaks the build in places; unused profiles are switched off at
 * run time in overlay/packages/apps/Bluetooth/res/values/config.xml.
 */
#ifndef _BDROID_BUILDCFG_H
#define _BDROID_BUILDCFG_H

#define BTM_DEF_LOCAL_NAME "Kindle PW4"

/* A2DP source only (bt_target.h:82-84 has the same default). */
#define BTA_AV_SINK_INCLUDED FALSE

#endif
