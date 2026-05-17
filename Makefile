CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2
TARGET := z80-asm
SRCS := src/main.c

.PHONY: all test c-programs clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

test: $(TARGET)
	./tests/run_tests.sh

c-programs: $(TARGET)
	$(MAKE) -C c_programs

clean:
	rm -f $(TARGET)
	rm -rf tests/out
	$(MAKE) -C c_programs clean
