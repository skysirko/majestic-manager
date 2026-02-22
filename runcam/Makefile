CFLAGS ?= -O2 -Wall -Wextra -pedantic
LDFLAGS ?=

ZIG ?= ./zig-macos-aarch64-0.13.0/zig
ZIG_CACHE ?= ./zig-cache
MAJESTIC_CFLAGS ?= -target arm-linux-musleabihf -static -O2 -DNDEBUG -Wall -Wextra -Wpedantic -Wl,-s

LIBYAML_DIR = third_party/libyaml-0.2.5
LIBYAML_SRCS = \
	$(LIBYAML_DIR)/src/api.c \
	$(LIBYAML_DIR)/src/dumper.c \
	$(LIBYAML_DIR)/src/emitter.c \
	$(LIBYAML_DIR)/src/loader.c \
	$(LIBYAML_DIR)/src/parser.c \
	$(LIBYAML_DIR)/src/reader.c \
	$(LIBYAML_DIR)/src/scanner.c \
	$(LIBYAML_DIR)/src/writer.c

MAJESTIC_SOURCES = \
	majestic_manager.c \
	matek_mavlink.c \
	majestic_config.c \
	$(LIBYAML_SRCS)

MAJESTIC_CFLAGS += -I$(LIBYAML_DIR)/include -I$(LIBYAML_DIR)/src -DHAVE_CONFIG_H=1

TARGETS = majestic_manager

all: $(TARGETS)

majestic_manager: $(MAJESTIC_SOURCES)
	ZIG_GLOBAL_CACHE_DIR=$(ZIG_CACHE) ZIG_LOCAL_CACHE_DIR=$(ZIG_CACHE) $(ZIG) cc $(MAJESTIC_CFLAGS) $^ -o $@

clean:
	$(RM) $(TARGETS)

.PHONY: all clean
