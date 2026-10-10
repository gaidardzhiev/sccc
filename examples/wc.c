#include <stdio.h>
#include <ctype.h>

int main(void) {
	long lines = 0, words = 0, chars = 0, longest = 0, cur = 0;
	int c, inword = 0;
	long freq[26] = { 0 };
	while ((c = getchar()) != EOF) {
		chars++;
		if (c == '\n') {
			lines++;
			if (cur > longest) longest = cur;
			cur = 0;
		} else cur++;
		if (isalpha(c)) freq[tolower(c) - 'a']++;
		if (isspace(c)) inword = 0;
		else if (!inword) {
			inword = 1;
			words++;
		}
	}
	printf("%ld lines, %ld words, %ld chars, longest line %ld\n", lines, words, chars, longest);
	for (int i = 0; i < 26; i++)
		if (freq[i]) printf("%c %3ld %.*s\n", 'a' + i, freq[i], (int)freq[i], "**************************************************************************************************************");
	return 0;
}
