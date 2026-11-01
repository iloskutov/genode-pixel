#
# Configured Linux tree (generated headers) for the panthor GPU driver:
# Genode's stock dde_linux port 'linux' with the configuration of
# src/panthor_linux/target.inc. As dde_linux's virt_linux_generated.inc.
#

CUSTOM_TARGET_DEPS := kernel_build.phony

LX_DIR := $(call select_from_ports,linux)/src/linux
PWD    := $(shell pwd)

LX_MK_ARGS = ARCH=arm64 CROSS_COMPILE=$(CROSS_DEV_PREFIX)

#
# Linux kernel configuration
#

# define 'LX_ENABLE', 'LX_DISABLE' and 'LX_NR_CPUS'
include $(REP_DIR)/src/panthor_linux/target.inc

# filter for make output of kernel build system
BUILD_OUTPUT_FILTER = 2>&1 | sed "s/^/      [Linux]  /"

# do not confuse third-party sub-makes
unexport .SHELLFLAGS

kernel_config.tag:
	$(MSG_CONFIG)Linux
	$(VERBOSE)$(MAKE) -C $(LX_DIR) O=$(PWD) $(LX_MK_ARGS) tinyconfig $(BUILD_OUTPUT_FILTER)
	$(VERBOSE)$(LX_DIR)/scripts/config $(addprefix --enable ,$(LX_ENABLE))
	$(VERBOSE)$(LX_DIR)/scripts/config $(addprefix --disable ,$(LX_DISABLE))
	$(VERBOSE)$(LX_DIR)/scripts/config $(addprefix --set-val NR_CPUS ,$(LX_NR_CPUS))
	$(VERBOSE)$(MAKE) $(LX_MK_ARGS) olddefconfig $(BUILD_OUTPUT_FILTER)
	$(VERBOSE)$(MAKE) $(LX_MK_ARGS) prepare      $(BUILD_OUTPUT_FILTER)
	$(VERBOSE)touch $@

# update Linux kernel config on makefile changes
kernel_config.tag: $(MAKEFILE_LIST)

kernel_build.phony: kernel_config.tag
