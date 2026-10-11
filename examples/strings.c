/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

static void reverse(char *s) {
	for (size_t i = 0, j = strlen(s); i + 1 < j; i++, j--) {
		char t = s[i];
		s[i] = s[j - 1];
		s[j - 1] = t;
	}
}

static int palindrome(const char *s) {
	const char *e = s + strlen(s);
	while (s < e) {
		if (!isalnum((unsigned char)*s)) { s++; continue; }
		if (!isalnum((unsigned char)e[-1])) { e--; continue; }
		if (tolower((unsigned char)*s++) != tolower((unsigned char)*--e)) return 0;
	}
	return 1;
}

static void caesar(char *s, int k) {
	for (; *s; s++) {
		if (isupper((unsigned char)*s)) *s = (char)('A' + (*s - 'A' + k + 26) % 26);
		else if (islower((unsigned char)*s)) *s = (char)('a' + (*s - 'a' + k + 26) % 26);
	}
}

static int count_sub(const char *s, const char *sub) {
	int n = 0;
	for (const char *p = s; (p = strstr(p, sub)) != NULL; p++) n++;
	return n;
}

int main(void) {
	char buf[128];
	const char *tests[] = { "A man, a plan, a canal: Panama", "hello", "Was it a car or a cat I saw?", "No lemon, no melon", "abcab" };
	for (int i = 0; i < 5; i++) printf("%-32s palindrome=%d\n", tests[i], palindrome(tests[i]));

	strcpy(buf, "stressed");
	reverse(buf);
	printf("reverse(stressed) = %s\n", buf);

	strcpy(buf, "Attack at Dawn!");
	caesar(buf, 3);
	printf("caesar +3: %s\n", buf);
	caesar(buf, -3);
	printf("caesar -3: %s\n", buf);

	strcpy(buf, "alpha, beta;gamma  delta,,epsilon");
	printf("tokens:");
	for (char *t = strtok(buf, ",; "); t; t = strtok(NULL, ",; ")) printf(" [%s]", t);
	printf("\n");

	printf("count of \"ana\" in \"bananarama ananas\" = %d\n", count_sub("bananarama ananas", "ana"));
	printf("strspn = %zu, strcspn = %zu\n", strspn("123abc", "0123456789"), strcspn("hello world", " "));
	printf("strchr = %s, strrchr = %s\n", strchr("a/b/c", '/'), strrchr("a/b/c", '/'));
	printf("strcmp: %d %d %d\n", strcmp("abc", "abd") < 0, strcmp("b", "a") > 0, strcmp("same", "same") == 0);

	snprintf(buf, sizeof buf, "[%5s|%-5s|%.2s|%c|%5.1f%%]", "ab", "cd", "efgh", 'Z', 99.44);
	printf("snprintf: %s\n", buf);
	int n = snprintf(buf, 8, "%s", "truncated output");
	printf("snprintf returned %d, kept \"%s\"\n", n, buf);

	char *end;
	long v = strtol("  -0x1F rest", &end, 0);
	printf("strtol: %ld, rest \"%s\"\n", v, end);
	printf("atoi: %d, atof: %g\n", atoi("  42abc"), atof("6.02e23"));
	memset(buf, '=', 20);
	buf[20] = 0;
	printf("%s\n", buf);
	return 0;
}
