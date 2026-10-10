/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <math.h>

#define SYS_read 0
#define SYS_write 1
#define SYS_open 2
#define SYS_close 3
#define SYS_stat 4
#define SYS_fstat 5
#define SYS_lseek 8
#define SYS_mmap 9
#define SYS_munmap 11
#define SYS_ioctl 16
#define SYS_access 21
#define SYS_getpid 39
#define SYS_kill 62
#define SYS_getcwd 79
#define SYS_rename 82
#define SYS_mkdir 83
#define SYS_unlink 87
#define SYS_chmod 90
#define SYS_exit_group 231

int errno;
char **environ;

static long sys_ret(long r) {
	if (r < 0 && r > -4096) { errno = (int)-r; return -1; }
	return r;
}

ssize_t read(int fd, void *buf, size_t n) { return sys_ret(__syscall(SYS_read, fd, buf, n)); }
ssize_t write(int fd, const void *buf, size_t n) { return sys_ret(__syscall(SYS_write, fd, buf, n)); }
int close(int fd) { return (int)sys_ret(__syscall(SYS_close, fd)); }
off_t lseek(int fd, off_t off, int whence) { return sys_ret(__syscall(SYS_lseek, fd, off, whence)); }
int unlink(const char *path) { return (int)sys_ret(__syscall(SYS_unlink, path)); }
int access(const char *path, int mode) { return (int)sys_ret(__syscall(SYS_access, path, mode)); }
pid_t getpid(void) { return (pid_t)__syscall(SYS_getpid); }
int chmod(const char *path, mode_t mode) { return (int)sys_ret(__syscall(SYS_chmod, path, mode)); }
int mkdir(const char *path, mode_t mode) { return (int)sys_ret(__syscall(SYS_mkdir, path, mode)); }
int stat(const char *path, struct stat *st) { return (int)sys_ret(__syscall(SYS_stat, path, st)); }
int fstat(int fd, struct stat *st) { return (int)sys_ret(__syscall(SYS_fstat, fd, st)); }
int rename(const char *from, const char *to) { return (int)sys_ret(__syscall(SYS_rename, from, to)); }
int remove(const char *path) { return unlink(path); }
void _exit(int status) { for (;;) __syscall(SYS_exit_group, status); }

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off) {
	long r = __syscall(SYS_mmap, addr, len, prot, flags, fd, off);
	if (r < 0 && r > -4096) { errno = (int)-r; return MAP_FAILED; }
	return (void *)r;
}
int munmap(void *addr, size_t len) { return (int)sys_ret(__syscall(SYS_munmap, addr, len)); }

char *getcwd(char *buf, size_t size) {
	if (sys_ret(__syscall(SYS_getcwd, buf, size)) < 0) return NULL;
	return buf;
}

int isatty(int fd) {
	char termios[64];
	return __syscall(SYS_ioctl, fd, 0x5401, termios) == 0;
}

int open(const char *path, int flags, ...) {
	va_list ap;
	va_start(ap, flags);
	int mode = va_arg(ap, int);
	va_end(ap);
	return (int)sys_ret(__syscall(SYS_open, path, flags, mode));
}

void *__va_arg_ptr(__va_elem *ap, unsigned long size) {
	void *p;
	if (size <= 8 && ap->gp_offset < 48) {
		p = (char *)ap->reg_save_area + ap->gp_offset;
		ap->gp_offset += 8;
		return p;
	}
	p = ap->overflow_arg_area;
	ap->overflow_arg_area = (char *)p + ((size + 7) & ~7UL);
	return p;
}

#define NCLASS 21
#define CHUNK (4L << 20)

typedef struct Block Block;
struct Block {
	unsigned long cls;
	unsigned long size;
	Block *next;
	unsigned long pad;
};

static Block *free_lists[NCLASS];
static char *chunk_cur;
static char *chunk_end;

static void *sys_mmap(unsigned long len) {
	long r = __syscall(SYS_mmap, 0, len, 3, 0x22, -1, 0);
	if (r < 0 && r > -4096) return NULL;
	return (void *)r;
}

void *malloc(size_t n) {
	if (n == 0) n = 1;
	int cls = 0;
	unsigned long sz = 16;
	while (sz < n && cls < NCLASS) { sz <<= 1; cls++; }
	Block *b;
	if (cls >= NCLASS) {
		unsigned long len = (n + sizeof(Block) + 4095) & ~4095UL;
		b = (Block *)sys_mmap(len);
		if (!b) { errno = ENOMEM; return NULL; }
		b->cls = NCLASS;
		b->size = len - sizeof(Block);
		return b + 1;
	}
	if (free_lists[cls]) {
		b = free_lists[cls];
		free_lists[cls] = b->next;
		return b + 1;
	}
	unsigned long need = sz + sizeof(Block);
	if (chunk_cur == NULL || (unsigned long)(chunk_end - chunk_cur) < need) {
		unsigned long len = need > CHUNK ? need : CHUNK;
		chunk_cur = (char *)sys_mmap(len);
		if (!chunk_cur) { errno = ENOMEM; return NULL; }
		chunk_end = chunk_cur + len;
	}
	b = (Block *)chunk_cur;
	chunk_cur += need;
	b->cls = cls;
	b->size = sz;
	return b + 1;
}

void free(void *p) {
	if (!p) return;
	Block *b = (Block *)p - 1;
	if (b->cls >= NCLASS) {
		__syscall(SYS_munmap, b, b->size + sizeof(Block));
		return;
	}
	b->next = free_lists[b->cls];
	free_lists[b->cls] = b;
}

void *calloc(size_t n, size_t size) {
	size_t total = n * size;
	if (size && total / size != n) { errno = ENOMEM; return NULL; }
	void *p = malloc(total);
	if (p) memset(p, 0, total);
	return p;
}

void *realloc(void *p, size_t n) {
	if (!p) return malloc(n);
	Block *b = (Block *)p - 1;
	if (n <= b->size) return p;
	void *q = malloc(n);
	if (!q) return NULL;
	memcpy(q, p, b->size);
	free(p);
	return q;
}

void *memcpy(void *dst, const void *src, size_t n) {
	char *d = (char *)dst;
	const char *s = (const char *)src;
	while (n >= 8) { *(long *)d = *(const long *)s; d += 8; s += 8; n -= 8; }
	while (n--) *d++ = *s++;
	return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
	char *d = (char *)dst;
	const char *s = (const char *)src;
	if (d == s || n == 0) return dst;
	if (d < s || d >= s + n) return memcpy(dst, src, n);
	d += n; s += n;
	while (n--) *--d = *--s;
	return dst;
}

