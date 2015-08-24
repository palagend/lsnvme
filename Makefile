ifdef DEBUG
	CFLAGS += -Wall -g -DDEBUG=1
else
	CFLAGS += -Wall -Werror -O2
endif

LDFLAGS += -ludev

VERSION := 0.1

# default paths for installation
prefix ?= $(DESTDIR)/usr/
mandir ?= $(prefix)/share/man/
datadir ?= $(prefix)/share/

# Compile against some specific kernel version.
# There is almost certainly not a good reason to do
# this so use at your own risk.
# see: http://kernelnewbies.org/KernelHeaders
ifdef KERNEL_VERSION
	NVME_H := /usr/src/kernels/$(KERNEL_VERSION)/include/uapi/linux/
	cp $(NVME_H)/nvme.h linux/
	CPPFLAGS += -I.
else
	NVME_H := /usr/include/linux/
endif

# Perform basic checks here

ifneq (,$(wildcard $(NVME_H)/nvme.h))
	CFLAGS += -DHAVE_NVME_H
endif

.PHONY: all
all: lsnvme

lsnvme: lsnvme.c

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
