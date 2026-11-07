#
# DDE modem driver for the Pixel 7 (README): the vendor's modem interface
# driver 'cpif' for the Samsung S5300 modem (google_s5300 port) on PCIe
# channel 0 with the vendor's PCIe host-controller and SysMMU drivers
# (google_soc_gs port). The character devices of the cpif are provided as
# Android_chardev sessions (lx_user.c, main.cc).
#

TARGET   = pixel_modem
REQUIRES = arm_v8a
LIBS     = base jitterentropy pixel_lx_emul android_chardev_server

INC_DIR += $(PRG_DIR)
INC_DIR += $(LX_SRC_DIR)/scripts/dtc/libfdt

SRC_CC  += main.cc
SRC_C   += lx_user.c lx_emul.c dummies.c
SRC_C   += $(notdir $(wildcard $(PRG_DIR)/generated_dummies.c))

# common 6.1 kernel dummies
SRC_C   += lx_emul/gs201/common_dummies.c

#
# The resource tree (PCI) and the network core (the cpif's network devices)
# are built, the common dummies of their parts are renamed and unused, as is
# the one that drops the softirqs raised in interrupt context (lx_emul.c)
#
COMMON_UNUSED := insert_resource release_resource iomem_resource \
                 ioport_resource skb_init net_ns_init do_softirq_own_stack

CC_OPT_lx_emul/gs201/common_dummies += \
	$(foreach s,$(COMMON_UNUSED),-D$(s)=common_unused_$(s))

#
# The device nodes are used from inside the kernel (lx_user.c), "user"
# pointers of an ioctl are kernel pointers: arm64's raw_copy_to/from_user
# become plain copies (lx_emul.c). The common dummies define the two
# functions as trace-and-stop and keep their names.
#
CC_OPT += -D__arch_copy_to_user=lx_emul_modem_copy_to_user \
          -D__arch_copy_from_user=lx_emul_modem_copy_from_user
CC_OPT_lx_emul/gs201/common_dummies += -U__arch_copy_to_user \
                                       -U__arch_copy_from_user

# GPIO banks and pin interrupts backed by gs201_pio sessions
SRC_CC  += lx_emul/pin.cc
SRC_C   += lx_emul/gs201/pio.c

SRC_C   += lx_emul/shadow/fs/libfs.c

vpath %.c  $(PRG_DIR)
vpath %.cc $(PRG_DIR)
vpath lx_emul/gs201/%.c $(REP_DIR)/src/lib

# PM_SLEEP changes the layout of 'struct dev_pm_info', as in all pixel DDEs
CC_OPT += -DCONFIG_PM_SLEEP

# DesignWare PCIe host, a Kconfig option that is selected only
CC_OPT += -DCONFIG_PCIE_DW_HOST -DCONFIG_PCIE_DW

# per-file flags of Linux sources
CC_OPT_drivers/pci/msi/msi += -I$(LX_SRC_DIR)/drivers/pci/msi
INC_DIR += $(LX_SRC_DIR)/drivers/pci/controller/dwc

#
# The short sleeps of the PCIe link training (usleep_range(10, 12), up to
# thousands of times per attempt) would each take a scheduling round:
# usleep_range_state of lx_emul.c busy-waits for sleeps below 1 ms and
# calls the one of the kernel, renamed, for longer ones
#
CC_OPT_kernel/time/timer += -Dusleep_range_state=timer_usleep_range_state

GS_DIR := $(call select_from_ports,google_soc_gs)/src/google_soc_gs

INC_DIR += $(GS_DIR)/include

SOC_DEFS := -DCONFIG_SOC_GS201 -DCONFIG_SOC_GOOGLE -DCONFIG_ARCH_EXYNOS

# the vendor's debug log of the PCIe link and the CP's GPIOs stays out of
# the log (misc/logbuffer.h)
CC_OPT += -DLOGBUFFER_DROP

#
# Options of the vendor's build of the cpif for the phone (pantah_defconfig)
#
# CONFIG_CH_EXTENSION selects the extended namespace of the channel IDs
# that the firmware of the CP uses, without it the CP does not take the
# messages of the AP.
#
CPIF_DEFS := -DCONFIG_EXYNOS_MODEM_IF -DCONFIG_SEC_MODEM_S5100 \
             -DCONFIG_CP_SECURE_BOOT -DCONFIG_LINK_DEVICE_PCIE \
             -DCONFIG_LINK_DEVICE_PCIE_IOCC -DCONFIG_LINK_DEVICE_PCIE_IOMMU \
             -DCONFIG_LINK_DEVICE_WITH_SBD_ARCH -DCONFIG_CP_PKTPROC \
             -DCONFIG_CP_PKTPROC_UL -DCONFIG_CH_EXTENSION -DCONFIG_SHM_IPC \
             -DCONFIG_GOOGLE_MODEMCTL -DCONFIG_EXYNOS_PCIE_IOMMU \
             -DCONFIG_CPIF_AP_SUSPEND_DURING_VOICE_CALL \
             -DCONFIG_PCI_EXYNOS_GS '-DCONFIG_OPTION_REGION=""'

CC_OPT += $(CPIF_DEFS)

#
# The cpif (google_s5300 port). Not built: the SPI boot device (the CP's
# boot loader is loaded over PCIe), the boot log (cp_btl), the throughput
# monitor and the packet steering (DIT), which the phone's build has as
# separate modules.
#
CPIF_DIR := $(call select_from_ports,google_s5300)/src/google_s5300

