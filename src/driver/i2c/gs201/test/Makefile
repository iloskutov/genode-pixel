#
# Host tests of the HSI2C driver logic
#   make -C repos/pixel/src/driver/i2c/gs201/test
#
CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O1 -g -Wall -Wextra -Wconversion -fno-exceptions -fno-rtti
TESTS    := test_timing test_policy test_transfer

all: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

test_%: test_%.cc check.h model.h ../hsi2c.h
	$(CXX) $(CXXFLAGS) -I.. -I. -o $@ $<

clean:
	rm -f $(TESTS)

.PHONY: all clean
