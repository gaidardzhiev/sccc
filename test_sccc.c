/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#define TMP_SRC "/tmp/tc_src.c"
#define TMP_ASM "/tmp/tc_asm.s"
#define TMP_ASMP "/tmp/tc_asm.S"
#define TMP_BIN "/tmp/tc_bin"

static int g_pass = 0;
static int g_fail = 0;
static const char *g_compiler = NULL;

static void write_file(const char *path, const char *src) {
	FILE *f = fopen(path, "w");
	if (!f) { perror(path); exit(1); }
	fputs(src, f);
	fclose(f);
}

static void write_src(const char *src) { write_file(TMP_SRC, src); }

static int run(char *const argv[]) {
	pid_t pid = fork();
	if (pid < 0) { perror("fork"); exit(1); }
	if (pid == 0) {
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) { dup2(devnull, 1); dup2(devnull, 2); close(devnull); }
		execv(argv[0], argv);
		perror("execv"); _exit(127);
	}
	int status;
	waitpid(pid, &status, 0);
	return status;
}

static int compile(void) {
	char *argv[] = { (char*)g_compiler, TMP_SRC, "-o", TMP_BIN, NULL };
	int st = run(argv);
	if (!WIFEXITED(st)) return 1;
	return WEXITSTATUS(st);
}

static int run_bin(void) {
	chmod(TMP_BIN, 0755);
	char *argv[] = { TMP_BIN, NULL };
	int st = run(argv);
	if (!WIFEXITED(st)) return -1;
	return WEXITSTATUS(st);
}

static void check(const char *desc, const char *src, int want) {
	write_src(src);
	int cc_ret = compile();
	if (cc_ret != 0) {
		printf("  FAIL  [%s]  compiler exited %d (expected success)\n", desc, cc_ret);
		g_fail++;
		return;
	}
	int got = run_bin();
	if (got == want) {
		printf("  PASS  [%s]  exit=%d\n", desc, got);
		g_pass++;
	} else {
		printf("  FAIL  [%s]  expected=%d got=%d\n", desc, want, got);
		g_fail++;
	}
}

static void check_compile_err(const char *desc, const char *src) {
	write_src(src);
	int cc_ret = compile();
	if (cc_ret != 0) {
		printf("  PASS  [%s]  (compile error as expected)\n", desc);
		g_pass++;
	} else {
		printf("  FAIL  [%s]  (expected compile error but succeeded)\n", desc);
		g_fail++;
	}
}

static int compile_asm(const char *csrc, const char *asrc, int cpp) {
	const char *apath = cpp ? TMP_ASMP : TMP_ASM;
	write_file(apath, asrc);
	if (csrc) write_src(csrc);
	char *argv[] = { (char*)g_compiler, (char*)apath, csrc ? TMP_SRC : NULL, NULL, NULL, NULL };
	if (csrc) { argv[3] = "-o"; argv[4] = TMP_BIN; }
	else { argv[2] = "-o"; argv[3] = TMP_BIN; }
	int st = run(argv);
	if (!WIFEXITED(st)) return 1;
	return WEXITSTATUS(st);
}

static void check_asm(const char *desc, const char *csrc, const char *asrc, int cpp, int want) {
	int cc_ret = compile_asm(csrc, asrc, cpp);
	if (cc_ret != 0) {
		printf("  FAIL  [%s]  compiler exited %d (expected success)\n", desc, cc_ret);
		g_fail++;
		return;
	}
	int got = run_bin();
	if (got == want) {
		printf("  PASS  [%s]  exit=%d\n", desc, got);
		g_pass++;
	} else {
		printf("  FAIL  [%s]  expected=%d got=%d\n", desc, want, got);
		g_fail++;
	}
}

static void check_asm_err(const char *desc, const char *asrc) {
	if (compile_asm(NULL, asrc, 0) != 0) {
		printf("  PASS  [%s]  (assembly error as expected)\n", desc);
		g_pass++;
	} else {
		printf("  FAIL  [%s]  (expected assembly error but succeeded)\n", desc);
		g_fail++;
	}
}

static int compile_objects(const char *a, const char *b) {
	write_file("/tmp/tc_a.c", a);
	write_file("/tmp/tc_b.c", b);
	char *c1[] = { (char*)g_compiler, "-c", "/tmp/tc_a.c", "-o", "/tmp/tc_a.o", NULL };
	char *c2[] = { (char*)g_compiler, "-c", "/tmp/tc_b.c", "-o", "/tmp/tc_b.o", NULL };
	char *ln[] = { (char*)g_compiler, "/tmp/tc_a.o", "/tmp/tc_b.o", "-o", TMP_BIN, NULL };
	int st = run(c1);
	if (!WIFEXITED(st) || WEXITSTATUS(st)) return 1;
	st = run(c2);
	if (!WIFEXITED(st) || WEXITSTATUS(st)) return 1;
	st = run(ln);
	return !WIFEXITED(st) || WEXITSTATUS(st);
}

