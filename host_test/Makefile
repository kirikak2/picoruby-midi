# Host build for picoruby-midi parser unit tests.
# No FreeRTOS / ESP-IDF — just gcc/clang + libc.
#
#     make            # build + run
#     make clean

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-unused-parameter -std=c99 -I..
LDFLAGS ?=

SRC     = ../src/midi_parser.c test_midi_parser.c
BIN     = test_midi_parser

all: run

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)

.PHONY: all run clean
