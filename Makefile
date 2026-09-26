TARGET=chttpx-server

RELEASE_DIR = libchttpx-dev
TAR = $(RELEASE_DIR).tar.gz

CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -O2 -Iinclude -Isrc

# Native TLS is opt-in so cleartext HTTP/2 builds keep zero OpenSSL dependency.
TLS ?= 0
TLS_CFLAGS =
TLS_LDFLAGS =

ifeq ($(TLS),1)
TLS_CFLAGS += -DCHTTPX_ENABLE_TLS
TLS_LDFLAGS += -lssl -lcrypto
endif

CFLAGS += $(TLS_CFLAGS)
CLANG_FORMAT = clang-format
FUZZ_CC ?= clang

OBJDIR = .out
BINDIR = .build

PREFIX ?= /usr/local
DESTDIR ?= pkg
PKGDIR ?= /pkg/usr/local

LIN_LDFLAGS = -lcjson -lz -lnghttp2 $(TLS_LDFLAGS)
TEST_TARGET = $(BINDIR)/test_core
TEST_SERVER_TARGET = $(BINDIR)/test_server
TEST_SANITIZE_TARGET = $(BINDIR)/test_core_sanitize
TEST_SERVER_SANITIZE_TARGET = $(BINDIR)/test_server_sanitize
TEST_TSAN_TARGET = $(BINDIR)/test_server_tsan
FUZZ_HEADERS_TARGET = $(BINDIR)/fuzz_headers
TEST_TLS_TARGET = $(BINDIR)/test_tls
TEST_COMPRESSION_TARGET = $(BINDIR)/test_compression
TEST_METRICS_TARGET = $(BINDIR)/test_metrics
EXAMPLE_SRC = example/basic.c
EXAMPLE_OBJ = $(OBJDIR)/example/basic.o

EXAMPLE_NAMES = basic multiple_servers local_call remote_call middleware json upload metrics
EXAMPLE_TARGETS = $(addprefix $(BINDIR)/example-,$(EXAMPLE_NAMES))

