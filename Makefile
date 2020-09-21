CC:=gcc
CFLAGS:=-Wall -Wextra -O2
INCS:=-I/usr/include/freetype2
LIBS:=-lX11 -lfontconfig -lXft

tfwm: tfwm.c
	$(CC) $(CFLAGS) -o $@ $< $(INCS) $(LIBS)

clean:
	rm -f tfwm

install: tfwm
	rm -f ~/bin/tfwm
	cp tfwm ~/bin/
