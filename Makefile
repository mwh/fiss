PREFIX = $(HOME)/.local
INSTALL_DIR = $(PREFIX)/bin

all: fiss

fiss: fiss.c util.c util.h
	$(CC) -o fiss util.c fiss.c

install: all
	cp -f fiss $(INSTALL_DIR)

test: fiss
	./fiss -f

clean:
	rm -f fiss

release: clean
	(cd .. && \
	mkdir fiss-$(VERSION) && \
	cp -Ra fiss/* fiss-$(VERSION) && \
	tar cjf fiss-$(VERSION).tar.bz2 fiss-$(VERSION) && \
	tar cv fiss-$(VERSION) | lzma -c > fiss-$(VERSION).tar.lzma )
