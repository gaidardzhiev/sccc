int g;
int arr[10];
int *gp = &g;
int swap(int *a, int *b) { int t = *a; *a = *b; *b = t; return 0; }
int sum(int *p, int n) { int s = 0; for (int i = 0; i < n; i++) s += p[i]; return s; }
int main() {
	int x = 3, y = 4;
	swap(&x, &y);
	if (x != 4 || y != 3) return 1;
	int a[5];
	for (int i = 0; i < 5; i++) a[i] = i * i;
	if (sum(a, 5) != 30) return 2;
	int *p = a;
	p++;
	if (*p != 1) return 3;
	p += 2;
	if (*p != 9 || p - a != 3) return 4;
	if (*(a + 4) != 16 || 4[a] != 16) return 5;
	*gp = 7;
	if (g != 7) return 6;
	for (int i = 0; i < 10; i++) arr[i] = i;
	if (sum(arr, 10) != 45) return 7;
	int **pp = &p;
	**pp = 100;
	if (a[3] != 100) return 8;
	char buf[4];
	char *s = buf;
	*s++ = 'a'; *s++ = 'b'; *s = 0;
	if (buf[0] != 'a' || buf[1] != 98 || buf[2]) return 9;
	int m[3][4];
	for (int i = 0; i < 3; i++) for (int j = 0; j < 4; j++) m[i][j] = i * 10 + j;
	if (m[2][3] != 23 || m[1][0] != 10) return 10;
	if (sizeof(m) != 48 || sizeof(m[0]) != 16 || sizeof(int *) != 8) return 11;
	void *v = a;
	int *q = v;
	if (q[1] != 1) return 12;
	if (p == 0 || !p) return 13;
	p = 0;
	if (p) return 14;
	return 0;
}
