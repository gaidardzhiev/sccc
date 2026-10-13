#!/usr/bin/env python3
# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

import os, random, resource, shutil, subprocess, sys, tempfile

CC = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.abspath('./sccc')
ROUNDS = int(sys.argv[2]) if len(sys.argv) > 2 else 10
SEED = int(os.environ.get('SEED0', '1'))
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GCC = ['gcc', '-std=c99', '-O1', '-fno-pic', '-fno-stack-protector', '-fcf-protection=none',
       '-nostdinc', '-I' + os.path.join(ROOT, 'lib', 'include'), '-w']

INTS = ['char', 'signed char', 'unsigned char', 'short', 'unsigned short', 'int', 'unsigned', 'long',
        'unsigned long', 'long long']
FLTS = ['float', 'double']

class Gen:
    def __init__(self, r):
        self.r = r
        self.structs = []

    def scalar(self):
        return self.r.choice(INTS + FLTS + FLTS)

    def struct(self, depth=0):
        name = 'S%d' % len(self.structs)
        fields = []
        for i in range(self.r.randint(1, 5)):
            k = self.r.random()
            if k < 0.12 and depth < 2 and self.structs:
                fields.append((self.r.choice(self.structs)[0], 'f%d' % i, 0))
            elif k < 0.25:
                fields.append((self.scalar(), 'f%d' % i, self.r.randint(1, 4)))
            else:
                fields.append((self.scalar(), 'f%d' % i, 0))
        self.structs.append((name, fields))
        return name

    def typ(self):
        if self.r.random() < 0.4:
            if self.structs and self.r.random() < 0.5:
                return self.r.choice(self.structs)[0]
            return self.struct()
        return self.scalar()

def is_struct(t):
    return t.startswith('S')

def fields_of(g, t):
    for n, f in g.structs:
        if n == t:
            return f

def emit_struct_decls(g):
    out = []
    for n, fields in g.structs:
        body = ' '.join('%s %s%s;' % (ctype(t), f, '[%d]' % a if a else '') for t, f, a in fields)
        out.append('struct %s { %s };' % (n, body))
    return '\n'.join(out)

def ctype(t):
    return 'struct ' + t if is_struct(t) else t

def leaves(g, t, expr):
    if not is_struct(t):
        return [(t, expr)]
    out = []
    for ft, f, a in fields_of(g, t):
        if a:
            for i in range(a):
                out += leaves(g, ft, '%s.%s[%d]' % (expr, f, i))
        else:
            out += leaves(g, ft, '%s.%s' % (expr, f))
    return out

def value(t, k):
    if t in FLTS:
        return '%d.%d' % (k % 50, (k * 7) % 10)
    if 'unsigned' in t or t == 'char':
        return str(k % 100)
    return str(k % 90 - 40)

def fmt(t, e):
    if t in FLTS:
        return '"%.6g ", (double)(' + e + ')'
    return '"%lld ", (long long)(' + e + ')'

def program(seed):
    r = random.Random(seed)
    g = Gen(r)
    funcs = []
    for i in range(r.randint(4, 8)):
        params = [g.typ() for _ in range(r.randint(1, 12))]
        ret = g.typ() if r.random() < 0.85 else 'void'
        variadic = r.random() < 0.3
        vargs = []
        if variadic:
            for _ in range(r.randint(1, 6)):
                t = g.typ()
                if t in ('char', 'signed char', 'unsigned char', 'short', 'unsigned short', 'float'):
                    t = {'float': 'double'}.get(t, 'int')
                vargs.append(t)
        funcs.append(('fn%d' % i, ret, params, variadic, vargs))
    decls = emit_struct_decls(g)
    protos = []
    for name, ret, params, variadic, vargs in funcs:
        ps = ', '.join('%s p%d' % (ctype(t), j) for j, t in enumerate(params))
        protos.append('%s %s(%s%s);' % (ctype(ret) if ret != 'void' else 'void', name, ps, ', ...' if variadic else ''))
    head = '#include <stdio.h>\n#include <stdarg.h>\n' + decls + '\n' + '\n'.join(protos) + '\n'
    callee = [head]
    for name, ret, params, variadic, vargs in funcs:
        ps = ', '.join('%s p%d' % (ctype(t), j) for j, t in enumerate(params))
        callee.append('%s %s(%s%s) {' % (ctype(ret) if ret != 'void' else 'void', name, ps, ', ...' if variadic else ''))
        callee.append('\tprintf("%s:");' % name)
        for j, t in enumerate(params):
            for lt, e in leaves(g, t, 'p%d' % j):
                callee.append('\tprintf(%s);' % fmt(lt, e))
        if variadic:
            callee.append('\tva_list ap;\n\tva_start(ap, p%d);' % (len(params) - 1))
            for j, t in enumerate(vargs):
                callee.append('\t{ %s v = va_arg(ap, %s);' % (ctype(t), ctype(t)))
                for lt, e in leaves(g, t, 'v'):
                    callee.append('\tprintf(%s);' % fmt(lt, e))
                callee.append('\t}')
            callee.append('\tva_end(ap);')
        callee.append('\tprintf("\\n");')
        if ret != 'void':
            callee.append('\t%s r;' % ctype(ret))
            k = 3
            for lt, e in leaves(g, ret, 'r'):
                callee.append('\t%s = %s;' % (e, value(lt, k + len(params))))
                k += 5
            callee.append('\treturn r;')
        callee.append('}')
    caller = [head, 'int main(void) {']
    k = 1
    for name, ret, params, variadic, vargs in funcs:
        args = []
        for j, t in enumerate(params + vargs):
            v = 'a_%s_%d' % (name, j)
            caller.append('\t%s %s;' % (ctype(t), v))
            for lt, e in leaves(g, t, v):
                caller.append('\t%s = %s;' % (e, value(lt, k)))
                k += 1
            args.append(v)
        call = '%s(%s)' % (name, ', '.join(args))
        if ret == 'void':
            caller.append('\t%s;' % call)
        else:
            caller.append('\t{ %s r = %s;' % (ctype(ret), call))
            caller.append('\tprintf("%s ret:");' % name)
            for lt, e in leaves(g, ret, 'r'):
                caller.append('\tprintf(%s);' % fmt(lt, e))
            caller.append('\tprintf("\\n"); }')
    caller.append('\treturn 0;\n}')
    return '\n'.join(caller) + '\n', '\n'.join(callee) + '\n'

