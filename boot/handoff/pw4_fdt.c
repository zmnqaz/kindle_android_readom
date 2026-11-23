/* SPDX-License-Identifier: GPL-2.0-only */
#include "pw4_fdt.h"

#define TOKEN_BEGIN 1u
#define TOKEN_END_NODE 2u
#define TOKEN_PROP 3u
#define TOKEN_NOP 4u
#define TOKEN_END 9u

static pw_u32 be32(const pw_u8 *p)
{
    return ((pw_u32)p[0] << 24) | ((pw_u32)p[1] << 16) | ((pw_u32)p[2] << 8) | p[3];
}
static pw_u32 get32(const pw_u8 *p)
{
    return (pw_u32)p[0] | ((pw_u32)p[1] << 8) | ((pw_u32)p[2] << 16) | ((pw_u32)p[3] << 24);
}
static void zero(void *p, pw_u32 n)
{
    pw_u8 *d = p;
    while (n--) *d++ = 0;
}
static pw_u64 end(struct pw_range r)
{
    return (pw_u64)r.start + r.size;
}
static int contains(struct pw_range outer, struct pw_range inner)
{
    return inner.start >= outer.start && end(inner) <= end(outer);
}
static int inside_any(struct pw_range r, const struct pw_range *map, pw_u32 n)
{
    pw_u32 i;
    for (i = 0; i < n; ++i) if (contains(map[i], r)) return 1;
    return 0;
}
static int valid_map(const struct pw_range *r, pw_u32 n, int disjoint)
{
    pw_u32 i, j;
    for (i = 0; i < n; ++i) {
        if (!pw_range_valid(r[i])) return 0;
        for (j = 0; disjoint && j < i; ++j)
            if (pw_ranges_overlap(r[i], r[j])) return 0;
    }
    return 1;
}
static int buffers_overlap(const void *a, pw_u32 an, const void *b, pw_u32 bn)
{
    pw_uptr x = (pw_uptr)a, y = (pw_uptr)b;
    return x <= y ? y - x < an : x - y < bn;
}
/* NUL-terminated within [p, p + room); returns the length without the NUL,
 * or room when there is none. */
static pw_u32 bounded_len(const pw_u8 *p, pw_u32 room)
{
    pw_u32 i;
    for (i = 0; i < room && p[i]; ++i) {}
    return i;
}
static int equals(const pw_u8 *p, pw_u32 n, const char *text)
{
    pw_u32 i;
    for (i = 0; i < n && text[i] && p[i] == (pw_u8)text[i]; ++i) {}
    return i == n && !text[i];
}
/* "memory" or "memory@<unit>": the node /memory the kernel scans. */
static int memory_node(const pw_u8 *name, pw_u32 n)
{
    return (n == 6 || (n > 7 && name[6] == '@')) && equals(name, 6, "memory");
}
static int cell_value(const pw_u8 *v, pw_u32 len, pw_u32 *out)
{
    if (len == 4) { *out = be32(v); return 1; }
    if (len == 8 && !be32(v)) { *out = be32(v + 4); return 1; }
    return 0;
}

