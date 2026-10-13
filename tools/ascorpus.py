#!/usr/bin/env python3
# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

import os, random, re, shutil, subprocess, sys, tempfile

CC = sys.argv[1] if len(sys.argv) > 1 else './sccc'
SEED = int(os.environ.get('SEED0', '1'))

R64 = ['rax', 'rcx', 'rdx', 'rbx', 'rsp', 'rbp', 'rsi', 'rdi'] + ['r%d' % i for i in range(8, 16)]
R32 = ['eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi'] + ['r%dd' % i for i in range(8, 16)]
R16 = ['ax', 'cx', 'dx', 'bx', 'sp', 'bp', 'si', 'di'] + ['r%dw' % i for i in range(8, 16)]
R8 = ['al', 'cl', 'dl', 'bl', 'spl', 'bpl', 'sil', 'dil'] + ['r%db' % i for i in range(8, 16)]
HI8 = ['ah', 'ch', 'dh', 'bh']
REGS = {1: R8, 2: R16, 4: R32, 8: R64}
SUF = {1: 'b', 2: 'w', 4: 'l', 8: 'q'}
CC_NAMES = ['o', 'no', 'b', 'c', 'nae', 'ae', 'nb', 'nc', 'e', 'z', 'ne', 'nz', 'be', 'na', 'a', 'nbe',
            's', 'ns', 'p', 'pe', 'np', 'po', 'l', 'nge', 'ge', 'nl', 'le', 'ng', 'g', 'nle']

MEM64 = ['(%rax)', '(%rsp)', '(%rbp)', '(%r12)', '(%r13)', '8(%rbx)', '-8(%rbp)', '0x1000(%rcx)',
         '(%rax,%rbx)', '(%rax,%rbx,2)', '4(%rsp,%rcx,8)', '(,%rdx,4)', '0x10(,%r9,8)', '(%r8,%r15,4)',
         '0x7fffffff(%rax)', '-0x80000000(%rdi)', '0x80(%rsi)', '-0x81(%rsi)', '127(%r11)', '-128(%r14)',
         '0x1234', '4(%rip)', '-100(%rip)', '(%eax)', '8(%r10d,%r11d,2)', '%fs:(%rax)', '%gs:8(%rbx)',
         '%es:(%rax)', '%ds:(%rbp)', '%ss:(%rax)', '(%rbp,%rax)', '(%r13,%r12,1)', '0(%rax)']
MEM32 = ['(%eax)', '(%esp)', '(%ebp)', '8(%ebx)', '-8(%ebp)', '0x1000(%ecx)', '(%eax,%ebx)',
         '4(%esp,%ecx,8)', '(,%edx,4)', '0x12345678', '0x80(%esi)', '%fs:(%eax)', '%ss:(%ebp)', '%es:(%edi)',
         '(%bx,%si)', '4(%bp,%di)', '(%si)', '(%bp)', '0x1234(%bx)']
MEM16 = ['(%bx,%si)', '(%bx,%di)', '(%bp,%si)', '(%bp,%di)', '(%si)', '(%di)', '(%bp)', '(%bx)',
         '4(%bx)', '-2(%bp)', '0x1234(%si)', '0x80(%di)', '0x1234', '%es:(%di)', '%ss:(%bp)', '%cs:4(%bx)',
         '(%eax)', '8(%esp)', '(%eax,%ebx,4)']
IMMS = ['0', '1', '-1', '2', '127', '128', '-128', '-129', '255', '256', '0x7fff', '0x8000', '-0x8000',
        '0xffff', '0x10000', '0x7fffffff', '-0x80000000', '0xffffffff', '0x123456789', '-0x123456789']

def fits(v, sz, signed_only=False):
    lo = -(1 << (8 * sz - 1))
    hi = (1 << (8 * sz - 1)) - 1 if signed_only else (1 << (8 * sz)) - 1
    return lo <= v <= hi

def imms_for(sz):
    out = []
    for s in IMMS:
        v = int(s, 0)
        if sz == 8 and fits(v, 4, True): out.append(s)
        elif sz < 8 and fits(v, sz): out.append(s)
    return out

