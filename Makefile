# Builds Diary for Linux with GTK 3:
#   make                   the diary program
#   make install           into ~/.local (program, menu entry and icons)
#   make install PREFIX=/usr/local   for everyone (as root)
#   make uninstall         removes it again (with the same PREFIX)

PREFIX  ?= $(HOME)/.local
CC      ?= gcc
CFLAGS  ?= -O2
CFLAGS  += -Wall $(shell pkg-config --cflags gtk+-3.0)
LIBS     = $(shell pkg-config --libs gtk+-3.0)

OBJECTS = diary.o dates.o server.o store.o undo.o
HEADERS = dates.h server.h store.h undo.h
SIZES   = 16 32 48 64 128

all: diary

diary: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LIBS)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

install: diary
	install -D -m 755 diary $(DESTDIR)$(PREFIX)/bin/diary
	sed 's|^Exec=.*|Exec=$(PREFIX)/bin/diary|' diary.desktop > diary.desktop.tmp
	install -D -m 644 diary.desktop.tmp $(DESTDIR)$(PREFIX)/share/applications/diary.desktop
	rm -f diary.desktop.tmp
	for size in $(SIZES); do \
	    install -D -m 644 icons/diary-$$size.png \
	        $(DESTDIR)$(PREFIX)/share/icons/hicolor/$${size}x$${size}/apps/diary.png; \
	done
	-gtk-update-icon-cache -q -t $(DESTDIR)$(PREFIX)/share/icons/hicolor 2>/dev/null

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/diary $(DESTDIR)$(PREFIX)/share/applications/diary.desktop
	for size in $(SIZES); do \
	    rm -f $(DESTDIR)$(PREFIX)/share/icons/hicolor/$${size}x$${size}/apps/diary.png; \
	done

clean:
	rm -f diary $(OBJECTS) diary.desktop.tmp

.PHONY: all install uninstall clean