CPIF_SRC := modem_main.c modem_variation.c modem_ctrl_s5100.c modem_ctrl.c \
            modem_io_device.c net_io_device.c bootdump_io_device.c \
            ipc_io_device.c modem_toe_device.c modem_utils.c modem_dump.c \
            link_device.c link_device_memory_flow_control.c \
            link_device_memory_debug.c link_device_memory_snapshot.c \
            link_device_memory_legacy.c link_device_memory_sbd.c \
            s51xx_pcie.c link_rx_pktproc.c link_tx_pktproc.c \
            link_device_pcie_iommu.c

SRC_C += $(CPIF_SRC)
vpath %.c $(CPIF_DIR)

INC_DIR += $(CPIF_DIR) $(CPIF_DIR)/include

#
# There is no linear map in DDE Linux: the cpif's phys_to_virt of the
# shared memory with the CP becomes a mapping of the I/O memory (lx_emul.c).
# The cpif's wakeup sources are kept by lx_emul.c, the one of the common
# dummies registers none.
#
$(foreach f,$(CPIF_SRC),\
  $(eval CC_OPT_$(basename $(f)) += \
    -DKBUILD_MODNAME='"cpif"' -DKBUILD_MODFILE='"cpif"' $(SOC_DEFS) \
    -include $(PRG_DIR)/cpif_shim.h))

CC_OPT_lx_user += -DKBUILD_MODNAME='"cpif"' $(SOC_DEFS)

#
# The ID table of s51xx_pcie.c matches any Samsung device, the root port of
# the controller as well. lx_emul.c registers the driver for the modem only.
#
CC_OPT_s51xx_pcie += -D__pci_register_driver=lx_emul_s51xx_pci_register_driver

#
# Reserved memory of the CP (shm_ipc.c of the google_s5300 port), built as
# a module for the lookup of the regions at probe time (shm_ipc_shim.h)
#
SRC_C += shm_ipc.c
CC_OPT_shm_ipc += -DMODULE -DKBUILD_MODNAME='"shm_ipc"' \
                  -DKBUILD_MODFILE='"shm_ipc"' $(SOC_DEFS) \
                  -include $(PRG_DIR)/cpif_shim.h \
                  -include $(PRG_DIR)/shm_ipc_shim.h

#
# Vendor PCIe host-controller driver of the modem link, the variant with the
# address translation programmed by the secure monitor (dwc-whi)
#
GS_PCIE_DIR := $(GS_DIR)/drivers/pci/controller/dwc-whi
GS_PCIE_SRC := pcie-exynos-rc.c pcie-exynos-gs201-rc-cal.c pcie-exynos-dbg.c

SRC_C   += $(GS_PCIE_SRC)
vpath pcie-exynos-%.c $(GS_PCIE_DIR)

INC_DIR += $(GS_PCIE_DIR)

$(foreach f,$(GS_PCIE_SRC),\
  $(eval CC_OPT_$(basename $(f)) += \
    -DKBUILD_MODNAME='"pcie_exynos_gs"' -DKBUILD_MODFILE='"pcie_exynos_gs"' \
    $(SOC_DEFS) -DANA_RX_DFE_EOM_PI_DIVSEL_G34=0x984))

#
# cpif asks for the width of each link, the driver takes it from the device
# tree: lx_emul.c sets it before the link comes up (exynos_pcie_poweron)
#
CC_OPT_pcie-exynos-rc += -Dexynos_pcie_poweron=vendor_exynos_pcie_poweron \
                         -include $(PRG_DIR)/pcie_shim.h

#
# Vendor SysMMU driver of the link. Its page tables come from kmem_cache
# (USE_DYNAMIC_MEM_ALLOC), the gen_pool variant needs a linear map.
#
GS_IOMMU_DIR := $(GS_DIR)/drivers/iommu

SRC_C += exynos-pcie-iommu-whi.c
vpath exynos-pcie-iommu-whi.c $(GS_IOMMU_DIR)

INC_DIR += $(GS_IOMMU_DIR)

CC_OPT_exynos-pcie-iommu-whi += -DKBUILD_MODNAME='"pcie_iommu"' \
                                -DKBUILD_MODFILE='"pcie_iommu"' $(SOC_DEFS) \
                                -DUSE_DYNAMIC_MEM_ALLOC \
                                -include $(PRG_DIR)/iommu_shim.h

#
# Notifier chain of the modem's state (google_soc_gs port)
#
SRC_C += modem_notifier.c
vpath modem_notifier.c $(GS_DIR)/drivers/soc/google/modemctl

CC_OPT_modem_notifier += -DKBUILD_MODNAME='"google_modemctl"' \
                         -DKBUILD_MODFILE='"google_modemctl"' $(SOC_DEFS)

#
# Device tree, see google_pixel7.dts
#
$(call check_tool,dtc)

MODEM_DTB := $(TARGET)-google_pixel7.dtb

CUSTOM_TARGET_DEPS += $(INSTALL_DIR)/$(MODEM_DTB)
BUILD_ARTIFACTS    := $(TARGET) $(MODEM_DTB)

$(INSTALL_DIR)/$(MODEM_DTB): $(MODEM_DTB)
	$(VERBOSE)cp -f $< $@

$(MODEM_DTB): $(PRG_DIR)/google_pixel7.dts
	$(MSG_CONVERT)$@
	$(VERBOSE)dtc -q -I dts -O dtb -o $@ $<