void *memset(void *s, int c, size_t n) {
	unsigned char *p = (unsigned char *)s;
	while (n--) *p++ = (unsigned char)c;
	return s;
}

int memcmp(const void *a, const void *b, size_t n) {
	const unsigned char *p = (const unsigned char *)a;
	const unsigned char *q = (const unsigned char *)b;
	for (; n; n--, p++, q++)
		if (*p != *q) return *p - *q;
	return 0;
}

void *memchr(const void *s, int c, size_t n) {
	const unsigned char *p = (const unsigned char *)s;
	for (; n; n--, p++)
		if (*p == (unsigned char)c) return (void *)p;
	return NULL;
}

size_t strlen(const char *s) {
	const char *p = s;
	while (*p) p++;
	return p - s;
}

size_t strnlen(const char *s, size_t n) {
	size_t i = 0;
	while (i < n && s[i]) i++;
	return i;
}

char *strcpy(char *dst, const char *src) {
	char *d = dst;
	while ((*d++ = *src++)) ;
	return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
	size_t i = 0;
	for (; i < n && src[i]; i++) dst[i] = src[i];
	for (; i < n; i++) dst[i] = 0;
	return dst;
}

char *strcat(char *dst, const char *src) {
	strcpy(dst + strlen(dst), src);
	return dst;
}

char *strncat(char *dst, const char *src, size_t n) {
	char *d = dst + strlen(dst);
	while (n-- && *src) *d++ = *src++;
	*d = 0;
	return dst;
}

int strcmp(const char *a, const char *b) {
	while (*a && *a == *b) { a++; b++; }
	return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
	for (; n; n--, a++, b++) {
		if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
		if (!*a) return 0;
	}
	return 0;
}

char *strchr(const char *s, int c) {
	for (;; s++) {
		if (*s == (char)c) return (char *)s;
		if (!*s) return NULL;
	}
}

char *strrchr(const char *s, int c) {
	const char *r = NULL;
	for (;; s++) {
		if (*s == (char)c) r = s;
		if (!*s) return (char *)r;
	}
}

char *strstr(const char *h, const char *n) {
	size_t ln = strlen(n);
	if (!ln) return (char *)h;
	for (; *h; h++)
		if (*h == *n && strncmp(h, n, ln) == 0) return (char *)h;
	return NULL;
}

size_t strspn(const char *s, const char *accept) {
	size_t i = 0;
	while (s[i] && strchr(accept, s[i])) i++;
	return i;
}

size_t strcspn(const char *s, const char *reject) {
	size_t i = 0;
	while (s[i] && !strchr(reject, s[i])) i++;
	return i;
}

char *strpbrk(const char *s, const char *accept) {
	s += strcspn(s, accept);
	return *s ? (char *)s : NULL;
}

char *strtok(char *s, const char *delim) {
	static char *save;
	if (!s) s = save;
	if (!s) return NULL;
	s += strspn(s, delim);
	if (!*s) { save = NULL; return NULL; }
	char *e = s + strcspn(s, delim);
	if (*e) { *e = 0; save = e + 1; } else save = NULL;
	return s;
}

char *strdup(const char *s) {
	size_t n = strlen(s) + 1;
	char *p = (char *)malloc(n);
	if (p) memcpy(p, s, n);
	return p;
}

char *strndup(const char *s, size_t n) {
	size_t l = strnlen(s, n);
	char *p = (char *)malloc(l + 1);
	if (!p) return NULL;
	memcpy(p, s, l);
	p[l] = 0;
	return p;
}

char *strerror(int err) {
	switch (err) {
	case 0: return "Success";
	case EPERM: return "Operation not permitted";
	case ENOENT: return "No such file or directory";
	case EINTR: return "Interrupted system call";
	case EIO: return "Input/output error";
	case EBADF: return "Bad file descriptor";
	case EAGAIN: return "Resource temporarily unavailable";
	case ENOMEM: return "Cannot allocate memory";
	case EACCES: return "Permission denied";
	case EFAULT: return "Bad address";
	case EBUSY: return "Device or resource busy";
	case EEXIST: return "File exists";
	case ENOTDIR: return "Not a directory";
	case EISDIR: return "Is a directory";
	case EINVAL: return "Invalid argument";
	case EMFILE: return "Too many open files";
	case ENOTTY: return "Inappropriate ioctl for device";
	case ENOSPC: return "No space left on device";
	case ESPIPE: return "Illegal seek";
	case EROFS: return "Read-only file system";
	case EPIPE: return "Broken pipe";
	case ERANGE: return "Numerical result out of range";
	case ENAMETOOLONG: return "File name too long";
	case ENOSYS: return "Function not implemented";
	case ENOTEMPTY: return "Directory not empty";
	}
	return "Unknown error";
}