static void check_objects(const char *desc, const char *a, const char *b, int want) {
	if (compile_objects(a, b)) {
		printf("  FAIL  [%s]  compile or link failed\n", desc);
		g_fail++;
		return;
	}
	int got = run_bin();
	if (got == want) { printf("  PASS  [%s]  exit=%d\n", desc, got); g_pass++; }
	else { printf("  FAIL  [%s]  expected=%d got=%d\n", desc, want, got); g_fail++; }
}

static void check_link_err(const char *desc, const char *a, const char *b) {
	if (compile_objects(a, b)) { printf("  PASS  [%s]  (link error as expected)\n", desc); g_pass++; }
	else { printf("  FAIL  [%s]  (expected link error but succeeded)\n", desc); g_fail++; }
}

static int compile_script(const char *src, const char *script, int compile_only) {
	write_src(src);
	write_file("/tmp/tc_script.ld", script);
	char *argv[] = { (char*)g_compiler, "-T", "/tmp/tc_script.ld", TMP_SRC, "-o", TMP_BIN, NULL, NULL };
	if (compile_only) argv[6] = "-c";
	int st = run(argv);
	return !WIFEXITED(st) || WEXITSTATUS(st);
}

static void check_script(const char *desc, const char *src, const char *script, int want) {
	if (compile_script(src, script, 0)) {
		printf("  FAIL  [%s]  compile or link failed\n", desc);
		g_fail++;
		return;
	}
	int got = run_bin();
	if (got == want) { printf("  PASS  [%s]  exit=%d\n", desc, got); g_pass++; }
	else { printf("  FAIL  [%s]  expected=%d got=%d\n", desc, want, got); g_fail++; }
}

static void check_script_err(const char *desc, const char *src, const char *script, int compile_only) {
	if (compile_script(src, script, compile_only)) { printf("  PASS  [%s]  (error as expected)\n", desc); g_pass++; }
	else { printf("  FAIL  [%s]  (expected an error but succeeded)\n", desc); g_fail++; }
}

static int run_args(const char **args) {
	char *argv[40];
	int n = 0;
	argv[n++] = (char*)g_compiler;
	for (int i = 0; args[i] && n < 39; i++) argv[n++] = (char*)args[i];
	argv[n] = NULL;
	int st = run(argv);
	return !WIFEXITED(st) || WEXITSTATUS(st);
}

static void check_args(const char *desc, const char *src, const char **args, int want) {
	write_src(src);
	if (run_args(args)) { printf("  FAIL  [%s]  compile or link failed\n", desc); g_fail++; return; }
	int got = run_bin();
	if (got == want) { printf("  PASS  [%s]  exit=%d\n", desc, got); g_pass++; }
	else { printf("  FAIL  [%s]  expected=%d got=%d\n", desc, want, got); g_fail++; }
}

static void check_args_err(const char *desc, const char *src, const char **args) {
	write_src(src);
	if (run_args(args)) { printf("  PASS  [%s]  (error as expected)\n", desc); g_pass++; }
	else { printf("  FAIL  [%s]  (expected an error but succeeded)\n", desc); g_fail++; }
}

