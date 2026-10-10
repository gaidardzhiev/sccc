#include <stdio.h>

enum { PUSH, ADD, SUB, MUL, LT, JMP, JZ, LOAD, STORE, PRINT, HALT, DUP, POP, CALL, RET };

static const char *names[] = { "push", "add", "sub", "mul", "lt", "jmp", "jz", "load", "store", "print", "halt", "dup", "pop", "call", "ret" };

typedef struct {
	long stack[256];
	int sp;
	long vars[16];
	int calls[64];
	int csp;
	long steps;
} VM;

static void run(const long *code, int trace) {
	VM vm = { { 0 }, 0, { 0 }, { 0 }, 0, 0 };
	int pc = 0;
	for (;;) {
		long op = code[pc++], a, b;
		vm.steps++;
		if (trace) printf("  %3d %-5s sp=%d\n", pc - 1, names[op], vm.sp);
		switch (op) {
		case PUSH: vm.stack[vm.sp++] = code[pc++]; break;
		case ADD: b = vm.stack[--vm.sp]; a = vm.stack[--vm.sp]; vm.stack[vm.sp++] = a + b; break;
		case SUB: b = vm.stack[--vm.sp]; a = vm.stack[--vm.sp]; vm.stack[vm.sp++] = a - b; break;
		case MUL: b = vm.stack[--vm.sp]; a = vm.stack[--vm.sp]; vm.stack[vm.sp++] = a * b; break;
		case LT: b = vm.stack[--vm.sp]; a = vm.stack[--vm.sp]; vm.stack[vm.sp++] = a < b; break;
		case JMP: pc = (int)code[pc]; break;
		case JZ: if (vm.stack[--vm.sp] == 0) pc = (int)code[pc]; else pc++; break;
		case LOAD: vm.stack[vm.sp++] = vm.vars[code[pc++]]; break;
		case STORE: vm.vars[code[pc++]] = vm.stack[--vm.sp]; break;
		case PRINT: printf("%ld\n", vm.stack[--vm.sp]); break;
		case DUP: vm.stack[vm.sp] = vm.stack[vm.sp - 1]; vm.sp++; break;
		case POP: vm.sp--; break;
		case CALL: vm.calls[vm.csp++] = pc + 1; pc = (int)code[pc]; break;
		case RET: pc = vm.calls[--vm.csp]; break;
		case HALT: printf("halted after %ld steps\n", vm.steps); return;
		}
	}
}

int main(void) {
	static const long countdown[] = {
		PUSH, 3, STORE, 0,
		LOAD, 0, JZ, 20,
		LOAD, 0, PRINT,
		LOAD, 0, PUSH, 1, SUB, STORE, 0, JMP, 4,
		HALT,
	};
	static const long factorials[] = {
		PUSH, 1, STORE, 0,
		PUSH, 1, STORE, 1,
		LOAD, 0, PUSH, 16, LT, JZ, 35,
		LOAD, 1, LOAD, 0, MUL, STORE, 1,
		LOAD, 1, PRINT,
		LOAD, 0, PUSH, 1, ADD, STORE, 0,
		JMP, 8,
		HALT, HALT,
	};
	static const long subroutine[] = {
		PUSH, 7, CALL, 9,
		PUSH, 12, CALL, 9,
		HALT,
		DUP, MUL, DUP, PRINT, POP, RET,
	};
	printf("countdown (traced):\n");
	run(countdown, 1);
	printf("factorials:\n");
	run(factorials, 0);
	printf("subroutine:\n");
	run(subroutine, 0);
	return 0;
}
