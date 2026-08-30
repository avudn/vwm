VERSION = 1.0
PREFIX = /usr/local
BINPREFIX = $(PREFIX)/bin

CC = cc
CFLAGS = -std=c99 -O2 -Wall -Wextra -pedantic
LDFLAGS = -lX11

SRC = void.c
BIN = void

all: $(BIN)

$(BIN): $(SRC) config.h
	$(CC) $(CFLAGS) -o $(BIN) $(SRC) $(LDFLAGS)

install: $(BIN)
	mkdir -p $(DESTDIR)$(BINPREFIX)
	cp -f $(BIN) $(DESTDIR)$(BINPREFIX)/$(BIN)
	chmod 755 $(DESTDIR)$(BINPREFIX)/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(BINPREFIX)/$(BIN)

clean:
	rm -f $(BIN)

.PHONY: all install uninstall clean