static void section(const char *title) {
	printf("\n=== %s ===\n", title);
}

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: %s <path-to-sccc-compiler>\n", argv[0]);
		return 1;
	}
	g_compiler = argv[1];

	section("arithmetic (regression)");
	check("literal 0", "int main(){ return 0; }", 0);
	check("literal 42", "int main(){ return 42; }", 42);
	check("add", "int main(){ return 1+2; }", 3);
	check("precedence *", "int main(){ return 2+3*4; }", 14);
	check("parens", "int main(){ return (2+3)*4; }", 20);
	check("sub", "int main(){ return 10-3; }", 7);
	check("div", "int main(){ return 20/4; }", 5);
	check("unary minus", "int main(){ return -3+10; }", 7);

	section("variables (regression)");
	check("declare+use", "int main(){ int x = 5; return x; }", 5);
	check("two vars", "int main(){ int a=3; int b=4; return a+b; }", 7);
	check("reassign", "int main(){ int x=1; x=99; return x; }", 99);
	check("accumulate", "int main(){ int s=0; s=s+10; s=s+20; return s; }", 30);

	section("comparison operators");
	check("1<2  → 1", "int main(){ return 1<2; }", 1);
	check("2<1  → 0", "int main(){ return 2<1; }", 0);
	check("1>2  → 0", "int main(){ return 1>2; }", 0);
	check("2>1  → 1", "int main(){ return 2>1; }", 1);
	check("1==1 → 1", "int main(){ return 1==1; }", 1);
	check("1==2 → 0", "int main(){ return 1==2; }", 0);
	check("1!=2 → 1", "int main(){ return 1!=2; }", 1);
	check("1!=1 → 0", "int main(){ return 1!=1; }", 0);
	check("2<=2 → 1", "int main(){ return 2<=2; }", 1);
	check("3<=2 → 0", "int main(){ return 3<=2; }", 0);
	check("2>=2 → 1", "int main(){ return 2>=2; }", 1);
	check("1>=2 → 0", "int main(){ return 1>=2; }", 0);
	check("cmp vars",
		"int main(){ int a=5; int b=3; return a>b; }", 1);
	check("cmp expr",
		"int main(){ int a=4; return a*2==8; }", 1);

	section("if / else");
	check("if true  branch",
		"int main(){ int x=0; if(1){ x=10; } return x; }", 10);
	check("if false branch",
		"int main(){ int x=0; if(0){ x=10; } return x; }", 0);
	check("if/else true",
		"int main(){ if(1){ return 7; } else { return 3; } return 0; }", 7);
	check("if/else false",
		"int main(){ if(0){ return 7; } else { return 3; } return 0; }", 3);
	check("if with cmp true",
		"int main(){ int a=5; if(a>3){ return 1; } return 0; }", 1);
	check("if with cmp false",
		"int main(){ int a=2; if(a>3){ return 1; } return 0; }", 0);
	check("if/else cmp",
		"int main(){ int a=4; int b=4; if(a==b){ return 11; } else { return 22; } return 0; }", 11);
	check("nested if",
		"int main(){"
		"  int a=5;"
		"  if(a>3){"
		"    if(a<10){ return 1; }"
		"    return 2;"
		"  }"
		"  return 3;"
		"}", 1);
	check("if sets var",
		"int main(){"
		"  int x=0;"
		"  int a=7;"
		"  if(a>=5){ x=100; } else { x=200; }"
		"  return x;"
		"}", 100);

	section("while loops");
	check("while false (never runs)",
		"int main(){ int x=5; while(0){ x=99; } return x; }", 5);
	check("count to 5",
		"int main(){"
		"  int i=0;"
		"  while(i<5){ i=i+1; }"
		"  return i;"
		"}", 5);
	check("sum 1..10",
		"int main(){"
		"  int s=0;"
		"  int i=1;"
		"  while(i<=10){ s=s+i; i=i+1; }"
		"  return s;"
		"}", 55);
	check("multiply via add (3*4)",
		"int main(){"
		"  int a=3; int b=4; int r=0;"
		"  while(b>0){ r=r+a; b=b-1; }"
		"  return r;"
		"}", 12);
	check("nested while",
		"int main(){"
		"  int s=0; int i=0;"
		"  while(i<3){"
		"    int j=0;"
		"    while(j<3){ s=s+1; j=j+1; }"
		"    i=i+1;"
		"  }"
		"  return s;"
		"}", 9);
	check("while with if inside",
		"int main(){"
		"  int s=0; int i=0;"
		"  while(i<10){"
		"    if(i==5){ s=s+10; } else { s=s+1; }"
		"    i=i+1;"
		"  }"
		"  return s;"
		"}", 19);

	section("combined");
	check("fibonacci(7)=13",
		"int main(){"
		"  int a=0; int b=1; int i=0;"
		"  while(i<7){"
		"    int t=b;"
		"    b=a+b;"
		"    a=t;"
		"    i=i+1;"
		"  }"
		"  return a;"
		"}", 13);
	check("abs(-5)=5 via if/else",
		"int main(){"
		"  int x=0-5;"
		"  if(x<0){ x=0-x; }"
		"  return x;"
		"}", 5);
	check("max(3,7)=7",
		"int main(){"
		"  int a=3; int b=7; int m=a;"
		"  if(b>a){ m=b; }"
		"  return m;"
		"}", 7);
	check("power 2^6=64",
		"int main(){"
		"  int r=1; int i=0;"
		"  while(i<6){ r=r*2; i=i+1; }"
		"  return r;"
		"}", 64);

	section("modulo operator");
	check("10%3=1", "int main(){ return 10%3; }", 1);
	check("9%3=0", "int main(){ return 9%3; }", 0);
	check("7%4=3", "int main(){ return 7%4; }", 3);
	check("even check",
		"int main(){ int n=8; return n%2==0; }", 1);
	check("odd check",
		"int main(){ int n=7; return n%2==0; }", 0);
	check("mod in while (count multiples of 3 up to 9)",
		"int main(){"
		"  int i=1; int c=0;"
		"  while(i<=9){ if(i%3==0){ c=c+1; } i=i+1; }"
		"  return c;"
		"}", 3);

	section("logical not (!)");
	check("!0 = 1", "int main(){ return !0; }", 1);
	check("!1 = 0", "int main(){ return !1; }", 0);
	check("!42 = 0", "int main(){ return !42; }", 0);
	check("!!1 = 1", "int main(){ return !!1; }", 1);
	check("!var false",
		"int main(){ int x=5; return !x; }", 0);
	check("!var true",
		"int main(){ int x=0; return !x; }", 1);
	check("! in if",
		"int main(){ int done=0; if(!done){ return 7; } return 3; }", 7);

	section("logical AND (&&)");
	check("1&&1 = 1", "int main(){ return 1&&1; }", 1);
	check("1&&0 = 0", "int main(){ return 1&&0; }", 0);
	check("0&&1 = 0", "int main(){ return 0&&1; }", 0);
	check("0&&0 = 0", "int main(){ return 0&&0; }", 0);
	check("cmp &&",
		"int main(){ int a=5; return a>0&&a<10; }", 1);
	check("cmp && false left",
		"int main(){ int a=0-1; return a>0&&a<10; }", 0);
	check("cmp && false right",
		"int main(){ int a=15; return a>0&&a<10; }", 0);
	check("three-way &&",
		"int main(){ int a=5; return a>0&&a<10&&a!=3; }", 1);
	check("short-circuit && (rhs not eval'd when lhs=0)",
		"int main(){"
		"  int x=0; int y=0;"
		"  int r = y!=0 && x==0;"
		"  return x;"
		"}", 0);

	section("logical OR (||)");
	check("1||1 = 1", "int main(){ return 1||1; }", 1);
	check("1||0 = 1", "int main(){ return 1||0; }", 1);
	check("0||1 = 1", "int main(){ return 0||1; }", 1);
	check("0||0 = 0", "int main(){ return 0||0; }", 0);
	check("cmp ||",
		"int main(){ int a=15; return a<0||a>10; }", 1);
	check("cmp || both false",
		"int main(){ int a=5; return a<0||a>10; }", 0);
	check("three-way ||",
		"int main(){ int a=5; return a<0||a>10||a==5; }", 1);

	section("mixed && || (and binds tighter than or)");
	check("0||1&&0 = 0", "int main(){ return 0||1&&0; }", 0);
	check("1||0&&0 = 1", "int main(){ return 1||0&&0; }", 1);
	check("1&&0||1 = 1", "int main(){ return 1&&0||1; }", 1);

	section("combined with new ops");
	check("fizzbuzz exit (n=15 → fizzbuzz → return 2)",
		"int main(){"
		"  int n=15;"
		"  if(n%15==0){ return 2; }"
		"  if(n%3==0){ return 1; }"
		"  return 0;"
		"}", 2);
	check("range check via &&",
		"int main(){"
		"  int x=7;"
		"  if(x>=5&&x<=10){ return 1; } else { return 0; }"
		"}", 1);
	check("count evens 1..10 = 5",
		"int main(){"
		"  int i=1; int c=0;"
		"  while(i<=10){"
		"    if(i%2==0){ c=c+1; }"
		"    i=i+1;"
		"  }"
		"  return c;"
		"}", 5);

	section("break");
	check("break exits loop immediately",
		"int main(){"
		"  int i=0;"
		"  while(1){ if(i==5){ break; } i=i+1; }"
		"  return i;"
		"}", 5);
	check("break skips rest of body",
		"int main(){"
		"  int x=0;"
		"  while(x<10){ x=x+1; if(x==3){ break; } }"
		"  return x;"
		"}", 3);
	check("break in nested if inside while",
		"int main(){"
		"  int i=0; int s=0;"
		"  while(i<10){"
		"    i=i+1;"
		"    if(i%2==0){ if(i==6){ break; } s=s+i; }"
		"  }"
		"  return s;"
		"}", 6);
	check("break in outer of nested loops",
		"int main(){"
		"  int i=0; int j=0;"
		"  while(i<5){"
		"    j=0;"
		"    while(j<5){ j=j+1; }"
		"    if(i==2){ break; }"
		"    i=i+1;"
		"  }"
		"  return i;"
		"}", 2);
	check("break in inner loop only",
		"int main(){"
		"  int i=0; int s=0;"
		"  while(i<3){"
		"    int j=0;"
		"    while(1){ if(j==2){ break; } j=j+1; }"
		"    s=s+j;"
		"    i=i+1;"
		"  }"
		"  return s;"
		"}", 6);
	check("multiple breaks in one loop",
		"int main(){"
		"  int i=0;"
		"  while(1){"
		"    if(i==3){ break; }"
		"    if(i==7){ break; }"
		"    i=i+1;"
		"  }"
		"  return i;"
		"}", 3);

	section("continue");
	check("continue skips rest of body",
		"int main(){"
		"  int i=0; int s=0;"
		"  while(i<5){"
		"    i=i+1;"
		"    if(i%2==0){ continue; }"
		"    s=s+i;"
		"  }"
		"  return s;"
		"}", 9);
	check("continue with counter still increments",
		"int main(){"
		"  int i=0; int c=0;"
		"  while(i<10){"
		"    i=i+1;"
		"    if(i==5){ continue; }"
		"    c=c+1;"
		"  }"
		"  return c;"
		"}", 9);
	check("continue in inner loop only",
		"int main(){"
		"  int i=0; int s=0;"
		"  while(i<3){"
		"    int j=0;"
		"    while(j<4){"
		"      j=j+1;"
		"      if(j==2){ continue; }"
		"      s=s+j;"
		"    }"
		"    i=i+1;"
		"  }"
		"  return s;"
		"}", 24);
	check("continue only applies to innermost loop",
		"int main(){"
		"  int i=0; int outer=0;"
		"  while(i<3){"
		"    int j=0;"
		"    while(j<3){"
		"      j=j+1;"
		"      if(j==2){ continue; }"
		"    }"
		"    outer=outer+1;"
		"    i=i+1;"
		"  }"
		"  return outer;"
		"}", 3);

	section("break and continue together");
	check("first even >= 4 that is not 6",
		"int main(){"
		"  int i=0; int r=0;"
		"  while(i<20){"
		"    i=i+1;"
		"    if(i%2!=0){ continue; }"
		"    if(i==6){   continue; }"
		"    if(i>=4){ r=i; break; }"
		"  }"
		"  return r;"
		"}", 4);
	check("sum evens 1..10 skipping 6 and 8",
		"int main(){"
		"  int i=0; int s=0;"
		"  while(i<10){"
		"    i=i+1;"
		"    if(i%2!=0){ continue; }"
		"    if(i==8){ break; }"
		"    if(i==6){ continue; }"
		"    s=s+i;"
		"  }"
		"  return s;"
		"}", 6);

	section("GNU C and C11 extensions");
	check("statement expression",
		"int main(){ int x = ({ int a = 4; a * 5; }); return x; }", 20);
	check("typeof",
		"int main(){ long v = 3; typeof(v) w = 9; return sizeof(w) + w; }", 17);
	check("_Generic",
		"int main(){ return _Generic(1L, int: 1, long: 2, default: 3); }", 2);
	check("case range",
		"int main(){ switch (42) { case 40 ... 49: return 4; default: return 0; } }", 4);
	check("range designator",
		"int a[6] = { [1 ... 4] = 3 }; int main(){ return a[0] + a[1] + a[4] + a[5]; }", 6);
	check("computed goto",
		"int main(){ void *p = &&b; goto *p; a: return 1; b: return 2; }", 2);
	check("__builtin_constant_p",
		"int main(){ int x = 1; return __builtin_constant_p(3) * 10 + __builtin_constant_p(x); }", 10);
	check("__builtin_offsetof",
		"struct s { char c; int v[4]; }; int main(){ return __builtin_offsetof(struct s, v[2]); }", 12);
	check("aligned global",
		"char c; int v __attribute__((aligned(256))); int main(){ return (long)&v % 256; }", 0);
	check("weak undefined",
		"int f(void) __attribute__((weak)); int main(){ return f ? 1 : 7; }", 7);

	section("assembler");
	check_asm("C calls asm",
		"int f(void); int main(){ return f(); }",
		".globl f\nf:\n\tmov $42, %eax\n\tret\n", 0, 42);
	check_asm("main in asm", NULL,
		"\t.globl main\nmain:\tmov $7, %eax; ret\n", 0, 7);
	check_asm("asm calls C",
		"int twice(int x){ return 2 * x; }",
		"\t.globl main\nmain:\tsub $8, %rsp\n\tmov $21, %edi\n\tcall twice\n\tadd $8, %rsp\n\tret\n", 0, 42);
	check_asm("asm data",
		"extern int v[]; int main(){ return v[0] * v[1]; }",
		"\t.data\n\t.globl v\nv:\t.long 5, 6\n", 0, 30);
	check_asm("local labels", NULL,
		"\t.globl main\nmain:\txor %eax, %eax\n\tmov $10, %ecx\n1:\tadd %ecx, %eax\n\tloop 1b\n\tret\n", 0, 55);
	check_asm(".S macros", NULL,
		"#define N 9\n#define F(x) ((x) * 2)\n\t.globl main\nmain:\tmov $F(N), %eax # comment\n\tret\n", 1, 18);
	check_asm("relaxed jump", NULL,
		"\t.globl main\nmain:\tmov $3, %eax\n\tjmp 1f\n\t.fill 300, 1, 0xcc\n1:\tret\n", 0, 3);
	check_asm(".comm buffer",
		"extern char buf[]; int main(){ buf[63] = 9; return buf[63] + buf[0]; }",
		"\t.comm buf, 64, 16\n", 0, 9);
	check_asm("forward equate", NULL,
		"\t.globl main\nmain:\tmov $K * 2, %eax\n\tret\n\t.set K, 21\n", 0, 42);
	check_asm("jump table", NULL,
		"\t.globl main\nmain:\tmov $2, %eax\n\tlea t(%rip), %rdx\n\tjmp *(%rdx,%rax,8)\n"
		"a:\tmov $1, %eax; ret\nb:\tmov $2, %eax; ret\nc:\tmov $3, %eax; ret\n"
		"\t.data\nt:\t.quad a, b, c\n", 0, 3);
	check_asm_err("unknown instruction", "\tfrob %eax\n");
	check_asm_err("unknown register", "\tmov %eqx, %eax\n");
	check_asm_err("operand size unknown", "\tmov $1, (%rax)\n");
	check_asm_err("immediate too large", "\tmovb $300, %al\n");
	check_asm_err("undefined local label", "\tjmp 1f\n");
	check_asm_err("duplicate label", "a:\tnop\na:\tnop\n");
	check_asm_err("64-bit register in 16-bit code", "\t.code16\n\tmov %rax, %rbx\n");
	check_asm_err("%ah with REX", "\tmov %ah, %sil\n");

	section("inline assembly");
	check("asm output",
		"int main(){ int r; asm(\"mov $42, %0\" : \"=r\"(r)); return r; }", 42);
	check("asm inputs",
		"int main(){ int a = 20, b = 22, r; __asm__(\"lea (%1,%2), %0\" : \"=r\"(r) : \"r\"(a), \"r\"(b)); return r; }", 42);
	check("asm read-write",
		"int main(){ long v = 40; __asm__ __volatile__(\"add $2, %0\" : \"+r\"(v)); return v; }", 42);
	check("asm memory operand",
		"int g = 41; int main(){ asm(\"incl %0\" : \"+m\"(g)); return g; }", 42);
	check("asm fixed registers",
		"int main(){ unsigned a, d; asm(\"mul %%edx\" : \"=a\"(a), \"=d\"(d) : \"a\"(6), \"d\"(7)); return a; }", 42);
	check("asm goto",
		"int main(){ asm goto(\"jmp %l0\" : : : : out); return 1; out: return 42; }", 42);
	check("naked function",
		"__attribute__((naked)) int f(void){ asm(\"mov $42, %eax; ret\"); } int main(){ return f(); }", 42);
	check("file scope asm",
		"asm(\".globl f; f: mov $42, %eax; ret\"); int f(void); int main(){ return f(); }", 42);
	check("register variable",
		"int main(){ register long x asm(\"r9\") = 41; long r; asm(\"lea 1(%%r9), %0\" : \"=r\"(r) : \"r\"(x)); return r; }", 42);
	check_compile_err("asm unsupported constraint", "int main(){ float f; asm(\"\" : \"=x\"(f)); return 0; }");
	check_compile_err("asm output not lvalue", "int main(){ asm(\"\" : \"=r\"(1)); return 0; }");
	check_compile_err("asm unknown operand name", "int main(){ int r; asm(\"mov %[nope], %0\" : \"=r\"(r)); return r; }");
	check_compile_err("asm operand out of range", "int main(){ int r; asm(\"mov %3, %0\" : \"=r\"(r)); return r; }");
	check_compile_err("asm operands at file scope", "asm(\"\" : : \"r\"(1)); int main(){ return 0; }");
	check_compile_err("naked with C code", "__attribute__((naked)) int f(void){ return 1; } int main(){ return f(); }");
	check_compile_err("asm clobbers rsp", "int main(){ asm(\"\" : : : \"rsp\"); return 0; }");
	check_compile_err("asm bad instruction", "int main(){ asm(\"frob %eax\"); return 0; }");

	section("objects and linking");
	check_objects("call across objects",
		"int f(int); int main(){ return f(40); }", "int f(int x){ return x + 2; }", 42);
	check_objects("struct return across objects",
		"struct s { long a, b; }; struct s f(void); int main(){ struct s v = f(); return v.a + v.b; }",
		"struct s { long a, b; }; struct s f(void){ struct s v = { 40, 2 }; return v; }", 42);
	check_objects("double across objects",
		"double f(double, float); int main(){ return (int)f(40.5, 1.5f); }", "double f(double a, float b){ return a + b; }", 42);
	check_objects("weak overridden",
		"int f(void) __attribute__((weak)); int f(void){ return 1; } int main(){ return f(); }", "int f(void){ return 42; }", 42);
	check_objects("tentative definitions merge",
		"int n; int g(void); int main(){ n = 40; return g(); }", "int n; int g(void){ return n + 2; }", 42);
	check_objects("global data across objects",
		"extern int t[]; int main(){ return t[1]; }", "int t[] = { 1, 42, 3 };", 42);
	check_link_err("multiple definition", "int f(void){ return 1; } int main(){ return f(); }", "int f(void){ return 2; }");
	check_link_err("undefined reference", "int g(void); int main(){ return g(); }", "int h(void){ return 2; }");

	section("linker scripts");
