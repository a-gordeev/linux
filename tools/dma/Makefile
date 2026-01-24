# SPDX-License-Identifier: GPL-2.0
PREFIX ?= /usr
SBINDIR ?= sbin
INSTALL ?= install
CFLAGS += -Wall -Wextra -I../../include/uapi -D__EXPORTED_HEADERS__

TARGET = dma-slave

all: $(TARGET)

%: %.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	$(RM) $(TARGET)

install: $(TARGET)
	$(INSTALL) -D -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/$(SBINDIR)/$(TARGET)

.PHONY: all clean install
