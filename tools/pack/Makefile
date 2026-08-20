CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
ROOT := ../..
FS := $(ROOT)/src/port/fs
DECOMP := $(ROOT)/third_party/goldeneye_src
PUFF := $(DECOMP)/tools/extractor

.PHONY: test extract clean

test_c0pack: test_c0pack.c $(FS)/c0pack.c $(FS)/sha256.c $(FS)/c0pack.h $(FS)/sha256.h
	$(CC) $(CFLAGS) -o $@ test_c0pack.c $(FS)/c0pack.c $(FS)/sha256.c

test: test_c0pack
	./test_c0pack

extract: extract.c $(FS)/c0pack.c $(FS)/filelist.c $(FS)/sha256.c $(PUFF)/puff.c
	$(CC) $(CFLAGS) -I$(FS) -I$(PUFF) -o extract extract.c $(FS)/c0pack.c \
		$(FS)/filelist.c $(FS)/sha256.c $(PUFF)/puff.c

clean:
	rm -f test_c0pack extract
