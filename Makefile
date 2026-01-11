TARGET=chttpx-server

CC=gcc
CFLAGS= -Wall -Wextra -O2 -I.

OBJDIR = .out
BINDIR = .build

PREFIX ?= /usr/local
DESTDIR ?= pkg
PKGDIR ?= /pkg/usr/local

LIN_LDFLAGS =
WIN_LDFLAGS = -lws2_32

LIN_SRCS = $(shell find . -name '*.c')
WIN_SRCS = $(wildcard *.c) $(wildcard */*.c) $(wildcard */*/*.c)

LIN_OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(LIN_SRCS))

# LINux build
# -

lin: $(BINDIR)/$(TARGET)

$(BINDIR)/$(TARGET): $(LIN_OBJS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(LIN_OBJS) $(LIN_LDFLAGS)

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

# WINdows build
# -

win:
	if not exist $(BINDIR) mkdir $(BINDIR)
	$(CC) $(CFLAGS) -D_WIN32 $(WIN_SRCS) -o $(BINDIR)/$(TARGET).exe $(WIN_LDFLAGS)

# LINux shared library
# -

lin-library: $(LIN_OBJS)
	$(CC) -shared -fPIC -o libchttpx.so $(LIN_OBJS)

# LINux install
# -

# before execution, you must run `make lin-library`
lin-install:
	@mkdir -p $(PKGDIR)

	@mkdir -p $(DESTDIR)$(PREFIX)/include/libchttpx
	@mkdir -p $(DESTDIR)$(PREFIX)/lib/pkgconfig

	cp include/*.h $(DESTDIR)$(PREFIX)/include/libchttpx
	cp libchttpx.so $(DESTDIR)$(PREFIX)/lib
	cp libchttpx.pc $(DESTDIR)$(PREFIX)/lib/pkgconfig

# LINux run
# -

lin-run: lin
	$(BINDIR)/$(TARGET)

# WINdows run
# -

win-run: win
	$(BINDIR)\$(TARGET).exe

run: run-lin

clean:
	rm -rf $(OBJDIR) $(BINDIR)
	rm libchttpx.so
