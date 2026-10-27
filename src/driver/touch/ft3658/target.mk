#
# Touchscreen driver for the Pixel 7: the vendor's driver for the FocalTech
# FT3658U over the vendor's Exynos SPI controller driver, in CPU (FIFO) mode.
# Touch events leave through the input core and the evdev emulation as an
# Event session. Pins, supplies, and the clocks of the SPI controller are
# provided by gs201_pio and gs201_platform.
#

DRIVER  := ft3658
TARGET   = $(DRIVER)_touch
REQUIRES = arm_v8a
LIBS     = base jitterentropy pixel_lx_emul

INC_DIR += $(PRG_DIR)
INC_DIR += $(LX_SRC_DIR)/scripts/dtc/libfdt

SRC_CC  += main.cc
SRC_CC  += lx_emul/pin.cc
SRC_CC  += lx_emul/event.cc
SRC_CC  += genode_c_api/event.cc
SRC_C   += lx_user.c dummies.c
SRC_C   += $(notdir $(wildcard $(PRG_DIR)/generated_dummies.c))
SRC_C   += lx_emul/shadow/drivers/input/evdev.c
SRC_C   += lx_emul/shadow/drivers/clk/clkdev.c
SRC_C   += lx_emul/shadow/drivers/clk/clk-devres.c

# GPIO banks and pin interrupts backed by gs201_pio sessions
SRC_C   += lx_emul/gs201/pio.c
SRC_C   += lx_emul/gs201/common_dummies.c

vpath %.c  $(PRG_DIR)
vpath %.cc $(PRG_DIR)
vpath lx_emul/gs201/%.c $(REP_DIR)/src/lib

GENODE_C_API_SRC_DIR := $(call select_from_repositories,src/lib/genode_c_api)
vpath genode_c_api/event.cc $(dir $(GENODE_C_API_SRC_DIR))

# per-file flags of Linux sources
CC_OPT_drivers/base/regmap/regmap += -I$(LX_SRC_DIR)/drivers/base/regmap

# PM_SLEEP changes the layout of 'struct dev_pm_info' (embedded in 'struct
# device'): define it for every unit of this driver
CC_OPT += -DCONFIG_PM_SLEEP

#
# Vendor SPI controller driver (google_soc_gs port)
#
GS_DIR := $(call select_from_ports,google_soc_gs)/src/google_soc_gs

SRC_C += spi-s3c64xx.c
vpath spi-s3c64xx.c $(GS_DIR)/drivers/spi

INC_DIR += $(GS_DIR)/include

CC_OPT_spi-s3c64xx += -DKBUILD_MODNAME='"spi_s3c64xx"' \
                      -DKBUILD_MODFILE='"spi_s3c64xx"' -DCONFIG_SOC_GS201 \
                      -DCONFIG_SOC_GOOGLE

#
# Vendor touchscreen driver (google_touch port), without offload, heatmap,
# panel bridge, and touch bus negotiation, which are optional in the driver
#
TOUCH_DIR := $(call select_from_ports,google_touch)/src/google_touch
FTS_DIR   := $(TOUCH_DIR)/focaltech/ft3658

FTS_SRC := focaltech_core.c focaltech_ex_fun.c focaltech_ex_mode.c \
           focaltech_gesture.c focaltech_esdcheck.c \
           focaltech_point_report_check.c focaltech_flash.c \
           focaltech_flash/focaltech_upgrade_ft3658u.c focaltech_spi.c

SRC_C += $(FTS_SRC)
vpath focaltech_%.c $(FTS_DIR)
vpath focaltech_flash/%.c $(FTS_DIR)

INC_DIR += $(FTS_DIR) $(FTS_DIR)/include $(TOUCH_DIR)/common \
           $(TOUCH_DIR)/common/include

# display headers of the panel-bridge part, as in the vendor's Kbuild
DISP_DIR := $(call select_from_ports,google_display)/src/google_display
INC_DIR  += $(DISP_DIR) $(DISP_DIR)/samsung/include/uapi

$(foreach f,$(FTS_SRC),\
  $(eval CC_OPT_$(basename $(f)) += \
    -DKBUILD_MODNAME='"focal_touch"' -DKBUILD_MODFILE='"focal_touch"' \
    -DCONFIG_TOUCHSCREEN_FTS -DCONFIG_SOC_GOOGLE -DCONFIG_SOC_GS201 \
    -DCONFIG_TOUCHSCREEN_HEATMAP -DCONFIG_TOUCHSCREEN_PANEL_BRIDGE))

#
# Device tree, see google_pixel7.dts
#
$(call check_tool,dtc)

TOUCH_DTB := $(TARGET)-google_pixel7.dtb

CUSTOM_TARGET_DEPS += $(INSTALL_DIR)/$(TOUCH_DTB)
BUILD_ARTIFACTS    := $(TARGET) $(TOUCH_DTB)

$(INSTALL_DIR)/$(TOUCH_DTB): $(TOUCH_DTB)
	$(VERBOSE)cp -f $< $@

$(TOUCH_DTB): $(PRG_DIR)/google_pixel7.dts
	$(MSG_CONVERT)$@
	$(VERBOSE)dtc -q -I dts -O dtb -o $@ $<
