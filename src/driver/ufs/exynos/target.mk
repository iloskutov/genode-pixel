#
# UFS storage driver for the Pixel 7: the UFS and SCSI cores of Linux with
# the vendor's Exynos UFS host-controller driver. Each logical unit of the
# storage is provided as a Block session, with the block size of the unit.
# The clocks and the I/O coherency of the host controller are provided by
# gs201_platform.
#

DRIVER  := exynos
TARGET   = $(DRIVER)_ufs
REQUIRES = arm_v8a
LIBS     = base jitterentropy pixel_lx_emul

INC_DIR += $(PRG_DIR)
INC_DIR += $(LX_SRC_DIR)/scripts/dtc/libfdt

# no-op Android vendor hooks, see compat/trace/hooks/vendor_hooks.h
INC_DIR += $(PRG_DIR)/compat

SRC_CC  += main.cc genode_block.cc
SRC_CC  += lx_emul/shared_dma_buffer.cc
SRC_C   += block.c dummies.c
SRC_C   += $(notdir $(wildcard $(PRG_DIR)/generated_dummies.c))
SRC_C   += lx_emul/shadow/drivers/clk/clkdev.c
SRC_C   += lx_emul/shadow/drivers/clk/clk-devres.c
SRC_C   += lx_emul/gs201/common_dummies.c

vpath %.c  $(PRG_DIR)
vpath %.cc $(PRG_DIR)
vpath lx_emul/gs201/%.c $(REP_DIR)/src/lib

#
# The C-API of Genode, compiled as part of genode_block.cc. Its anonymous
# namespace is not in a header there, so GCC's warning about it is void.
#
CC_OPT_genode_block += -I$(call select_from_repositories,src/lib/genode_c_api)
CC_OPT_genode_block += -Wno-subobject-linkage

# PM_SLEEP changes the layout of 'struct dev_pm_info' (embedded in 'struct
# device'): define it for every unit of this driver
CC_OPT += -DCONFIG_PM_SLEEP

#
# Table of the SCSI device-info flags, generated from the kernel header as
# by the kernel's build (drivers/scsi/Makefile) and included by scsi_sysfs.c
#
CC_OPT_drivers/scsi/scsi_sysfs += -I.

drivers/scsi/scsi_sysfs.o: scsi_devinfo_tbl.c

scsi_devinfo_tbl.c: $(call select_from_ports,pixel_linux)/src/linux/include/scsi/scsi_devinfo.h
	$(MSG_CONVERT)$@
	$(VERBOSE)sed -n 's/.*define *BLIST_\([A-Z0-9_]*\) *.*/BLIST_FLAG_NAME(\1),/p' $< > $@

#
# Vendor UFS host-controller driver (google_soc_gs port), without the inline
# encryption, whose keys are programmed by the security chip
#
GS_DIR  := $(call select_from_ports,google_soc_gs)/src/google_soc_gs
UFS_DIR := $(GS_DIR)/drivers/ufs

UFS_SRC := ufs-exynos.c ufs-exynos-dbg.c ufs-pixel.c gs201/ufs-cal-if.c

SRC_C += $(UFS_SRC)
vpath ufs-%.c        $(UFS_DIR)
vpath gs201/ufs-%.c  $(UFS_DIR)

INC_DIR += $(UFS_DIR) $(UFS_DIR)/gs201 $(GS_DIR)/include

# private headers of the UFS core, as in the vendor's Kbuild
INC_DIR += $(LX_SRC_DIR)/drivers/ufs

$(foreach f,$(UFS_SRC),\
  $(eval CC_OPT_$(basename $(f)) += \
    -DKBUILD_MODNAME='"ufs_exynos_gs"' -DKBUILD_MODFILE='"ufs_exynos_gs"' \
    -DCONFIG_SOC_GS201 -DCONFIG_SCSI_UFS_EXYNOS_GS))

#
# Device tree, see google_pixel7.dts
#
$(call check_tool,dtc)

UFS_DTB := $(TARGET)-google_pixel7.dtb

CUSTOM_TARGET_DEPS += $(INSTALL_DIR)/$(UFS_DTB)
BUILD_ARTIFACTS    := $(TARGET) $(UFS_DTB)

$(INSTALL_DIR)/$(UFS_DTB): $(UFS_DTB)
	$(VERBOSE)cp -f $< $@

$(UFS_DTB): $(PRG_DIR)/google_pixel7.dts
	$(MSG_CONVERT)$@
	$(VERBOSE)dtc -q -I dts -O dtb -o $@ $<