int pw_fdt_parse(const pw_u8 *data, pw_u32 bytes, const char *cmdline,
                 struct pw_fdt *out)
{
    struct pw_fdt f;
    pw_u32 total, off_struct, off_strings, off_rsv, version, last, size_strings, size_struct;
    pw_u32 pos, limit, depth = 0, i, node = 0, chosen_seen = 0, addr_cells = 0, size_cells = 0;
    pw_u32 initrd_start = 0, initrd_end = 0, have_start = 0, have_end = 0, root_done = 0;
    enum { OTHER, CHOSEN, MEMORY, RESERVED } kind = OTHER;
    if (!data || !out) return PW_E_ARGUMENT;
    zero(out, sizeof(*out));
    zero(&f, sizeof(f));
    if (bytes < 40 || bytes > PW_FDT_CAP || (bytes & 3)) return PW_E_LENGTH;
    if (be32(data) != PW_FDT_MAGIC) return PW_E_FORMAT;
    total = be32(data + 4); off_struct = be32(data + 8); off_strings = be32(data + 12);
    off_rsv = be32(data + 16); version = be32(data + 20); last = be32(data + 24);
    size_strings = be32(data + 32); size_struct = be32(data + 36);
    if (total < 40 || total > bytes || version < 17 || last > 17) return PW_E_FORMAT;
    for (i = total; i < bytes; ++i) if (data[i]) return PW_E_FORMAT;
    if ((off_rsv & 7) || (off_struct & 3) || off_rsv < 40 || off_struct < 40 || off_strings < 40 ||
        off_struct > total || size_struct > total - off_struct ||
        off_strings > total || size_strings > total - off_strings ||
        size_struct < 8 || (size_struct & 3)) return PW_E_RANGE;
    /* Reservation map: 16-byte {address, size} pairs ending in a zero pair. */
    for (pos = off_rsv;; pos += 16) {
        pw_u32 hi_a, lo_a, hi_s, lo_s;
        if (pos > total - 16) return PW_E_RANGE;
        hi_a = be32(data + pos); lo_a = be32(data + pos + 4);
        hi_s = be32(data + pos + 8); lo_s = be32(data + pos + 12);
        if (!hi_a && !lo_a && !hi_s && !lo_s) break;
        if (hi_a || hi_s || f.rsv_count == PW_MAX_RANGES) return PW_E_CAPACITY;
        f.rsv[f.rsv_count].start = lo_a; f.rsv[f.rsv_count].size = lo_s;
        if (!pw_range_valid(f.rsv[f.rsv_count++])) return PW_E_RANGE;
    }
    /* The structure block, token by token. */
    pos = off_struct; limit = off_struct + size_struct;
    for (;;) {
        pw_u32 token;
        if (pos > limit - 4) return PW_E_LENGTH;
        token = be32(data + pos); pos += 4;
        if (token == TOKEN_NOP) continue;
        if (token == TOKEN_BEGIN) {
            const pw_u8 *name = data + pos;
            pw_u32 n = bounded_len(name, limit - pos);
            if (n == limit - pos) return PW_E_FORMAT;
            if (!node++ ? n != 0 : depth == 0) return PW_E_FORMAT;   /* one root, unnamed */
            if (++depth > PW_FDT_MAX_DEPTH) return PW_E_CAPACITY;
            pos += (n + 4) & ~3u;
            if (depth == 2) {
                root_done = 1;
                if (n == 6 && equals(name, 6, "chosen")) {
                    if (chosen_seen++) return PW_E_DUPLICATE;
                    kind = CHOSEN;
                } else if (memory_node(name, n)) {
                    if (addr_cells != 1 || size_cells != 1) return PW_E_CONTRACT;
                    kind = MEMORY;
                } else if (n == 15 && equals(name, n, "reserved-memory")) {
                    if (addr_cells != 1 || size_cells != 1) return PW_E_CONTRACT;
                    kind = RESERVED;
                } else kind = OTHER;
            }
            continue;
        }
        if (token == TOKEN_END_NODE) {
            if (!depth) return PW_E_FORMAT;
            if (--depth < 2) kind = OTHER;
            continue;
        }
        if (token == TOKEN_PROP) {
            pw_u32 len, nameoff, n;
            const pw_u8 *value, *name;
            if (!depth || pos > limit - 8) return PW_E_FORMAT;
            len = be32(data + pos); nameoff = be32(data + pos + 4); pos += 8;
            if (len > limit - pos || nameoff >= size_strings) return PW_E_RANGE;
            name = data + off_strings + nameoff;
            n = bounded_len(name, size_strings - nameoff);
            if (n == size_strings - nameoff) return PW_E_FORMAT;
            value = data + pos;
            pos += (len + 3) & ~3u;
            if (pos > limit) return PW_E_RANGE;
            if (depth == 1) {
                /* Root properties precede the root's children in a blob. */
                if (root_done) return PW_E_FORMAT;
                if (n == 14 && equals(name, n, "#address-cells") && !cell_value(value, len, &addr_cells)) return PW_E_FORMAT;
                if (n == 11 && equals(name, n, "#size-cells") && !cell_value(value, len, &size_cells)) return PW_E_FORMAT;
                continue;
            }
            if (depth == 3 && kind == RESERVED && n == 3 && equals(name, n, "reg")) {
                if (!len || (len & 7)) return PW_E_FORMAT;
                for (i = 0; i < len; i += 8) {
                    if (f.rsv_count == PW_MAX_RANGES) return PW_E_CAPACITY;
                    f.rsv[f.rsv_count].start = be32(value + i);
                    f.rsv[f.rsv_count].size = be32(value + i + 4);
                    if (!pw_range_valid(f.rsv[f.rsv_count++])) return PW_E_RANGE;
                }
                continue;
            }
            if (depth != 2) continue;
            if (kind == CHOSEN) {
                if (n == 8 && equals(name, n, "bootargs")) {
                    if (f.has_bootargs++) return PW_E_DUPLICATE;
                    if (!len || value[len - 1] || bounded_len(value, len) != len - 1 ||
                        len - 1 >= PW_CMDLINE_CAP) return PW_E_FORMAT;
                    if (cmdline && !equals(value, len - 1, cmdline)) return PW_E_CONTRACT;
                } else if (n == 18 && equals(name, n, "linux,initrd-start")) {
                    if (have_start++ || !cell_value(value, len, &initrd_start)) return PW_E_FORMAT;
                } else if (n == 16 && equals(name, n, "linux,initrd-end")) {
                    if (have_end++ || !cell_value(value, len, &initrd_end)) return PW_E_FORMAT;
                }
            } else if (kind == MEMORY && n == 3 && equals(name, n, "reg")) {
                if (!len || (len & 7)) return PW_E_FORMAT;
                for (i = 0; i < len; i += 8) {
                    if (f.mem_count == PW_MAX_RANGES) return PW_E_CAPACITY;
                    f.mem[f.mem_count].start = be32(value + i);
                    f.mem[f.mem_count].size = be32(value + i + 4);
                    ++f.mem_count;
                }
                if (!valid_map(f.mem, f.mem_count, 1)) return PW_E_RANGE;
            }
            continue;
        }
        if (token == TOKEN_END) {
            if (depth || !node) return PW_E_FORMAT;
            /* Only the end of the block may follow. */
            if (pos != limit) return PW_E_FORMAT;
            break;
        }
        return PW_E_FORMAT;
    }
    if (!f.mem_count) return PW_E_CONTRACT;
    if (cmdline && !f.has_bootargs) return PW_E_CONTRACT;
    if (have_start != have_end) return PW_E_CONTRACT;
    if (have_start) {
        if (initrd_end <= initrd_start) return PW_E_RANGE;
        f.has_initrd = 1;
        f.initrd.start = initrd_start; f.initrd.size = initrd_end - initrd_start;
    }
    f.totalsize = total;
    *out = f;
    return PW_VALID_DATA_ONLY;
}

