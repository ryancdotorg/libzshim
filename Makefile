# libzshim

ifeq ($(shell id -u),0)
	PREFIX?=/usr/local
	INSTALL=install -D
else
	PREFIX?=$(HOME)/.local
	INSTALL=mkdir -p $(LIBDIR) && cp
endif

LIBDIR=$(PREFIX)/lib

CC=gcc
CFLAGS=-O2 --std=gnu11 -Wall -Wextra -Wformat -Wformat-signedness -pedantic -Wno-unused-function
SOFLAGS=-shared -fPIC
LIBS=-lsystemd -ldl

lib: libzshim.so

debug: libzshim-debug.so

all: lib debug

zshim.c: zshim.in.c gen.py
	python3 gen.py $< > $@

libzshim.so: zshim.c dlutil.h
	$(CC) $(CFLAGS) $(SOFLAGS) -s -DNDEBUG $< $(LIBS) -o $@

libzshim-debug.so: zshim.c dlutil.h debugp.h
	$(CC) $(CFLAGS) $(SOFLAGS) -ggdb $< $(LIBS) -o $@

install: libzshim.so
	$(INSTALL) libzshim.so $(DESTDIR)$(LIBDIR)/libzshim.so

install-debug: libzshim-debug.so
	$(INSTALL) libzshim-debug.so $(DESTDIR)$(LIBDIR)/libzshim.so

uninstall:
	$(RM) $(DESTDIR)$(LIBDIR)/libzshim.so

.PHONY: lib debug all install install-debug uninstall clean _clean _nop

# hack to force clean to run first *to completion* even for parallel builds
# note that $(info ...) prints everything on one line
clean: _nop $(foreach _,$(filter clean,$(MAKECMDGOALS)),$(info $(shell $(MAKE) _clean)))
_clean:
	$(RM) libzshim.so libzshim-debug.so
_nop:
	@true
