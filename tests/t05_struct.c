struct point { int x, y; };
typedef struct { char c; long l; short s; } Mixed;
struct node { int val; struct node *next; };
union u { int i; char c[4]; long l; };
enum color { RED, GREEN = 5, BLUE };
typedef enum { A1, A2, A3 } Abc;

struct point mkpoint(int x, int y) { struct point p; p.x = x; p.y = y; return p; }
int dot(struct point a, struct point b) { return a.x * b.x + a.y * b.y; }
struct big { long a, b, c, d; };
struct big mkbig(long v) { struct big b; b.a = v; b.b = v * 2; b.c = v * 3; b.d = v * 4; return b; }
int sumbig(struct big b) { b.a = 1000; return b.b + b.c + b.d; }

struct point gp = { 3, 4 };
struct point gps[] = { {1, 2}, {3, 4}, {5, 6} };
Mixed gm;

int main() {
	struct point p;
	p.x = 1; p.y = 2;
	struct point *pp = &p;
	pp->x = 10;
	if (p.x != 10 || pp->y != 2) return 1;
	if (sizeof(struct point) != 8 || sizeof(Mixed) != 24) return 2;
	struct node n1, n2, n3;
	n1.val = 1; n1.next = &n2;
	n2.val = 2; n2.next = &n3;
	n3.val = 3; n3.next = 0;
	int s = 0;
	for (struct node *n = &n1; n; n = n->next) s += n->val;
	if (s != 6) return 3;
	union u x;
	x.l = 0;
	x.i = 0x41424344;
	if (x.c[0] != 0x44 || sizeof(union u) != 8) return 4;
	if (RED != 0 || GREEN != 5 || BLUE != 6 || A3 != 2) return 5;
	enum color col = BLUE;
	if (col != 6) return 6;
	struct point q = mkpoint(3, 4);
	if (q.x != 3 || q.y != 4) return 7;
	if (dot(q, mkpoint(1, 2)) != 11) return 8;
	struct point r = q;
	r.x = 99;
	if (q.x != 3 || r.x != 99) return 9;
	q = r;
	if (q.x != 99) return 10;
	struct big b = mkbig(5);
	if (b.a != 5 || b.d != 20) return 11;
	if (sumbig(b) != 45 || b.a != 5) return 12;
	if (mkbig(2).c != 6) return 13;
	if (gp.x != 3 || gp.y != 4) return 14;
	if (gps[2].y != 6 || sizeof(gps) != 24) return 15;
	gm.l = 5;
	if (gm.c != 0 || gm.l != 5) return 16;
	struct point arr[3];
	arr[1].x = 7;
	struct point *ap = arr;
	ap[1].y = 8;
	if ((ap + 1)->x + arr[1].y != 15) return 17;
	struct { int a; struct { int b; int c; } in; } nest;
	nest.in.c = 9;
	if (nest.in.c != 9) return 18;
	struct point li = { 5, 6 };
	if (li.x != 5 || li.y != 6) return 19;
	int ai[5] = { 1, 2, 3 };
	if (ai[2] != 3 || ai[3] != 0 || ai[4] != 0) return 20;
	int an[] = { 4, 5, 6, 7 };
	if (sizeof(an) != 16 || an[3] != 7) return 21;
	char str[] = "hello";
	if (sizeof(str) != 6 || str[4] != 'o' || str[5] != 0) return 22;
	char s10[10] = "hi";
	if (s10[1] != 'i' || s10[2] != 0 || s10[9] != 0) return 23;
	struct point pa[] = { {1, 1}, {2, 2} };
	if (pa[1].y != 2 || sizeof(pa) != 16) return 24;
	struct point d = { .y = 5 };
	if (d.x != 0 || d.y != 5) return 25;
	int des[5] = { [3] = 7, 8 };
	if (des[3] != 7 || des[4] != 8 || des[0] != 0) return 26;
	return 0;
}
