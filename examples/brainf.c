#include <stdio.h>
#include <string.h>

static unsigned char tape[30000];

static void run(const char *prog) {
	unsigned char *p = tape;
	memset(tape, 0, sizeof tape);
	for (const char *pc = prog; *pc; pc++) {
		switch (*pc) {
		case '>': p++; break;
		case '<': p--; break;
		case '+': ++*p; break;
		case '-': --*p; break;
		case '.': putchar(*p); break;
		case '[':
			if (!*p)
				for (int depth = 1; depth; ) {
					pc++;
					depth += (*pc == '[') - (*pc == ']');
				}
			break;
		case ']':
			if (*p)
				for (int depth = 1; depth; ) {
					pc--;
					depth += (*pc == ']') - (*pc == '[');
				}
			break;
		}
	}
}

int main(void) {
	run("++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+.>++.");
	run("++++++++++[>+>+++>+++++++>++++++++++<<<<-]>>>++.>+.+++++++..+++.<<++.>+++++++++++++++.>.+++.------.--------.<<+.<.");
	run("++++++++[>++++++++<-]>+.+.+.+.+.+.+.+.+.+.+.+.+.+.+.+.+.+.+.+.+.+.+.+.+.[-]++++++++++.");
	return 0;
}