LIN_SRCS = $(wildcard src/*.c)
FORMAT_SRCS = $(wildcard src/*.c src/*.h include/*.h example/*.c tests/*.c)

LIN_OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(LIN_SRCS))

# Linux build
# -

lin: $(BINDIR)/$(TARGET)

$(BINDIR)/$(TARGET): $(LIN_OBJS) $(EXAMPLE_OBJ)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(LIN_OBJS) $(EXAMPLE_OBJ) $(LIN_LDFLAGS)

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

# Examples
# -

examples: $(EXAMPLE_TARGETS)

$(BINDIR)/example-%: example/%.c $(LIN_SRCS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -std=gnu11 $< $(LIN_SRCS) -o $@ $(LIN_LDFLAGS) -pthread

examples-tls:
	@$(MAKE) TLS=1 $(BINDIR)/example-tls

examples-compression: $(BINDIR)/example-compression

# Linux shared library
# -

libchttpx.so: $(LIN_OBJS)
	$(CC) -shared -fPIC -o libchttpx.so $(LIN_OBJS) $(LIN_LDFLAGS) -pthread

# Linux lib install
# -

# before execution, you must run `make libchttpx.so`
lib-install: libchttpx.so
	@mkdir -p $(DESTDIR)$(PREFIX)/include/libchttpx
	@mkdir -p $(DESTDIR)$(PREFIX)/lib/pkgconfig

	cp include/*.h $(DESTDIR)$(PREFIX)/include/libchttpx
	cp libchttpx.so $(DESTDIR)$(PREFIX)/lib
	@if [ "$(TLS)" = "1" ]; then \
		cp libchttpx-tls.pc $(DESTDIR)$(PREFIX)/lib/pkgconfig/libchttpx.pc; \
	else \
		cp libchttpx.pc $(DESTDIR)$(PREFIX)/lib/pkgconfig/libchttpx.pc; \
	fi

# Linux tests
# -

test: $(TEST_TARGET) $(TEST_SERVER_TARGET) $(TEST_METRICS_TARGET)
	$(TEST_TARGET)
	$(TEST_SERVER_TARGET)
	$(TEST_METRICS_TARGET)

$(TEST_TARGET): tests/test_core.c $(LIN_SRCS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -std=gnu11 -g tests/test_core.c $(LIN_SRCS) -o $@ $(LIN_LDFLAGS) -pthread

$(TEST_SERVER_TARGET): tests/test_server.c $(LIN_SRCS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -std=gnu11 -g tests/test_server.c $(LIN_SRCS) -o $@ $(LIN_LDFLAGS) -pthread

test-sanitize:
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -std=gnu11 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined tests/test_core.c $(LIN_SRCS) -o $(TEST_SANITIZE_TARGET) $(LIN_LDFLAGS) -pthread
	ASAN_OPTIONS=detect_leaks=1 $(TEST_SANITIZE_TARGET)
	$(CC) $(CFLAGS) -std=gnu11 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined tests/test_server.c $(LIN_SRCS) -o $(TEST_SERVER_SANITIZE_TARGET) $(LIN_LDFLAGS) -pthread
	ASAN_OPTIONS=detect_leaks=1 $(TEST_SERVER_SANITIZE_TARGET)

test-tsan:
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -std=gnu11 -O1 -g -fno-omit-frame-pointer -fsanitize=thread tests/test_server.c $(LIN_SRCS) -o $(TEST_TSAN_TARGET) $(LIN_LDFLAGS) -pthread
	TSAN_OPTIONS=halt_on_error=1 $(TEST_TSAN_TARGET)

test-fuzz:
	@mkdir -p $(BINDIR)
	$(FUZZ_CC) $(CFLAGS) -std=gnu11 -O1 -g -fno-omit-frame-pointer -fsanitize=fuzzer,address,undefined tests/fuzz_headers.c $(LIN_SRCS) -o $(FUZZ_HEADERS_TARGET) $(LIN_LDFLAGS) -pthread
	$(FUZZ_HEADERS_TARGET) -runs=2000 -max_len=32768

test-tls:
	@mkdir -p $(BINDIR)/tls
	openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
		-keyout $(BINDIR)/tls/server.key -out $(BINDIR)/tls/server.crt \
		-subj "/CN=localhost" -addext "subjectAltName=DNS:localhost" >/dev/null 2>&1
	@$(MAKE) TLS=1 $(TEST_TLS_TARGET)
	$(TEST_TLS_TARGET) $(BINDIR)/tls/server.crt $(BINDIR)/tls/server.key

$(TEST_TLS_TARGET): tests/test_tls.c $(LIN_SRCS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -std=gnu11 -g tests/test_tls.c $(LIN_SRCS) -o $@ $(LIN_LDFLAGS) -pthread

test-compression: $(TEST_COMPRESSION_TARGET)
	$(TEST_COMPRESSION_TARGET)

$(TEST_COMPRESSION_TARGET): tests/test_compression.c $(LIN_SRCS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -std=gnu11 -g tests/test_compression.c $(LIN_SRCS) -o $@ $(LIN_LDFLAGS) -pthread

test-metrics: $(TEST_METRICS_TARGET)
	$(TEST_METRICS_TARGET)

$(TEST_METRICS_TARGET): tests/test_metrics.c $(LIN_SRCS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -std=gnu11 -O2 -g tests/test_metrics.c $(LIN_SRCS) -o $@ $(LIN_LDFLAGS) -pthread

# Linux lib compile
# -

lin-lib: clean libchttpx.so
	@echo "Preparing release directory..."
	rm -rf $(RELEASE_DIR)
	mkdir -p $(RELEASE_DIR)

	cp -r include $(RELEASE_DIR)/
	cp libchttpx.so $(RELEASE_DIR)/
	@if [ "$(TLS)" = "1" ]; then \
		cp libchttpx-tls.pc $(RELEASE_DIR)/libchttpx.pc; \
	else \
		cp libchttpx.pc $(RELEASE_DIR)/libchttpx.pc; \
	fi
	cp Makefile $(RELEASE_DIR)/
	cp README.md $(RELEASE_DIR)/

	@echo "Creating tar.gz..."
	tar -czf $(TAR) $(RELEASE_DIR)

	rm -rf $(RELEASE_DIR)

	@echo "Release package created: $(TAR)"

# Linux run
# -

lin-run: lin
	$(BINDIR)/$(TARGET)

# Linux format
# -

lin-format:
	@echo ">> Formatting C source and header files"
	$(CLANG_FORMAT) -i $(FORMAT_SRCS)

format-check:
	@echo ">> Checking clang-format"
	$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_SRCS)

run: lin-run

clean:
	rm -rf $(OBJDIR) $(BINDIR) *.a
	rm -rf $(RELEASE_DIR)
	rm -rf libchttpx.so