int isdigit(int c) { return c >= '0' && c <= '9'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int isalpha(int c) { return islower(c) || isupper(c); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
int isblank(int c) { return c == ' ' || c == '\t'; }
int iscntrl(int c) { return (c >= 0 && c < 32) || c == 127; }
int isprint(int c) { return c >= 32 && c < 127; }
int isgraph(int c) { return c > 32 && c < 127; }
int ispunct(int c) { return isgraph(c) && !isalnum(c); }
int tolower(int c) { return isupper(c) ? c + 32 : c; }
int toupper(int c) { return islower(c) ? c - 32 : c; }

unsigned long long strtoull(const char *s, char **end, int base) {
	const char *p = s;
	int neg = 0;
	while (isspace(*p)) p++;
	if (*p == '+') p++;
	else if (*p == '-') { neg = 1; p++; }
	if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && isxdigit(p[2])) {
		p += 2; base = 16;
	} else if (base == 0) base = (p[0] == '0') ? 8 : 10;
	unsigned long long v = 0;
	const char *start = p;
	for (;; p++) {
		int d;
		if (isdigit(*p)) d = *p - '0';
		else if (islower(*p)) d = *p - 'a' + 10;
		else if (isupper(*p)) d = *p - 'A' + 10;
		else break;
		if (d >= base) break;
		v = v * base + d;
	}
	if (p == start) p = s;
	if (end) *end = (char *)p;
	return neg ? -v : v;
}

unsigned long strtoul(const char *s, char **end, int base) { return strtoull(s, end, base); }
long long strtoll(const char *s, char **end, int base) { return (long long)strtoull(s, end, base); }
long strtol(const char *s, char **end, int base) { return (long)strtoull(s, end, base); }
int atoi(const char *s) { return (int)strtol(s, NULL, 10); }
long atol(const char *s) { return strtol(s, NULL, 10); }
long long atoll(const char *s) { return strtoll(s, NULL, 10); }

#define BIGN 170

typedef struct {
	unsigned int d[BIGN];
	int n;
} Big;

static void big_set(Big *b, unsigned long v) {
	b->n = 0;
	while (v) { b->d[b->n++] = (unsigned int)v; v >>= 32; }
}

static void big_mul_add(Big *b, unsigned int m, unsigned int add) {
	unsigned long carry = add;
	for (int i = 0; i < b->n; i++) {
		unsigned long t = (unsigned long)b->d[i] * m + carry;
		b->d[i] = (unsigned int)t;
		carry = t >> 32;
	}
	if (carry && b->n < BIGN) b->d[b->n++] = (unsigned int)carry;
}

static void big_mul_pow10(Big *b, int k) {
	while (k >= 9) { big_mul_add(b, 1000000000u, 0); k -= 9; }
	while (k-- > 0) big_mul_add(b, 10, 0);
}

static int big_bitlen(Big *b) {
	if (b->n == 0) return 0;
	unsigned int top = b->d[b->n - 1];
	int bits = 0;
	while (top) { bits++; top >>= 1; }
	return (b->n - 1) * 32 + bits;
}

static int big_bit(Big *b, int i) {
	if (i < 0 || i / 32 >= b->n) return 0;
	return (b->d[i / 32] >> (i % 32)) & 1;
}

static void big_shl(Big *dst, Big *src, int s) {
	int w = s / 32, r = s % 32;
	int n = src->n + w + 1;
	if (n > BIGN) n = BIGN;
	for (int i = 0; i < n; i++) dst->d[i] = 0;
	for (int i = 0; i < src->n && i + w < BIGN; i++) {
		unsigned long v = (unsigned long)src->d[i] << r;
		dst->d[i + w] |= (unsigned int)v;
		if (i + w + 1 < BIGN) dst->d[i + w + 1] |= (unsigned int)(v >> 32);
	}
	dst->n = n;
	while (dst->n > 0 && dst->d[dst->n - 1] == 0) dst->n--;
}

static int big_cmp(Big *a, Big *b) {
	if (a->n != b->n) return a->n < b->n ? -1 : 1;
	for (int i = a->n - 1; i >= 0; i--)
		if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1;
	return 0;
}

static void big_sub(Big *a, Big *b) {
	long borrow = 0;
	for (int i = 0; i < a->n; i++) {
		long t = (long)a->d[i] - (i < b->n ? (long)b->d[i] : 0) - borrow;
		borrow = t < 0;
		a->d[i] = (unsigned int)(t + (borrow ? 0x100000000L : 0));
	}
	while (a->n > 0 && a->d[a->n - 1] == 0) a->n--;
}

static double make_double(unsigned long q, int e2, int sticky) {
	if (q == 0) return 0.0;
	while (!(q >> 63)) { q <<= 1; e2--; }
	int lead = 63 + e2;
	int kept = 53;
	if (lead < -1022) kept = 53 - (-1022 - lead);
	unsigned long m;
	int up;
	if (kept <= 0) {
		m = 0;
		up = kept == 0 && (q > (1UL << 63) || sticky);
	} else {
		int drop = 64 - kept;
		m = q >> drop;
		unsigned long rest = q & ((1UL << drop) - 1);
		unsigned long half = 1UL << (drop - 1);
		up = rest > half || (rest == half && (sticky || (m & 1)));
	}
	if (up) m++;
	unsigned long bits;
	if (lead >= -1022) {
		long biased = lead + 1023;
		if (m >> 53) { m >>= 1; biased++; }
		if (biased >= 2047) return INFINITY;
		bits = ((unsigned long)biased << 52) | (m & ((1UL << 52) - 1));
	} else bits = m;
	double v;
	memcpy(&v, &bits, 8);
	return v;
}

static double decimal_to_double(Big *digits, int e10, int sticky_in) {
	Big n, p, t;
	if (digits->n == 0) return 0.0;
	if (e10 > 400) return INFINITY;
	if (e10 < -1200) return 0.0;
	if (e10 >= 0) {
		n = *digits;
		big_mul_pow10(&n, e10);
		int len = big_bitlen(&n);
		if (len <= 64) {
			unsigned long q = 0;
			for (int i = 0; i < n.n; i++) q |= (unsigned long)n.d[i] << (32 * i);
			return make_double(q, 0, sticky_in);
		}
		unsigned long q = 0;
		for (int i = 0; i < 64; i++) q |= (unsigned long)big_bit(&n, len - 64 + i) << i;
		int sticky = sticky_in;
		for (int i = 0; i < len - 64 && !sticky; i++) sticky = big_bit(&n, i);
		return make_double(q, len - 64, sticky);
	}
	big_set(&p, 1);
	big_mul_pow10(&p, -e10);
	int s = 63 - (big_bitlen(digits) - big_bitlen(&p));
	if (s >= 0) { big_shl(&n, digits, s); }
	else { n = *digits; big_shl(&t, &p, -s); p = t; }
	unsigned long q = 0;
	for (int bit = 63; bit >= 0; bit--) {
		big_shl(&t, &p, bit);
		if (big_cmp(&n, &t) >= 0) { big_sub(&n, &t); q |= 1UL << bit; }
	}
	return make_double(q, -s, n.n != 0 || sticky_in);
}

static double pow10_exact(int n) {
	double p = 1.0;
	while (n-- > 0) p = p * 10.0;
	return p;
}

static int strncasecmp_ascii(const char *a, const char *b, int n) {
	for (int i = 0; i < n; i++) {
		int x = a[i] | 32, y = b[i];
		if (!a[i] || x != y) return 1;
	}
	return 0;
}

double strtod(const char *s, char **end) {
	const char *p = s;
	int neg = 0;
	while (isspace(*p)) p++;
	if (*p == '+') p++;
	else if (*p == '-') { neg = 1; p++; }
	if ((p[0] == 'i' || p[0] == 'I') && (p[1] == 'n' || p[1] == 'N') && (p[2] == 'f' || p[2] == 'F')) {
		p += 3;
		if (strncasecmp_ascii(p, "inity", 5) == 0) p += 5;
		if (end) *end = (char *)p;
		return neg ? -INFINITY : INFINITY;
	}
	if ((p[0] == 'n' || p[0] == 'N') && (p[1] == 'a' || p[1] == 'A') && (p[2] == 'n' || p[2] == 'N')) {
		p += 3;
		if (*p == '(') {
			const char *q = p + 1;
			while (isalnum(*q) || *q == '_') q++;
			if (*q == ')') p = q + 1;
		}
		if (end) *end = (char *)p;
		return neg ? -NAN : NAN;
	}
	if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && (isxdigit(p[2]) || (p[2] == '.' && isxdigit(p[3])))) {
		unsigned long q = 0;
		int e2 = 0, sticky = 0;
		p += 2;
		for (int frac = 0;; p++) {
			if (*p == '.' && !frac) { frac = 1; continue; }
			if (!isxdigit(*p)) break;
			int dv = isdigit(*p) ? *p - '0' : (*p | 32) - 'a' + 10;
			if (q >> 60) {
				if (dv) sticky = 1;
				if (!frac) e2 += 4;
			} else {
				q = q << 4 | (unsigned long)dv;
				if (frac) e2 -= 4;
			}
		}
		if (*p == 'p' || *p == 'P') {
			const char *r = p + 1;
			int eneg = 0, e = 0;
			if (*r == '+') r++;
			else if (*r == '-') { eneg = 1; r++; }
			if (isdigit(*r)) {
				while (isdigit(*r)) { if (e < 100000) e = e * 10 + (*r - '0'); r++; }
				e2 += eneg ? -e : e;
				p = r;
			}
		}
		if (end) *end = (char *)p;
		double v = make_double(q, e2, sticky);
		if (v == INFINITY || (v == 0 && q)) errno = ERANGE;
		return neg ? -v : v;
	}
	Big digits;
	big_set(&digits, 0);
	unsigned long mant = 0;
	int ndig = 0, exp10 = 0, any = 0, sticky = 0;
	for (int frac = 0;; p++) {
		if (*p == '.' && !frac) { frac = 1; continue; }
		if (!isdigit(*p)) break;
		any = 1;
		int dv = *p - '0';
		if (ndig < 760) {
			if (ndig < 19) mant = mant * 10 + dv;
			big_mul_add(&digits, 10, (unsigned int)dv);
			if (digits.n) ndig++;
			if (frac) exp10--;
		} else {
			if (dv) sticky = 1;
			if (!frac) exp10++;
		}
	}
	if (!any) { if (end) *end = (char *)s; return 0.0; }
	if (*p == 'e' || *p == 'E') {
		const char *q = p + 1;
		int eneg = 0, e = 0;
		if (*q == '+') q++;
		else if (*q == '-') { eneg = 1; q++; }
		if (isdigit(*q)) {
			while (isdigit(*q)) { if (e < 100000) e = e * 10 + (*q - '0'); q++; }
			exp10 += eneg ? -e : e;
			p = q;
		}
	}
	if (end) *end = (char *)p;
	double v;
	if (ndig <= 15 && !sticky && exp10 >= -22 && exp10 <= 22) {
		v = (double)mant;
		if (exp10 > 0) v = v * pow10_exact(exp10);
		else if (exp10 < 0) v = v / pow10_exact(-exp10);
	} else v = decimal_to_double(&digits, exp10, sticky);
	return neg ? -v : v;
}

