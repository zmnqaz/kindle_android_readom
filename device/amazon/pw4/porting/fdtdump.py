"""Print a flattened device tree (.dtb) as text, like `dtc -I dtb -O dts` but without needing dtc.

Usage: python -I fdtdump.py <file.dtb> [path-prefix ...]
With prefixes, only nodes whose path starts with one of them are printed.
"""
import struct
import sys


def be32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def fmt(name, val):
    if not val:
        return ""
    printable = all(32 <= c < 127 or c == 0 for c in val) and val[-1] == 0 and b"\0\0" not in val
    if printable and name not in ("reg", "ranges") and not name.endswith("phandle"):
        return " = " + ", ".join('"%s"' % s.decode() for s in val[:-1].split(b"\0"))
    if len(val) % 4 == 0:
        return " = <" + " ".join("0x%08x" % be32(val, i) for i in range(0, len(val), 4)) + ">"
    return " = [" + " ".join("%02x" % c for c in val) + "]"


def main():
    data = open(sys.argv[1], "rb").read()
    prefixes = sys.argv[2:]
    magic, total, off_struct, off_strings, off_rsv, version = struct.unpack_from(">6I", data, 0)
    assert magic == 0xD00DFEED, "not a dtb"
    print("/* size %d, version %d */" % (total, version))
    o = off_rsv
    while True:
        addr, size = struct.unpack_from(">QQ", data, o)
        o += 16
        if not size:
            break
        print("/memreserve/ 0x%x 0x%x;" % (addr, size))
    path = []
    o = off_struct
    while True:
        tok = be32(data, o)
        o += 4
        if tok == 1:
            end = data.index(b"\0", o)
            path.append(data[o:end].decode() or "")
            o = (end + 4) & ~3
            p = "/" + "/".join(path[1:])
            if not prefixes or any(p.startswith(x) for x in prefixes):
                print("%s%s {" % ("  " * (len(path) - 1), p))
        elif tok == 2:
            path.pop()
        elif tok == 3:
            ln, nameoff = struct.unpack_from(">II", data, o)
            o += 8
            val = data[o:o + ln]
            o = (o + ln + 3) & ~3
            name = data[off_strings + nameoff:data.index(b"\0", off_strings + nameoff)].decode()
            p = "/" + "/".join(path[1:])
            if not prefixes or any(p.startswith(x) for x in prefixes):
                print("%s%s%s;" % ("  " * len(path), name, fmt(name, val)))
        elif tok == 4:
            continue
        elif tok == 9:
            break
        else:
            raise ValueError("bad token %d at %d" % (tok, o))


if __name__ == "__main__":
    main()