#define BASIC_LD "SECTIONS { . = 0x500000 + SIZEOF_HEADERS; .text : { *(.text) } .rodata : { *(.rodata) } "
	check_script("sections and symbols",
		"extern char start_d[], end_d[]; int x = 5, y = 6; int main(){ return (end_d - start_d >= 8) + 41 * !((long)start_d & 0xFFF); }",
		BASIC_LD ". = ALIGN(0x1000); .data : { start_d = .; *(.data) end_d = .; } .bss : { *(.bss) } }", 42);
	check_script("PROVIDE gives an absolute symbol",
		"extern char answer[]; int main(){ return (int)(long)answer; }",
		BASIC_LD ".data : { *(.data) } .bss : { *(.bss) } PROVIDE(answer = 42); PROVIDE(unused = 1); }", 42);
	check_script("data commands",
		"extern unsigned char tbl[]; int main(){ return tbl[0] + tbl[1] + tbl[2]; }",
		BASIC_LD ".data : { *(.data) tbl = .; BYTE(40) BYTE(2) BYTE(0) } .bss : { *(.bss) } }", 42);
	check_script("SORT and KEEP build a table",
		"__attribute__((section(\".t.2\"))) const int b = 2; __attribute__((section(\".t.1\"))) const int a = 40;"
		"extern const int ts[], te[]; int main(){ return te - ts == 2 && ts[0] == 40 ? ts[0] + ts[1] : 1; }",
		BASIC_LD ".tab : { ts = .; KEEP(*(SORT(.t.*))) te = .; } .data : { *(.data) } .bss : { *(.bss) } }", 42);
	check_script("orphans follow their kind",
		"__attribute__((section(\"mine\"))) int m = 42; int main(){ return m; }",
		BASIC_LD ".data : { *(.data) } .bss : { *(.bss) } }", 42);
	check_script_err("ASSERT fails", "int main(){ return 0; }", BASIC_LD "ASSERT(0, \"no\") }", 0);
	check_script_err("undefined symbol in a script", "int main(){ return 0; }", BASIC_LD "x = nothing + 1; }", 0);
	check_script_err("script syntax error", "int main(){ return 0; }", "SECTIONS { .text : { *(.text) ", 0);
	check_script_err("unsupported command", "int main(){ return 0; }", "MEMORY { ram : ORIGIN = 0, LENGTH = 1M }", 0);
	check_script_err("sections overlap", "int x = 1; int main(){ return x; }",
		"SECTIONS { .text 0x500000 : { *(.text) } .data 0x500010 : { *(.data) } }", 0);
	check_script_err("symbol in a discarded section", "const char *s = \"text\"; int main(){ return s[0]; }",
		"SECTIONS { /DISCARD/ : { *(.rodata) } . = 0x500000; .text : { *(.text) } .data : { *(.data) } }", 0);
	check_script_err("-T with -c", "int main(){ return 0; }", BASIC_LD "}", 1);
	check_asm("conditional assembly",
		"int f(void); int main(){ return f(); }",
		"\t.set X, 3\n\t.globl f\nf:\n.if X - 3\n\tmov $1, %eax\n.elseif X\n.ifdef f\n\tmov $42, %eax\n.else\n\tmov $2, %eax\n.endif\n.else\n\tmov $3, %eax\n.endif\n\tret\n", 0, 42);
	check_asm_err("missing .endif", ".if 1\n\tnop\n");

	section("gcc and ld options, thread-local storage");
	{
		static const char *gccflags[] = { "-fno-pic", "-static", "-fno-builtin", "-fno-strict-aliasing", "-Wall", "-MD", "-ggdb",
			"-fno-omit-frame-pointer", "-fno-common", "-gdwarf-2", "-m64", "-mcmodel=kernel", "-mtls-direct-seg-refs",
			"-mno-red-zone", "-O0", "-O2", "-fno-stack-protector", "-Wa,-divide", TMP_SRC, "-o", TMP_BIN, NULL };
		check_args("harmless gcc options", "int main(){ return 42; }", gccflags, 42);
		static const char *nostdinc[] = { "-nostdinc", TMP_SRC, "-o", TMP_BIN, NULL };
		check_args_err("-nostdinc hides the built-in headers", "#include <stdio.h>\nint main(){ return 0; }", nostdinc);
		static const char *omagic[] = { "-N", TMP_SRC, "-o", TMP_BIN, NULL };
		check_args("-N puts everything in one segment", "int x = 40; int main(){ static char b[100]; b[99] = 2; return x + b[99]; }", omagic, 42);
		static const char *emul[] = { "-m", "elf_x86_64", TMP_SRC, "-o", TMP_BIN, NULL };
		check_args("-m elf_x86_64", "int main(){ return 42; }", emul, 42);
		static const char *bademul[] = { "-m", "elf_i386", TMP_SRC, "-o", TMP_BIN, NULL };
		check_args_err("other emulations are refused", "int main(){ return 0; }", bademul);
		write_file("/tmp/tc_blob.bin", "0123456789abcdefghijklmnopqrstuvwxyz0123456");
		static const char *bin[] = { TMP_SRC, "-b", "binary", "/tmp/tc_blob.bin", "-o", TMP_BIN, NULL };
		check_args("-b binary", "extern char _binary__tmp_tc_blob_bin_start[], _binary__tmp_tc_blob_bin_end[], _binary__tmp_tc_blob_bin_size[];"
			"int main(){ return (_binary__tmp_tc_blob_bin_end - _binary__tmp_tc_blob_bin_start == (long)_binary__tmp_tc_blob_bin_size)"
			" * (_binary__tmp_tc_blob_bin_start[1] - '0' + 41); }", bin, 42);
		write_file("/tmp/tc_a.c", "int n; int g(void){ return n; }");
		write_file("/tmp/tc_b.c", "int n; int main(){ n = 42; return 0; }");
		static const char *c1[] = { "-c", "-fno-common", "/tmp/tc_a.c", "-o", "/tmp/tc_a.o", NULL };
		static const char *c2[] = { "-c", "-fno-common", "/tmp/tc_b.c", "-o", "/tmp/tc_b.o", NULL };
		static const char *ln[] = { "/tmp/tc_a.o", "/tmp/tc_b.o", "-o", TMP_BIN, NULL };
		int ok = !run_args(c1) && !run_args(c2) && run_args(ln);
		printf("  %s  [-fno-common makes tentative definitions clash]\n", ok ? "PASS" : "FAIL");
		if (ok) g_pass++; else g_fail++;
		write_file("/tmp/tc_a.c", "void start(void){ __syscall(60, 42); }");
		static const char *c3[] = { "-c", "/tmp/tc_a.c", "-o", "/tmp/tc_a.o", NULL };
		static const char *ln2[] = { "-nodefaultlibs", "-e", "start", "/tmp/tc_a.o", "-o", TMP_BIN, NULL };
		ok = !run_args(c3) && !run_args(ln2) && run_bin() == 42;
		printf("  %s  [-nodefaultlibs links no library and no startup code]\n", ok ? "PASS" : "FAIL");
		if (ok) g_pass++; else g_fail++;
	}
	check("__builtin_return_address and frame_address",
		"__attribute__((noinline)) void *ra(void){ return __builtin_return_address(0); }"
		"void *fa(void){ return __builtin_frame_address(1); }"
		"int main(){ void *r = ra(); void *f = __builtin_frame_address(0); return (r != 0) + 40 * (fa() == f) + (f != 0); }", 42);
	check("thread-local variables",
		"__thread int a = 40; static __thread long b; int main(){ b += 2; a++; return a + (int)b - 1; }", 42);
	check_compile_err("thread-local automatic variable", "int main(){ __thread int x = 1; return x; }");
	check_compile_err("thread-local function", "__thread int f(void); int main(){ return 0; }");
	check_asm("sum with a forward difference",
		"long target = 30; extern long v[]; int main(){ return (int)(v[0] - (long)&target); }",
		"\t.data\n\t.globl v\nv:\t.quad end - start + target\nstart:\t.skip 42\nend:\n", 0, 42);

	section("expected compile errors");
	check_compile_err("undeclared var", "int main(){ return x; }");
	check_compile_err("redeclare var", "int main(){ int x=1; int x=2; return x; }");
	check_compile_err("break outside loop", "int main(){ break; return 0; }");
	check_compile_err("continue outside loop","int main(){ continue; return 0; }");
	check_compile_err("_Generic no match", "int main(){ return _Generic(1.0, int: 1); }");
	check_compile_err("local aligned 64", "int main(){ int x __attribute__((aligned(64))) = 0; return x; }");
	check_compile_err("alignment not 2^n", "int x __attribute__((aligned(12))); int main(){ return 0; }");
	check_compile_err("alias of undefined", "int f(void) __attribute__((alias(\"g\"))); int main(){ return 0; }");
	check_compile_err("section kind clash",
		"int a __attribute__((section(\"s\"))) = 1; void f(void) __attribute__((section(\"s\"))); void f(void) {} int main(){ return 0; }");
	check_compile_err("goto * integer", "int main(){ int x = 0; goto *x; }");

	printf("\n");
	printf("Results: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