float strtof(const char *s, char **end) { return (float)strtod(s, end); }
double atof(const char *s) { return strtod(s, NULL); }

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }
long long llabs(long long x) { return x < 0 ? -x : x; }

static unsigned long rand_state = 1;
int rand(void) {
	rand_state = rand_state * 6364136223846793005UL + 1442695040888963407UL;
	return (int)((rand_state >> 33) & RAND_MAX);
}
void srand(unsigned int seed) { rand_state = seed; }

char *getenv(const char *name) {
	size_t n = strlen(name);
	if (!environ) return NULL;
	for (char **e = environ; *e; e++)
		if (strncmp(*e, name, n) == 0 && (*e)[n] == '=') return *e + n + 1;
	return NULL;
}

static void swap_bytes(char *a, char *b, size_t n) {
	while (n--) { char t = *a; *a++ = *b; *b++ = t; }
}

static void sift_down(char *base, size_t start, size_t end, size_t size, int (*cmp)(const void *, const void *)) {
	size_t root = start;
	while (root * 2 + 1 < end) {
		size_t child = root * 2 + 1;
		if (child + 1 < end && cmp(base + child * size, base + (child + 1) * size) < 0) child++;
		if (cmp(base + root * size, base + child * size) >= 0) return;
		swap_bytes(base + root * size, base + child * size, size);
		root = child;
	}
}

void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *)) {
	char *b = (char *)base;
	if (n < 2) return;
	for (size_t i = n / 2; i > 0; i--) sift_down(b, i - 1, n, size, cmp);
	for (size_t end = n - 1; end > 0; end--) {
		swap_bytes(b, b + end * size, size);
		sift_down(b, 0, end, size, cmp);
	}
}

void *bsearch(const void *key, const void *base, size_t n, size_t size, int (*cmp)(const void *, const void *)) {
	const char *b = (const char *)base;
	size_t lo = 0, hi = n;
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		int r = cmp(key, b + mid * size);
		if (r == 0) return (void *)(b + mid * size);
		if (r < 0) hi = mid; else lo = mid + 1;
	}
	return NULL;
}

double fabs(double x) { return x < 0 ? -x : x; }
float fabsf(float x) { return x < 0 ? -x : x; }

double trunc(double x) {
	if (isnan(x) || fabs(x) >= 4503599627370496.0) return x;
	return (double)(long)x;
}

double floor(double x) {
	double t = trunc(x);
	return t > x ? t - 1.0 : t;
}

double ceil(double x) {
	double t = trunc(x);
	return t < x ? t + 1.0 : t;
}

double round(double x) { return x < 0 ? -floor(-x + 0.5) : floor(x + 0.5); }

double fmod(double x, double y) {
	if (y == 0.0) return NAN;
	return x - trunc(x / y) * y;
}

double sqrt(double x) {
	if (x < 0) return NAN;
	if (x == 0 || isinf(x)) return x;
	double g = x > 1 ? x / 2 : 1.0;
	for (int i = 0; i < 200; i++) {
		double n = 0.5 * (g + x / g);
		if (n == g) break;
		g = n;
	}
	return g;
}

double ldexp(double x, int e) {
	while (e > 0) { x = x * 2.0; e--; }
	while (e < 0) { x = x * 0.5; e++; }
	return x;
}

