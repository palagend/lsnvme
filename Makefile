CFLAGS ?= -Wall -std=c99 -D_GNU_SOURCE

ifdef DEBUG
	CFLAGS += -g -DDEBUG=1
else
	CFLAGS += -Werror -O2
endif

LDFLAGS += -ludev

VERSION := 0.1

# default paths for installation
prefix ?= $(DESTDIR)/usr/
mandir ?= $(prefix)/share/man/
datadir ?= $(prefix)/share/

ifndef NVME_H
	CPPFLAGS += -I.
else
	CPPFLAGS += $(NVME_H)
endif

.PHONY: all
all: lsnvme

lsnvme: lsnvme.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# just enough targets for building an RPM:

DISTFILES := Makefile lsnvme.spec lsnvme.c lsnvme.8 AUTHORS COPYING README.md

.PHONY: install
install:
	gzip -c lsnvme.8 > lsnvme.8.gz
	install -d $(prefix)/bin $(mandir)/man8 $(datadir)/doc/lsnvme-$(VERSION)
	install lsnvme $(prefix)/bin/
	install lsnvme.8.gz $(mandir)/man8/
	install AUTHORS COPYING README.md $(datadir)/doc/lsnvme-$(VERSION)/

.PHONY: dist
dist: TARDIR:= lsnvme-$(VERSION)
dist:
	mkdir -p $(TARDIR)
	cp $(DISTFILES) $(TARDIR)
	tar -cjf lsnvme-$(VERSION).tar.bz2 $(TARDIR)

.PHONY: clean
clean:
	rm -rf lsnvme-$(VERSION)
	rm -f lsnvme
	rm -f lsnvme.8.gz lsnvme-*.bz2
	rm -f linux/*
