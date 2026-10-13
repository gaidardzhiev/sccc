#!/usr/bin/env python3
# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

import os, random, shutil, subprocess, sys, tempfile

CC = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.abspath('./sccc')
ROUNDS = int(sys.argv[2]) if len(sys.argv) > 2 else 20
SEED = int(os.environ.get('SEED0', '1'))

SECS = [('.text', 'ax', None), ('.text.hot', 'ax', None), ('.text.boot', 'ax', None),
        ('.rodata', 'a', None), ('.rodata.str', 'a', None), ('.data', 'aw', None), ('.data.rel', 'aw', None),
        ('.init.1', 'aw', None), ('.init.2', 'aw', None), ('.init.10', 'aw', None), ('.tbl', 'aw', None),
        ('.bss', 'aw', 'nobits'), ('.bss.page', 'aw', 'nobits'), ('.note.x', 'a', None)]


def gen_file(r, idx, allsyms):
    lines = []
    syms = []
    for name, flags, typ in r.sample(SECS, r.randint(2, 7)):
        if typ:
            lines.append('\t.section %s, "%s", @%s' % (name, flags, typ))
        else:
            lines.append('\t.section %s, "%s"' % (name, flags))
        for k in range(r.randint(1, 3)):
            if r.random() < 0.5:
                lines.append('\t.p2align %d' % r.randint(0, 5))
            s = 'f%d_%s_%d' % (idx, name.strip('.').replace('.', '_'), k)
            syms.append(s)
            lines.append('\t.globl %s\n%s:' % (s, s))
            if typ:
                lines.append('\t.skip %d' % r.randint(1, 40))
                continue
            for _ in range(r.randint(1, 4)):
                c = r.random()
                if c < 0.3:
                    lines.append('\t.byte %s' % ', '.join(str(r.randint(0, 255)) for _ in range(r.randint(1, 6))))
                elif c < 0.55 and allsyms:
                    lines.append('\t.quad %s + %d' % (r.choice(allsyms), r.randint(0, 8)))
                elif c < 0.7 and allsyms:
                    lines.append('\t.long %s - .' % r.choice(allsyms))
                elif c < 0.8 and 'x' in flags and allsyms:
                    lines.append('\tcall %s' % r.choice(allsyms))
                else:
                    lines.append('\t.long 0x%x' % r.getrandbits(32))
    return '\n'.join(lines) + '\n', syms


def gen_expr(r, syms, depth=0):
    c = r.random()
    if c < 0.3 or depth > 2:
        return str(r.randint(0, 64))
    if c < 0.45 and syms:
        return r.choice(syms)
    if c < 0.55:
        return '.'
    if c < 0.65:
        return 'ALIGN(%d)' % (1 << r.randint(0, 6))
    op = r.choice(['+', '-', '*', '&', '|', '<<', '>>', '/', '%'])
    a = gen_expr(r, syms, depth + 1)
    b = str(r.randint(1, 9)) if op in ('/', '%', '<<', '>>', '*') else gen_expr(r, syms, depth + 1)
    return '(%s %s %s)' % (a, op, b)


INPUTS = ['*(.text)', '*(.text.*)', '*(.text .text.*)', '*(.rodata*)', '*(.data)', '*(.data .data.*)',
          'KEEP(*(SORT(.init.*)))', 'KEEP(*(SORT_BY_NAME(.init.*) .tbl))', '*(.tbl)', '*(.bss .bss.*)',
          '*f0.o(.data)', '*f1.o(.text*)', '*(EXCLUDE_FILE(*f0.o) .rodata)', 'EXCLUDE_FILE(*f1.o) *(.tbl)',
          '*(.init.2 .init.1)', '*(COMMON)', '*(.note.x)', 'SORT(*)(.data.rel)']