def corpus(mode):
    r = random.Random(SEED + mode)
    L = []
    sizes = [1, 2, 4, 8] if mode == 64 else [1, 2, 4]
    mems = {64: MEM64, 32: MEM32, 16: MEM16}[mode]
    def regs(sz):
        rs = REGS[sz] if mode == 64 else REGS[sz][:8]
        if sz == 1 and mode != 64: rs = ['al', 'cl', 'dl', 'bl'] + HI8
        return rs
    def some(xs, n): return xs if len(xs) <= n else r.sample(xs, n)

    for op in ['add', 'or', 'adc', 'sbb', 'and', 'sub', 'xor', 'cmp']:
        for sz in sizes:
            s = SUF[sz]
            for a in some(regs(sz), 6):
                for b in some(regs(sz), 6):
                    L.append('%s %%%s, %%%s' % (op, a, b))
            for m in some(mems, 8):
                L.append('%s %%%s, %s' % (op, r.choice(regs(sz)), m))
                L.append('%s %s, %%%s' % (op, m, r.choice(regs(sz))))
            for i in imms_for(sz):
                L.append('%s $%s, %%%s' % (op, i, regs(sz)[0]))
                L.append('%s $%s, %%%s' % (op, i, r.choice(regs(sz)[1:])))
                L.append('%s%s $%s, %s' % (op, s, i, r.choice(mems)))
    for op in ['rol', 'ror', 'rcl', 'rcr', 'shl', 'sal', 'shr', 'sar']:
        for sz in sizes:
            x = r.choice(regs(sz))
            L += ['%s %%%s' % (op, x), '%s $1, %%%s' % (op, x), '%s $5, %%%s' % (op, x), '%s %%cl, %%%s' % (op, x),
                  '%s%s %s' % (op, SUF[sz], r.choice(mems)), '%s%s $3, %s' % (op, SUF[sz], r.choice(mems)),
                  '%s%s %%cl, %s' % (op, SUF[sz], r.choice(mems))]
    for op in ['not', 'neg', 'mul', 'div', 'idiv', 'imul', 'inc', 'dec']:
        for sz in sizes:
            for x in some(regs(sz), 5): L.append('%s %%%s' % (op, x))
            for m in some(mems, 4): L.append('%s%s %s' % (op, SUF[sz], m))
    for sz in [s for s in sizes if s > 1]:
        for i in imms_for(sz):
            L.append('imul $%s, %%%s, %%%s' % (i, r.choice(regs(sz)), r.choice(regs(sz))))
        L.append('imul $3, %%%s' % r.choice(regs(sz)))
        L.append('imul %%%s, %%%s' % (r.choice(regs(sz)), r.choice(regs(sz))))
        L.append('imul %s, %%%s' % (r.choice(mems), r.choice(regs(sz))))
        L.append('imul $300, %s, %%%s' % (r.choice(mems), r.choice(regs(sz))))
    for sz in sizes:
        for a in regs(sz):
            for b in some(regs(sz), 4):
                L.append('mov %%%s, %%%s' % (a, b))
        for m in mems:
            L.append('mov %%%s, %s' % (r.choice(regs(sz)), m))
            L.append('mov %s, %%%s' % (m, r.choice(regs(sz))))
            L.append('mov %%%s, %s' % (regs(sz)[0], m))
            L.append('mov %s, %%%s' % (m, regs(sz)[0]))
        for i in imms_for(sz):
            L.append('mov $%s, %%%s' % (i, r.choice(regs(sz))))
            L.append('mov%s $%s, %s' % (SUF[sz], i, r.choice(mems)))
    if mode == 64:
        for i in IMMS: L.append('mov $%s, %%%s' % (i, r.choice(R64)))
        for i in IMMS: L.append('movabs $%s, %%%s' % (i, r.choice(R64)))
        L += ['movabs 0x1122334455667788, %al', 'movabs 0x1122334455667788, %rax', 'movabs %eax, 0x1234',
              'movabs %ax, 0x1234', 'mov %rax, %cr8', 'mov %cr8, %r9', 'mov %r10, %cr3', 'movq $1, %r15']
    seg = ['es', 'cs', 'ss', 'ds', 'fs', 'gs']
    for s_ in seg:
        L.append('mov %%%s, %%%s' % (s_, r.choice(regs(2))))
        L.append('mov %%%s, %%%s' % (s_, r.choice(regs(4))))
        L.append('mov %%%s, %s' % (s_, r.choice(mems)))
        if s_ != 'cs':
            L.append('mov %%%s, %%%s' % (r.choice(regs(2)), s_))
            L.append('mov %%%s, %%%s' % (r.choice(regs(4)), s_))
            L.append('mov %s, %%%s' % (r.choice(mems), s_))
    wide = R64 if mode == 64 else R32[:8]
    for c in ['cr0', 'cr2', 'cr3', 'cr4']:
        L += ['mov %%%s, %%%s' % (c, r.choice(wide[:8])), 'mov %%%s, %%%s' % (r.choice(wide[:8]), c)]
    for d in ['dr0', 'dr6', 'dr7']:
        L += ['mov %%%s, %%%s' % (d, r.choice(wide[:8])), 'mov %%%s, %%%s' % (r.choice(wide[:8]), d)]
    for sz in sizes:
        if sz == 1: continue
        for m in some(mems, 6): L.append('lea %s, %%%s' % (m, r.choice(regs(sz))))
    for sz in sizes:
        for a in some(regs(sz), 4):
            for b in some(regs(sz), 4):
                L.append('test %%%s, %%%s' % (a, b))
                L.append('xchg %%%s, %%%s' % (a, b))
            L.append('xchg %%%s, %%%s' % (regs(sz)[0], a))
            L.append('xchg %%%s, %%%s' % (a, regs(sz)[0]))
        for m in some(mems, 4):
            L += ['test %%%s, %s' % (r.choice(regs(sz)), m), 'xchg %%%s, %s' % (r.choice(regs(sz)), m),
                  'xchg %s, %%%s' % (m, r.choice(regs(sz)))]
        for i in some(imms_for(sz), 6):
            L += ['test $%s, %%%s' % (i, regs(sz)[0]), 'test $%s, %%%s' % (i, r.choice(regs(sz))),
                  'test%s $%s, %s' % (SUF[sz], i, r.choice(mems))]
    movx = [('movzbw', 1, 2), ('movzbl', 1, 4), ('movzwl', 2, 4), ('movsbw', 1, 2), ('movsbl', 1, 4), ('movswl', 2, 4)]
    if mode == 64: movx += [('movzbq', 1, 8), ('movzwq', 2, 8), ('movsbq', 1, 8), ('movswq', 2, 8), ('movslq', 4, 8)]
    for n, a, b in movx:
        L.append('%s %%%s, %%%s' % (n, r.choice(regs(a)), r.choice(regs(b))))
        L.append('%s %s, %%%s' % (n, r.choice(mems), r.choice(regs(b))))
    L += ['movzx %al, %eax', 'movzx %ax, %ecx', 'movsx %bl, %dx', 'movsx %cx, %esi']
    L += ['hlt', 'cli', 'sti', 'clc', 'stc', 'cld', 'std', 'cmc', 'pause', 'int3', 'leave', 'ud2', 'cpuid',
          'rdtsc', 'rdtscp', 'rdmsr', 'wrmsr', 'rdpmc', 'clts', 'wbinvd', 'invd', 'mfence', 'lfence', 'sfence',
          'monitor', 'mwait', 'lahf', 'sahf', 'xgetbv', 'xsetbv', 'cbtw', 'cwtl', 'cwtd', 'cltd', 'nop',
          'sysenter', 'sysexit', 'iret', 'iretw', 'iretl', 'lret', 'lretw', 'lretl', 'ret', 'ret $8',
          'lret $16', 'int $0x80', 'int $3', 'int $255', 'retw', 'pushf', 'popf', 'pushfw', 'popfw']
    if mode == 64:
        L += ['cltq', 'cqto', 'swapgs', 'syscall', 'sysret', 'sysretq', 'iretq', 'lretq', 'retq', 'pushfq',
              'popfq', 'leaveq', 'cdqe', 'cqo']
    else:
        L += ['pusha', 'popa', 'pushaw', 'popaw', 'pushal', 'popal', 'pushfl', 'popfl', 'retl']
    for op in ['movs', 'cmps', 'stos', 'lods', 'scas', 'ins', 'outs']:
        for sz in sizes:
            if sz == 8 and op in ('ins', 'outs'): continue
            L += ['%s%s' % (op, SUF[sz]), 'rep %s%s' % (op, SUF[sz])]
    L += ['repe cmpsb', 'repne scasb', 'repz cmpsl', 'repnz scasw', 'lock incl (%s)' % ('%rax' if mode == 64 else '%eax'),
          'lock addq $1, (%rax)' if mode == 64 else 'lock addl $1, (%eax)', 'lock xchg %eax, (%ebx)' if mode != 16 else 'lock xchg %ax, (%bx)',
          'lock cmpxchg %ecx, (%edx)' if mode != 16 else 'lock cmpxchg %cx, (%bx)', 'rep stosq' if mode == 64 else 'rep stosl']
    dflt = {64: 8, 32: 4, 16: 2}[mode]
    psz = [2, dflt] if mode != 16 else [2, 4]
    for sz in psz:
        for x in some(regs(sz), 8): L += ['push %%%s' % x, 'pop %%%s' % x]
        for m in some(mems, 4): L += ['push%s %s' % (SUF[sz], m), 'pop%s %s' % (SUF[sz], m)]
        for i in ['0', '1', '-1', '127', '128', '-129', '0x1234']: L.append('push%s $%s' % (SUF[sz], i))
    L += ['push $1', 'push $1000', 'push %fs', 'push %gs', 'pop %fs', 'pop %gs']
    if mode != 64: L += ['push %es', 'push %cs', 'push %ss', 'push %ds', 'pop %es', 'pop %ss', 'pop %ds']
    for p in ['$0x60', '$0x80', '%dx', '(%dx)']:
        for a in ['al', 'ax', 'eax']:
            L += ['in %s, %%%s' % (p, a), 'out %%%s, %s' % (a, p)]
    L += ['inb $0x60', 'inw %dx', 'inl %dx', 'outb $0x80', 'outw %dx', 'outl %dx']
    for c in CC_NAMES:
        L.append('set%s %%%s' % (c, r.choice(regs(1))))
        L.append('set%s %s' % (c, r.choice(mems)))
        L.append('cmov%s %%%s, %%%s' % (c, r.choice(regs(4)), r.choice(regs(4))))
        L.append('cmov%s %s, %%%s' % (c, r.choice(mems), r.choice(regs(2))))
    for op in ['bt', 'bts', 'btr', 'btc']:
        for sz in [s for s in sizes if s > 1]:
            L += ['%s %%%s, %%%s' % (op, r.choice(regs(sz)), r.choice(regs(sz))), '%s $%d, %%%s' % (op, r.randrange(64), r.choice(regs(sz))),
                  '%s%s $5, %s' % (op, SUF[sz], r.choice(mems)), '%s %%%s, %s' % (op, r.choice(regs(sz)), r.choice(mems))]
    for op in ['bsf', 'bsr', 'popcnt', 'lzcnt', 'tzcnt']:
        for sz in [s for s in sizes if s > 1]:
            L += ['%s %%%s, %%%s' % (op, r.choice(regs(sz)), r.choice(regs(sz))), '%s %s, %%%s' % (op, r.choice(mems), r.choice(regs(sz)))]
    for op in ['xadd', 'cmpxchg']:
        for sz in sizes:
            L += ['%s %%%s, %%%s' % (op, r.choice(regs(sz)), r.choice(regs(sz))), '%s %%%s, %s' % (op, r.choice(regs(sz)), r.choice(mems))]
    for sz in [s for s in sizes if s > 2]: L += ['bswap %%%s' % x for x in some(regs(sz), 6)]
    for op in ['shld', 'shrd']:
        for sz in [s for s in sizes if s > 1]:
            L += ['%s $3, %%%s, %%%s' % (op, r.choice(regs(sz)), r.choice(regs(sz))), '%s %%cl, %%%s, %s' % (op, r.choice(regs(sz)), r.choice(mems))]
    for op in ['lgdt', 'lidt', 'sgdt', 'sidt', 'invlpg', 'fxsave', 'fxrstor', 'ldmxcsr', 'stmxcsr', 'clflush',
               'cmpxchg8b', 'xsave', 'xrstor']:
        for m in some(mems, 3): L.append('%s %s' % (op, m))
    if mode == 64: L += ['fxsaveq (%rax)', 'fxrstorq (%rbx)', 'fxsave64 (%rcx)', 'cmpxchg16b (%rdi)', 'xsaveq (%rsp)', 'lgdtq (%rax)']
    if mode == 16: L += ['lgdtl 0x1000', 'lidtl (%bx)', 'lgdtw 0x10']
    if mode == 32: L += ['lgdtl (%eax)', 'lgdtw (%eax)']
    for op in ['lldt', 'ltr', 'sldt', 'str', 'verr', 'verw', 'smsw', 'lmsw']:
        L += ['%s %%%s' % (op, r.choice(regs(2))), '%s %s' % (op, r.choice(mems))]
    L += ['str %eax', 'sldt %ecx', 'smsw %edx']
    for sz in [s for s in sizes if s > 1]:
        L += ['nop%s %s' % (SUF[sz], m) for m in some(mems, 3)]
        L.append('nop %%%s' % r.choice(regs(sz)))
    L += ['nopw']
    xmm = ['xmm%d' % i for i in range(16 if mode == 64 else 8)]
    for op in ['movaps', 'movups', 'movapd', 'movupd', 'movdqa', 'movdqu', 'movss', 'movsd']:
        L += ['%s %%%s, %%%s' % (op, r.choice(xmm), r.choice(xmm)), '%s %s, %%%s' % (op, r.choice(mems), r.choice(xmm)),
              '%s %%%s, %s' % (op, r.choice(xmm), r.choice(mems))]
    for op in ['addsd', 'subsd', 'mulsd', 'divsd', 'sqrtsd', 'minsd', 'maxsd', 'addss', 'subss', 'mulss', 'divss',
               'sqrtss', 'addps', 'addpd', 'mulps', 'subpd', 'divps', 'andps', 'andpd', 'andnps', 'orps', 'xorps',
               'xorpd', 'pxor', 'por', 'pand', 'paddq', 'ucomisd', 'ucomiss', 'comisd', 'comiss', 'cvtsd2ss', 'cvtss2sd']:
        L += ['%s %%%s, %%%s' % (op, r.choice(xmm), r.choice(xmm)), '%s %s, %%%s' % (op, r.choice(mems), r.choice(xmm))]
    gp = (R64 if mode == 64 else R32[:8])
    L += ['movd %%%s, %%%s' % (r.choice(regs(4)), r.choice(xmm)), 'movd %%%s, %%%s' % (r.choice(xmm), r.choice(regs(4))),
          'movd %s, %%%s' % (r.choice(mems), r.choice(xmm)), 'movd %%%s, %s' % (r.choice(xmm), r.choice(mems)),
          'movq %%%s, %%%s' % (r.choice(xmm), r.choice(xmm)), 'movq %s, %%%s' % (r.choice(mems), r.choice(xmm)),
          'movq %%%s, %s' % (r.choice(xmm), r.choice(mems))]
    if mode == 64:
        L += ['movq %%%s, %%%s' % (r.choice(R64), r.choice(xmm)), 'movq %%%s, %%%s' % (r.choice(xmm), r.choice(R64)),
              'movd %rax, %xmm3']
    for op in ['cvtsi2sd', 'cvtsi2ss']:
        L += ['%s %%%s, %%%s' % (op, r.choice(gp), r.choice(xmm)), '%sl %s, %%%s' % (op, r.choice(mems), r.choice(xmm))]
        if mode == 64: L.append('%sq %s, %%%s' % (op, r.choice(mems), r.choice(xmm)))
    for op in ['cvttsd2si', 'cvtsd2si', 'cvttss2si', 'cvtss2si']:
        L += ['%s %%%s, %%%s' % (op, r.choice(xmm), r.choice(gp)), '%s %s, %%%s' % (op, r.choice(mems), r.choice(gp))]
    ind = R64 if mode == 64 else R32[:8] if mode == 32 else R16[:8]
    for x in some(ind, 6): L += ['jmp *%%%s' % x, 'call *%%%s' % x]
    for m in some(mems, 6): L += ['jmp *%s' % m, 'call *%s' % m, 'ljmp *%s' % m, 'lcall *%s' % m]
    if mode != 64:
        L += ['ljmp $0x08, $0x1234', 'lcall $0x10, $0x5678', 'ljmpw $0x08, $0x1234', 'ljmpl $0x08, $0x12345678']
    return L

