#
# Host tests of the message handling of the panthor driver's Gpu session
#   make -C repos/pixel/src/driver/gpu/panthor/test
#
# test_tunnel  validation and relocation of messages (../tunnel.h) and the
#              client-side composition (include/gpu/panthor_session.h)
# test_layout  the protocol's mirrors of the uAPI structures against the
#              headers of the Linux tree the driver is built from; skipped
#              if the 'linux' port is not prepared
# test_bo_sync the checks of BO_CREATE's flags and of BO_SYNC's ranges
#              (../bo_sync.h) and the messages of the cached-object requests
#
CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O1 -g -Wall -Wextra -Wconversion -fno-exceptions -fno-rtti

GENODE_DIR := $(abspath ../../../../../../..)
REP_DIR    := $(abspath ../../../../..)

INC := -I.. -I. -I$(REP_DIR)/include -I$(GENODE_DIR)/repos/base/include/spec/64bit

LINUX_HASH := $(shell cat $(GENODE_DIR)/repos/dde_linux/ports/linux.hash 2>/dev/null)
LINUX_UAPI := $(wildcard $(GENODE_DIR)/contrib/linux-$(LINUX_HASH)/src/linux/include/uapi)

TESTS := test_tunnel test_bo_sync
ifneq ($(LINUX_UAPI),)
TESTS += test_layout
endif

HEADERS := check.h ../tunnel.h ../bo_sync.h $(REP_DIR)/include/gpu/panthor_session.h

all: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done
ifeq ($(LINUX_UAPI),)
	@echo "test_layout: SKIPPED (no contrib/linux-$(LINUX_HASH), prepare the 'linux' port)"
endif

test_tunnel: test_tunnel.cc $(HEADERS)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ $<

# the headers are the kernel's own, not the exported ones: '__user' is there
test_layout: test_layout.cc $(HEADERS)
	$(CXX) $(CXXFLAGS) $(INC) -D__user= -I$(LINUX_UAPI)/drm -o $@ $<

test_bo_sync: test_bo_sync.cc $(HEADERS)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ $<

clean:
	rm -f test_tunnel test_layout test_bo_sync

.PHONY: all clean