def limit():
    resource.setrlimit(resource.RLIMIT_AS, (1 << 30, 1 << 30))
    resource.setrlimit(resource.RLIMIT_FSIZE, (1 << 22, 1 << 22))

def run(cmd, cwd):
    return subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)

def execute(path, cwd):
    out = os.path.join(cwd, 'out.txt')
    with open(out, 'wb') as f:
        try:
            p = subprocess.run([path], cwd=cwd, stdout=f, stderr=subprocess.STDOUT, timeout=10, preexec_fn=limit)
            rc = p.returncode
        except subprocess.TimeoutExpired:
            rc = -1
    with open(out, 'rb') as f:
        return rc, f.read(1 << 20)

def compile_obj(comp, src, obj, cwd):
    if comp == 'gcc':
        return run(GCC + ['-c', src, '-o', obj], cwd)
    return run([CC, '-c', src, '-o', obj], cwd)

def link(linker, objs, exe, cwd):
    if linker == 'gcc':
        return run(['gcc', '-no-pie'] + objs + ['-o', exe], cwd)
    return run([CC] + objs + ['-o', exe], cwd)

def main():
    if not shutil.which('gcc'):
        print('abifuzz: gcc not found, skipped')
        return 0
    tmp = tempfile.mkdtemp(prefix='sccc-abi.')
    combos = [('sccc', 'gcc', 'sccc'), ('gcc', 'sccc', 'sccc'), ('sccc', 'gcc', 'gcc'),
              ('gcc', 'sccc', 'gcc'), ('sccc', 'sccc', 'sccc')]
    bad = 0
    try:
        for rnd in range(ROUNDS):
            seed = SEED * 1000 + rnd
            caller, callee = program(seed)
            for n, text in (('caller.c', caller), ('callee.c', callee)):
                with open(os.path.join(tmp, n), 'w') as f:
                    f.write(text)
            for c in ('gcc', 'sccc'):
                for n in ('caller', 'callee'):
                    p = compile_obj(c, n + '.c', '%s_%s.o' % (n, c), tmp)
                    if p.returncode != 0:
                        print('abifuzz: %s failed on %s.c (seed %d):\n%s' % (c, n, seed, p.stderr.decode()[:2000]))
                        shutil.copy(os.path.join(tmp, n + '.c'), 'abi_fail_%d_%s.c' % (seed, n))
                        return 1
            link('gcc', ['caller_gcc.o', 'callee_gcc.o'], 'ref', tmp)
            rc, ref = execute(os.path.join(tmp, 'ref'), tmp)
            if rc != 0:
                print('abifuzz: the gcc build failed (seed %d)' % seed)
                return 1
            for a, b, l in combos:
                p = link(l, ['caller_%s.o' % a, 'callee_%s.o' % b], 'prog', tmp)
                if p.returncode != 0:
                    rc, got = 1, p.stderr
                else:
                    rc, got = execute(os.path.join(tmp, 'prog'), tmp)
                if rc != 0 or got != ref:
                    bad += 1
                    print('abifuzz: seed %d, caller %s, callee %s, linker %s differs' % (seed, a, b, l))
                    for n in ('caller', 'callee'):
                        shutil.copy(os.path.join(tmp, n + '.c'), 'abi_fail_%d_%s.c' % (seed, n))
        print('abifuzz: %d rounds, %d builds differ' % (ROUNDS, bad))
        return 1 if bad else 0
    finally:
        shutil.rmtree(tmp)

sys.exit(main())
