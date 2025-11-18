# User-configurable installation prefix
PREFIX ?= /usr/local
BINDIR := $(PREFIX)/bin

# Your program name and sources
TARGET := mowz
SRCS := mowz.c
OBJS := $(SRCS:.c=.o)

# Compiler flags
CFLAGS := -O2 -Wall
LDLIBS :=

# Default rule: build the program
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

install: $(TARGET)
	mkdir -p $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all install uninstall clean

