CC ?= gcc
CFLAGS ?= -std=gnu11 -Wall -Wextra -O2 -g
INCLUDES = $(shell pkg-config --cflags gtk+-3.0 x11 xi xext xrandr)
LIBS = $(shell pkg-config --libs gtk+-3.0 x11 xi xext xrandr) -lm

TARGET = bin/phoneoverlay
SRCS = src/main.c src/window.c src/hotkey.c src/xutil.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS) | bin
	$(CC) $(OBJS) $(LIBS) -o $(TARGET)

src/%.o: src/%.c src/window.h src/hotkey.h src/xutil.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

bin:
	mkdir -p bin

clean:
	rm -rf bin $(OBJS) /tmp/phoneoverlay*.log

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	@bash tests/acceptance_phase1.sh

.PHONY: all clean run test
