CROSS   := x86_64-tinykern-elf

CC      := $(CROSS)-gcc
CXX     := $(CROSS)-g++
AR      := $(CROSS)-ar

CFLAGS  := -O2 -ffreestanding -fno-stack-protector -fno-stack-check \
           -fno-asynchronous-unwind-tables -mno-red-zone -m64 -mcmodel=small -Wall -Wextra

MLIBC := $(abspath ../mlibc/build/install/usr/local)
MLIBC_INC := $(MLIBC)/include
MLIBC_LIB := $(MLIBC)/lib

CRTBEGIN := $(shell $(CC) -print-file-name=crtbegin.o)
CRTEND   := $(shell $(CC) -print-file-name=crtend.o)

LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -z noexecstack

LDSCRIPT := linker.ld
BINDIR   := bin

LIBS := $(MLIBC_LIB)/libc.a $(MLIBC_LIB)/libssp_nonshared.a $(MLIBC_LIB)/libssp.a \
$(MLIBC_LIB)/libpthread.a $(MLIBC_LIB)/libm.a $(MLIBC_LIB)/libutil.a

SRCS := $(filter-out dso_stub.c,$(wildcard *.c))
BINS := $(addprefix $(BINDIR)/,$(SRCS:.c=))

TCCSRC := $(abspath ../tools/tcc-0.9.27)

.PHONY: all clean usr

all: $(BINS) usr

$(BINDIR)/%: %.c $(LDSCRIPT) $(LIBS) $(MLIBC_LIB)/crt1.o
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -I$(MLIBC_INC) -c $< -o $@.o
	$(CC) $(LDFLAGS) -T $(LDSCRIPT) \
		$(MLIBC_LIB)/crt1.o $(CRTBEGIN) $@.o \
		$(LIBS) $(CRTEND) \
		-o $@
	strip --strip-debug $@
	rm -f $@.o

clean:
	rm -rf $(BINDIR) usr

bin/tcc: $(TCCSRC)/tcc
	cp $< $@

bin/edit: bin/kilo
	cp $< $@

usr: bin/tcc bin/edit
	@mkdir -p usr/bin usr/lib usr/lib/tcc usr/include usr/src
	cp $(TCCSRC)/tcc usr/bin/tcc
	cp $(TCCSRC)/libtcc1.a usr/lib/tcc/
	cp $(MLIBC_LIB)/libc.a $(MLIBC_LIB)/libm.a $(MLIBC_LIB)/libssp.a \
	$(MLIBC_LIB)/libssp_nonshared.a $(MLIBC_LIB)/libpthread.a \
	$(MLIBC_LIB)/libutil.a $(MLIBC_LIB)/crt1.o usr/lib/
	$(CC) $(CFLAGS) -c dso_stub.c -o usr/lib/crti.o
	printf '.text\n' | $(CROSS)-as -o usr/lib/crtn.o
	cp -r $(MLIBC_INC)/. usr/include/
	cp $(TCCSRC)/include/*.h usr/include/