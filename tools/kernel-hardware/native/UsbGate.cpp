// SPDX-License-Identifier: Apache-2.0
#include "UsbGate.h"

namespace pw3 {
namespace {
const char kPhase[] = "sys.pw3.usb.phase";
const char kSerial[] = "persist.pw3.usb.serial";
// g_ffs too: the Android gadget registers the functionfs file system itself.
const char* const kCompeting[] = {
    "g_ffs", "g_ether", "g_file_storage", "g_serial", "g_mass_storage", "g_multi",
    "g_zero", "g_hid", "g_audio", "g_webcam", "g_printer", "g_cdc",
    "g_ncm", "g_acm_ms", "g_nokia", "g_dbgp", "g_midi"
};
// USB is part of every boot (MTP; adb when Android's own switch turns it on), but
// only on a build whose adb requires authentication.
bool Allowed(Platform& p) {
    return p.Get("ro.secure") == "1" && p.Get("ro.adb.secure") == "1";
}
bool NoCompetitor(Platform& p) {
    for (const char* module : kCompeting)
        if (p.Module(module) != Presence::Absent) return false;
    return true;
}
bool Otg(Platform& p) {
    return p.ModuleLive("fsl_otg_arc") && p.DriverBound("fsl-usb2-otg");
}
bool Udc(Platform& p) {
    return Otg(p) && p.ModuleLive("arcotg_udc") && p.DriverBound("fsl-usb2-udc");
}
bool Gadget(Platform& p) {
    return Udc(p) && p.ModuleLive("g_android") && p.AndroidUsb() && p.FunctionFs();
}
// The PW4's 4.1 kernel: ChipIdea (built in) and the configfs gadget, whose
// modules init.pw3.usb.modules.rc loads (device/amazon/pw4): configfs,
// libcomposite and usb_f_fs live and functionfs known, checked one by one in
// the configfs-serial stage below so that a failure names its check.
// The check a stage stopped at, for the log: without it a failed stage said
// only that it failed (PW4, every start through rc2: the serial step).
const char* gFailure = "";
bool Fail(Platform& p, const char* why = "unnamed check") {
    gFailure = why;
    p.Set(kPhase, "error");
    return false;
}
bool MakeSerial(Platform& p) {
    if (ValidSerial(p.Get(kSerial))) return true;
    std::string serial;
    if (!p.RandomSerial(&serial) || !ValidSerial(serial)) { gFailure = "random serial"; return false; }
    if (!p.Set(kSerial, serial)) { gFailure = "setting the serial property"; return false; }
    if (p.Get(kSerial) != serial) { gFailure = "reading the serial property back"; return false; }
    return true;
}
}

bool ValidSerial(const std::string& serial) {
    if (serial.size() != 32) return false;
    bool nonzero = false;
    for (char c : serial) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        nonzero |= c != '0';
    }
    return nonzero;
}

const char* UsbGateFailure() { return gFailure; }

bool RunUsbGate(Platform& p, const std::string& stage) {
    gFailure = "";
    if (!Allowed(p)) return Fail(p, "ro.secure and ro.adb.secure are not both 1");
    if (!NoCompetitor(p)) return Fail(p, "another gadget module is present or unreadable");
    if (stage == "prepare") {
        if (p.Get(kPhase) != "none") return Fail(p);
        for (const char* module : {"fsl_otg_arc", "arcotg_udc", "g_android"})
            if (p.Module(module) != Presence::Absent) return Fail(p);
        return p.Set(kPhase, "prepared");
    }
    if (stage == "serial") {
        // The USB serial: random, made once per device and kept (a computer then knows
        // the device again, instead of seeing a new one at every boot); never the
        // factory serial. Needs persistent properties, so it runs at boot.
        if (p.Get(kPhase) != "gadget-bound" || !Gadget(p)) return Fail(p);
        if (!MakeSerial(p)) return Fail(p);
        return p.Set(kPhase, "ready");
    }
    if (stage == "configfs-serial") {
        // The same serial for the configfs gadget, before the gadget is made.
        if (p.Get(kPhase) != "configfs-loaded") return Fail(p, "phase is not configfs-loaded");
        if (!p.ModuleLive("configfs")) return Fail(p, "configfs module not live");
        if (!p.ModuleLive("libcomposite")) return Fail(p, "libcomposite module not live");
        if (!p.ModuleLive("usb_f_fs")) return Fail(p, "usb_f_fs module not live");
        if (!p.FunctionFs()) return Fail(p, "functionfs not in /proc/filesystems");
        if (!MakeSerial(p)) return Fail(p, gFailure);
        if (!p.Set(kPhase, "configfs-ready")) return Fail(p, "setting the phase");
        return true;
    }
    const char* expected = nullptr;
    const char* next = nullptr;
    if (stage == "otg") { expected = "prepared"; next = "otg-bound"; }
    else if (stage == "udc") { expected = "otg-bound"; next = "udc-bound"; }
    else if (stage == "gadget") { expected = "udc-bound"; next = "gadget-bound"; }
    else return Fail(p);
    if (p.Get(kPhase) != expected) return Fail(p);
    // Module insertion may return success despite a failed platform probe.
    // Retry 100 times at 20 ms intervals; never unload/reset/power-toggle after failure.
    for (unsigned i = 0; i <= 100; ++i) {
        if (!Allowed(p) || !NoCompetitor(p) || p.Get(kPhase) != expected)
            return Fail(p);
        bool ready = stage == "otg" ? Otg(p) : stage == "udc" ? Udc(p) : Gadget(p);
        if (ready) return p.Set(kPhase, next);
        if (i != 100) p.Wait();
    }
    return Fail(p);
}
}
