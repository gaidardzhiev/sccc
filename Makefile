CC=gcc
BIN=sccc
FLAGS=-std=c99 -pedantic -Wall -Wextra -O2

.PHONY: all embed test bootstrap check examples fuzz clean

all: $(BIN)

$(BIN): sccc.c builtin.h
	$(CC) -o $@ sccc.c $(FLAGS)

embed:
	tools/embed.sh

test:
	$(CC) -o test_sccc test_sccc.c

bootstrap: $(BIN)
	./$(BIN) sccc.c -o sccc2
	./sccc2 sccc.c -o sccc3
	cmp sccc2 sccc3 && echo "bootstrap verified: sccc2 == sccc3"

check: $(BIN) test bootstrap
	./test_sccc ./$(BIN) | tail -1
	tests/run.sh ./$(BIN)
	./test_sccc ./sccc2 | tail -1
	tests/run.sh ./sccc2
	examples/run.sh ./$(BIN)
	examples/run.sh ./sccc2

examples: $(BIN)
	examples/run.sh ./$(BIN)

fuzz: $(BIN)
	tools/fuzz_expr.py ./$(BIN) 50

clean:
	rm -rf $(BIN) test_sccc sccc2 sccc3
