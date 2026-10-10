#!/usr/bin/env python3
import random, subprocess, sys, os, tempfile

TYPES = ['signed char', 'unsigned char', 'short', 'unsigned short', 'int', 'unsigned',
         'long', 'unsigned long', 'long long', 'unsigned long long', '_Bool']
FTYPES = ['double', 'float']
BIN = ['+', '-', '*', '/', '%', '&', '|', '^', '<<', '>>', '<', '>', '<=', '>=', '==', '!=', '&&', '||']

def lit(r):
    return r.choice(['0', '1', '-1', '7', '255', '256', '-128', '32767', '65535', '0x7fffffff',
                     '0x80000000', '4294967295u', '-2147483647', '123456789L', '0xffffffffffffffffUL',
                     "'a'", "'\\xff'", '3000000000', '-5L', '9u'])

def expr(r, depth, vars_):
    if depth == 0 or r.random() < 0.25:
        return r.choice(vars_) if r.random() < 0.7 else lit(r)
    k = r.random()
    if k < 0.6:
        op = r.choice(BIN)
        a = expr(r, depth - 1, vars_)
        b = expr(r, depth - 1, vars_)
        if op in ('/', '%'):
            b = '(((%s) & 0x7f) + 1)' % b
        if op in ('<<', '>>'):
            b = '((%s) & 31)' % b
            if op == '<<':
                a = '((unsigned long)(%s))' % a
        return '(%s %s %s)' % (a, op, b)
    if k < 0.75:
        return '(%s)(%s)' % (r.choice(TYPES), expr(r, depth - 1, vars_))
    if k < 0.85:
        return '(%s %s)' % (r.choice(['-', '~', '!', '+']), expr(r, depth - 1, vars_))
    return '(%s ? %s : %s)' % (expr(r, depth - 1, vars_), expr(r, depth - 1, vars_), expr(r, depth - 1, vars_))

def fexpr(r, depth, fvars, ivars):
    if depth == 0 or r.random() < 0.3:
        c = r.random()
        if c < 0.5: return r.choice(fvars)
        if c < 0.7: return r.choice(ivars)
        return r.choice(['1.5', '-0.25', '3.0f', '1e10', '2.5e-3', '100', '0.1'])
    k = r.random()
    if k < 0.6:
        return '(%s %s %s)' % (fexpr(r, depth - 1, fvars, ivars), r.choice(['+', '-', '*']), fexpr(r, depth - 1, fvars, ivars))
    if k < 0.75:
        return '(%s)(%s)' % (r.choice(FTYPES), fexpr(r, depth - 1, fvars, ivars))
    if k < 0.85:
        return '(- %s)' % fexpr(r, depth - 1, fvars, ivars)
    return '(%s %s %s ? %s : %s)' % (fexpr(r, depth - 1, fvars, ivars), r.choice(['<', '>', '==', '!=', '<=']),
                                     fexpr(r, depth - 1, fvars, ivars), fexpr(r, depth - 1, fvars, ivars),
                                     fexpr(r, depth - 1, fvars, ivars))

def program(seed, n):
    r = random.Random(seed)
    out = ['#include <stdio.h>', 'int main(void) {']
    vars_ = []
    for i, t in enumerate(TYPES):
        v = 'v%d' % i
        out.append('\t%s %s = (%s)%s;' % (t, v, t, lit(r)))
        vars_.append(v)
    fvars = []
    for i, t in enumerate(FTYPES):
        v = 'f%d' % i
        out.append('\t%s %s = %s;' % (t, v, r.choice(['1.25', '-3.5', '1e3', '0.1'])))
        fvars.append(v)
    for i in range(n):
        e = expr(r, 4, vars_)
        out.append('\tprintf("%%d: %%llx\\n", %d, (unsigned long long)(%s));' % (i, e))
        if r.random() < 0.3:
            v = r.choice(vars_[:-1])
            op = r.choice(['=', '+=', '-=', '*=', '&=', '|=', '^=', '<<=', '>>='])
            rhs = expr(r, 2, vars_)
            if op in ('<<=', '>>='): rhs = '((%s) & 7)' % rhs
            out.append('\t%s %s %s;' % (v, op, rhs))
        if r.random() < 0.2:
            out.append('\t%s++; --%s;' % (r.choice(vars_[:-1]), r.choice(vars_[:-1])))
        if r.random() < 0.3:
            fe = fexpr(r, 3, fvars, vars_)
            out.append('\tprintf("%%d: %%.6g\\n", %d, (double)(%s));' % (i, fe))
    out.append('\treturn 0;\n}')
    return '\n'.join(out) + '\n'

def main():
    cc = sys.argv[1] if len(sys.argv) > 1 else './sccc'
    rounds = int(sys.argv[2]) if len(sys.argv) > 2 else 20
    tmp = tempfile.mkdtemp()
    fails = 0
    for seed in range(int(os.environ.get("SEED0", "0")), int(os.environ.get("SEED0", "0")) + rounds):
        src = os.path.join(tmp, 'f.c')
        open(src, 'w').write(program(seed, 200))
        g = subprocess.run(['gcc', '-w', '-fwrapv', '-O0', src, '-o', tmp + '/g'], capture_output=True)
        if g.returncode: print('gcc failed', seed, g.stderr.decode()[:300]); continue
        c = subprocess.run([cc, src, '-o', tmp + '/c'], capture_output=True)
        if c.returncode: print('sccc failed seed', seed, c.stderr.decode()[:300]); fails += 1; continue
        go = subprocess.run([tmp + '/g'], capture_output=True).stdout
        co = subprocess.run([tmp + '/c'], capture_output=True).stdout
        if go != co:
            fails += 1
            gl, cl = go.decode().splitlines(), co.decode().splitlines()
            for a, b in zip(gl, cl):
                if a != b:
                    print('seed %d mismatch: gcc %s | sccc %s' % (seed, a, b)); break
            open('fuzz_fail_%d.c' % seed, 'w').write(open(src).read())
    print('fuzz: %d rounds, %d failures' % (rounds, fails))
    sys.exit(1 if fails else 0)

main()
