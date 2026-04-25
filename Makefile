# SPDX-FileCopyrightText: 2026 SombrAbsol
#
# SPDX-License-Identifier: MIT
CC       := $(shell command -v clang >/dev/null 2>&1 && echo clang || echo gcc)
CFLAGS   := -O3 -Wall -Wextra -Werror -MMD -MP
CPPFLAGS := -I include
LDFLAGS  :=
LDLIBS   :=

SRC_DIR   := src
BUILD_DIR := build
PREFIX    := /usr/local

TARGET_NAMES := ra2mes ra3mes
EXTENSION    := $(if $(filter Windows_NT,$(OS)),.exe)

COMMON_SRCS := $(wildcard $(SRC_DIR)/common/*.c)
COMMON_OBJS := $(patsubst $(SRC_DIR)/common/%.c,$(BUILD_DIR)/common.dir/%.o,$(COMMON_SRCS))
COMMON_DEPS := $(COMMON_OBJS:.o=.d)

.PHONY: all clean install $(TARGET_NAMES)

all: $(addprefix $(BUILD_DIR)/,$(addsuffix $(EXTENSION),$(TARGET_NAMES)))

define RULES
$(1): $(BUILD_DIR)/$(1)$(EXTENSION)

$(BUILD_DIR)/$(1)$(EXTENSION): $$(patsubst $(SRC_DIR)/$(1)/%.c,$(BUILD_DIR)/$(1).dir/%.o,$$(wildcard $(SRC_DIR)/$(1)/*.c)) $(COMMON_OBJS) | $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $$@ $$^ $(LDLIBS)

$(BUILD_DIR)/$(1).dir/%.o: $(SRC_DIR)/$(1)/%.c | $(BUILD_DIR)/$(1).dir
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $$< -o $$@

$(BUILD_DIR)/$(1).dir:
	mkdir -p $$@

DEPS_$(1) := $$(patsubst $(SRC_DIR)/$(1)/%.c,$(BUILD_DIR)/$(1).dir/%.d,$$(wildcard $(SRC_DIR)/$(1)/*.c))
-include $$(DEPS_$(1))
endef

$(foreach t,$(TARGET_NAMES),$(eval $(call RULES,$(t))))

$(BUILD_DIR)/common.dir/%.o: $(SRC_DIR)/common/%.c | $(BUILD_DIR)/common.dir
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/common.dir:
	mkdir -p $@

$(BUILD_DIR):
	mkdir -p $@

-include $(COMMON_DEPS)

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	$(foreach t,$(TARGET_NAMES),install -m 755 $(BUILD_DIR)/$(t)$(EXTENSION) $(DESTDIR)$(PREFIX)/bin/$(t)$(EXTENSION);)

clean:
	rm -rf $(BUILD_DIR)