int pw_plan_validate_fdt(const struct pw_plan *p, const pw_u8 *fdt,
                         pw_u32 fdt_bytes, const char *cmdline,
                         const pw_u8 *zimage, pw_u32 zimage_bytes)
{
    struct pw_fdt t;
    pw_u32 i, j, seen = 0, blob = 0, kernel = 0, initrd = 0;
    struct pw_range d;
    int rc;
    if (!p || !zimage || !cmdline) return PW_E_ARGUMENT;
    if (!p->ram_count || p->ram_count > PW_MAX_RANGES ||
        p->forbidden_count > PW_MAX_RANGES || !p->footprint_count ||
        p->footprint_count > PW_MAX_RANGES || p->segment_count < 2 ||
        p->segment_count > PW_MAX_SEGMENTS) return PW_E_LENGTH;
    /* Device tree boot: the kernel ignores r1 once r2 holds a blob, and the
     * plan says so by carrying ~0 rather than a machine number. */
    if (p->machine_id != 0xffffffffu || p->expected_machine_id != 0xffffffffu)
        return PW_E_CONTRACT;
    if (p->identity_limit <= p->boot_ram_base ||
        (p->identity_limit & (PW_PAGE_SIZE - 1))) return PW_E_CONTRACT;
    rc = pw_fdt_parse(fdt, fdt_bytes, cmdline, &t);
    if (rc) return rc;
    if (!valid_map(p->ram, p->ram_count, 1) ||
        !valid_map(p->forbidden, p->forbidden_count, 0) ||
        !valid_map(p->footprint, p->footprint_count, 0)) return PW_E_RANGE;
    for (i = 0; i < p->ram_count; ++i)
        if (!inside_any(p->ram[i], t.mem, t.mem_count) ||
            p->ram[i].start < p->boot_ram_base) return PW_E_CONTRACT;
    if (p->ram[0].start != p->boot_ram_base) return PW_E_CONTRACT;
    for (i = 0; i < p->segment_count; ++i) {
        const struct pw_segment *s = &p->segments[i];
        if (s->kind < PW_ZIMAGE || s->kind > PW_INITRAMFS) return PW_E_FORMAT;
        if (seen & (1u << s->kind)) return PW_E_DUPLICATE;
        seen |= 1u << s->kind;
        if (s->kind == PW_ZIMAGE) kernel = i;
        if (s->kind == PW_ATAGS) blob = i;      /* the boot data: here a blob */
        if (s->kind == PW_INITRAMFS) initrd = i;
        d = s->destination;
        if (!pw_range_valid(d) || !s->buffer_bytes || s->buffer_bytes > d.size)
            return PW_E_RANGE;
        if (end(d) >= 0xffffffffull || end(d) > p->identity_limit) return PW_E_RANGE;
        if ((d.start | d.size) & (PW_PAGE_SIZE - 1)) return PW_E_ALIGNMENT;
        if (!inside_any(d, p->ram, p->ram_count)) return PW_E_RANGE;
        for (j = 0; j < i; ++j)
            if (pw_ranges_overlap(d, p->segments[j].destination)) return PW_E_OVERLAP;
        for (j = 0; j < p->forbidden_count; ++j)
            if (pw_ranges_overlap(d, p->forbidden[j])) return PW_E_OVERLAP;
        /* Nothing is copied over memory the blob itself reserves. */
        for (j = 0; j < t.rsv_count; ++j)
            if (pw_ranges_overlap(d, t.rsv[j])) return PW_E_OVERLAP;
    }
    if (!(seen & (1u << PW_ZIMAGE)) || !(seen & (1u << PW_ATAGS)))
        return PW_E_CONTRACT;
    if (p->entry != p->segments[kernel].destination.start ||
        p->segments[blob].buffer_bytes != fdt_bytes || t.totalsize > fdt_bytes)
        return PW_E_CONTRACT;
    /* 4.1 head.S maps the two 1 MiB sections from the blob's section on; a
     * blob that crossed the second would be read past its mapping. */
    if ((p->segments[blob].destination.start & 0xfffffu) + t.totalsize > 0x200000u)
        return PW_E_CONTRACT;
    if (zimage_bytes < 0x30 || zimage_bytes != p->segments[kernel].buffer_bytes ||
        get32(zimage + 0x24) != 0x016f2818u) return PW_E_FORMAT;
    if (get32(zimage + 0x2c) <= get32(zimage + 0x28) ||
        (pw_u64)get32(zimage + 0x2c) - get32(zimage + 0x28) > zimage_bytes)
        return PW_E_FORMAT;
    if (t.has_initrd != !!(seen & (1u << PW_INITRAMFS))) return PW_E_CONTRACT;
    if (t.has_initrd && (t.initrd.start != p->segments[initrd].destination.start ||
                         t.initrd.size != p->segments[initrd].buffer_bytes))
        return PW_E_CONTRACT;
    for (i = 0; i < p->footprint_count; ++i) {
        d = p->footprint[i];
        if (!inside_any(d, p->ram, p->ram_count) || end(d) > p->identity_limit)
            return PW_E_RANGE;
        for (j = 0; j < p->forbidden_count; ++j)
            if (pw_ranges_overlap(d, p->forbidden[j])) return PW_E_OVERLAP;
        for (j = 0; j < t.rsv_count; ++j)
            if (pw_ranges_overlap(d, t.rsv[j])) return PW_E_OVERLAP;
        if (pw_ranges_overlap(d, p->segments[blob].destination) ||
            (t.has_initrd && pw_ranges_overlap(d, p->segments[initrd].destination)))
            return PW_E_OVERLAP;
    }
    return PW_VALID_DATA_ONLY;
}

