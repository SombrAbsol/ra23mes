# SPDX-FileCopyrightText: 2026 SombrAbsol
#
# SPDX-License-Identifier: MIT

CC    := $(shell command -v clang >/dev/null 2>&1 && echo clang || echo gcc)
STRIP := $(shell command -v llvm-strip >/dev/null 2>&1 && echo llvm-strip || echo strip)

SRC_DIR    := src
HEADER_DIR := include
BUILD_DIR  := build
PREFIX     := /usr/local

STATIC ?= 0
ifeq ($(shell uname -s),Darwin)
ifeq ($(STATIC),1)
$(error Static linking is not supported on macOS)
endif
endif

CFLAGS   := -Wall -Wextra -Werror
CPPFLAGS := -I $(HEADER_DIR) $(if $(filter 1,$(STATIC)),-DSTATIC_LINK)
LDFLAGS   = $(if $(filter 1,$(STATIC)),-static)
LDLIBS   :=
DEPFLAGS := -MMD -MP

ifeq ($(OS),Windows_NT)
SANITIZE := -fsanitize=undefined -fsanitize-trap=undefined
LTO_LD   := $(if $(findstring clang,$(CC)),-fuse-ld=lld)
else
SANITIZE := -fsanitize=address,undefined
LTO_LD   :=
endif

ifeq ($(STATIC),1)
SANITIZE := -fsanitize=undefined -fsanitize-trap=undefined
endif

TARGET_NAMES := ra2mes ra3mes
EXTENSION    := $(if $(filter Windows_NT,$(OS)),.exe,)
TARGETS      := $(foreach t,$(TARGET_NAMES),$(BUILD_DIR)/$(t)$(EXTENSION))

objects_of = $(patsubst $(SRC_DIR)/$(1)/%.c,$(BUILD_DIR)/$(1).dir/%.o,$(wildcard $(SRC_DIR)/$(1)/*.c))

SRCS        := $(foreach d,common $(TARGET_NAMES),$(wildcard $(SRC_DIR)/$(d)/*.c))
HEADERS     := $(wildcard $(HEADER_DIR)/*.h)
COMMON_OBJS := $(call objects_of,common)
OBJS        := $(foreach d,common $(TARGET_NAMES),$(call objects_of,$(d)))
DEPS        := $(OBJS:.o=.d)

$(if $(SRCS),,$(error No .c files found in $(SRC_DIR)))

.PHONY: all clean format release native debug install uninstall FORCE $(TARGET_NAMES)

all: release

define TARGET_RULES
$(1): $(BUILD_DIR)/$(1)$(EXTENSION)
	$(STRIP) $(BUILD_DIR)/$(1)$(EXTENSION)

$(BUILD_DIR)/$(1)$(EXTENSION): $(call objects_of,$(1)) $(COMMON_OBJS)
	$$(CC) $$(LDFLAGS) -o $$@ $$^ $$(LDLIBS)
endef

define OBJECT_RULE
$(BUILD_DIR)/$(1).dir/%.o: $(SRC_DIR)/$(1)/%.c
	@mkdir -p $$(@D)
	$$(CC) $$(DEPFLAGS) $$(CFLAGS) $$(CPPFLAGS) $$(VERSION_FLAGS) -c $$< -o $$@
endef

$(foreach t,$(TARGET_NAMES),$(eval $(call TARGET_RULES,$(t))))
$(foreach d,common $(TARGET_NAMES),$(eval $(call OBJECT_RULE,$(d))))

-include $(DEPS)

$(BUILD_DIR)/common.dir/cli.o: FORCE
$(BUILD_DIR)/common.dir/cli.o: VERSION_FLAGS := -DBUILD_DATE=$(shell date -u +%Y.%m.%d-%H:%M:%S)

$(OBJS): $(BUILD_DIR)/flags
$(BUILD_DIR)/flags: FORCE
	@mkdir -p $(@D)
	@flags='$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS)'; \
		[ "$$(cat $@ 2>/dev/null)" = "$$flags" ] || echo "$$flags" > $@

release $(TARGET_NAMES): CFLAGS   += -O3 -DNDEBUG
release $(TARGET_NAMES): CPPFLAGS += -DMAKE_RECIPE=release
release: $(TARGETS)
	$(STRIP) $(TARGETS)

native: CFLAGS   += -O3 -march=native -flto -DNDEBUG
native: CPPFLAGS += -DMAKE_RECIPE=native
native: LDFLAGS  += -flto $(LTO_LD)
native: $(TARGETS)
	$(STRIP) $(TARGETS)

debug: CFLAGS   += -Og -g $(SANITIZE) -fno-omit-frame-pointer
debug: CPPFLAGS += -DMAKE_RECIPE=debug
debug: LDFLAGS  += $(SANITIZE)
debug: $(TARGETS)

install: $(foreach t,$(TARGET_NAMES),$(if $(wildcard $(BUILD_DIR)/$(t)$(EXTENSION)),,$(t)))
	install -d $(DESTDIR)$(PREFIX)/bin
	$(foreach t,$(TARGET_NAMES),install -m 755 $(BUILD_DIR)/$(t)$(EXTENSION) $(DESTDIR)$(PREFIX)/bin/$(t)$(EXTENSION);)

uninstall:
	$(foreach t,$(TARGET_NAMES),rm -f $(DESTDIR)$(PREFIX)/bin/$(t)$(EXTENSION);)

format:
	@command -v clang-format >/dev/null 2>&1 || \
		{ echo "clang-format not found"; exit 1; }
	clang-format -i $(SRCS) $(HEADERS)

clean:
	rm -rf $(BUILD_DIR)
