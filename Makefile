CC ?= gcc
CFLAGS ?= -std=gnu11 -Wall -Wextra -O2 -g
INCLUDES = $(shell pkg-config --cflags gtk+-3.0 x11 xi xext xrandr)
LIBS = $(shell pkg-config --libs gtk+-3.0 x11 xi xext xrandr) -lm

TARGET = bin/phoneoverlay
SRCS = src/main.c src/window.c src/hotkey.c src/raise.c src/xutil.c src/input.c
OBJS = $(SRCS:.c=.o)

ADVERSARIES = tests/stackcheck tests/adversary/adv1_ewmh tests/adversary/adv2_override tests/adversary/adv3_focuslock

all: $(TARGET)

$(TARGET): $(OBJS) | bin
	$(CC) $(OBJS) $(LIBS) -o $(TARGET)

src/%.o: src/%.c src/window.h src/hotkey.h src/raise.h src/xutil.h src/input.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

bin:
	mkdir -p bin

adversaries: $(ADVERSARIES)

tests/stackcheck: tests/stackcheck.c
	$(CC) $(CFLAGS) $< -lX11 -o $@

tests/adversary/adv1_ewmh: tests/adversary/adv1_ewmh.c
	$(CC) $(CFLAGS) $< -lX11 -o $@

tests/adversary/adv2_override: tests/adversary/adv2_override.c
	$(CC) $(CFLAGS) $< -lX11 -o $@

tests/adversary/adv3_focuslock: tests/adversary/adv3_focuslock.c
	$(CC) $(CFLAGS) $< -lX11 -o $@

clean:
	rm -rf bin $(OBJS) $(ADVERSARIES) /tmp/phoneoverlay*.log

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	@bash tests/acceptance_phase1.sh

test2: $(TARGET) adversaries
	@bash tests/run_adversaries.sh

test3: $(TARGET) adversaries
	@bash tests/run_phase3_tests.sh

.PHONY: all clean run test test2 test3 adversaries