/* ---- pw_fdt_rewrite: the device's side ---------------------------------- */

struct writer { pw_u8 *p; pw_u32 cap, at; int bad; };
static void w_bytes(struct writer *w, const pw_u8 *s, pw_u32 n)
{
    if (w->bad || n > w->cap - w->at) { w->bad = 1; return; }
    while (n--) w->p[w->at++] = *s++;
}
static void w_be32(struct writer *w, pw_u32 v)
{
    pw_u8 b[4];
    b[0] = (pw_u8)(v >> 24); b[1] = (pw_u8)(v >> 16); b[2] = (pw_u8)(v >> 8); b[3] = (pw_u8)v;
    w_bytes(w, b, 4);
}
static void w_pad(struct writer *w)
{
    static const pw_u8 z[4];
    w_bytes(w, z, (4 - (w->at & 3)) & 3);
}
static void w_prop(struct writer *w, pw_u32 nameoff, const pw_u8 *value, pw_u32 len)
{
    w_be32(w, TOKEN_PROP); w_be32(w, len); w_be32(w, nameoff);
    w_bytes(w, value, len); w_pad(w);
}
static pw_u32 text_len(const char *s)
{
    pw_u32 n = 0;
    while (s[n]) ++n;
    return n;
}
/* Offset of `name` as a whole string in the strings block, or ~0. */
static pw_u32 find_string(const pw_u8 *strings, pw_u32 size, const char *name)
{
    pw_u32 n = text_len(name), at;
    for (at = 0; at + n < size; ++at)
        if ((!at || !strings[at - 1]) && !strings[at + n] && equals(strings + at, n, name))
            return at;
    return 0xffffffffu;
}

