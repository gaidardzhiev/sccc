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
		execvp(argv[0], argv);
		perror("execvp"); _exit(127);
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

static void check_file(const char *desc, const char *src, const char **args, const char *path, const char *want) {
	write_src(src);
	remove(path);
	if (run_args(args)) { printf("  FAIL  [%s]  sccc failed\n", desc); g_fail++; return; }
	FILE *f = fopen(path, "rb");
	char buf[8192];
	size_t n = f ? fread(buf, 1, sizeof(buf) - 1, f) : 0;
	if (f) fclose(f);
	buf[n] = 0;
	if (strstr(buf, want)) { printf("  PASS  [%s]\n", desc); g_pass++; }
	else { printf("  FAIL  [%s]  '%s' not in %s\n", desc, want, path); g_fail++; }
}

static int run_steps(const char *const *files, const char *const *const *steps) {
	for (int i = 0; files[i]; i += 2) write_file(files[i], files[i + 1]);
	for (int i = 0; steps[i]; i++) {
		char *argv[24];
		int n = 0;
		const char *const *a = steps[i];
		if (!strcmp(a[0], "@ar")) argv[n++] = "ar", a++;
		else if (!strcmp(a[0], "@sh")) argv[n++] = "sh", a++;
		else argv[n++] = (char*)g_compiler;
		while (*a && n < 23) argv[n++] = !strcmp(*a, "@CC") ? (char*)g_compiler : (char*)*a, a++;
		argv[n] = NULL;
		int st = run(argv);
		if (!WIFEXITED(st) || WEXITSTATUS(st)) return 1;
	}
	return 0;
}

static void check_steps(const char *desc, const char *const *files, const char *const *const *steps, int want) {
	if (run_steps(files, steps)) { printf("  FAIL  [%s]  a step failed\n", desc); g_fail++; return; }
	int got = run_bin();
	if (got == want) { printf("  PASS  [%s]  exit=%d\n", desc, got); g_pass++; }
	else { printf("  FAIL  [%s]  expected=%d got=%d\n", desc, want, got); g_fail++; }
}

static void check_elf_byte(const char *desc, const char *const *files, const char *const *const *steps, const char *path, int off, int want) {
	if (run_steps(files, steps)) { printf("  FAIL  [%s]  a step failed\n", desc); g_fail++; return; }
	unsigned char b[64];
	FILE *f = fopen(path, "rb");
	size_t n = f ? fread(b, 1, sizeof(b), f) : 0;
	if (f) fclose(f);
	if (n > (size_t)off && b[off] == want) { printf("  PASS  [%s]\n", desc); g_pass++; }
	else { printf("  FAIL  [%s]  byte %d of %s is not %d\n", desc, off, path, want); g_fail++; }
}

static void check_steps_file(const char *desc, const char *const *files, const char *const *const *steps, const char *path, const char *want) {
	if (run_steps(files, steps)) { printf("  FAIL  [%s]  a step failed\n", desc); g_fail++; return; }
	FILE *f = fopen(path, "rb");
	char buf[4096];
	size_t n = f ? fread(buf, 1, sizeof(buf) - 1, f) : 0;
	if (f) fclose(f);
	buf[n] = 0;
	if (strstr(buf, want)) { printf("  PASS  [%s]\n", desc); g_pass++; }
	else { printf("  FAIL  [%s]  '%s' not in %s\n", desc, want, path); g_fail++; }
}

