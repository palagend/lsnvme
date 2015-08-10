ifdef DEBUG
	CFLAGS += -Wall -g -DDEBUG=1
else
	CFLAGS += -Wall -Werror -O2
endif

LDFLAGS += -ludev

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

.PHONY: clean
clean:
	rm -f lsnvme
	rm -f linux/*