double frexp(double x, int *e) {
	*e = 0;
	if (x == 0 || isnan(x) || isinf(x)) return x;
	double a = fabs(x);
	while (a >= 1.0) { a = a * 0.5; (*e)++; }
	while (a < 0.5) { a = a * 2.0; (*e)--; }
	return x < 0 ? -a : a;
}

double modf(double x, double *ip) {
	double t = trunc(x);
	*ip = t;
	return x - t;
}

double exp(double x) {
	if (isnan(x)) return x;
	if (x > 709.0) return INFINITY;
	if (x < -745.0) return 0.0;
	double ln2 = 0.69314718055994530942;
	int k = (int)(x / ln2 + (x < 0 ? -0.5 : 0.5));
	double r = x - k * ln2;
	double term = 1.0, sum = 1.0;
	for (int i = 1; i < 30; i++) { term = term * r / i; sum = sum + term; }
	return ldexp(sum, k);
}

double log(double x) {
	if (isnan(x) || x < 0) return NAN;
	if (x == 0) return -INFINITY;
	if (isinf(x)) return x;
	int e;
	double m = frexp(x, &e);
	double ln2 = 0.69314718055994530942;
	double z = (m - 1) / (m + 1), z2 = z * z, term = z, sum = 0;
	for (int i = 1; i < 60; i += 2) { sum = sum + term / i; term = term * z2; }
	return 2 * sum + e * ln2;
}

double log10(double x) { return log(x) / 2.30258509299404568402; }

double pow(double x, double y) {
	if (y == 0) return 1.0;
	if (y == trunc(y) && fabs(y) < 1e9) {
		long n = (long)y;
		int inv = n < 0;
		if (inv) n = -n;
		double r = 1.0, b = x;
		while (n) { if (n & 1) r = r * b; b = b * b; n >>= 1; }
		return inv ? 1.0 / r : r;
	}
	if (x < 0) return NAN;
	return exp(y * log(x));
}

double sin(double x) {
	double pi = 3.14159265358979323846;
	x = fmod(x, 2 * pi);
	if (x > pi) x = x - 2 * pi;
	if (x < -pi) x = x + 2 * pi;
	double term = x, sum = x, x2 = x * x;
	for (int i = 1; i < 20; i++) { term = -term * x2 / ((2 * i) * (2 * i + 1)); sum = sum + term; }
	return sum;
}

double cos(double x) { return sin(x + 1.57079632679489661923); }
double tan(double x) { return sin(x) / cos(x); }

double atan(double x) {
	if (isnan(x)) return x;
	double pi2 = 1.57079632679489661923;
	if (x > 1) return pi2 - atan(1 / x);
	if (x < -1) return -pi2 - atan(1 / x);
	double y = x / (1 + sqrt(1 + x * x));
	double term = y, sum = 0, y2 = y * y;
	for (int i = 1; i < 80; i += 2) { sum = sum + term / i; term = -term * y2; }
	return 2 * sum;
}

double atan2(double y, double x) {
	double pi = 3.14159265358979323846;
	if (x > 0) return atan(y / x);
	if (x < 0) return y >= 0 ? atan(y / x) + pi : atan(y / x) - pi;
	if (y > 0) return pi / 2;
	if (y < 0) return -pi / 2;
	return 0.0;
}

#define F_READ 1
#define F_WRITE 2
#define F_EOF 4
#define F_ERR 8
#define F_UNBUF 16
#define F_LINE 32

struct __FILE {
	int fd;
	int flags;
	unsigned char *buf;
	int bufsize;
	int pos;
	int len;
	int unget;
	FILE *next;
};

static struct __FILE file_stdin;
static struct __FILE file_stdout;
static struct __FILE file_stderr;
FILE *stdin;
FILE *stdout;
FILE *stderr;
static FILE *open_files;
static int stdio_ready;

static void (*atexit_fns[32])(void);
static int natexit;

static void init_file(FILE *f, int fd, int flags) {
	f->fd = fd;
	f->flags = flags;
	f->buf = NULL;
	f->bufsize = 0;
	f->pos = 0;
	f->len = 0;
	f->unget = -1;
	f->next = open_files;
	open_files = f;
}

static void stdio_init(void) {
	if (stdio_ready) return;
	stdio_ready = 1;
	init_file(&file_stdin, 0, F_READ);
	init_file(&file_stdout, 1, F_WRITE);
	init_file(&file_stderr, 2, F_WRITE | F_UNBUF);
	stdin = &file_stdin;
	stdout = &file_stdout;
	stderr = &file_stderr;
	if (isatty(1)) file_stdout.flags |= F_LINE;
}

void __libc_init(void) { stdio_init(); }

static int write_all(int fd, const unsigned char *p, size_t n) {
	while (n > 0) {
		ssize_t w = write(fd, p, n);
		if (w < 0) { if (errno == EINTR) continue; return -1; }
		p += w; n -= w;
	}
	return 0;
}

int fflush(FILE *f) {
	if (!f) {
		for (FILE *g = open_files; g; g = g->next) fflush(g);
		return 0;
	}
	if ((f->flags & F_WRITE) && f->pos > 0) {
		int r = write_all(f->fd, f->buf, f->pos);
		f->pos = 0;
		if (r < 0) { f->flags |= F_ERR; return EOF; }
	}
	return 0;
}

static int ensure_buf(FILE *f) {
	if (f->buf) return 0;
	f->bufsize = BUFSIZ;
	f->buf = (unsigned char *)malloc(f->bufsize);
	if (!f->buf) { f->flags |= F_ERR; return -1; }
	return 0;
}

size_t fwrite(const void *buf, size_t size, size_t n, FILE *f) {
	size_t total = size * n;
	const unsigned char *p = (const unsigned char *)buf;
	if (total == 0) return 0;
	if (f->flags & F_UNBUF) {
		if (write_all(f->fd, p, total) < 0) { f->flags |= F_ERR; return 0; }
		return n;
	}
	if (ensure_buf(f) < 0) return 0;
	if (f->pos + total > (size_t)f->bufsize) {
		if (fflush(f) == EOF) return 0;
		if (total >= (size_t)f->bufsize) {
			if (write_all(f->fd, p, total) < 0) { f->flags |= F_ERR; return 0; }
			return n;
		}
	}
	memcpy(f->buf + f->pos, p, total);
	f->pos += (int)total;
	if ((f->flags & F_LINE) && memchr(p, '\n', total) && fflush(f) == EOF) return 0;
	return n;
}

