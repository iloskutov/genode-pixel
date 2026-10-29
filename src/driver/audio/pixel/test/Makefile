#
# Host test for the value rules of the driver's config nodes (ctl_merge.h)
#   make -C repos/pixel/src/driver/audio/pixel/test
#
CC     ?= gcc
CFLAGS ?= -std=gnu11 -O1 -g -Wall -Wextra

all: test_ctl_merge
	./test_ctl_merge

test_ctl_merge: test_ctl_merge.c ../ctl_merge.h
	$(CC) $(CFLAGS) -I.. -o $@ test_ctl_merge.c

clean:
	rm -f test_ctl_merge

.PHONY: all clean
