CROSS_COMPILE ?= mipsel-linux-gnu-
CC = $(CROSS_COMPILE)gcc

CFLAGS = -std=c99 -O2 -Wall -Wextra -Werror -march=mips32r2 -mabi=32 -EL -msoft-float \
         -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -mno-abicalls -G0
LDFLAGS = -nostdlib -static -no-pie -Wl,-Ttext-segment=0x400000 -Wl,-e,_start -Wl,--build-id=none -Wl,-z,noexecstack

.PHONY: all clean smoke

all: dist/switchmirror

dist/switchmirror: src/switchmirror.c
	mkdir -p dist
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

smoke: dist/switchmirror
	@set +e; output=$$(qemu-mipsel ./dist/switchmirror 2>&1); result=$$?; \
	set -e; printf '%s\n' "$$output"; \
	test $$result -eq 2 && printf '%s\n' "$$output" | grep -q 'Usage: switchmirror'

clean:
	rm -rf dist
