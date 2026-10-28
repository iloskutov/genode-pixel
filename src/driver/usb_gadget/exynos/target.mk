#
# USB gadget driver for the Pixel 7: Linux' DWC3 core in device mode with
# the UDC core and the composite framework, the vendor's USB PHY driver
# (google_soc_gs port, with the USBDP combo PHY) and a slim Exynos glue. It
# serves one Usb_gadget session per function. The power of the controller
# comes from gs201_platform, the cable orientation from the Type-C
# controller via gs201_i2c.
#

DRIVER  := exynos
TARGET   = $(DRIVER)_usb_gadget
REQUIRES = arm_v8a
LIBS     = base jitterentropy pixel_lx_emul

INC_DIR += $(PRG_DIR)
INC_DIR += $(LX_SRC_DIR)/scripts/dtc/libfdt

# drivers/usb/dwc3/core.h for the glue
INC_DIR += $(LX_SRC_DIR)/drivers/usb/dwc3

SRC_CC  += main.cc session.cc
SRC_CC  += lx_emul/shared_dma_buffer.cc
SRC_C   += lx_user.c lx_emul.c gadget.c exynos_glue.c dummies.c
SRC_C   += $(notdir $(wildcard $(PRG_DIR)/generated_dummies.c))
SRC_C   += lx_emul/shadow/drivers/clk/clkdev.c
SRC_C   += lx_emul/gs201/common_dummies.c

vpath %.c  $(PRG_DIR)
vpath %.cc $(PRG_DIR)
vpath lx_emul/gs201/%.c $(REP_DIR)/src/lib

# dwc3_gadget_pullup() needs 1 ("active") from a runtime resume (lx_emul.c)
LD_OPT += --wrap=__pm_runtime_resume

# PM_SLEEP changes the layout of 'struct dev_pm_info' (embedded in 'struct
# device'): define it for every unit of this driver
CC_OPT += -DCONFIG_PM_SLEEP

#
# Vendor USB PHY driver (google_soc_gs port), the files its Kbuild builds
# for the GS201 (PHY_EXYNOS_USBDRD, PHY_SAMSUNG_USB_GEN2_V4)
#
GS_DIR  := $(call select_from_ports,google_soc_gs)/src/google_soc_gs
PHY_SRC := phy-exynos-usbdrd.c phy-exynos-usb3p1.c phy-exynos-usbdp-gen2-v4.c

SRC_C   += $(PHY_SRC)
vpath phy-exynos-%.c $(GS_DIR)/drivers/phy/samsung

INC_DIR += $(GS_DIR)/include

$(foreach f,$(PHY_SRC),\
  $(eval CC_OPT_$(basename $(f)) += \
    -DKBUILD_MODNAME='"phy_exynos_usbdrd_super"' \
    -DKBUILD_MODFILE='"phy_exynos_usbdrd_super"' \
    -DCONFIG_PHY_EXYNOS_USBDRD -DCONFIG_PHY_SAMSUNG_USB_GEN2_V4 \
    -DCONFIG_SOC_GS201 -DCONFIG_SOC_GOOGLE))

CC_OPT_exynos_glue += -DKBUILD_MODNAME='"exynos_dwc3_glue"'
CC_OPT_gadget      += -DKBUILD_MODNAME='"usb_gadget"'

#
# Device tree, see google_pixel7.dts
#
$(call check_tool,dtc)

GADGET_DTB := $(TARGET)-google_pixel7.dtb

CUSTOM_TARGET_DEPS += $(INSTALL_DIR)/$(GADGET_DTB)
BUILD_ARTIFACTS    := $(TARGET) $(GADGET_DTB)

$(INSTALL_DIR)/$(GADGET_DTB): $(GADGET_DTB)
	$(VERBOSE)cp -f $< $@

$(GADGET_DTB): $(PRG_DIR)/google_pixel7.dts
	$(MSG_CONVERT)$@
	$(VERBOSE)dtc -q -I dts -O dtb -o $@ $<
