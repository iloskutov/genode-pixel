#
# WiFi driver library of the Pixel 7, used as 'wifi.lib.so' by Genode's WiFi
# driver: the vendor's driver for the Broadcom chip (port bcmdhd_wlan) on
# PCIe channel 1 with the vendor's PCIe host-controller driver (port
# google_soc_gs), see src/driver/wifi/pixel/README
#

REQUIRES := arm_v8a

TARGET_LIB_DIR := $(REP_DIR)/src/driver/wifi/pixel

#
# The Linux-facing files of the wifi library in their form for the 6.1
# kernel, searched before the ones of the dde_linux repository
#
vpath lx_emul.c        $(TARGET_LIB_DIR)
vpath lx_socket_call.c $(TARGET_LIB_DIR)
vpath uplink.c         $(TARGET_LIB_DIR)

include $(call select_from_repositories,lib/mk/wifi.inc)

LIBS += pixel_lx_emul

INC_DIR += $(TARGET_LIB_DIR) $(TARGET_LIB_DIR)/compat
INC_DIR += $(LX_SRC_DIR)/scripts/dtc/libfdt

SRC_CC += lx_emul/pin.cc
SRC_CC += dtb_helper.cc

SRC_C += dummies.c
SRC_C += $(notdir $(wildcard $(TARGET_LIB_DIR)/generated_dummies.c))
SRC_C += lx_emul/shadow/fs/libfs.c

# GPIO banks backed by gs201_pio sessions
SRC_C += lx_emul/gs201/pio.c
vpath lx_emul/gs201/%.c $(REP_DIR)/src/lib

vpath %.c  $(TARGET_LIB_DIR)
vpath %.cc $(TARGET_LIB_DIR)

#
# The fixed interface index of 'wlan.cc' does not fit, uplink.c looks up
# the index of 'wlan0' instead
#
CC_OPT_wlan += -Dwifi_ifindex=wlan_cc_wifi_ifindex

# PM_SLEEP changes the layout of 'struct dev_pm_info' (embedded in 'struct
# device'): define it for every unit of this library
CC_OPT += -DCONFIG_PM_SLEEP

# DesignWare PCIe host, a Kconfig option that is selected only
CC_OPT += -DCONFIG_PCIE_DW_HOST -DCONFIG_PCIE_DW

# per-file flags of Linux sources
CC_OPT_net/wireless/trace    += -I$(LX_SRC_DIR)/net/wireless
CC_OPT_drivers/pci/msi/msi   += -I$(LX_SRC_DIR)/drivers/pci/msi
INC_DIR += $(LX_SRC_DIR)/drivers/pci/controller/dwc

#
# Vendor PCIe host-controller driver (google_soc_gs port)
#
GS_DIR      := $(call select_from_ports,google_soc_gs)/src/google_soc_gs
GS_PCIE_DIR := $(GS_DIR)/drivers/pci/controller/dwc

GS_PCIE_SRC := pcie-exynos-rc.c pcie-exynos-gs201-rc-cal.c pcie-exynos-dbg.c

SRC_C   += $(GS_PCIE_SRC)
vpath pcie-exynos-%.c $(GS_PCIE_DIR)

INC_DIR += $(GS_PCIE_DIR) $(GS_DIR)/include

$(foreach f,$(GS_PCIE_SRC),\
  $(eval CC_OPT_$(basename $(f)) += \
    -DKBUILD_MODNAME='"pcie_exynos_gs"' -DKBUILD_MODFILE='"pcie_exynos_gs"' \
    -DCONFIG_SOC_GS201 -DCONFIG_SOC_GOOGLE -DCONFIG_ARCH_EXYNOS \
    -DCONFIG_PCI_EXYNOS_GS -DANA_RX_DFE_EOM_PI_DIVSEL_G34=0x984))

#
# Vendor WiFi driver (bcmdhd_wlan port), with the options of the vendor's
# build for the BCM4389 of the Pixel 7, without the power management of the
# bus (deep sleep, out-of-band wakeup, runtime PM, ASPM)
#
BCMDHD_DIR := $(call select_from_ports,bcmdhd_wlan)/src/bcmdhd_wlan

include $(REP_DIR)/lib/mk/pixel_wifi_bcmdhd.inc

SRC_C   += $(BCMDHD_SRC)
vpath %.c $(BCMDHD_DIR)

INC_DIR += $(BCMDHD_DIR)/include $(BCMDHD_DIR)

$(foreach f,$(BCMDHD_SRC),\
  $(eval CC_OPT_$(basename $(f)) += \
    -DKBUILD_MODNAME='"bcmdhd4389"' -DKBUILD_MODFILE='"bcmdhd4389"' \
    $(BCMDHD_OPT)))

# the physical address of a buffer for a log message
CC_OPT_dhd_log_dump += -D__virt_to_phys=__pa

#
# Device tree, see google_pixel7.dts
#
$(call check_tool,dtc)

WIFI_DTB := pixel_wifi-google_pixel7.dtb

CUSTOM_TARGET_DEPS += $(INSTALL_DIR)/$(WIFI_DTB)
BUILD_ARTIFACTS    := $(LIB).lib.so $(WIFI_DTB)

$(INSTALL_DIR)/$(WIFI_DTB): $(WIFI_DTB)
	$(VERBOSE)cp -f $< $@

$(WIFI_DTB): $(TARGET_LIB_DIR)/google_pixel7.dts
	$(MSG_CONVERT)$@
	$(VERBOSE)dtc -q -I dts -O dtb -o $@ $<
