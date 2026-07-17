TARGET = libchttpx-tests
EXAMPLE_TARGET = chttpx-server

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

LIN_LDFLAGS = -lcjson -lpthread
WIN_LDFLAGS = -lws2_32

LIB_SRCS = $(wildcard src/*.c)
LIB_OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(LIB_SRCS))

TEST_SRCS = $(wildcard tests/test_*.c)
TEST_OBJS = $(patsubst tests/%.c,$(OBJDIR)/tests/%.o,$(TEST_SRCS))

EXAMPLE_OBJ = $(OBJDIR)/exmaples.o

WIN_LIB_SRCS = $(wildcard src/*.c) lib/cjson/cJSON.c

# LINux build
# -

lin: $(BINDIR)/$(TARGET) libchttpx.so

$(BINDIR)/$(TARGET): $(LIB_OBJS) $(TEST_OBJS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(LIB_OBJS) $(TEST_OBJS) $(LIN_LDFLAGS)

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

$(OBJDIR)/tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

example: $(BINDIR)/$(EXAMPLE_TARGET)

$(BINDIR)/$(EXAMPLE_TARGET): $(LIB_OBJS) $(EXAMPLE_OBJ)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(LIB_OBJS) $(EXAMPLE_OBJ) $(LIN_LDFLAGS)

# WINdows build
# -

win:
	if not exist $(BINDIR) mkdir $(BINDIR)
	$(CC) $(CFLAGS) -D_WIN32 -mconsole $(WIN_LIB_SRCS) exmaples.c -o $(BINDIR)/$(EXAMPLE_TARGET).exe $(WIN_LDFLAGS)

win-test: $(BINDIR)/$(TARGET).exe

$(BINDIR)/$(TARGET).exe:
	if not exist $(BINDIR) mkdir $(BINDIR)
	$(CC) $(CFLAGS) -D_WIN32 -mconsole $(WIN_LIB_SRCS) $(TEST_SRCS) -o $@ $(WIN_LDFLAGS)

# LINux shared library
# -

libchttpx.so: $(LIB_OBJS)
	$(CC) -shared -fPIC -o libchttpx.so $(LIB_OBJS) $(LIN_LDFLAGS)

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

win-lib:
	@echo "Building Windows DLL..."
	$(CC) -shared -o $(TARGET_DLL) $(WIN_LIB_SRCS) -Wl,--out-implib,libchttpx.a -lws2_32

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

test: lin
	$(BINDIR)/$(TARGET)

lin-run: test

lin-example: example
	$(BINDIR)/$(EXAMPLE_TARGET)

# WINdows run
# -

win-run: win
	$(BINDIR)\$(EXAMPLE_TARGET).exe

win-test-run: win-test
	$(BINDIR)\$(TARGET).exe

# LINux format
# -

lin-format:
	@echo ">> Formatting clang source files"
	$(CLANG_FORMAT) -i $(LIB_SRCS) $(TEST_SRCS)

# WINdows format
# -

win-format:
	@echo ">> Formatting clang source files"
	$(CLANG_FORMAT) -i $(WIN_LIB_SRCS) $(TEST_SRCS) exmaples.c

run: lin-run

clean:
	rm -rf $(OBJDIR) $(BINDIR) *.a *.dll
	rm -rf $(RELEASE_DIR)
	rm -rf libchttpx.so
