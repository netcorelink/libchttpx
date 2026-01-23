TARGET=chttpx-server

RELEASE_DIR = libchttpx-dev
TAR = $(RELEASE_DIR).tar.gz

CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude
TARGET_DLL = libchttpx.dll
CLANG_FORMAT = clang-format

OBJDIR = .out
BINDIR = .build

PREFIX ?= /usr/local
DESTDIR ?= pkg
PKGDIR ?= /pkg/usr/local

WIN_LIB_DIR = tools

LIN_LDFLAGS = -lcjson
WIN_LDFLAGS = -lws2_32

LIN_SRCS = $(filter-out ./lib/cjson/cJSON.c, $(shell find . -name '*.c'))
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

# before execution, you must run `make libchttpx.so`
lib-install: libchttpx.so
	@mkdir -p $(DESTDIR)$(PREFIX)/include/libchttpx
	@mkdir -p $(DESTDIR)$(PREFIX)/lib/pkgconfig

	cp include/*.h $(DESTDIR)$(PREFIX)/include/libchttpx
	cp libchttpx.so $(DESTDIR)$(PREFIX)/lib
	cp libchttpx.pc $(DESTDIR)$(PREFIX)/lib/pkgconfig

# WINdows lib compile
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

# LINux lib compile
# -

lin-lib: clean libchttpx.so
	@echo "Preparing release directory..."
	rm -rf $(RELEASE_DIR)
	mkdir -p $(RELEASE_DIR)

	cp -r include $(RELEASE_DIR)/
	cp libchttpx.so $(RELEASE_DIR)/
	cp libchttpx.pc $(RELEASE_DIR)/
	cp Makefile $(RELEASE_DIR)/
	cp README.md $(RELEASE_DIR)/

	@echo "Creating tar.gz..."
	tar -czf $(TAR) $(RELEASE_DIR)

	rm -rf $(RELEASE_DIR)

	@echo "Release package created: $(TAR)"

# LINux run
# -

lin-run: lin
	$(BINDIR)/$(TARGET)

# WINdows run
# -

win-run: win
	$(BINDIR)\$(TARGET).exe

# LINux format
# -

lin-format:
	@echo ">> Formatting clang source files"
	$(CLANG_FORMAT) -i $(LIN_SRCS)

# WINdows format
# -

win-format:
	@echo ">> Formatting clang source files"
	$(CLANG_FORMAT) -i $(WIN_SRCS)

run: run-lin

clean:
	rm -rf $(OBJDIR) $(BINDIR) *.a *.dll
	rm -rf $(RELEASE_DIR)
	rm -rf libchttpx.so
