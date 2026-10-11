# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

CC=gcc
BIN=sccc
STAGE2=sccc2
STAGE3=sccc3
TESTC=test_sccc
FLAGS=-std=c99 -pedantic -Wall -Wextra -O2

.PHONY: all embed test bootstrap check examples baremetal fuzz clean

all: $(BIN)

$(BIN): sccc.c builtin.h
	$(CC) -o $@ sccc.c $(FLAGS)

embed:
	tools/embed.sh

test:
	$(CC) -o $(TESTC) test_sccc.c

bootstrap: $(BIN)
	./$(BIN) sccc.c -o $(STAGE2)
	./$(STAGE2) sccc.c -o $(STAGE3)
	cmp $(STAGE2) $(STAGE3) && printf "bootstrap verified: sccc2 == sccc3\n"

check: $(BIN) test bootstrap
	./$(TESTC) ./$(BIN) | tail -1
	tests/run.sh ./$(BIN)
	./$(TESTC) ./$(STAGE2) | tail -1
	tests/run.sh ./$(STAGE2)
	examples/run.sh ./$(BIN)
	examples/run.sh ./$(STAGE2)
	baremetal/run.sh ./$(BIN)

examples: $(BIN)
	examples/run.sh ./$(BIN)

baremetal: $(BIN)
	baremetal/run.sh ./$(BIN)

fuzz: $(BIN)
	tools/fuzz_expr.py ./$(BIN) 50

clean:
	rm -rf $(BIN) $(TESTC) $(STAGE2) $(STAGE3)
