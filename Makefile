TARGET=chttpx-server

CC=gcc
CFLAGS= -Wall -Wextra -O2 -I.

OBJDIR=.out
BINDIR=.build

ifeq ($(OS),Windows_NT)
    IS_WIN = 1
else
    IS_WIN = 0
endif

ifeq ($(IS_WIN),1)
    CFLAGS += -D_WIN32
    LDFLAGS=-lws2_32
    SRCS = $(wildcard *.c) $(wildcard */*.c) $(wildcard */*/*.c)
else
    LDFLAGS =
    SRCS = $(shell find . -name '*.c')
    OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS))
endif

all: $(BINDIR)/$(TARGET)

ifeq ($(IS_WIN),0)
$(BINDIR)/$(TARGET): $(OBJS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
endif

ifeq ($(IS_WIN),1)
$(BINDIR)/$(TARGET):
	if not exist $(BINDIR) mkdir $(BINDIR)
	$(CC) $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS)
endif

run: all
	$(BINDIR)/$(TARGET)

clean:
	rm -rf $(OBJDIR) $(BINDIR)