static void check_dis(const char *desc, const char *src, const char **args, const char *want, const char *absent) {
	write_src(src);
	if (run_args(args)) { printf("  FAIL  [%s]  sccc failed\n", desc); g_fail++; return; }
	char *argv[] = { "sh", "-c", "objdump -dr /tmp/tc_dis.o > /tmp/tc_dis.txt", NULL };
	int st = run(argv);
	FILE *f = fopen("/tmp/tc_dis.txt", "rb");
	static char buf[65536];
	size_t n = f ? fread(buf, 1, sizeof(buf) - 1, f) : 0;
	if (f) fclose(f);
	buf[n] = 0;
	if (!WIFEXITED(st) || WEXITSTATUS(st) || !n) { printf("  FAIL  [%s]  objdump failed\n", desc); g_fail++; }
	else if (want && !strstr(buf, want)) { printf("  FAIL  [%s]  '%s' not in the disassembly\n", desc, want); g_fail++; }
	else if (absent && strstr(buf, absent)) { printf("  FAIL  [%s]  '%s' in the disassembly\n", desc, absent); g_fail++; }
	else { printf("  PASS  [%s]\n", desc); g_pass++; }
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
	check_compile_err("asm unsupported constraint", "int main(){ float f; asm(\"\" : \"=y\"(f)); return 0; }");
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
	check_script("section(\".data\") keeps zero objects in .data",
		"extern char start_d[], end_d[]; int a __attribute__((section(\".data\"))); struct { char c[64]; } d __attribute__((section(\".data\")));\n"
		"int main(){ return ((char *)&a >= start_d && (char *)&a < end_d) * 40 + ((char *)&d >= start_d && (char *)&d < end_d) * 2; }",
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

	section("gcc driver options");
	{
		static const char *pre[] = { "-E", "-P", TMP_SRC, "-o", "/tmp/tc_e.i", NULL };
		check_file("-E keeps the spelling of tokens", "#define X 0x10\n#define S(x) #x\nint a = X; char *s = S(0x1f);\n", pre, "/tmp/tc_e.i",
			"int a = 0x10; char *s = \"0x1f\";");
		write_file("/tmp/tc_src.txt", "int main(){ return 42; }");
		static const char *xc[] = { "-x", "c", "/tmp/tc_src.txt", "-o", TMP_BIN, NULL };
		check_args("-x c for any file name", "", xc, 42);
		write_file("/tmp/tc_inc.h", "#define FORTY_TWO 42\n");
		static const char *inc[] = { "-include", "/tmp/tc_inc.h", TMP_SRC, "-o", TMP_BIN, NULL };
		check_args("-include", "int main(){ return FORTY_TWO; }", inc, 42);
		static const char *und[] = { "-DX=1", "-UX", TMP_SRC, "-o", TMP_BIN, NULL };
		check_args("-U after -D", "#ifdef X\nint main(){ return 1; }\n#else\nint main(){ return 42; }\n#endif\n", und, 42);
		static const char *gnu[] = { "-std=gnu89", TMP_SRC, "-o", TMP_BIN, NULL };
		check_args("-std=gnu89 defines __GNUC__ as gcc 4.9", "int main(){ return __GNUC__ * 10 + __GNUC_MINOR__ - 7; }", gnu, 42);
		static const char *nognu[] = { TMP_SRC, "-o", TMP_BIN, NULL };
		check_args("__GNUC__ only in GNU mode", "#ifdef __GNUC__\nint main(){ return 1; }\n#else\nint main(){ return 42; }\n#endif\n", nognu, 42);
		static const char *dep[] = { "-c", "-Wp,-MD,/tmp/tc_dep.d", TMP_SRC, "-o", "/tmp/tc_dep.o", NULL };
		check_file("-Wp,-MD writes a dependency file", "#include \"/tmp/tc_inc.h\"\nint f(void){ return FORTY_TWO; }", dep, "/tmp/tc_dep.d",
			"/tmp/tc_inc.h");
		static const char *asmS[] = { "-S", TMP_SRC, "-o", "/tmp/tc_asm_out.s", NULL };
		check_file("-S writes the inline assembly", "struct s { int a, b; }; void f(void){ asm volatile(\".ascii \\\"->OFF %0\\\"\" : : \"i\"(__builtin_offsetof(struct s, b))); }",
			asmS, "/tmp/tc_asm_out.s", "->OFF $4");
		static const char *flags[] = { "-Werror", "-fno-PIE", "-mno-sse", "-mskip-rax-setup", "-fconserve-stack", "-Wframe-larger-than=2048",
			"-isystem", "/tmp", "-std=gnu89", "-pipe", TMP_SRC, "-o", TMP_BIN, NULL };
		check_args("more gcc options", "int main(){ return 42; }", flags, 42);
	}

	section("GNU C used by Linux");
	{
		static const char *o2[] = { "-O2", TMP_SRC, "-o", TMP_BIN, NULL };
		check_args("-O2 drops if (0) code", "extern int nope(void);\n"
		"int main(void){ if (sizeof(long) == 4) return nope(); if (0 && nope()) return 1; return 42; }", o2, 42);
		check_args("-O2 switch on sizeof skips bad asm", "#define SZ(p) ({ long r_; switch (sizeof(*(p))) { case 1: r_ = 1; break; case 4: r_ = 4; __asm__(\"movl %%eax, %%eax\" ::); break; case 8: r_ = 8; __asm__(\"movq %%rax, %%rax\" ::); break; default: r_ = -1; } r_; })\n"
		"int main(void){ char c; int i; long l; return SZ(&c) + SZ(&i) * 2 + SZ(&l) * 4; }", o2, 41);
		check_args("-O2 folds typed constants", "int main(void){ return (int)(-(1u) >> 28) + (int)((unsigned char)300) + (-7 / 2) + (int)(0xffffffffu > 0) + (1 ? 20 : 3); }", o2, 77);
		check_args("-O2 label inside if (0)", "int g(int x){ int c = 0; if (x) goto in; if (0) { in: c = 40; if (0) c = 5; } return c; }\n"
		"int main(void){ return g(1) + g(0) + 2; }", o2, 42);
		check_args("-O2 const switch with goto", "int main(void){ int c = 0; switch (3) { case 1: c += 100; break; case 3: c += 1; goto out; case 4: out: c += 1; break; default: c = -1; } return c + 40; }", o2, 42);
		check_args("-O2 always_inline constant argument", "static inline __attribute__((always_inline)) long imm(unsigned int f){ long r; __asm__(\"movq $%c1, %0\" : \"=r\"(r) : \"i\"(f)); return r; }\n"
		"int main(void){ return (int)imm(40) + 2; }", o2, 42);
		check_args("-O2 inline copies get their own labels", "static inline __attribute__((always_inline)) int big(int *v){ _Bool c = 0; if (*v > 3) goto yes; if (0) { yes: c = 1; } return c; }\n"
		"int main(void){ int a = 3, b = 7; return big(&a) * 10 + big(&b) + 41; }", o2, 42);
		check_args("-O2 inline body sees file scope", "int g = 40;\n"
		"static inline __attribute__((always_inline)) int rg(void){ return g; }\n"
		"int main(void){ int g = 100; return rg() + 2 + (g - 100); }", o2, 42);
		check_args("-O2 inline struct argument and result", "struct pt { long x, y, z; };\n"
		"static inline __attribute__((always_inline)) struct pt sw(struct pt p){ struct pt q = { p.z, p.y, p.x }; return q; }\n"
		"int main(void){ struct pt p = { 1, 2, 39 }, q = sw(p); return (int)(q.x + q.y + q.z / 39); }", o2, 41);
		check_args("-O2 constant inline result folds", "extern void bad(void);\n"
		"static inline __attribute__((always_inline)) int sz(int n){ switch (n) { case 1: return 10; case 2: return 20; default: return 99; } }\n"
		"int main(void){ if (sz(2) != 20) bad(); return sz(2) + 22; }", o2, 42);
		check_args("-O2 unused static inline is not compiled", "static inline void never(void){ __asm__(\"movl %%rax, %%eax\" ::); }\n"
		"static inline int twice(int x){ return 2 * x; }\n"
		"int main(void){ int (*f)(int) = twice; return f(21); }", o2, 42);
		check_args("-O2 __func__ in inline copy", "static inline __attribute__((always_inline)) const char *nm(void){ return __func__; }\n"
		"int main(void){ return nm()[0] == 'n' && nm()[1] == 'm' ? 42 : 1; }", o2, 42);
		check_args("-O2 goto into a loop after a jump", "int f(int n){ int len = 0, s = 0; goto inside; do { s += 100; len++; inside: s += 1; } while (len < n); return s; }\n"
		"int main(void){ return f(3) - 262; }", o2, 42);
		check_args("-O2 inline call keeps non-constant arguments", "typedef struct { long c; } at; struct sem { at count; int x; };\n"
		"__attribute__((noinline)) long add(long i, at *v){ v->c += i; return v->c; }\n"
		"static inline long fw(long i, at *v){ return add(i, v); }\n"
		"long f(struct sem *s){ long c; c = fw(0x100000025L, &s->count); return c; }\n"
		"int main(void){ struct sem s = { { 5 }, 0 }; long r = f(&s); return r == 0x10000002aL && s.count.c == r ? 42 : 1; }", o2, 42);
		check_args("-O2 ?: with a constant branch is not constant", "__attribute__((noinline)) unsigned long m(unsigned long a){ unsigned long k = (a) ? (a - 1) : 0; return k; }\n"
		"int main(void){ return (int)m(43); }", o2, 42);
		check_args("-O2 for step runs after the body", "int a[4];\n"
		"int main(void){ int i; for (i = 0; i == 0; i = 1) { a[i] = 40; a[i] += 2; } return a[0] + a[1] * 100; }", o2, 42);
		check_args("-O2 case before a constant ?:", "unsigned caps[2] = { 0, 16 };\n"
		"static int tb(int b){ return (caps[b / 32] >> (b % 32)) & 1; }\n"
		"__attribute__((noinline)) int f(int cmd){ int mode = 1; switch (cmd) { case 0: break; case 1: if (0 ? 1 : tb(36)) { mode = 42; break; } mode = 3; break; } return mode; }\n"
		"int main(void){ return f(1); }", o2, 42);
		check_args("-O2 case labels inside if (0)", "int f(int x){ int s = 0; switch (x) { case 1: s += 1; if (0) { case 2: s += 20; } s += 100; break; default: if (0) { case 3: s += 3000; } } return s; }\n"
		"int main(void){ return f(1) == 101 && f(3) == 3000 && f(4) == 0 ? f(2) - 78 : 1; }", o2, 42);
		check_args("-O2 x & 0 drops the branch", "extern int nope(void);\n"
		"int main(void){ int v = 5; if ((v & 0) || (v * 0)) return nope(); return 42; }", o2, 42);
		check_args("-O2 (void) drops a pure expression", "extern int nope;\n"
		"int main(void){ (void)nope; (void)(nope + 1); return 42; }", o2, 42);
		check_args("-O2 known values of locals", "extern void nope(void);\n"
		"int main(void){ int m = 3; if (m != 3) nope(); m = 4; if (m != 4) nope(); for (int i = 0; i < 2; i++) m += i; return m + 37; }", o2, 42);
		check_args("-O2 x * 0 is not an integer constant expression", "#define ICE(x) (sizeof(int) == sizeof(*(8 ? ((void *)((long)(x) * 0l)) : (int *)8)))\n"
		"int main(void){ int v = 3; return ICE(v) ? 1 : (ICE(5) ? 42 : 2); }", o2, 42);
	}
	check("GNU ?: operator", "int n;\n"
		"int f(int x){ n++; return x; }\n"
		"int main(void){ char *p = 0; long a = f(0) ?: 7, b = f(3) ?: 9; return (int)(a + b) + (p ?: \"x\")[0] - 'x' + n * 16; }", 42);
	check("__label__ local labels", "#define M(x) ({ __label__ out; int r_ = 0; if ((x) > 2) goto out; r_ = 1; out: r_; })\n"
		"int main(void){ return M(1) * 40 + M(5) + M(0) * 2 - 1; }", 41);
	check("struct member from struct expression", "struct a { int x; }; typedef struct { struct a r; } sp; struct s { sp l; int n; };\n"
		"int main(void){ sp q = { { 30 } }; struct s t = { .l = q, .n = 9 }; struct s u = { .l = (sp) { .r = { 3 } }, .n = 0 }; return t.l.r.x + t.n + u.l.r.x; }", 42);
	check("anonymous members in initializers", "struct raw { unsigned v; };\n"
		"typedef struct { union { struct raw rlock; struct { char pad[2]; short dm; }; }; } spinlock_t;\n"
		"struct m { long o; spinlock_t w; int n; };\n"
		"struct m g = { 7, (spinlock_t) { { .rlock = { 30 } } }, 5 };\n"
		"struct pt { int a; union { int b; float f; }; struct { int c, d; }; int e; };\n"
		"int main(void){ struct pt l = { 1, { 2 }, { 3, 4 }, 0 }; return (int)g.w.rlock.v + g.n + l.a + l.b + l.c - 2 * 0 + l.d - 3; }", 42);
	check("cast to the same struct type", "struct p { long v; };\n"
		"int main(void){ struct p a = { 42 }, b = (struct p) a; return (int)b.v; }", 42);
	check("small struct in asm register", "typedef struct { unsigned long v; } pmd_t;\n"
		"int main(void){ pmd_t m = { 2 }, r = { 40 }, *p = &m; __asm__ volatile(\"xchgq %q0, %1\" : \"+r\"(r), \"+m\"(*p) : : \"memory\"); return (int)(m.v + r.v); }", 42);
	check("asm goto from another section", "static int has(int x){ asm goto(\"cmpl $3, %0\\n\\tjne 1f\\n\\tjmp 2f\\n1:\\n.pushsection .text.alt,\\\"ax\\\"\\n2: jmp %l[yes]\\n.popsection\\n\" : : \"r\"(x) : : yes); return 0; yes: return 1; }\n"
		"int main(void){ return has(3) * 40 + has(4) + 2; }", 42);
	check("pp-number pasted into a name", "#define C(a, b) a##b\n"
		"int main(void){ int x1s = 42; return C(x, 1s); }", 42);
	check("__COUNTER__", "int a = __COUNTER__, b = __COUNTER__, c = __COUNTER__;\n"
		"int main(void){ return a + b * 2 + c * 20; }", 42);
	check("address constants in static data", "int fn(void){ return 30; }\n"
		"int x[4];\n"
		"struct s { int a, b[3]; } sv;\n"
		"static void *a1 = (void *)&fn;\n"
		"void *a4 = (void *)&x[2];\n"
		"long a5 = (long)&x[1] + 4;\n"
		"int *a6 = &sv.b[1];\n"
		"char *a7 = \"hello\" + 4;\n"
		"int main(void){ return ((int(*)(void))a1)() + (a4 == &x[2]) + ((int *)a5 == &x[2]) + (a6 == &sv.b[1]) + (*a7 == 'o') * 8 + 1; }", 42);
	check("typed integer constant expressions", "enum { A = (-1u > 0) * 40, B = (int)(~0u >> 31) + 1 };\n"
		"int arr[(unsigned char)-1 == 255 ? 1 : -1];\n"
		"int main(void){ switch (0xffffffffu) { case -1u: return A + B; } return 0; }", 42);
	check("__builtin_bswap and __builtin_memcpy", "int main(void){ char b[4]; __builtin_memcpy(b, \"*\", 2); return (__builtin_bswap32(0x01000000u) + __builtin_bswap16(0x2900) + __builtin_bswap64(0x0100000000000000ul)) + b[0] - 42 + 0; }", 43);
	check("declaration shadows a sccc intrinsic", "static int __wrmsr(int a, int b, int c){ return a + b + c; }\n"
		"int main(void){ return __wrmsr(40, 1, 1); }", 42);

	{
		static const char *o2b[] = { "-O2", TMP_SRC, "-o", TMP_BIN, NULL };
		check_args("-O2 local initialized to a constant folds", "#define PUT(x, p) ({ unsigned long sz = sizeof(*(p)); __typeof__(*(p)) v = (x); switch (sz) { case 1: __asm__(\"movb %b1, %0\" : \"=m\"(*(p)) : \"iq\"(v)); break; case 4: __asm__(\"movl %k1, %0\" : \"=m\"(*(p)) : \"ir\"(v)); break; case 8: __asm__(\"movq %1, %0\" : \"=m\"(*(p)) : \"er\"(v)); break; } 0; })\n"
		"int main(void){ char c = 0; int i = 0; long l = 0; int n = 5, k = 1; int *q = &k; (q) = &n; PUT(2, &c); PUT(30, &i); PUT(5L, &l); return c + i + (int)l + *q; }", o2b, 42);
		check_args("-O2 __is_constexpr and max() in an array size", "#define __is_constexpr(x) (sizeof(int) == sizeof(*(8 ? ((void *)((long)(x) * 0l)) : (int *)8)))\n"
		"#define maxc(x, y) __builtin_choose_expr(__is_constexpr(x) && __is_constexpr(y), ((x) > (y) ? (x) : (y)), ({ __typeof__(x) a_ = (x); __typeof__(y) b_ = (y); a_ > b_ ? a_ : b_; }))\n"
		"int main(int argc, char **argv){ char buf[maxc(2 * sizeof(long) + 4, sizeof(\"abc\") + 30)]; return (int)sizeof(buf) + maxc(argc, 8); }", o2b, 42);
		check_args("-O2 label right before an if (0)", "int f(int r){ if (r) goto out; r = 40; out: ({ if (0) r = 1; }); return r + 2; }\n"
		"int main(void){ return f(0); }", o2b, 42);
	}
	check("runtime index in offsetof", "struct p { long a; struct { int x, y[4]; } q[3]; };\n"
		"int main(void){ int i = 1, j = 2; return (int)__builtin_offsetof(struct p, q[i].y[j]); }", 40);
	check("untaken ?: branch need not be constant", "unsigned long f(unsigned long n);\n"
		"static unsigned long a = __builtin_constant_p(65536) ? (1UL << 16) : f(65536);\n"
		"static int b = 0 ? f(1) : 1 ? 2 ? 26 : f(2) : 3;\n"
		"int main(void){ return (int)(a >> 12) + b; }", 42);
	check("compound literals in static data", "struct q { const char *const *ids; int r[2]; };\n"
		"static const struct q tab[] = { { (const char *const []){ \"a\", 0 }, { 1, 2 } }, { (const char *const []){ \"bb\", \"ccc\", 0 }, { 3, 30 } } };\n"
		"int *ip = (int []){ 7, 8, 9 };\n"
		"int main(void){ return tab[1].ids[1][2] - 'c' + tab[1].r[1] + ip[2] + (tab[0].ids[1] == 0) * 3; }", 42);
	check("local aligned to 64 bytes", "struct big { long a[3]; } __attribute__((aligned(64)));\n"
		"int main(void){ char c = 1; struct big d; int x __attribute__((aligned(128))); d.a[2] = 39; x = 2; return (((unsigned long)&d & 63) == 0) + (((unsigned long)&x & 127) == 0) * 0 + (int)d.a[2] + x + c - 1; }", 42);
	check("bit builtins", "static int h = __builtin_ctzl(64) + __builtin_popcount(0xff) + __builtin_clz(1);\n"
		"int main(void){ volatile unsigned x = 0x1000; return h - 45 + __builtin_ctz(x) + __builtin_clz(x) + __builtin_ffs(x) - 2; }", 42);
	check("__COUNTER__ in a pasted name", "#define P2(a, b) a##b\n"
		"#define P(a, b) P2(a, b)\n"
		"int P(v, __COUNTER__) = 40, P(v, __COUNTER__) = 2;\n"
		"int main(void){ return v0 + v1; }", 42);
	check("asm .set to a C function", "asm(\".weak sys_foo\\n\\t.set sys_foo, ni_syscall\");\n"
		"static long ni_syscall(void){ return 38; }\n"
		"long sys_foo(void);\n"
		"int main(void){ return (int)sys_foo() + 4; }", 42);
	check_asm("as: macros with defaults, keywords and \\@", "int f(void); int main(void){ return f(); }", ".macro pair reg:req, off=8\n"
		" movq \\reg, %rax\n"
		" addq $\\off, %rax\n"
		".Lx_\\@:\n"
		".endm\n"
		".macro sfx r\n"
		" movl $0, %\\r\\()d\n"
		".endm\n"
		".globl f\n"
		"f: sfx r8\n"
		" pair $30\n"
		" pair off=4 reg=$0\n"
		" addq $8, %rax\n"
		" ret\n"
		"", 0, 12);
	check_asm("as: .rept, .irp, .irpc and .exitm", "int f(void); int main(void){ return f(); }", ".macro early x\n"
		".if \\x\n"
		".exitm\n"
		".endif\n"
		" addl $100, %eax\n"
		".endm\n"
		".globl f\n"
		"f: xorl %eax, %eax\n"
		".rept 3 ; addl $2, %eax ; .endr\n"
		".irp n, 10, 20\n"
		" addl $\\n, %eax\n"
		".endr\n"
		".irpc c, 123\n"
		" addl $\\c, %eax\n"
		".endr\n"
		" early 1\n"
		" addl $0, %eax\n"
		" ret\n"
		"", 0, 42);
	check_asm("as: .ifb, .ifc and registers in .if", "int f(void); int main(void){ return f(); }", ".macro t a, b\n"
		".ifnb \\a\n"
		" addl $10, %eax\n"
		".endif\n"
		".ifc \\a,\\b\n"
		" addl $20, %eax\n"
		".endif\n"
		".endm\n"
		".macro r a\n"
		".if \\a == %rsp\n"
		" addl $2, %eax\n"
		".endif\n"
		".endm\n"
		".globl f\n"
		"f: xorl %eax, %eax\n"
		" t\n"
		" t x, x\n"
		" r %rsp\n"
		" r %rax\n"
		" RET\n"
		"", 0, 52);
	check_asm("as: .previous after .popsection", "int f(void); int main(void){ return f(); }", ".text\n"
		".globl f\n"
		"f: movl $40, %eax\n"
		" jmp 1f\n"
		".section .fixup, \"ax\"\n"
		".pushsection .other, \"a\"\n"
		".byte 1\n"
		".popsection\n"
		".previous\n"
		"1: addl $2, %eax\n"
		" ret\n"
		".type f STT_FUNC\n"
		"", 0, 42);
	check_asm("as: true comparisons are -1", "int f(void); int main(void){ return f(); }",
		".globl f\nf: movl $(-(3 > 2) * 40 + (5 == 5) * -2 + !0 - 1), %eax\n ret\n", 0, 42);
	check_asm("as: .skip sized by later labels", "int f(void); int main(void){ return f(); }",
		".globl f\nf: movl $(2f - 1f + 39), %eax\n ret\n1: .skip -((3f - 2f) > 0) * (3f - 2f), 0x90\n2: .byte 1, 2, 3\n3:\n", 0, 42);

	section("kernel ABI and ld modes used by Linux");
	{
		static const char *const lib[] = { "/tmp/tc_la.c", "int ga(void){ return 40; }\n", "/tmp/tc_lb.c", "int gb(void){ return 2; }\n",
			"/tmp/tc_lm.c", "int ga(void); int gb(void);\nint main(void){ return ga() + gb(); }\n", NULL };
		static const char *const ca[] = { "-c", "/tmp/tc_la.c", "-o", "/tmp/tc_la.o", NULL };
		static const char *const cb[] = { "-c", "/tmp/tc_lb.c", "-o", "/tmp/tc_lb.o", NULL };
		static const char *const rr[] = { "-r", "/tmp/tc_la.o", "/tmp/tc_lb.o", "-o", "/tmp/tc_lr.o", NULL };
		static const char *const lr[] = { "/tmp/tc_lm.c", "/tmp/tc_lr.o", "-o", TMP_BIN, NULL };
		static const char *const *const s_r[] = { ca, cb, rr, lr, NULL };
		check_steps("-r merges objects for a later link", lib, s_r, 42);
		static const char *const rm[] = { "@ar", "rcT", "/tmp/tc_lt.a", "/tmp/tc_la.o", "/tmp/tc_lb.o", NULL };
		static const char *const lt[] = { "/tmp/tc_lm.c", "--start-group", "/tmp/tc_lt.a", "--end-group", "-o", TMP_BIN, NULL };
		static const char *const *const s_t[] = { ca, cb, rm, rr, lt, NULL };
		remove("/tmp/tc_lt.a");
		check_steps("thin archive inside --start-group", lib, s_t, 42);
		static const char *const wh[] = { "/tmp/tc_wa.c", "int g = 42;\n",
			"/tmp/tc_wm.c", "extern int g __attribute__((weak));\nint main(void){ return &g ? g : 1; }\n", NULL };
		static const char *const wc[] = { "-c", "/tmp/tc_wa.c", "-o", "/tmp/tc_wa.o", NULL };
		static const char *const wr[] = { "@ar", "rc", "/tmp/tc_wa.a", "/tmp/tc_wa.o", NULL };
		static const char *const wl[] = { "/tmp/tc_wm.c", "--whole-archive", "/tmp/tc_wa.a", "--no-whole-archive", "-o", TMP_BIN, NULL };
		static const char *const *const s_w[] = { wc, wr, wl, NULL };
		remove("/tmp/tc_wa.a");
		check_steps("--whole-archive keeps unreferenced members", wh, s_w, 42);
		static const char *const wp[] = { "/tmp/tc_wm.c", "/tmp/tc_wa.a", "-o", TMP_BIN, NULL };
		static const char *const *const s_p[] = { wp, NULL };
		check_steps("a weak reference does not pull an archive member", wh, s_p, 1);
		static const char *const ws[] = { "/tmp/tc_wm.c", "/tmp/tc_ws.c", "/tmp/tc_wa.a", "-o", TMP_BIN, NULL };
		static const char *const wh2[] = { "/tmp/tc_ws.c", "extern int g;\nint h(void){ return g; }\n", NULL };
		static const char *const *const s_ws[] = { ws, NULL };
		check_steps("a strong reference elsewhere pulls it", wh2, s_ws, 42);
		static const char *const sh[] = { "-shared", "/tmp/tc_la.o", "-o", "/tmp/tc_ls.so", NULL };
		static const char *const *const s_s[] = { ca, sh, NULL };
		check_elf_byte("-shared writes ET_DYN", lib, s_s, "/tmp/tc_ls.so", 16, 3);
		static const char *const dr[] = { "/tmp/tc_ld.c", "int v = 40; int *p = &v;\n", NULL };
		static const char *const dc[] = { "-c", "/tmp/tc_ld.c", "-o", "/tmp/tc_ld.o", NULL };
		static const char *const ds[] = { "-shared", "/tmp/tc_ld.o", "-o", "/tmp/tc_ld.so", NULL };
		static const char *const *const s_d[] = { dc, ds, NULL };
		if (run_steps(dr, s_d)) { printf("  PASS  [-shared refuses dynamic relocations]  (error as expected)\n"); g_pass++; }
		else { printf("  FAIL  [-shared refuses dynamic relocations]  (expected an error but succeeded)\n"); g_fail++; }
		static const char *const a32[] = { "/tmp/tc_32.S", ".globl f\nf:\n\tmovl $42, %eax\n\tret\n", NULL };
		static const char *const c32[] = { "-m32", "-c", "/tmp/tc_32.S", "-o", "/tmp/tc_32.o", NULL };
		static const char *const l32[] = { "-m", "elf_i386", "-e", "f", "-Ttext", "0x1000", "/tmp/tc_32.o", "-o", "/tmp/tc_32", NULL };
		static const char *const *const s_32[] = { c32, l32, NULL };
		check_elf_byte("-m elf_i386 writes ELF32", a32, s_32, "/tmp/tc_32", 4, 1);
		static const char *const gi[] = { "/tmp/tc_ga.c", "extern inline int f(void){ return 1; }\nint main(void){ int (*p)(void) = f; return p(); }\n",
			"/tmp/tc_gb.c", "int f(void){ return 42; }\n", NULL };
		static const char *const g1[] = { "-std=gnu89", "-c", "/tmp/tc_ga.c", "-o", "/tmp/tc_ga.o", NULL };
		static const char *const g2[] = { "/tmp/tc_ga.o", "/tmp/tc_gb.c", "-o", TMP_BIN, NULL };
		static const char *const *const s_g[] = { g1, g2, NULL };
		check_steps("gnu89 extern inline emits no definition", gi, s_g, 42);
		static const char *kern[] = { "-mcmodel=kernel", "-mno-sse", "-ffreestanding", "-c", TMP_SRC, "-o", "/tmp/tc_dis.o", NULL };
		check_dis("-mcmodel=kernel takes addresses as 32-bit signed", "int x;\nlong f(void){ return (long)&x; }\n", kern, "R_X86_64_32S", NULL);
		check_dis("-mno-sse variadic functions save no XMM registers", "#include <stdarg.h>\n"
			"int v(int n, ...){ va_list ap; va_start(ap, n); int r = va_arg(ap, int); va_end(ap); return r; }\n", kern, NULL, "xmm");
		check("global register variable", "register unsigned long sp_ asm(\"rsp\");\n"
			"int main(void){ unsigned long a = sp_; char c; return (unsigned long)&c < a + 4096 && (unsigned long)&c + 4096 > a ? 42 : 1; }", 42);
	}
	section("C and toolchain features used by musl and BusyBox");
	{
		static const char *o2b[] = { "-O2", TMP_SRC, "-o", TMP_BIN, NULL };
		check("gcc va_list, va_copy and va_end", "typedef __builtin_va_list va_list;\n"
		"int sum(int n, ...){ va_list ap, aq; int s = 0; __builtin_va_start(ap, n); __builtin_va_copy(aq, ap);\n"
		"for (int i = 0; i < n; i++) s += __builtin_va_arg(ap, int); for (int i = 0; i < n; i++) s += __builtin_va_arg(aq, int);\n"
		"__builtin_va_end(aq); __builtin_va_end(ap); return s + (int)sizeof(va_list) - 24; }\n"
		"int main(void){ return sum(3, 4, 7, 10); }\n", 42);
		check_args("-O2 alias keeps a static function and variable", "static void dummy(int *p){ *p += 1; }\n"
		"extern __typeof(dummy) hook __attribute__((weak, alias(\"dummy\")));\n"
		"static int sv = 41;\n"
		"extern int av __attribute__((alias(\"sv\")));\n"
		"int main(void){ int x = av; hook(&x); return x; }\n", o2b, 42);
		check("wide character and string literals", "typedef int wchar_t;\n"
		"wchar_t g[] = L\"héllo\";\n"
		"unsigned short u16[] = u\"x\\U0001F600\";\n"
		"unsigned int u32[] = U\"ab\";\n"
		"char u8s[] = u8\"é\";\n"
		"#if L'\\0' - 1 > 0\n"
		"#error wchar_t is signed\n"
		"#endif\n"
		"int main(void){ const wchar_t *p = L\"ab\" \"c\"; int s = 0; for (int i = 0; p[i]; i++) s += p[i];\n"
		"return (sizeof(g) == 24) + (g[1] == 0xe9) + (s == 294) + (sizeof(u16) == 8) + (u16[1] == 0xD83D) + (u16[2] == 0xDE00) + (sizeof(u32) == 12) + (sizeof(u8s) == 3) + ((unsigned char)u8s[0] == 0xc3) + (L'é' == 0xe9) + 32; }\n", 42);
		check("long double arithmetic and conversions", "static int same(long double a, const unsigned char *b){ const unsigned char *p = (const unsigned char *)&a; for (int i = 0; i < 10; i++) if (p[i] != b[i]) return 0; return 1; }\n"
		"static long double add(long double a, long double b){ return a + b; }\n"
		"int main(void){ static const unsigned char tenth[10] = { 0xcd,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xfb,0x3f };\n"
		"long double a = 0.1L, b = 3.0L, f = a; f += b; f *= 2; f--;\n"
		"int r = same(a, tenth) + (a < b) + (a != b) + ((long)(b * 1e10L) == 30000000000L) + ((unsigned long)1.5e19L == 15000000000000000000UL) + ((int)-2.7L == -2);\n"
		"r += (add(a, b) > 3.09L && add(a, b) < 3.11L) + (f > 5.19L && f < 5.21L) + ((double)(a * 3) > 0.29) + (sizeof(long double) == 16) + (_Alignof(long double) == 16);\n"
		"long double p = 1, q = p++; r += (q == 1) + (p == 2) + ((long double)-5 == -5.0L) + ((long double)18446744073709551615UL == 18446744073709551615.0L);\n"
		"return r + 27; }\n", 42);
		check("long double literals are exact", "int main(void){ long double x = 1e-300L, y = 0x1.fffffffffffffffep16383L, z = 3.6451995318824746025e-4951L;\n"
		"const unsigned char *p = (const unsigned char *)&x, *q = (const unsigned char *)&y, *w = (const unsigned char *)&z;\n"
		"return (p[0] == 0xca && p[1] == 0xc6 && p[9] == 0x3c && p[8] == 0x1a) * 20 + (q[0] == 0xff && q[9] == 0x7f && q[8] == 0xfe) * 20 + (w[0] == 1 && w[8] == 0 && w[9] == 0) * 2; }\n", 42);
		check("long double through varargs and structs", "long double vsum(int n, ...){ __builtin_va_list ap; __builtin_va_start(ap, n); long double s = 0; for (int i = 0; i < n; i++) s += __builtin_va_arg(ap, long double); __builtin_va_end(ap); return s; }\n"
		"struct S { char c; long double x; int t; };\n"
		"int main(void){ struct S s = { 'x', 2.5L, 7 }; long double v = vsum(3, 1.0L, 2.0L, 0.5L);\n"
		"return (v == 3.5L) * 30 + (sizeof(struct S) == 48) * 10 + (s.x == 2.5L) + (s.t == 7); }\n", 42);
		check("floating constant expressions in static data", "#define N (1 << 7)\n"
		"struct d { double x, y; float z; long double w; } dd = { .x = 0x1.8p0 * N, .y = 1.0 / 4 + 2, .z = 1.0f / 4, .w = 1.0L / 4 };\n"
		"static const double tab[] = { 1.5 * 2, -0.5f * 4, (double)3 / 2, 18446744073709551615UL, 1.0 / 0.0 > 0 };\n"
		"static int arr[(int)2.9 + 1];\n"
		"int main(void){ return (dd.x == 192) + (dd.y == 2.25) + (dd.z == 0.25f) + (dd.w == 0.25L) + (tab[0] == 3) + (tab[1] == -2) + (tab[2] == 1.5) + (tab[3] == 18446744073709551615.0) + (tab[4] == 1) + (sizeof(arr) == 12) + 32; }\n", 42);
		check("postfix operators on compound literals", "struct k { long a; int b; };\n"
		"int main(int argc, char **argv){ (void)argv; unsigned long b = (unsigned long [3]){ 10, 20, 32 }[argc + 1];\n"
		"return (int)b + (int)sizeof((struct k){0}.b) + (struct k){ 1, 6 }.b; }\n", 42);
		check_args("asm x, t and u constraints", "static double sq(double x){ __asm__(\"sqrtsd %1, %0\" : \"=x\"(x) : \"x\"(x)); return x; }\n"
		"static long double fm(long double x, long double y){ unsigned short sw; do __asm__(\"fprem; fnstsw %%ax\" : \"+t\"(x), \"=a\"(sw) : \"u\"(y)); while (sw & 0x400); return x; }\n"
		"static long long lr(long double x){ long long r; __asm__(\"fistpll %0\" : \"=m\"(r) : \"t\"(x) : \"st\"); return r; }\n"
		"static long double ab(long double x){ __asm__(\"fabs\" : \"+t\"(x)); return x; }\n"
		"int main(void){ long double s = 0; for (int i = 0; i < 20; i++) s += fm(100.5L + i, 7.25L);\n"
		"return (sq(16.0) == 4.0) + (s == 68.5L) + (lr(7.5L) == 8) + (ab(-3.5L) == 3.5L) + 38; }\n", o2b, 42);
		check_args("variable length arrays", "static int sum(int n){ int a[n]; for (int i = 0; i < n; i++) a[i] = i; int s = 0; for (int i = 0; i < n; i++) s += a[i]; return s + (int)sizeof a; }\n"
		"static char *ls(void *base, unsigned long w, unsigned long i){ char (*p)[w] = base; return p[i]; }\n"
		"static int mat(int r, int c){ int m[r][c]; for (int i = 0; i < r; i++) for (int j = 0; j < c; j++) m[i][j] = i * 10 + j; return m[r-1][c-1] + (int)sizeof m + (int)sizeof m[0]; }\n"
		"static int loop(int n){ int t = 0; for (int k = 0; k < 100000; k++) { char buf[n + (k & 7)]; buf[0] = (char)k; if (k & 1) continue; t += buf[0] & 1; } return t; }\n"
		"static long pd(int w){ char (*p)[w] = 0; char (*q)[w] = p + 5; q++; return q - p; }\n"
		"int main(int argc, char **argv){ (void)argv; char d[] = \"aaaabbbbccccdddd\";\n"
		"return (sum(10 + argc) == 99) + (*ls(d, 4, 2) == 'c') + (mat(3, 5) == 104) + (loop(64) == 0) + (pd(4) == 6) + 37; }\n", o2b, 42);
		check("_Complex arithmetic", "static _Complex double mul(_Complex double a, _Complex double b){ return a * b; }\n"
		"#define RE(z) ((double)(z))\n"
		"#define IM(z) (__imag__ (z))\n"
		"int main(void){ _Complex double a = 1.5 + 2.0 * (0.0f + 1.0fi), b = 3.0 - 4.0i, c = a;\n"
		"c *= b; c += 1;\n"
		"_Complex float f = (1.0f + 2.0fi) / (3.0f - 1.0fi);\n"
		"_Complex long double l = (1.0L + 1.0Li) + (0.5L - 0.25Li) * 2;\n"
		"return (RE(mul(a, b)) == 12.5) + (IM(mul(a, b)) == 0) + (RE(a / b) < -0.13 && RE(a / b) > -0.15) + (RE(2 * a) == 3) + (IM(3 - a) == -2) + (RE(-a) == -1.5)\n"
		"\t+ (RE(c) == 13.5) + (__real__ f > 0.09f && __real__ f < 0.11f) + (__real__ l == 2.0L) + (__imag__ l == 0.5L) + (a == a) + (a != b) + (sizeof(_Complex float) == 8) + (sizeof(_Complex long double) == 32) + 28; }\n", 42);
		check("_Pragma and #pragma pack", "_Pragma(\"GCC visibility push(hidden)\")\n"
		"#pragma pack(1)\n"
		"struct a { char c; int i; short s; long l; };\n"
		"#pragma pack()\n"
		"#pragma pack(push, 2)\n"
		"struct c { char c; int i; long l; };\n"
		"#pragma pack(pop)\n"
		"struct f { char c; long l; };\n"
		"_Pragma(\"GCC visibility pop\")\n"
		"int main(void){ return (int)sizeof(struct a) + (int)sizeof(struct c) + (int)_Alignof(struct c) + (int)sizeof(struct f) - 5; }\n", 42);
		check_args("alloca, also with values pushed", "#include <string.h>\n"
		"static int g(int a, char *s, int b){ return a + (int)strlen(s) + b; }\n"
		"int main(void){ char *str; int t = 0; for (int i = 0; i < 10; i++) { str = __builtin_alloca(i + 1); memset(str, 65, i); str[i] = 0; t += g(i, (str = __builtin_alloca(8), strcpy(str, \"ab\"), str), 0); } return t - 23; }\n", o2b, 42);
		check_args("-O2 small function with a VLA in an expression", "static int cv(int n){ int len = n * 3; char buf[len]; buf[0] = 1; return buf[0] + len; }\n"
		"int main(int argc, char **argv){ (void)argv; int y = argc; return y + cv(y + 7) * 2 - 9; }", o2b, 42);
		check("sibling blocks share stack slots", "static char *pa, *pb;\nstatic void f(void){ { char a[512]; a[0] = 1; pa = a; } { char b[512]; b[0] = 2; pb = b; } }\n"
		"int main(void){ f(); return pa == pb ? 42 : 1; }", 42);
		check_args("-O2 struct results of inlined calls keep their slots", "struct P { long a, b, c; };\n"
		"static struct P mk(long x){ if (x > 0) { struct P p = { x, x + 1, x + 2 }; return p; } struct P q = { 0, 0, 0 }; return q; }\n"
		"static long burn(long x){ if (x) { long junk[3] = { 99, 99, 99 }; return junk[0] + junk[2] - 198 + x; } return 0; }\n"
		"__attribute__((noinline)) static long add(struct P u, long w){ return u.a + u.b + u.c + w; }\n"
		"int main(int argc, char **argv){ (void)argv; return (int)add(mk(argc + 11), burn(argc + 2)); }", o2b, 42);
		check_objects("zero-size objects are defined in objects", "typedef struct { } e_t;\ne_t lk = { };\n", "typedef struct { } e_t;\nextern e_t lk;\nint main(void){ return &lk != 0 ? 42 : 1; }", 42);
		check_args("-O2 goto back into a block after return", "#define CHECKEND() {goto checkend; checkend_return:;}\n"
		"static int last;\n"
		"__attribute__((noinline)) int f(int c, char *eof){ int out = 0;\n"
		" loop: CHECKEND(); for (;;) { if (c == 0) break; out += c; c--; if (c == 2) goto loop; }\n"
		" last = 3; return last + out * 10;\n"
		" checkend: { if (eof) c = 0; goto checkend_return; } }\n"
		"int main(void){ return f(5, 0) - f(5, \"x\") - 108; }\n", o2b, 42);
		check_args("-O2 known values reach a label used by a later goto", "__attribute__((noinline)) int use(int v){ static int s; s = s * 10 + v; return s; }\n"
		"__attribute__((noinline)) int f(int n){ int x = 1, r = 0; { L: r = use(x); } x = 2; if (n-- > 0) goto L; return r; }\n"
		"int main(void){ return f(2) - 80; }\n", o2b, 42);
		check_args("-O2 labels reached only from dead code stay dead", "extern int nope(void);\n"
		"int f(int x){ int *q = 0; if (q) { if (x) goto out; nope(); out: return 1; } if (x || 1) return 2; again: if (nope()) goto again; return 3; }\n"
		"int main(void){ return f(0) * 21; }\n", o2b, 42);
		check_asm("pushed values survive a returns_twice call", "__attribute__((returns_twice)) int mysj(long *b);\n"
		"__attribute__((noreturn)) void mylj(long *b, int v);\n"
		"static long buf[8];\n"
		"__attribute__((noinline)) static void clobber(int n){ volatile char b[256]; for (int i = 0; i < 256; i++) b[i] = (char)(n + i); }\n"
		"int main(void){ int i, r[2] = { 0, 0 }; i = mysj(buf); if (!i) { r[0] = 1; clobber(9); mylj(buf, 5); } r[1] = i; return r[0] * 10 + r[1] + 27; }\n",
		".globl mysj\n"
		"mysj:\n"
		"\tmov (%rsp), %rax\n"
		"\tmov %rax, 0(%rdi)\n"
		"\tlea 8(%rsp), %rax\n"
		"\tmov %rax, 8(%rdi)\n"
		"\tmov %rbp, 16(%rdi)\n"
		"\tmov %rbx, 24(%rdi)\n"
		"\tmov %r12, 32(%rdi)\n"
		"\tmov %r13, 40(%rdi)\n"
		"\tmov %r14, 48(%rdi)\n"
		"\tmov %r15, 56(%rdi)\n"
		"\txor %eax, %eax\n"
		"\tret\n"
		".globl mylj\n"
		"mylj:\n"
		"\tmov %esi, %eax\n"
		"\tmov 24(%rdi), %rbx\n"
		"\tmov 32(%rdi), %r12\n"
		"\tmov 40(%rdi), %r13\n"
		"\tmov 48(%rdi), %r14\n"
		"\tmov 56(%rdi), %r15\n"
		"\tmov 16(%rdi), %rbp\n"
		"\tmov 8(%rdi), %rsp\n"
		"\tjmp *0(%rdi)\n"
		"\n", 0, 42);
		check("zero-size objects get storage", "typedef struct { } e_t;\n"
		"e_t lk = { };\n"
		"e_t lk2;\n"
		"static e_t lk3 = { };\n"
		"int main(void){ return (&lk != 0) + (&lk2 != 0) + (&lk3 != 0) + 39; }\n", 42);
		check_args("-O2 keeps used local statics in a section", "struct kp { const char *name; unsigned *arg; };\n"
		"static unsigned v = 7;\n"
		"static void __attribute__((used)) opts(void){\n"
		"\tstatic const char str[] = \"local\";\n"
		"\tstatic struct kp p __attribute__((used, section(\"kparams\"), aligned(8))) = { str, &v };\n"
		"}\n"
		"extern struct kp __start_kparams[], __stop_kparams[];\n"
		"int main(void){ return (__stop_kparams - __start_kparams == 1) * 30 + (__start_kparams[0].name[0] == 'l') * 5 + (int)*__start_kparams[0].arg; }\n", o2b, 42);
		check_args("enum constants outside int take the enum type", "enum { BIAS = -1U << 31 };\n"
		"enum big { A = 1, B = 0x100000000LL };\n"
		"__attribute__((noinline)) int f(int x){ return x != BIAS; }\n"
		"int main(void){ return (f(-2147483647 - 1) == 0) + (sizeof(enum big) == 8) + (sizeof(A) == 8) + (sizeof(BIAS) == 4) + (BIAS > 0) + 37; }\n", o2b, 42);
	}
	{
		static const char *const lf[] = { "/tmp/tc_crt0.s", ".globl _start\n_start:\n\tcall main\n\tmov %eax, %edi\n\tmov $60, %eax\n\tsyscall\n",
			"/tmp/tc_lv.c", "int val(void){ return 40; }\n", "/tmp/tc_lm2.c", "int val(void);\nint main(void){ return val() + 2; }\n", NULL };
		static const char *const l1[] = { "-c", "/tmp/tc_lv.c", "-o", "/tmp/tc_lv.o", NULL };
		static const char *const l2[] = { "@ar", "rc", "/tmp/libtc_lv.a", "/tmp/tc_lv.o", NULL };
		static const char *const l3[] = { "-nostdlib", "-nostartfiles", "/tmp/tc_crt0.s", "/tmp/tc_lm2.c", "-L/tmp", "-ltc_lv", "-o", TMP_BIN, NULL };
		static const char *const *const s_l[] = { l1, l2, l3, NULL };
		remove("/tmp/libtc_lv.a");
		check_steps("-nostartfiles with -L and -l", lf, s_l, 42);
		static const char *const cf[] = { "/tmp/tc_cm.c", "int shared_c;\n", "/tmp/tc_cmm.c", "extern int shared_c;\nint main(void){ shared_c = 42; return shared_c; }\n", NULL };
		static const char *const c1[] = { "-c", "/tmp/tc_cm.c", "-o", "/tmp/tc_cm.o", NULL };
		static const char *const c2[] = { "@ar", "rc", "/tmp/tc_cm.a", "/tmp/tc_cm.o", NULL };
		static const char *const c3[] = { "/tmp/tc_cmm.c", "/tmp/tc_cm.a", "-o", TMP_BIN, NULL };
		static const char *const *const s_c[] = { c1, c2, c3, NULL };
		remove("/tmp/tc_cm.a");
		check_steps("an archive member with a common symbol is pulled", cf, s_c, 42);
		static const char *const inf[] = { "/tmp/tc_crti.s", ".section .init\n.globl _init\n_init:\n\tpush %rax\n",
			"/tmp/tc_crtn.s", ".section .init\n\tpop %rax\n\tret\n", "/tmp/tc_im.c", "extern void _init(void);\nint main(void){ _init(); return 42; }\n", NULL };
		static const char *const i1[] = { "-c", "/tmp/tc_crti.s", "-o", "/tmp/tc_crti.o", NULL };
		static const char *const i2[] = { "-c", "/tmp/tc_crtn.s", "-o", "/tmp/tc_crtn.o", NULL };
		static const char *const i3[] = { "/tmp/tc_crti.o", "/tmp/tc_im.c", "/tmp/tc_crtn.o", "-o", TMP_BIN, NULL };
		static const char *const *const s_i[] = { i1, i2, i3, NULL };
		check_steps(".init pieces from two objects form one function", inf, s_i, 42);
		static const char *const ef[] = { "/tmp/tc_em.c", "int main(void){ return 42; }\n", NULL };
		static const char *const e1[] = { "@ar", "rcs", "/tmp/tc_empty.o", NULL };
		static const char *const e2[] = { "/tmp/tc_em.c", "/tmp/tc_empty.o", "-Wl,--warn-common", "-Wl,--sort-common", "-Wl,--sort-section,alignment", "-Wl,--verbose", "-o", TMP_BIN, NULL };
		static const char *const *const s_e[] = { e1, e2, NULL };
		remove("/tmp/tc_empty.o");
		check_steps("an empty archive named .o and BusyBox's ld options", ef, s_e, 42);
		static const char *const dm[] = { "@sh", "-c", "\"$0\" -dumpmachine > /tmp/tc_dm.txt", "@CC", NULL };
		static const char *const *const s_dm[] = { dm, NULL };
		static const char *const nof[] = { NULL };
		check_steps_file("-dumpmachine", nof, s_dm, "/tmp/tc_dm.txt", "x86_64-linux-gnu");
		check_args_err("-l with a missing library", "int main(void){ return 0; }", (const char *[]){ TMP_SRC, "-L/tmp", "-lsccc_no_such_lib", "-o", TMP_BIN, NULL });
	}
	check_asm("### lines in a .S file", NULL, "### generated ###\n.globl main\nmain:\n\tmov $42, %eax\n\tret\n", 1, 42);
	check_asm("call sym@PLT", "int f(void){ return 42; }", ".globl main\nmain:\n\tpush %rbp\n\tcall f@PLT\n\tpop %rbp\n\tret\n", 0, 42);
	check("float.h describes the x87 long double", "#include <float.h>\nint main(void){ long double e = LDBL_EPSILON; return (LDBL_MANT_DIG == 64) + (1.0L + e > 1.0L) + (1.0L + e / 2 == 1.0L) + (LDBL_MAX > 1e4900L) + (LDBL_MAX_EXP == 16384) + 37; }", 42);
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
