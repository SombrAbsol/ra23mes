CC := $(shell command -v clang >/dev/null 2>&1 && echo clang || echo gcc)
CFLAGS := -O3 -Wall -Wextra -Werror
CPPFLAGS := -I include
SRC_DIR := src
BUILD_DIR := build

TARGET_NAMES := ra2mes ra3mes
EXTENSION := $(if $(filter Windows_NT,$(OS)),.exe)

COMMON_SRCS := $(wildcard $(SRC_DIR)/common/*.c)
COMMON_OBJS := $(patsubst $(SRC_DIR)/common/%.c,$(BUILD_DIR)/common.dir/%.o,$(COMMON_SRCS))

.PHONY: all clean

all: $(addprefix $(BUILD_DIR)/,$(addsuffix $(EXTENSION),$(TARGET_NAMES)))

define RULES
$(1): $(BUILD_DIR)/$(1)$(EXTENSION)

$(BUILD_DIR)/$(1)$(EXTENSION): $(patsubst $(SRC_DIR)/$(1)/%.c,$(BUILD_DIR)/$(1).dir/%.o,$(wildcard $(SRC_DIR)/$(1)/*.c)) $(COMMON_OBJS) | $(BUILD_DIR)
	$(CC) -o $$@ $$^

$(BUILD_DIR)/$(1).dir/%.o: $(SRC_DIR)/$(1)/%.c | $(BUILD_DIR)/$(1).dir
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $$< -o $$@

$(BUILD_DIR)/$(1).dir:
	mkdir -p $$@
endef

$(foreach t,$(TARGET_NAMES),$(eval $(call RULES,$(t))))

$(BUILD_DIR)/common.dir/%.o: $(SRC_DIR)/common/%.c | $(BUILD_DIR)/common.dir
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/common.dir:
	mkdir -p $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)