def gen_script(r, binary, symnames):
    lines = []
    if binary:
        lines.append('OUTPUT_FORMAT(binary)')
    lines.append('SECTIONS\n{')
    base = r.choice([0x100000, 0x200000, 0x400000])
    lines.append('\t. = 0x%x;' % base)
    defined = []
    high = r.random() < 0.3
    lowat = 0x10000 * r.randint(1, 4) if not high and r.random() < 0.2 else 0
    for i in range(r.randint(2, 6)):
        if high and i == 1:
            lines.append('\t. += 0xffffffff80000000;')
        body = []
        for _ in range(r.randint(1, 4)):
            c = r.random()
            if c < 0.6:
                body.append(r.choice(INPUTS))
            elif c < 0.7:
                body.append('. = ALIGN(%d);' % (1 << r.randint(0, 6)))
            elif c < 0.8:
                s = 'ls%d' % len(defined)
                body.append('%s = %s;' % (s, gen_expr(r, defined)))
                defined.append(s)
            elif c < 0.9:
                body.append('%s(%s)' % (r.choice(['BYTE', 'SHORT', 'LONG', 'QUAD']), gen_expr(r, defined + ['.'])))
            elif c < 0.95:
                body.append('FILL(0x%x)' % r.getrandbits(16))
            else:
                body.append('. += %d;' % r.randint(0, 20))
        body.insert(r.randint(0, len(body)), 'BYTE(0x%x)' % r.getrandbits(8))
        attrs = ''
        if r.random() < 0.3:
            attrs += ' ALIGN(%d)' % (1 << r.randint(0, 8))
        addr = ''
        if r.random() < 0.15:
            addr = ' ALIGN(0x%x)' % (1 << r.randint(4, 12))
        at = ''
        if high and i > 0:
            at = ' AT(ADDR(.o%d) - 0xffffffff80000000)' % i
        elif lowat:
            at = ' AT(ADDR(.o%d) + 0x%x)' % (i, lowat)
        fill = ' =0x%x' % r.getrandbits(8) if r.random() < 0.2 else ''
        lines.append('\t.o%d%s :%s%s {\n\t\t%s\n\t}%s' % (i, addr, at, attrs, '\n\t\t'.join(body), fill))
        if r.random() < 0.3:
            s = 'ts%d' % len(defined)
            lines.append('\t%s = %s;' % (s, gen_expr(r, defined)))
            defined.append(s)
    lines.append('\t.rest : { *(.text* .rodata* .data* .bss* .init* .tbl .note.x COMMON) }')
    if r.random() < 0.3:
        lines.append('\t/DISCARD/ : { *(.note.x) }')
    for s in symnames:
        lines.append('\tPROVIDE(%s = 0);' % s)
    lines.append('}')
    return '\n'.join(lines) + '\n'


def run(cmd, cwd):
    return subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)


def dump(path, cwd):
    out = []
    h = run(['objdump', '-h', path], cwd).stdout.decode().splitlines()
    secs = []
    for i, l in enumerate(h):
        f = l.split()
        if len(f) >= 7 and f[0].isdigit() and i + 1 < len(h) and 'ALLOC' in h[i + 1]:
            out.append(' '.join([f[1], f[2], f[3], f[4] if int(f[2], 16) else '-', f[6]]))
            if 'CONTENTS' in h[i + 1]:
                secs.append(f[1])
    nm = run(['nm', '-n', path], cwd).stdout.decode().splitlines()
    out += sorted(' '.join((l.split()[0], l.split()[2])) for l in nm if len(l.split()) == 3)
    for s in secs:
        out += run(['objdump', '-s', '-j', s, path], cwd).stdout.decode().splitlines()[3:]
    return '\n'.join(out)


def main():
    for t in ('as', 'ld', 'objdump', 'nm'):
        if not shutil.which(t):
            print('ldfuzz: %s not found, skipped' % t)
            return 0
    tmp = tempfile.mkdtemp(prefix='sccc-ldfuzz.')
    bad = 0
    checked = 0
    try:
        for rnd in range(ROUNDS):
            seed = SEED * 1000 + rnd
            r = random.Random(seed)
            nf = r.randint(1, 3)
            files = []
            allsyms = []
            for i in range(nf):
                text, syms = gen_file(r, i, allsyms)
                allsyms += syms
                files.append(text)
            binary = r.random() < 0.5
            script = gen_script(r, binary, [])
            with open(os.path.join(tmp, 's.ld'), 'w') as f:
                f.write(script)
            objs = []
            for i, text in enumerate(files):
                with open(os.path.join(tmp, 'f%d.s' % i), 'w') as f:
                    f.write(text)
                obj = 'f%d.o' % i
                if r.random() < 0.5:
                    p = run(['as', 'f%d.s' % i, '-o', obj], tmp)
                else:
                    p = run([CC, '-c', 'f%d.s' % i, '-o', obj], tmp)
                if p.returncode:
                    print('ldfuzz: assembling failed (seed %d):\n%s' % (seed, p.stderr.decode()[:500]))
                    return 1
                objs.append(obj)
            ref = run(['ld', '-T', 's.ld', '--no-warn-rwx-segments'] + objs + ['-o', 'ref'], tmp)
            if ref.returncode:
                continue
            got = run([CC, '-ffreestanding', '-T', 's.ld'] + objs + ['-o', 'out'], tmp)
            checked += 1
            if got.returncode:
                same = False
                why = got.stderr.decode()[:300]
            elif binary:
                same = open(os.path.join(tmp, 'ref'), 'rb').read() == open(os.path.join(tmp, 'out'), 'rb').read()
                why = 'binary output differs'
            else:
                same = dump('ref', tmp) == dump('out', tmp)
                why = 'ELF output differs'
            if not same:
                bad += 1
                d = 'ldfuzz_fail_%d' % seed
                os.makedirs(d, exist_ok=True)
                for n in ['s.ld'] + ['f%d.s' % i for i in range(nf)] + objs:
                    shutil.copy(os.path.join(tmp, n), d)
                print('ldfuzz: seed %d: %s (saved in %s)' % (seed, why, d))
        print('ldfuzz: %d rounds, %d linked by ld, %d differ' % (ROUNDS, checked, bad))
        return 1 if bad else 0
    finally:
        shutil.rmtree(tmp)


sys.exit(main())