int pw_fdt_rewrite(const pw_u8 *src, pw_u32 src_bytes, const char *cmdline,
                   struct pw_range initrd, struct pw_range zone,
                   pw_u8 *dst, pw_u32 capacity, pw_u32 *used)
{
    static const char *const names[3] = {"bootargs", "linux,initrd-start", "linux,initrd-end"};
    struct pw_fdt t, check;
    struct writer w;
    pw_u32 off_struct, off_strings, off_rsv, size_strings, size_struct, limit, pos, depth = 0;
    pw_u32 nameoff[3], extra = 0, i, cmd_len, struct_start, strings_start, in_chosen = 0, chosen_seen = 0;
    pw_u8 cell[8];
    int rc;
    if (!used) return PW_E_ARGUMENT;
    *used = 0;
    if (!src || !dst || !cmdline || buffers_overlap(src, src_bytes, dst, capacity) ||
        buffers_overlap(cmdline, text_len(cmdline) + 1, dst, capacity)) return PW_E_ARGUMENT;
    cmd_len = text_len(cmdline);
    if (!cmd_len || cmd_len >= PW_CMDLINE_CAP) return PW_E_LENGTH;
    if (!pw_range_valid(initrd) || !pw_range_valid(zone)) return PW_E_RANGE;
    rc = pw_fdt_parse(src, src_bytes, 0, &t);
    if (rc) return rc;
    off_struct = be32(src + 8); off_strings = be32(src + 12); off_rsv = be32(src + 16);
    size_strings = be32(src + 32); size_struct = be32(src + 36);
    for (i = 0; i < 3; ++i) {
        nameoff[i] = find_string(src + off_strings, size_strings, names[i]);
        if (nameoff[i] == 0xffffffffu) { nameoff[i] = size_strings + extra; extra += text_len(names[i]) + 1; }
    }
    w.p = dst; w.cap = capacity; w.at = 0; w.bad = 0;
    for (i = 0; i < 40; ++i) w_bytes(&w, (const pw_u8 *)"", 1);   /* header, filled in last */
    /* Reservations the stock boot made for its own payload go; the rest stay. */
    for (pos = off_rsv;; pos += 16) {
        struct pw_range r;
        r.start = be32(src + pos + 4); r.size = be32(src + pos + 12);
        if (!r.start && !r.size && !be32(src + pos) && !be32(src + pos + 8)) break;
        if ((t.has_initrd && pw_ranges_overlap(r, t.initrd)) ||
            (r.start >= zone.start && end(r) <= end(zone))) continue;
        w_bytes(&w, src + pos, 16);
    }
    for (i = 0; i < 16; ++i) w_bytes(&w, (const pw_u8 *)"", 1);
    struct_start = w.at;
    pos = off_struct; limit = off_struct + size_struct;
    for (;;) {
        pw_u32 token = be32(src + pos);
        pos += 4;
        if (token == TOKEN_NOP) continue;
        if (token == TOKEN_BEGIN) {
            const pw_u8 *name = src + pos;
            pw_u32 n = bounded_len(name, limit - pos), padded = (n + 4) & ~3u;
            ++depth;
            if (depth == 2 && n == 6 && equals(name, 6, "chosen")) in_chosen = chosen_seen = 1;
            w_be32(&w, TOKEN_BEGIN); w_bytes(&w, name, padded);
            pos += padded;
            continue;
        }
        if (token == TOKEN_PROP) {
            pw_u32 len = be32(src + pos), off = be32(src + pos + 4), skip = 0;
            if (in_chosen && depth == 2)
                for (i = 0; i < 3; ++i)
                    if (off < size_strings && !src[off_strings + off + text_len(names[i])] &&
                        equals(src + off_strings + off, text_len(names[i]), names[i])) skip = 1;
            if (!skip) {
                w_be32(&w, TOKEN_PROP); w_bytes(&w, src + pos, 8 + len); w_pad(&w);
            }
            pos += 8 + ((len + 3) & ~3u);
            continue;
        }
        if (token == TOKEN_END_NODE) {
            if (depth == 1 && !chosen_seen) {
                w_be32(&w, TOKEN_BEGIN); w_bytes(&w, (const pw_u8 *)"chosen\0", 8);
                in_chosen = 1; depth = 2;
            }
            if (in_chosen && depth == 2) {
                w_prop(&w, nameoff[0], (const pw_u8 *)cmdline, cmd_len + 1);
                cell[0] = (pw_u8)(initrd.start >> 24); cell[1] = (pw_u8)(initrd.start >> 16);
                cell[2] = (pw_u8)(initrd.start >> 8); cell[3] = (pw_u8)initrd.start;
                w_prop(&w, nameoff[1], cell, 4);
                cell[0] = (pw_u8)(end(initrd) >> 24); cell[1] = (pw_u8)(end(initrd) >> 16);
                cell[2] = (pw_u8)(end(initrd) >> 8); cell[3] = (pw_u8)end(initrd);
                w_prop(&w, nameoff[2], cell, 4);
                in_chosen = 0;
                if (!chosen_seen) { chosen_seen = 1; w_be32(&w, TOKEN_END_NODE); depth = 1; }
            }
            --depth;
            w_be32(&w, TOKEN_END_NODE);
            continue;
        }
        /* TOKEN_END: pw_fdt_parse() admitted no other token. */
        w_be32(&w, TOKEN_END);
        break;
    }
    strings_start = w.at;
    w_bytes(&w, src + off_strings, size_strings);
    for (i = 0; i < 3; ++i)
        if (nameoff[i] >= size_strings) w_bytes(&w, (const pw_u8 *)names[i], text_len(names[i]) + 1);
    w_pad(&w);
    if (w.bad) return PW_E_CAPACITY;
    {
        pw_u32 total = w.at, fields[10];
        fields[0] = PW_FDT_MAGIC; fields[1] = total; fields[2] = struct_start; fields[3] = strings_start;
        fields[4] = 40; fields[5] = 17; fields[6] = 16; fields[7] = be32(src + 28);
        fields[8] = size_strings + extra; fields[9] = strings_start - struct_start;
        w.at = 0;
        for (i = 0; i < 10; ++i) w_be32(&w, fields[i]);
        w.at = total;
    }
    rc = pw_fdt_parse(dst, w.at, cmdline, &check);
    if (rc) return rc;
    if (!check.has_initrd || check.initrd.start != initrd.start || check.initrd.size != initrd.size)
        return PW_E_CONTRACT;
    *used = w.at;
    return PW_VALID_DATA_ONLY;
}
