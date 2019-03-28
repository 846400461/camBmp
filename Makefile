# ----------------------------------------------------------------------------
# Makefile for building tapp
#
# Copyright 2010 FriendlyARM (http://www.arm9.net/)
#

ifndef DESTDIR
DESTDIR			   ?= /tmp/FriendlyARM/mini210/rootfs
endif

CFLAGS				= -Wall -O2
CC					= arm-linux-g++
INSTALL				= install

TARGET				= ccdcamtest


all: $(TARGET)

ccdcamtest: camtest.o 
	$(CC) $(CFLAGS) $^ -o $@

camtest.o:
	$(CC) $(CFLAGS) -c camtest.cpp -o $@

install: $(TARGET)
	$(INSTALL) $^ $(DESTDIR)/usr/bin

clean distclean:
	rm -rf *.o $(TARGET)


# ----------------------------------------------------------------------------

.PHONY: $(PHONY) install clean distclean

# End of file
# vim: syntax=make

