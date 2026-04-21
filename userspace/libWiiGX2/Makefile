#
# libWiiGX2 — userland client for the Wii U GPU7 kernel driver.
#
# Targets Mac OS X 10.4 "Tiger" on PowerPC by default, since that is the
# lowest system this driver is intended to run on. Set MACOSX_VERSION to
# 10.5 to build for Leopard.
#

KEXT_ROOT   ?= ..
DARLING_CLT ?= /Library/Developer/DarlingCLT/usr/bin
DARLING_SHELL ?= sudo darling shell

CC       ?= $(DARLING_CLT)/powerpc-apple-darwin10-gcc-4.2.1
AR       ?= $(DARLING_CLT)/ar
RANLIB   ?= $(DARLING_CLT)/ranlib

MACOSX_VERSION ?= 10.4

CFLAGS  := -arch ppc -mmacosx-version-min=$(MACOSX_VERSION) \
           -Wall -Wextra -Os -fno-common \
           -I.

LDFLAGS := -arch ppc -mmacosx-version-min=$(MACOSX_VERSION) \
           -framework IOKit -framework CoreFoundation

LIB_OBJS := WiiGX2.o
LIB      := libWiiGX2.a

EXAMPLE_BIN := example_clear
EXAMPLE_SRC := example_clear.c

all: $(LIB) $(EXAMPLE_BIN)

$(LIB): $(LIB_OBJS)
	$(DARLING_SHELL) $(AR) rcs $@ $^
	$(DARLING_SHELL) $(RANLIB) $@

%.o: %.c WiiGX2.h WiiGX2PM4.h WiiR600ISA.h
	$(DARLING_SHELL) $(CC) $(CFLAGS) -c $< -o $@

$(EXAMPLE_BIN): $(EXAMPLE_SRC) $(LIB)
	$(DARLING_SHELL) $(CC) $(CFLAGS) $(EXAMPLE_SRC) $(LIB) $(LDFLAGS) -o $@

clean:
	rm -f $(LIB_OBJS) $(LIB) $(EXAMPLE_BIN)

.PHONY: all clean
