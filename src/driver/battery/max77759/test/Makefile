#
# Host tests of the fuel-gauge conversions and the monitor's rules
#   make -C repos/pixel/src/driver/battery/max77759/test
#
CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O1 -g -Wall -Wextra -Wconversion -fno-exceptions -fno-rtti

all: test_gauge
	./test_gauge

test_gauge: test_gauge.cc ../gauge.h
	$(CXX) $(CXXFLAGS) -I.. -o $@ $<

clean:
	rm -f test_gauge

.PHONY: all clean
