ROOT := ..
TOOL := $(ROOT)/tools/sdcc-z80-bin

SOURCES := test_main.c $(wildcard flags/*.c) $(wildcard games/*.c)
BINS := $(SOURCES:%.c=bin/%.bin)
ASMS := $(SOURCES:%.c=asm/%.asm)

.PHONY: all clean

all: $(BINS)

$(ROOT)/z80-asm: $(ROOT)/src/main.c
	$(MAKE) -C $(ROOT) z80-asm

bin/%.bin: %.c $(TOOL) $(ROOT)/z80-asm
	mkdir -p $(dir $@) $(dir asm/$*.asm)
	$(TOOL) -o $@ --asm-out asm/$*.asm $<

clean:
	rm -rf bin asm