int setvbuf(FILE *f, char *buf, int mode, size_t size) {
	(void)buf;
	(void)size;
	if (fflush(f) == EOF) return -1;
	f->flags &= ~(F_UNBUF | F_LINE);
	if (mode == _IONBF) f->flags |= F_UNBUF;
	else if (mode == _IOLBF) f->flags |= F_LINE;
	else if (mode != _IOFBF) return -1;
	return 0;
}

void setbuf(FILE *f, char *buf) { setvbuf(f, buf, buf ? _IOFBF : _IONBF, BUFSIZ); }

int fputc(int c, FILE *f) {
	unsigned char ch = (unsigned char)c;
	if (fwrite(&ch, 1, 1, f) != 1) return EOF;
	return ch;
}
int putc(int c, FILE *f) { return fputc(c, f); }
int putchar(int c) { return fputc(c, stdout); }

int fputs(const char *s, FILE *f) {
	size_t n = strlen(s);
	return fwrite(s, 1, n, f) == n || n == 0 ? 0 : EOF;
}

int puts(const char *s) {
	if (fputs(s, stdout) == EOF) return EOF;
	return fputc('\n', stdout) == EOF ? EOF : 0;
}

static int fill_buf(FILE *f) {
	if (ensure_buf(f) < 0) return -1;
	for (FILE *g = open_files; g; g = g->next)
		if (g->flags & F_LINE) fflush(g);
	for (;;) {
		ssize_t r = read(f->fd, f->buf, f->bufsize);
		if (r < 0 && errno == EINTR) continue;
		if (r < 0) { f->flags |= F_ERR; return -1; }
		if (r == 0) { f->flags |= F_EOF; return -1; }
		f->pos = 0;
		f->len = (int)r;
		return 0;
	}
}

int fgetc(FILE *f) {
	if (f->unget >= 0) { int c = f->unget; f->unget = -1; return c; }
	if (f->pos >= f->len && fill_buf(f) < 0) return EOF;
	return f->buf[f->pos++];
}
int getc(FILE *f) { return fgetc(f); }
int getchar(void) { return fgetc(stdin); }

int ungetc(int c, FILE *f) {
	if (c == EOF) return EOF;
	f->unget = (unsigned char)c;
	f->flags &= ~F_EOF;
	return c;
}

char *fgets(char *s, int n, FILE *f) {
	int i = 0;
	if (n <= 0) return NULL;
	while (i < n - 1) {
		int c = fgetc(f);
		if (c == EOF) break;
		s[i++] = (char)c;
		if (c == '\n') break;
	}
	if (i == 0) return NULL;
	s[i] = 0;
	return s;
}

size_t fread(void *buf, size_t size, size_t n, FILE *f) {
	size_t total = size * n, got = 0;
	unsigned char *p = (unsigned char *)buf;
	if (total == 0) return 0;
	if (f->unget >= 0) { p[got++] = (unsigned char)f->unget; f->unget = -1; }
	while (got < total) {
		if (f->pos < f->len) {
			size_t k = f->len - f->pos;
			if (k > total - got) k = total - got;
			memcpy(p + got, f->buf + f->pos, k);
			f->pos += (int)k;
			got += k;
			continue;
		}
		if (total - got >= BUFSIZ) {
			ssize_t r = read(f->fd, p + got, total - got);
			if (r < 0 && errno == EINTR) continue;
			if (r < 0) { f->flags |= F_ERR; break; }
			if (r == 0) { f->flags |= F_EOF; break; }
			got += r;
			continue;
		}
		if (fill_buf(f) < 0) break;
	}
	return got / size;
}

FILE *fdopen(int fd, const char *mode) {
	FILE *f = (FILE *)malloc(sizeof(FILE));
	if (!f) return NULL;
	stdio_init();
	init_file(f, fd, mode[0] == 'r' && !strchr(mode, '+') ? F_READ : (mode[0] == 'r' ? F_READ | F_WRITE : F_WRITE));
	return f;
}

FILE *fopen(const char *path, const char *mode) {
	int flags;
	int plus = strchr(mode, '+') != NULL;
	if (mode[0] == 'r') flags = plus ? O_RDWR : O_RDONLY;
	else if (mode[0] == 'w') flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
	else if (mode[0] == 'a') flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
	else { errno = EINVAL; return NULL; }
	int fd = open(path, flags, 0666);
	if (fd < 0) return NULL;
	FILE *f = fdopen(fd, mode);
	if (!f) close(fd);
	return f;
}

int fclose(FILE *f) {
	int r = fflush(f);
	if (close(f->fd) < 0) r = EOF;
	FILE **pp = &open_files;
	while (*pp && *pp != f) pp = &(*pp)->next;
	if (*pp) *pp = f->next;
	if (f->buf) free(f->buf);
	if (f != &file_stdin && f != &file_stdout && f != &file_stderr) free(f);
	return r;
}

int fseek(FILE *f, long off, int whence) {
	if (fflush(f) == EOF) return -1;
	if (whence == SEEK_CUR && (f->flags & F_READ)) off -= f->len - f->pos + (f->unget >= 0);
	f->pos = f->len = 0;
	f->unget = -1;
	if (lseek(f->fd, off, whence) < 0) return -1;
	f->flags &= ~F_EOF;
	return 0;
}

long ftell(FILE *f) {
	long p = lseek(f->fd, 0, SEEK_CUR);
	if (p < 0) return -1;
	if (f->flags & F_WRITE) return p + f->pos;
	return p - (f->len - f->pos) - (f->unget >= 0);
}

void rewind(FILE *f) { fseek(f, 0, SEEK_SET); f->flags &= ~F_ERR; }
int feof(FILE *f) { return (f->flags & F_EOF) != 0; }
int ferror(FILE *f) { return (f->flags & F_ERR) != 0; }
void clearerr(FILE *f) { f->flags &= ~(F_EOF | F_ERR); }
int fileno(FILE *f) { return f->fd; }

typedef struct {
	FILE *f;
	char *buf;
	size_t cap;
	size_t n;
} Out;

static void out_ch(Out *o, int c) {
	if (o->f) fputc(c, o->f);
	else if (o->n + 1 < o->cap) o->buf[o->n] = (char)c;
	o->n++;
}

static void out_pad(Out *o, int c, int n) { while (n-- > 0) out_ch(o, c); }

#define BIG_LIMBS 90

