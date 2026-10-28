#
# Host tests of the USB gadget logic of include/usb_gadget (descriptors,
# CDC ACM, Type-C)
#
#   make -C repos/pixel/src/test/usb_gadget_host
#
# SPDX-License-Identifier: MIT
#

CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O1 -g -Wall -Wextra -fno-exceptions -fno-rtti
INC      := -I$(abspath ../../../include)
HDRS     := $(wildcard ../../../include/usb_gadget/*.h)
TESTS    := test_descriptor test_acm test_type_c

all: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

test_%: test_%.cc $(HDRS)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ $<

clean:
	rm -f $(TESTS)

.PHONY: all clean
