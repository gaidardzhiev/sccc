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
#define TMP_BIN "/tmp/tc_bin"

static int g_pass = 0;
static int g_fail = 0;
static const char *g_compiler = NULL;

static void write_src(const char *src) {
	FILE *f = fopen(TMP_SRC, "w");
	if (!f) { perror("fopen " TMP_SRC); exit(1); }
	fputs(src, f);
	fclose(f);
}

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

	section("expected compile errors");
	check_compile_err("undeclared var", "int main(){ return x; }");
	check_compile_err("redeclare var", "int main(){ int x=1; int x=2; return x; }");
	check_compile_err("break outside loop", "int main(){ break; return 0; }");
	check_compile_err("continue outside loop","int main(){ continue; return 0; }");

	printf("\n");
	printf("Results: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