static int exact_decimal(double v, char *digits, int *dp) {
	unsigned long bits;
	memcpy(&bits, &v, 8);
	int e = (int)((bits >> 52) & 0x7ff);
	unsigned long m = bits & ((1UL << 52) - 1);
	if (e == 0) e = 1; else m |= 1UL << 52;
	int e2 = e - 1075;
	if (m == 0) { digits[0] = '0'; *dp = 1; return 1; }
	unsigned int big[BIG_LIMBS];
	int n = 0;
	big[n++] = (unsigned int)m;
	big[n++] = (unsigned int)(m >> 32);
	int mul, count, frac = 0;
	if (e2 >= 0) { mul = 2; count = e2; }
	else { mul = 5; count = -e2; frac = -e2; }
	while (count > 0) {
		unsigned long f = 1;
		int k = 0;
		while (k < count && f * mul <= 0xFFFFFFFFUL) { f *= mul; k++; }
		unsigned long carry = 0;
		for (int i = 0; i < n; i++) {
			unsigned long t = (unsigned long)big[i] * f + carry;
			big[i] = (unsigned int)t;
			carry = t >> 32;
		}
		if (carry) big[n++] = (unsigned int)carry;
		count -= k;
	}
	char rev[1200];
	int nd = 0;
	while (n > 0) {
		unsigned long rem = 0;
		for (int i = n - 1; i >= 0; i--) {
			unsigned long cur = (rem << 32) | big[i];
			big[i] = (unsigned int)(cur / 1000000000UL);
			rem = cur % 1000000000UL;
		}
		while (n > 0 && big[n - 1] == 0) n--;
		for (int j = 0; j < 9; j++) { rev[nd++] = (char)('0' + rem % 10); rem /= 10; }
	}
	while (nd > 1 && rev[nd - 1] == '0') nd--;
	for (int i = 0; i < nd; i++) digits[i] = rev[nd - 1 - i];
	*dp = nd - frac;
	int lead = 0;
	while (lead < nd - 1 && digits[lead] == '0') lead++;
	if (lead) { memmove(digits, digits + lead, nd - lead); nd -= lead; *dp -= lead; }
	while (nd > 1 && digits[nd - 1] == '0') nd--;
	return nd;
}

static int round_digits(char *d, int nd, int keep, int *dp) {
	if (keep >= nd) return nd;
	if (keep < 0) { d[0] = '0'; *dp = *dp + 1; return 0; }
	int up = 0;
	if (d[keep] > '5') up = 1;
	else if (d[keep] == '5') {
		for (int i = keep + 1; i < nd; i++) if (d[i] != '0') up = 1;
		if (!up) up = keep > 0 && ((d[keep - 1] - '0') & 1);
	}
	nd = keep;
	if (up) {
		int i = keep - 1;
		while (i >= 0 && d[i] == '9') { d[i] = '0'; i--; }
		if (i >= 0) d[i]++;
		else {
			memmove(d + 1, d, nd);
			d[0] = '1';
			nd++;
			*dp = *dp + 1;
		}
	}
	return nd;
}

static char digit_at(char *d, int nd, int i) { return i >= 0 && i < nd ? d[i] : '0'; }

static int put_fixed(char *buf, char *d, int nd, int dp, int prec, int alt) {
	int n = 0;
	if (dp <= 0) buf[n++] = '0';
	else for (int i = 0; i < dp; i++) buf[n++] = digit_at(d, nd, i);
	if (prec > 0 || alt) buf[n++] = '.';
	for (int i = 0; i < prec; i++) buf[n++] = digit_at(d, nd, dp + i);
	return n;
}

static int put_exp(char *buf, char *d, int nd, int x, int prec, int alt, int upper) {
	int n = 0;
	buf[n++] = digit_at(d, nd, 0);
	if (prec > 0 || alt) buf[n++] = '.';
	for (int i = 1; i <= prec; i++) buf[n++] = digit_at(d, nd, i);
	buf[n++] = upper ? 'E' : 'e';
	buf[n++] = x < 0 ? '-' : '+';
	if (x < 0) x = -x;
	if (x >= 100) buf[n++] = (char)('0' + x / 100);
	buf[n++] = (char)('0' + x / 10 % 10);
	buf[n++] = (char)('0' + x % 10);
	return n;
}

static int fmt_double(char *buf, double v, int prec, int conv, int alt, int plus, int space) {
	int n = 0;
	int upper = conv == 'F' || conv == 'E' || conv == 'G';
	unsigned long bits;
	memcpy(&bits, &v, 8);
	if (bits >> 63) { buf[n++] = '-'; v = -v; }
	else if (plus) buf[n++] = '+';
	else if (space) buf[n++] = ' ';
	if (v != v) {
		memcpy(buf + n, upper ? "NAN" : "nan", 3);
		return n + 3;
	}
	if (v - v != 0) {
		memcpy(buf + n, upper ? "INF" : "inf", 3);
		return n + 3;
	}
	if (prec > 350) prec = 350;
	char d[1200];
	int dp;
	int nd = exact_decimal(v, d, &dp);
	int zero = v == 0;
	if (conv == 'f' || conv == 'F') {
		nd = round_digits(d, nd, dp + prec, &dp);
		return n + put_fixed(buf + n, d, nd, dp, prec, alt);
	}
	if (conv == 'e' || conv == 'E') {
		nd = round_digits(d, nd, prec + 1, &dp);
		return n + put_exp(buf + n, d, nd, zero ? 0 : dp - 1, prec, alt, upper);
	}
	if (prec == 0) prec = 1;
	nd = round_digits(d, nd, prec, &dp);
	if (!alt) while (nd > 1 && d[nd - 1] == '0') nd--;
	int x = zero ? 0 : dp - 1;
	int len;
	if (x < -4 || x >= prec) {
		int p = prec - 1;
		if (!alt) { p = nd - 1; if (p < 0) p = 0; }
		len = put_exp(buf + n, d, nd, x, p, alt, upper);
	} else {
		int p = prec - 1 - x;
		if (!alt) { p = nd - dp; if (p < 0) p = 0; }
		len = put_fixed(buf + n, d, nd, dp, p, alt);
	}
	return n + len;
}

