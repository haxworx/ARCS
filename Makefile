#!/usr/bin/make -f

CC      = gcc -std=gnu99
CFLAGS  = -Wall -Wextra -Werror -g
#LDFLAGS = -s

PREFIX  = /usr/local

ifdef WINDIR
LDLIBS  = -lws2_32
EXE     = .exe
else
EXE     = 
endif

ELFS    = arcs sshc
SCRIPTS = conflicts pacify git_commit_all
EXES    = $(ELFS) $(SCRIPTS)

ELFSEXE = $(shell for A in $(ELFS); do echo $$A$(EXE); done)
EXESEXE = $(ELFSEXE) $(SCRIPTS)
MANGZ   = $(shell for A in $(EXES); do echo $$A.1.gz; done)
MANTXT  = $(shell for A in $(EXES); do echo $$A.1.txt; done)

build: $(ELFSEXE) $(MANGZ) $(MANTXT)

arcs$(EXE): arcs.c arcs_backend.c arcs_backend.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o arcs$(EXE) arcs_backend.c arcs.c $(LDLIBS)

sshc$(EXE): sshc.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o sshc$(EXE) sshc.c

install: build instdirs
	install $(EXESEXE) "$(PREFIX)/bin"
	install -m 644 $(MANGZ) "$(PREFIX)/share/man/man1/"

uninstall:
	for I in $(EXESEXE); do rm -f "$(PREFIX)/bin/$$I"; done
	for I in $(MANGZ); do rm -f "$(PREFIX)/share/man/man1/$$I"; done

devinst: build instdirs
	for I in $(EXESEXE); do ln -sf "$$PWD/$$I" "$(PREFIX)/bin"; done
	for I in $(MANGZ); do ln -sf "$$PWD/$$I" "$(PREFIX)/share/man/man1"; done

clean:
	rm -f $(ELFSEXE) $(MANGZ) $(MANTXT)
	for I in $(ELFS); do rm -f .$$I.exe; done
	for I in $(MANGZ) $(MANTXT); do rm -f .$$I; done

instdirs:
	mkdir -p "$(PREFIX)/bin" "$(PREFIX)/share/man/man1/"

distclean: clean

deb:
	debuild -b

debclean:
	fakeroot debian/rules clean

sic: build
	sudo $(MAKE) install
	$(MAKE) clean

%.1.gz: %.1
	gzip -9 -c $< >.$@
	ln -sf .$@ $@

%.1.txt: %.1
	man -l $< >$@ || touch $@

.PHONY: build install uninstall clean distclean deb cleandeb install distclean sic instdirs

