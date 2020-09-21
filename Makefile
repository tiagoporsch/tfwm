PREFIX?=$(HOME)

CC:=gcc
CFLAGS:=-Wall -Wextra -O2
INCS:=-I/usr/include/freetype2
LIBS:=-lX11 -lfontconfig -lXft

tfwm: tfwm.c
	$(CC) $(CFLAGS) -o $@ $< $(INCS) $(LIBS)

clean:
	rm --force tfwm

install: tfwm
	mkdir --parents "$(PREFIX)/bin/"
	rm --force "$(PREFIX)/bin/tfwm"
	cp tfwm "$(PREFIX)/bin/"

uninstall:
	rm --force "$(PREFIX)/bin/tfwm"
	rmdir --ignore-fail-on-non-empty "$(PREFIX)/bin"