static int format(Out *o, const char *fmt, va_list ap) {
	for (; *fmt; fmt++) {
		if (*fmt != '%') { out_ch(o, *fmt); continue; }
		fmt++;
		int left = 0, plus = 0, space = 0, alt = 0, zero = 0;
		for (;; fmt++) {
			if (*fmt == '-') left = 1;
			else if (*fmt == '+') plus = 1;
			else if (*fmt == ' ') space = 1;
			else if (*fmt == '#') alt = 1;
			else if (*fmt == '0') zero = 1;
			else break;
		}
		int width = 0;
		if (*fmt == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } fmt++; }
		else while (isdigit(*fmt)) width = width * 10 + (*fmt++ - '0');
		int prec = -1;
		if (*fmt == '.') {
			fmt++;
			prec = 0;
			if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
			else while (isdigit(*fmt)) prec = prec * 10 + (*fmt++ - '0');
		}
		int lng = 0;
		for (;; fmt++) {
			if (*fmt == 'l' || *fmt == 'z' || *fmt == 't' || *fmt == 'j' || *fmt == 'L' || *fmt == 'q') lng = 1;
			else if (*fmt == 'h') lng = lng == -1 ? -2 : -1;
			else break;
		}
		int c = *fmt;
		if (c == 0) break;
		if (c == '%') { out_ch(o, '%'); continue; }
		if (c == 'c') {
			int ch = va_arg(ap, int);
			if (!left) out_pad(o, ' ', width - 1);
			out_ch(o, ch);
			if (left) out_pad(o, ' ', width - 1);
			continue;
		}
		if (c == 's') {
			const char *s = va_arg(ap, const char *);
			if (!s) s = "(null)";
			int len = 0;
			while (s[len] && (prec < 0 || len < prec)) len++;
			if (!left) out_pad(o, ' ', width - len);
			for (int i = 0; i < len; i++) out_ch(o, s[i]);
			if (left) out_pad(o, ' ', width - len);
			continue;
		}
		if (c == 'n') { int *p = va_arg(ap, int *); *p = (int)o->n; continue; }
		if (c == 'f' || c == 'F' || c == 'e' || c == 'E' || c == 'g' || c == 'G') {
			double d = va_arg(ap, double);
			char tmp[400];
			int len = fmt_double(tmp, d, prec < 0 ? 6 : prec, c, alt, plus, space);
			if (zero && !left && len < width && isdigit(tmp[len - 1])) {
				int skip = (tmp[0] == '-' || tmp[0] == '+' || tmp[0] == ' ');
				for (int i = 0; i < skip; i++) out_ch(o, tmp[i]);
				out_pad(o, '0', width - len);
				for (int i = skip; i < len; i++) out_ch(o, tmp[i]);
				continue;
			}
			if (!left) out_pad(o, ' ', width - len);
			for (int i = 0; i < len; i++) out_ch(o, tmp[i]);
			if (left) out_pad(o, ' ', width - len);
			continue;
		}
		unsigned long v;
		int neg = 0, base = 10, upper = 0;
		const char *prefix = "";
		if (c == 'd' || c == 'i') {
			long sv;
			if (lng == 1) sv = va_arg(ap, long);
			else sv = va_arg(ap, int);
			if (lng == -1) sv = (short)sv;
			if (lng == -2) sv = (signed char)sv;
			if (sv < 0) { neg = 1; v = -(unsigned long)sv; } else v = sv;
		} else if (c == 'u' || c == 'x' || c == 'X' || c == 'o' || c == 'p') {
			if (c == 'p') { v = (unsigned long)va_arg(ap, void *); lng = 1; alt = 1; }
			else if (lng == 1) v = va_arg(ap, unsigned long);
			else v = va_arg(ap, unsigned int);
			if (lng == -1) v = (unsigned short)v;
			if (lng == -2) v = (unsigned char)v;
			if (c == 'x' || c == 'p') base = 16;
			if (c == 'X') { base = 16; upper = 1; }
			if (c == 'o') base = 8;
			if (alt && base == 16 && (v || c == 'p')) prefix = upper ? "0X" : "0x";
			if (alt && base == 8) prefix = "0";
		} else {
			out_ch(o, '%');
			out_ch(o, c);
			continue;
		}
		char digits[32];
		int nd = 0;
		const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
		while (v) { digits[nd++] = dig[v % base]; v /= base; }
		if (nd == 0 && prec != 0) digits[nd++] = '0';
		if (base == 8 && prefix[0] && nd && digits[nd - 1] == '0') prefix = "";
		int nzero = prec > nd ? prec - nd : 0;
		const char *sign = neg ? "-" : plus ? "+" : space ? " " : "";
		int len = (int)strlen(sign) + (int)strlen(prefix) + nzero + nd;
		if (zero && !left && prec < 0) { nzero += width - len; len = width; }
		if (!left) out_pad(o, ' ', width - len);
		for (const char *p = sign; *p; p++) out_ch(o, *p);
		for (const char *p = prefix; *p; p++) out_ch(o, *p);
		out_pad(o, '0', nzero);
		while (nd) out_ch(o, digits[--nd]);
		if (left) out_pad(o, ' ', width - len);
	}
	return (int)o->n;
}

int vfprintf(FILE *f, const char *fmt, va_list ap) {
	Out o;
	o.f = f; o.buf = NULL; o.cap = 0; o.n = 0;
	return format(&o, fmt, ap);
}

int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap) {
	Out o;
	o.f = NULL; o.buf = buf; o.cap = n; o.n = 0;
	int r = format(&o, fmt, ap);
	if (n) buf[o.n < n ? o.n : n - 1] = 0;
	return r;
}

int vsprintf(char *buf, const char *fmt, va_list ap) { return vsnprintf(buf, (size_t)INT_MAX, fmt, ap); }
int vprintf(const char *fmt, va_list ap) { return vfprintf(stdout, fmt, ap); }

int printf(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int r = vfprintf(stdout, fmt, ap);
	va_end(ap);
	return r;
}

int fprintf(FILE *f, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int r = vfprintf(f, fmt, ap);
	va_end(ap);
	return r;
}

int sprintf(char *buf, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int r = vsnprintf(buf, (size_t)INT_MAX, fmt, ap);
	va_end(ap);
	return r;
}

int snprintf(char *buf, size_t n, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int r = vsnprintf(buf, n, fmt, ap);
	va_end(ap);
	return r;
}

void perror(const char *s) {
	if (s && *s) fprintf(stderr, "%s: ", s);
	fprintf(stderr, "%s\n", strerror(errno));
}

int atexit(void (*fn)(void)) {
	if (natexit >= 32) return -1;
	atexit_fns[natexit++] = fn;
	return 0;
}

void exit(int status) {
	while (natexit > 0) atexit_fns[--natexit]();
	fflush(NULL);
	_exit(status);
}

void abort(void) {
	fflush(NULL);
	__syscall(SYS_kill, getpid(), 6);
	_exit(134);
}

void __assert_fail(const char *expr, const char *file, int line) {
	fprintf(stderr, "%s:%d: assertion failed: %s\n", file, line, expr);
	abort();
}
