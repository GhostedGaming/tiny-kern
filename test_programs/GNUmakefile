CC      ?= cc
LD      ?= ld
CFLAGS  := -O2 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-stack-check \
           -fno-asynchronous-unwind-tables -mno-red-zone -m64 -mcmodel=small -Wall -Wextra
LDFLAGS := -nostdlib -static -z max-page-size=0x1000

LDSCRIPT := linker.ld
BINDIR   := bins

SRCS := $(wildcard *.c)
BINS := $(addprefix $(BINDIR)/,$(SRCS:.c=))

.PHONY: all clean

all: $(BINS)

$(BINDIR)/%: %.c $(LDSCRIPT)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -c $< -o $@.o
	$(LD) $(LDFLAGS) -T $(LDSCRIPT) $@.o -o $@
	rm -f $@.o

clean:
	rm -rf $(BINDIR)
