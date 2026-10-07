REQUIRES = hw

REP_INC_DIR += src/core/board/google_pixel7
REP_INC_DIR += src/core/spec/arm/virtualization

# add C++ sources
SRC_CC += kernel/vcpu_thread_on.cc
SRC_CC += spec/arm/gicv3.cc
SRC_CC += spec/arm_v8/kernel/vcpu.cc
SRC_CC += spec/arm/virtualization/platform_services.cc
SRC_CC += spec/arm/virtualization/vm_session_component.cc

#add assembly sources
SRC_S += spec/arm_v8/hypervisor.s

#
# Serial reset monitor: with 'REBOOT=serial', the line "RST" received at the
# kernel's UART reboots the phone into its boot loader, e.g.,
# 'make run/log KERNEL=hw BOARD=google_pixel7 REBOOT=serial'
#
# The stamp file lets the selected variant be compiled again whenever the
# argument changes, so that the library never keeps the previous variant.
#
REBOOT ?= none

ifeq ($(filter $(REBOOT),none serial),)
$(error REBOOT must be 'none' or 'serial' (is '$(REBOOT)'))
endif

SRC_CC += board/google_pixel7/reboot_$(REBOOT).cc

REBOOT_STAMP := reboot.$(REBOOT)

$(REBOOT_STAMP):
	$(VERBOSE)rm -f reboot.*
	$(VERBOSE)touch $@

board/google_pixel7/reboot_$(REBOOT).o: $(REBOOT_STAMP)

vpath board/google_pixel7/% $(REP_DIR)/src/core

# include less specific configuration
include $(call select_from_repositories,lib/mk/spec/arm_v8/core-hw.inc)
