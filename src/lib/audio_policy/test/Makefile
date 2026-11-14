#
# Host tests for the audio policy
#   make -C repos/pixel/src/lib/audio_policy/test
#
CXX       ?= g++
CXXFLAGS  ?= -std=c++17 -O1 -g -Wall -Wextra -fno-exceptions -fno-rtti
INC       := -I$(abspath ../../../../include)
SRC       := $(abspath ..)
HDRS      := $(wildcard ../../../../include/audio_policy/*.h)

all: test_policy
	./test_policy

test_policy: test_policy.cc $(SRC)/policy.cc $(HDRS)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ test_policy.cc $(SRC)/policy.cc

clean:
	rm -f test_policy

.PHONY: all clean
