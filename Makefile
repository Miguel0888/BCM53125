CROSS_COMPILE ?= mipsel-linux-gnu-
CC = $(CROSS_COMPILE)gcc

CFLAGS = -std=c11 -O2 -Wall -Wextra -Werror -march=mips32r2 -mabi=32 -EL
LDFLAGS = -static -no-pie -Wl,--build-id=none

.PHONY: all clean smoke

all: dist/switchmirror

dist/switchmirror: src/switchmirror.c
	mkdir -p dist
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

smoke: dist/switchmirror
	@set +e; output=$$(qemu-mipsel ./dist/switchmirror 2>&1); result=$$?; \
	set -e; printf '%s\n' "$$output"; \
	test $$result -eq 2 && printf '%s\n' "$$output" | grep -q 'Usage: switchmirror'
	@set +e; output=$$(qemu-mipsel ./dist/switchmirror probe 2>&1); result=$$?; \
	set -e; printf '%s\n' "$$output"; \
	test $$result -eq 4 || test $$result -eq 5; \
	printf '%s\n' "$$output" | grep -q 'normal libc startup'; \
	printf '%s\n' "$$output" | grep -Eq 'ioctl failed|Mirror register read failed'
	@set +e; output=$$(qemu-mipsel ./dist/switchmirror restore invalid 1 2 2>&1); result=$$?; \
	set -e; printf '%s\n' "$$output"; test $$result -eq 2

clean:
	rm -rf dist
