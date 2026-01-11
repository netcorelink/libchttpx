TARGET=chttpx-server

CC = gcc
CFLAGS = -Wall -Wextra -O2 -I.
TARGET_DLL = libchttpx.dll

OBJDIR = .out
BINDIR = .build

PREFIX ?= /usr/local
DESTDIR ?= pkg
PKGDIR ?= /pkg/usr/local

WIN_LIB_DIR = choco/tools

LIN_LDFLAGS = -lcjson
WIN_LDFLAGS = -lws2_32

LIN_SRCS = $(shell find . -name '*.c')
WIN_SRCS = $(wildcard *.c) $(wildcard */*.c) $(wildcard */*/*.c)

LIN_OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(LIN_SRCS))
WIN_OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(WIN_SRCS))

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
	$(CC) $(CFLAGS) -D_WIN32 -mconsole $(WIN_SRCS) -o $(BINDIR)/$(TARGET).exe $(WIN_LDFLAGS)

# LINux shared library
# -

libchttpx.so: $(LIN_OBJS)
	$(CC) -shared -fPIC -o libchttpx.so $(LIN_OBJS)

# LINux lib install
# -

# before execution, you must run `make lin-library`
lib-install: libchttpx.so
	@mkdir -p $(DESTDIR)$(PREFIX)/include/libchttpx
	@mkdir -p $(DESTDIR)$(PREFIX)/lib/pkgconfig

	cp include/*.h $(DESTDIR)$(PREFIX)/include/libchttpx
	cp libchttpx.so $(DESTDIR)$(PREFIX)/lib
	cp libchttpx.pc $(DESTDIR)$(PREFIX)/lib/pkgconfig

# WINdows lib install
# -

win-lib: $(WIN_OBJS)
	@echo "Building Windows DLL..."
	$(CC) -shared -o $(TARGET_DLL) $(WIN_OBJS) -Wl,--out-implib,libchttpx.a -lws2_32

	@echo "Copying files to $(WIN_LIB_DIR)..."
	@mkdir -p $(WIN_LIB_DIR)
	@cp $(TARGET_DLL) $(WIN_LIB_DIR)/
	@cp libchttpx.a $(WIN_LIB_DIR)/
	@mkdir -p $(WIN_LIB_DIR)/include
	@cp -r include/* $(WIN_LIB_DIR)/include/

	@rm $(TARGET_DLL) libchttpx.a

	@echo "Files copied to $(WIN_LIB_DIR) successfully!"

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
	rm -rf $(OBJDIR) $(BINDIR) *.a *.dll
	rm libchttpx.so
