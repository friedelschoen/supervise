NAME = supervise
VERSION = 0.1
HOMEPAGE = https://git.friedelschoen.io/unix/supervise

# paths
PREFIX ?= /usr/local

# flags
CC 		 ?= gcc
CFLAGS 	 += -Wall -Wextra -Wpedantic -Werror
CPPFLAGS += -D_XOPEN_SOURCE=700 -D_GNU_SOURCE -DVERSION=\"$(VERSION)\"

ifeq ($(CC),gcc)
CFLAGS += -Wno-stringop-truncation \
		  -Wno-format-truncation
endif

ifneq ($(DEBUG),)
CFLAGS += -g -O0
CPPFLAGS += -DDEBUG
else
CFLAGS += -O2
endif

BINS = supervise
MAN1 =
MAN5 =

HEADER = \
	arg.h \
	buffer.h \
	lock.h

OBJECTS = \
	buffer.o \
	lock.o

CLEAN = \
	$(BINS) \
	$(BINS:=.o) \
	$(OBJECTS) \
	$(MAN1) \
	$(MAN5) \
	compile_flags.txt	

.PHONY: all clean \
	install install-bins install-man1 install-man5 \
	uninstall uninstall-bins uninstall-man1 uninstall-man5

# default target, make everything
all: $(BINS) $(MAN1) $(MAN5) compile_flags.txt

# automatic tagets

%.o: %.c $(HEADER)
	$(CC) -c -o $@ $< $(CFLAGS) $(CPPFLAGS)

%: %.in
	sed 's/%VERSION%/$(VERSION)/g;s|%HOMEPAGE%|$(HOMEPAGE)|g' $< > $@

%: %.o
	$(CC) -o $@ $^ $(LDFLAGS)

# binary targets

supervise: $(OBJECTS)

compile_flags.txt: LIBS = libgit2 libarchive
compile_flags.txt: Makefile
	echo $(CFLAGS) $(CPPFLAGS) | tr ' ' '\n' > $@

# pseudo targets

clean: 
	rm -f $(CLEAN)

install: install-bins install-man1 install-man5 install-assets install-icons

install-bins: $(BINS)
	install -d $(PREFIX)/bin
	install -m 755 $(BINS) $(PREFIX)/bin

install-man1: $(MAN1)
	install -d $(PREFIX)/share/man/man1
	install -m 644 $(MAN1) $(PREFIX)/share/man/man1

install-man5: $(MAN5)
	install -d $(PREFIX)/share/man/man5
	install -m 644 $(MAN5) $(PREFIX)/share/man/man5

uninstall: uninstall-bins uninstall-man1 uninstall-man5 uninstall-assets

uninstall-bins:
	rm -f $(addprefix $(PREFIX)/bin/, $(BINS))

uninstall-man1:
	rm -f $(addprefix $(PREFIX)/share/man/man1/, $(MAN1))

uninstall-man5:
	rm -f $(addprefix $(PREFIX)/share/man/man5/, $(MAN5))