def run(cmd):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)

def assemble_slots(tmp, mode, lines):
    while True:
        src = os.path.join(tmp, 'c%d.s' % mode)
        with open(src, 'w') as f:
            f.write('\t.globl _start\n_start:\n\t.code%d\n' % mode)
            for l in lines:
                f.write('\t%s\n\t.balign 16, 0xcc\n' % l)
        p = run(['as', '--64', '-o', os.path.join(tmp, 'g.o'), src])
        bad = set()
        for m in re.finditer(r':(\d+): Error', p.stderr.decode()):
            bad.add((int(m.group(1)) - 4) // 2)
        if p.returncode == 0 and not bad:
            break
        if not bad:
            sys.exit('as failed:\n' + p.stderr.decode())
        lines = [l for i, l in enumerate(lines) if i not in bad]
    run(['objcopy', '-O', 'binary', '-j', '.text', os.path.join(tmp, 'g.o'), os.path.join(tmp, 'g.bin')])
    p = run([CC, '-ffreestanding', '-oformat', 'bin', src, '-o', os.path.join(tmp, 's.bin')])
    if p.returncode != 0:
        print(p.stderr.decode().strip())
        return len(lines), len(lines)
    g = open(os.path.join(tmp, 'g.bin'), 'rb').read()
    s = open(os.path.join(tmp, 's.bin'), 'rb').read()
    bad = 0
    for i, l in enumerate(lines):
        a, b = g[16 * i:16 * i + 16], s[16 * i:16 * i + 16]
        if a != b:
            bad += 1
            if bad <= 20:
                print('  %d-bit: %-40s as %s  sccc %s' % (mode, l, a.rstrip(b'\xcc').hex(' '), b.rstrip(b'\xcc').hex(' ')))
    return len(lines), bad

def whole(tmp, name, text):
    src = os.path.join(tmp, name + '.s')
    with open(src, 'w') as f:
        f.write('\t.globl _start\n_start:\n' + text)
    p = run(['as', '--64', '-o', os.path.join(tmp, 'g.o'), src])
    if p.returncode != 0:
        sys.exit('as failed on %s:\n%s' % (name, p.stderr.decode()))
    p = run(['ld', '-m', 'elf_x86_64', '-Ttext=0', '-e', '_start', '--oformat', 'binary',
             os.path.join(tmp, 'g.o'), '-o', os.path.join(tmp, 'g.bin')])
    if p.returncode != 0:
        sys.exit('ld failed on %s:\n%s' % (name, p.stderr.decode()))
    p = run([CC, '-ffreestanding', '-oformat', 'bin', src, '-o', os.path.join(tmp, 's.bin')])
    if p.returncode != 0:
        print('  %s: %s' % (name, p.stderr.decode().strip()))
        return 1
    g = open(os.path.join(tmp, 'g.bin'), 'rb').read()
    s = open(os.path.join(tmp, 's.bin'), 'rb').read()
    if s[:len(g)] != g or s[len(g):].strip(b'\0'):
        i = next((k for k in range(min(len(g), len(s))) if g[k] != s[k]), min(len(g), len(s)))
        print('  %s: differs at 0x%x (as %d bytes, sccc %d)' % (name, i, len(g), len(s)))
        shutil.copy(src, name + '_fail.s')
        return 1
    return 0

def branches(seed):
    r = random.Random(seed)
    out = []
    nl = 40
    for i in range(300):
        k = r.random()
        if k < 0.35:
            out.append('\tj%s L%d' % (r.choice(CC_NAMES), r.randrange(nl)))
        elif k < 0.55:
            out.append('\tjmp L%d' % r.randrange(nl))
        elif k < 0.65:
            out.append('\tcall L%d' % r.randrange(nl))
        elif k < 0.75:
            out.append('\t.fill %d, 1, 0x90' % r.choice([1, 50, 100, 120, 126, 127, 128, 129, 200]))
        elif k < 0.85:
            out.append('\tjmp %df' % r.randrange(3) if r.random() < 0.5 else '\tjne %db' % r.randrange(3))
        else:
            out.append('\tmovl $L%d - L%d, %%eax' % (r.randrange(nl), r.randrange(nl)))
        if r.random() < 0.15:
            out.append('%d:' % r.randrange(3))
    pos = sorted(r.sample(range(len(out) + 1), nl))
    for n, p in reversed(list(enumerate(pos))):
        out.insert(p, 'L%d:' % n)
    text = '0: 1: 2:\n' + '\n'.join(out) + '\n0: 1: 2:\n'
    return text

DATA = r'''
	.set K, 10
	.equ M, K * 4 + 2
	nop
start:
	.byte 1, 2, -1, 255, 'a', K, M & 0xff
	.word 0x1234, -2, end - start
	.long 0x12345678, end - start, (end - start) / 2
	.quad 0x1122334455667788, -1
	.ascii "abc\n\t\"\\"
	.asciz "zero"
	.string "str"
	.zero 3
	.skip 2, 0x41
	.space 2
	.fill 3, 2, 0x1234
	.fill 2, 4, -1
	.balign 8, 0xee
	.p2align 4, 0xdd
	.align 4, 0
	mov $end - start, %eax
	movl $(1 << 4) | 3, (%rax)
	addl $~0, %ebx
	mov $-K, %ecx
	mov (M - 2)(%rbx), %edx
	lea .Lx - start(%rip), %rax
	jmp .Lx
.Lx:	mov $'A', %al
	cmp $(3 * 4 - 2) % 7, %eax
	.long . - start
	.org start + 0x100, 0x55
	nop
	.set K2, . - start
	.long K2
end:
'''

MODES16 = r'''
	.code16
	cli
	xor %ax, %ax
	mov %ax, %ds
	mov $0x7c00, %sp
	ljmp $0, $1f
1:	mov $msg, %si
2:	lodsb
	test %al, %al
	jz 3f
	mov $0x0e, %ah
	int $0x10
	jmp 2b
3:	lgdtl gdtr
	mov %cr0, %eax
	or $1, %eax
	mov %eax, %cr0
	ljmpl $0x08, $pm
	.code32
pm:	mov $0x10, %ax
	mov %ax, %ds
	movl $0xb8000, %edi
	movl %eax, 0xb8000
	jmp pm
	.code64
lm:	movabs $0x1122334455667788, %rax
	jmp lm
msg:	.asciz "hi"
	.balign 8, 0
gdt:	.quad 0, 0x00cf9a000000ffff, 0x00cf92000000ffff
gdtr:	.word gdtr - gdt - 1
	.long gdt
	.fill 510 - (. - _start), 1, 0
	.word 0xaa55
'''

def main():
    if not shutil.which('as') or not shutil.which('objcopy') or not shutil.which('ld'):
        print('ascorpus: GNU as, ld or objcopy not found, skipped')
        return 0
    tmp = tempfile.mkdtemp(prefix='sccc-as.')
    try:
        total = bad = 0
        for mode in (64, 32, 16):
            n, b = assemble_slots(tmp, mode, corpus(mode))
            total += n
            bad += b
        files = 0
        fbad = 0
        for i in range(20):
            files += 1
            fbad += whole(tmp, 'branch%d' % i, branches(SEED * 100 + i))
        for name, text in (('data', DATA), ('modes', MODES16)):
            files += 1
            fbad += whole(tmp, name, text)
        print('ascorpus: %d instructions, %d differ; %d files, %d differ' % (total, bad, files, fbad))
        return 1 if bad or fbad else 0
    finally:
        shutil.rmtree(tmp)

sys.exit(main())
