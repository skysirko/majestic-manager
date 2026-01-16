CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -pedantic
LDFLAGS ?=

ZIG ?= ./zig-macos-aarch64-0.13.0/zig
ZIG_CACHE ?= ./zig-cache
MAJESTIC_CFLAGS ?= -target arm-linux-musleabihf -static -O2 -DNDEBUG -Wall -Wextra -Wpedantic -Wl,-s

TARGETS = majestic_manager

all: $(TARGETS)

majestic_manager: majestic_manager.c mavlink.c
	ZIG_GLOBAL_CACHE_DIR=$(ZIG_CACHE) ZIG_LOCAL_CACHE_DIR=$(ZIG_CACHE) $(ZIG) cc $(MAJESTIC_CFLAGS) $^ -o $@

clean:
	$(RM) $(TARGETS)

.PHONY: all clean
