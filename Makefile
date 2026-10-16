CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror=vla -pedantic -O2
INCLUDES = -Iinclude $(shell pkg-config --cflags gtk+-3.0 x11 xi xext xrandr xfixes)
LIBS = $(shell pkg-config --libs gtk+-3.0 x11 xi xext xrandr xfixes)

TARGET = bin/phoneoverlay
SRCS = src/main.c src/x11_dom.c src/ui.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS) | bin
	$(CC) $(OBJS) $(LIBS) -o $(TARGET)

src/%.o: src/%.c include/overlay.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

bin:
	mkdir -p bin

clean:
	rm -f $(OBJS) $(TARGET)

test_phase1: $(TARGET)
	@bash tests/test_phase1.sh

.PHONY: all clean test_phase1
