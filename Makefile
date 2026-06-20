# SPDX-FileCopyrightText: 2026 SombrAbsol
#
# SPDX-License-Identifier: MIT
CC    := $(shell command -v clang >/dev/null 2>&1 && echo clang || echo gcc)
STRIP := $(shell command -v llvm-strip >/dev/null 2>&1 && echo llvm-strip || echo strip)

SRC_DIR    := src
HEADER_DIR := include
BUILD_DIR  := build
PREFIX     := /usr/local

CFLAGS   := -Wall -Wextra -Werror
CPPFLAGS := -I $(HEADER_DIR)
LDFLAGS  :=
LDLIBS   :=
DEPFLAGS := -MMD -MP

TARGET_NAMES  := ra2mes ra3mes
EXTENSION     := $(if $(filter Windows_NT,$(OS)),.exe)
BUILD_TARGETS := $(addprefix $(BUILD_DIR)/,$(addsuffix $(EXTENSION),$(TARGET_NAMES)))
DO_STRIP      := 1

COMMON_SRCS := $(wildcard $(SRC_DIR)/common/*.c)
HEADERS     := $(wildcard $(HEADER_DIR)/*.h)
COMMON_OBJS := $(patsubst $(SRC_DIR)/common/%.c,$(BUILD_DIR)/common.dir/%.o,$(COMMON_SRCS))
COMMON_DEPS := $(COMMON_OBJS:.o=.d)
FORMAT_SRCS := $(foreach t,$(TARGET_NAMES),$(wildcard $(SRC_DIR)/$(t)/*.c)) $(HEADERS)
STRIP_CMD    = $(if $(DO_STRIP),$(foreach t,$(TARGET_NAMES),$(STRIP) $(BUILD_DIR)/$(t)$(EXTENSION);))

.PHONY: all clean format release native debug install uninstall $(TARGET_NAMES)

all: release

release: CFLAGS += -O3 -DNDEBUG
release: $(BUILD_TARGETS)
	$(STRIP_CMD)

native: CFLAGS  += -O3 -march=native -flto -DNDEBUG
native: LDFLAGS += -flto
native: $(BUILD_TARGETS)
	$(STRIP_CMD)

debug: DO_STRIP :=
debug: CFLAGS   += -Og -g -fsanitize=address,undefined -fno-omit-frame-pointer
debug: LDFLAGS  += -fsanitize=address,undefined
debug: $(BUILD_TARGETS)

define RULES
$(1): $(BUILD_DIR)/$(1)$(EXTENSION)
	$(if $(DO_STRIP),$(STRIP) $(BUILD_DIR)/$(1)$(EXTENSION))

$(BUILD_DIR)/$(1)$(EXTENSION): $$(patsubst $(SRC_DIR)/$(1)/%.c,$(BUILD_DIR)/$(1).dir/%.o,$$(wildcard $(SRC_DIR)/$(1)/*.c)) $(COMMON_OBJS)
	$(CC) $$(LDFLAGS) -o $$@ $$^ $$(LDLIBS)

$(BUILD_DIR)/$(1).dir/%.o: $(SRC_DIR)/$(1)/%.c | $(BUILD_DIR)/$(1).dir
	$(CC) $$(DEPFLAGS) $$(CFLAGS) $$(CPPFLAGS) -c $$< -o $$@

DEPS_$(1) := $$(patsubst $(SRC_DIR)/$(1)/%.c,$(BUILD_DIR)/$(1).dir/%.d,$$(wildcard $(SRC_DIR)/$(1)/*.c))
-include $$(DEPS_$(1))
endef

$(foreach t,$(TARGET_NAMES),$(eval $(call RULES,$(t))))

$(BUILD_DIR)/%.dir:
	mkdir -p $@

$(BUILD_DIR)/common.dir/%.o: $(SRC_DIR)/common/%.c | $(BUILD_DIR)/common.dir
	$(CC) $(DEPFLAGS) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

-include $(COMMON_DEPS)

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	$(foreach t,$(TARGET_NAMES),install -m 755 $(BUILD_DIR)/$(t)$(EXTENSION) $(DESTDIR)$(PREFIX)/bin/$(t)$(EXTENSION);)

uninstall:
	$(foreach t,$(TARGET_NAMES),rm -f $(DESTDIR)$(PREFIX)/bin/$(t)$(EXTENSION);)

format:
	@command -v clang-format >/dev/null 2>&1 || { echo "clang-format not found"; exit 1; }
	clang-format -i $(FORMAT_SRCS)

clean:
	rm -rf $(BUILD_DIR)
