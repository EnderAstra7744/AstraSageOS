# AstraScript engine 2 - builds the test host and runs the .as tests.
CC ?= gcc
CFLAGS = -O1 -g -Wall -Wextra -Iinclude -fno-stack-protector
SRC = $(filter-out src/lib_data.c,$(wildcard src/*.c)) src/lib_data.c
all: build/as2_host
src/lib_data.c: $(wildcard include/lib/*.ash) tools/embed_lib.py
	python3 tools/embed_lib.py
build/as2_host: $(SRC) tests/host_main.c $(wildcard include/*.h)
	mkdir -p build && $(CC) $(CFLAGS) $(SRC) tests/host_main.c -o $@
test: build/as2_host
	python3 tests/run_tests.py
clean:
	rm -rf build
