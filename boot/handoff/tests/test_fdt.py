#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Host tests for pw4_fdt.c (the PW4 device-tree rules of the handoff).

Builds tests/fdt-cli.c with AddressSanitizer and UBSan, makes blobs with dtc
from small trees shaped like the PW4 one (root cells, /memory, /chosen with
bootargs and initrd, /reserved-memory, /memreserve/), and checks what is
accepted and what is refused, then feeds a few thousand corrupted blobs to
the parser to make sure it only ever answers.

Usage: test_fdt.py [--dtc PATH] [--blob STOCK.dtb --cmdline TEXT]
"""
import argparse, os, random, shutil, subprocess, sys, tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
CMD = 'console=ttymxc0,115200 rdinit=/init quiet'


def tree(chosen=None, memreserve=True, memory='reg = <0x80000000 0x20000000>;', extra='', root_cells=(1, 1)):
    if chosen is None:
        chosen = 'bootargs = "%s"; linux,initrd-start = <0x84000000>; linux,initrd-end = <0x84100000>;' % CMD
    return '''/dts-v1/;
%s
/ {
  #address-cells = <%d>; #size-cells = <%d>;
  model = "test"; compatible = "fsl,imx6sll";
  chosen { %s };
  memory { device_type = "memory"; %s };
  reserved-memory { #address-cells = <1>; #size-cells = <1>; ranges;
    epdc { reg = <0x9b000000 0x00300000>; no-map; }; };
  soc { compatible = "simple-bus"; #address-cells = <1>; #size-cells = <1>;
    l2-cache@a02000 { compatible = "arm,pl310-cache"; reg = <0x00a02000 0x1000>; }; };
  %s
};
''' % ('/memreserve/ 0x9ffc0000 0x30000;' if memreserve else '', root_cells[0], root_cells[1], chosen, memory, extra)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dtc', default=shutil.which('dtc'))
    ap.add_argument('--blob', type=Path)
    ap.add_argument('--cmdline')
    a = ap.parse_args()
    if not a.dtc:
        sys.exit('dtc not found: pass --dtc')
    work = Path(tempfile.mkdtemp(prefix='pw4-fdt-'))
    cli = work / 'fdt-cli'
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-g', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-o', str(cli), str(HERE / 'fdt-cli.c'),
                    str(HERE.parent / 'pw4_fdt.c'), str(HERE.parent / 'pw3_plan.c')], check=True)
    failures = 0

    def blob(text, name):
        src, out = work / (name + '.dts'), work / (name + '.dtb')
        src.write_text(text)
        subprocess.run([a.dtc, '-q', '-I', 'dts', '-O', 'dtb', '-o', str(out), str(src)], check=True)
        return out

    def run(*args):
        r = subprocess.run([str(cli)] + [str(x) for x in args], capture_output=True, text=True)
        if r.returncode:
            raise SystemExit('fdt-cli crashed on %s:\n%s' % (args, r.stderr))
        return r.stdout.strip()

    def expect(label, got, want):
        nonlocal failures
        ok = got.startswith(want)
        failures += not ok
        print('%-4s %-46s %s' % ('ok' if ok else 'FAIL', label, got if not ok else ''))

    good = blob(tree(), 'good')
    expect('well-formed tree', run('parse', good, CMD),
           'rc=0 total=')
    out = run('parse', good, CMD)
    expect('memory, memreserve and reserved-memory found', 'rc=0' if
           ' mem=1 0x80000000+0x20000000' in out and 'rsv=2 0x9ffc0000+0x30000 0x9b000000+0x300000' in out and
           'initrd=1 0x84000000+0x100000' in out else out, 'rc=0')
    expect('other command line', run('parse', good, CMD + ' x'), 'rc=-9')
    expect('command line prefix only', run('parse', good, CMD[:-1]), 'rc=-9')
    expect('no bootargs', run('parse', blob(tree(chosen='linux,initrd-start = <0x84000000>; linux,initrd-end = <0x84100000>;'), 'nobootargs'), CMD), 'rc=-9')
    expect('initrd start without end', run('parse', blob(tree(chosen='bootargs = "%s"; linux,initrd-start = <0x84000000>;' % CMD), 'halfinitrd'), CMD), 'rc=-9')
    expect('initrd end before start', run('parse', blob(tree(chosen='bootargs = "%s"; linux,initrd-start = <0x84100000>; linux,initrd-end = <0x84000000>;' % CMD), 'backinitrd'), CMD), 'rc=-5')
    expect('64-bit initrd cells', run('parse', blob(tree(chosen='bootargs = "%s"; linux,initrd-start = /bits/ 64 <0x84000000>; linux,initrd-end = /bits/ 64 <0x84100000>;' % CMD), 'initrd64'), CMD), 'rc=0')
    expect('two cells of address', run('parse', blob(tree(root_cells=(2, 1), memory='reg = <0 0x80000000 0x20000000>;'), 'cells'), CMD), 'rc=-9')
    expect('no memory node', run('parse', blob(tree(memory='').replace('memory { device_type = "memory";  };', ''), 'nomem'), CMD), 'rc=-9')
    expect('overlapping memory ranges', run('parse', blob(tree(memory='reg = <0x80000000 0x20000000 0x90000000 0x1000>;'), 'overlap'), CMD), 'rc=-5')
    expect('command line not required', run('parse', good), 'rc=0')

    # The plan: kernel at +0x8000, blob at +63 MiB, initramfs at +64 MiB.
    plan = lambda b, fdt=0x83f00000, initrd=0x84000000, size=0x100000, foot=0x81000000, cmd=CMD: run(
        'plan', b, cmd, hex(fdt), hex(initrd), hex(size), hex(foot))
    nores = blob(tree(memreserve=False), 'nores')
    expect('plan as laid out', plan(nores), 'rc=0')
    expect('plan with a stale memreserve over nothing', plan(good), 'rc=0')
    expect('initrd size differs from the blob', plan(nores, size=0x200000), 'rc=-9')
    expect('initrd elsewhere than the blob says', plan(nores, initrd=0x85000000), 'rc=-9')
    expect('blob inside the decompressor footprint', plan(nores, fdt=0x80800000), 'rc=-6')
    expect('blob on the kernel', plan(nores, fdt=0x80008000), 'rc=-6')
    expect('blob in a forbidden range', plan(nores, fdt=0x88000000), 'rc=-6')
    expect('blob over a reserved range', plan(blob(tree(memreserve=False, extra='').replace(
        '0x9b000000 0x00300000', '0x83f00000 0x00010000'), 'resv'), fdt=0x83f00000), 'rc=-6')
    expect('blob at the end of a section', plan(nores, fdt=0x83ff0000), 'rc=0')
    expect('blob above RAM', plan(nores, fdt=0xa0000000), 'rc=-5')

    # The device side: the stock blob made into the next kernel's.
    NEW = 'console=ttymxc0,115200 quiet panic=5'
    out = work / 'rewritten.dtb'
    stock_like = blob(tree(memreserve=True).replace('/memreserve/ 0x9ffc0000 0x30000;',
        '/memreserve/ 0x83f077b0 0x8850;\n/memreserve/ 0x84000000 0x100000;\n/memreserve/ 0x9ffc0000 0x30000;'), 'stocklike')
    expect('rewrite', run('rewrite', stock_like, NEW, 0x84000000, 0x200000, out), 'rc=0')
    got = run('parse', out, NEW)
    expect('rewritten: new bootargs and initrd', got if 'initrd=1 0x84000000+0x200000' not in got else 'rc=0', 'rc=0')
    expect('rewritten: payload reservations dropped, others kept',
           'rc=0' if ' rsv=2 0x9ffc0000+0x30000 0x9b000000+0x300000' in got else got, 'rc=0')
    expect('rewritten: old command line gone', run('parse', out, CMD), 'rc=-9')
    expect('rewritten: passes the plan', plan(out, size=0x200000, cmd=NEW), 'rc=0')
    out2 = work / 'rewritten2.dtb'
    run('rewrite', out, NEW, 0x84000000, 0x200000, out2)
    expect('rewrite is idempotent', 'rc=0' if out.read_bytes() == out2.read_bytes() else 'differs', 'rc=0')
    nochosen = blob(tree().replace('chosen { bootargs = "%s"; linux,initrd-start = <0x84000000>; linux,initrd-end = <0x84100000>; };' % CMD, ''), 'nochosen')
    expect('rewrite adds a missing /chosen', run('rewrite', nochosen, NEW, 0x84000000, 0x1000, out), 'rc=0')
    expect('  and the result parses', run('parse', out, NEW), 'rc=0')
    expect('rewrite refuses an empty command line', run('rewrite', good, '', 0x84000000, 0x1000, out), 'rc=-2')
    with tempfile.NamedTemporaryFile(dir=work, delete=False) as tmp:
        tmp.write(good.read_bytes()[:100])
    expect('rewrite refuses a broken source', run('rewrite', tmp.name, NEW, 0x84000000, 0x1000, out), 'rc=-')

    if a.blob:
        expect('stock blob, its own command line', run('parse', a.blob, a.cmdline or ''), 'rc=0' if a.cmdline else 'rc=')
        print('     ' + run('parse', a.blob, a.cmdline or ''))

    # Corruption: random bytes, truncation and length fields. Only answers.
    data = bytearray(good.read_bytes())
    rnd = random.Random(1)
    path = work / 'fuzz.dtb'
    for i in range(3000):
        b = bytearray(data)
        kind = i % 3
        if kind == 0:
            for _ in range(rnd.randint(1, 8)):
                b[rnd.randrange(len(b))] = rnd.randrange(256)
        elif kind == 1:
            b = b[:rnd.randrange(len(b))]
        else:
            off = rnd.choice([4, 8, 12, 16, 32, 36]) if rnd.random() < .5 else rnd.randrange(0, len(b) - 4) & ~3
            b[off:off + 4] = rnd.randrange(1 << 32).to_bytes(4, 'big')
        path.write_bytes(bytes(b))
        out = run('parse', path, CMD)
        if not out.startswith('rc='):
            failures += 1
            print('FAIL fuzz', i, out)
    print('ok   3000 corrupted blobs answered without a fault')
    shutil.rmtree(work)
    if failures:
        sys.exit('%d failure(s)' % failures)
    print('all passed')


if __name__ == '__main__':
    main()
