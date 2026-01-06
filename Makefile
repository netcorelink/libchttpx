TARGET=uhttpx-tinyserv

CC=gcc
CFLAGS= -Wall -Wextra -O2 -I.

OBJDIR=.out
BINDIR=.build

SRCS=$(shell find . -name '*.c')
OBJS = $(patsubst %.c,.out/%.o,$(SRCS))

all: $(BINDIR)/$(TARGET)

$(BINDIR)/$(TARGET): $(OBJS)
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(OBJDIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

run: 
	$(BINDIR)/$(TARGET)

clean:
	rm -rf $(OBJDIR) $(BINDIR)