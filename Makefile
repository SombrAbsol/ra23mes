CC := $(shell command -v clang >/dev/null 2>&1 && echo clang || echo gcc)
CFLAGS := -O3 -Wall -Wextra -Werror

ifeq ($(OS),Windows_NT)
	EXT := .exe
	CFLAGS += -D_CRT_SECURE_NO_WARNINGS
else
	EXT :=
endif

RA2_TARGET := ra2mes$(EXT)
RA3_TARGET := ra3mes$(EXT)

COMMON_SRCS := utils.c
RA2_SRCS := ra2mes.c $(COMMON_SRCS)
RA3_SRCS := ra3mes.c $(COMMON_SRCS)

RA2_OBJS := $(RA2_SRCS:.c=.o)
RA3_OBJS := $(RA3_SRCS:.c=.o)

.PHONY: all clean

all: $(RA2_TARGET) $(RA3_TARGET)

$(RA2_TARGET): $(RA2_OBJS)
	$(CC) -o $@ $^

$(RA3_TARGET): $(RA3_OBJS)
	$(CC) -o $@ $^

utils.o: utils.c utils.h
	$(CC) $(CFLAGS) -c $< -o $@

ra2mes.o: ra2mes.c ra2mes.h utils.h
	$(CC) $(CFLAGS) -c $< -o $@

ra3mes.o: ra3mes.c ra3mes.h utils.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(RA2_OBJS) $(RA3_OBJS) $(RA2_TARGET) $(RA3_TARGET)
