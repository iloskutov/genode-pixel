#
# Host tests of the CPU frequency requests of the platform driver
#   make -C repos/pixel/src/driver/platform/gs201/test
#
CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O1 -g -Wall -Wextra -fno-exceptions -fno-rtti
INC      := -I$(abspath ../../../../../include)
HDRS     := $(wildcard ../../../../../include/cpufreq/*.h)
TESTS    := test_request_policy test_steps

all: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

test_%: test_%.cc $(HDRS)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ $<

clean:
	rm -f $(TESTS)

.PHONY: all clean
