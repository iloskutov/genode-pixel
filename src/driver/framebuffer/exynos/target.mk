#
# Framebuffer driver for the display of the Pixel 7: the DRM core of Linux,
# the vendor's exynos-drm driver (DECON, DPP, DSIM, CAL 9845 and 9855 for the
# GS201), and the s6e3fc3-p10 panel driver. The driver takes over the display
# pipeline as set up by the boot loader. Pins, supplies, and power domains
# are provided by gs201_pio and gs201_platform.
#

DRIVER  := exynos
TARGET   = $(DRIVER)_fb
REQUIRES = arm_v8a
LIBS     = base blit jitterentropy pixel_lx_emul

INC_DIR += $(PRG_DIR)
INC_DIR += $(LX_SRC_DIR)/scripts/dtc/libfdt

SRC_CC  += main.cc
SRC_CC  += lx_emul/pin.cc
SRC_C   += fb.c lx_user.c lx_emul.c dummies.c
SRC_C   += $(notdir $(wildcard $(PRG_DIR)/generated_dummies.c))

# GPIO banks backed by gs201_pio sessions
SRC_C   += lx_emul/gs201/pio.c
SRC_C   += lx_emul/gs201/common_dummies.c

vpath %.c  $(PRG_DIR)
vpath %.cc $(PRG_DIR)
vpath lx_emul/gs201/%.c $(REP_DIR)/src/lib

CC_OPT_lx_emul += -DKBUILD_MODNAME='"exynos_fb"'

# records the DRM device for the DRM client in lx_user.c (lx_emul.c)
LD_OPT += --wrap=drm_dev_register

# per-file flags of Linux sources
CC_OPT_drivers/base/regmap/regmap   += -I$(LX_SRC_DIR)/drivers/base/regmap
CC_OPT_drivers/gpu/drm/drm_plane_helper += -Wno-uninitialized

# VIDEOMODE_HELPERS is a hidden Kconfig symbol (select-only): enable the
# drm_modes.c part (drm_display_mode_to_videomode, used by the DSIM)
CC_OPT_drivers/gpu/drm/drm_modes += -DCONFIG_VIDEOMODE_HELPERS

#
# Vendor display stack (google_display port)
#
DISP_DIR := $(call select_from_ports,google_display)/src/google_display
SAMSUNG  := $(DISP_DIR)/samsung
COMMON   := $(DISP_DIR)/common

EXYNOS_DRM_SRC := \
	cal_9845/decon_reg.c cal_9845/dsim_reg.c cal_9845/dpp_reg.c \
	cal_9845/dqe_reg.c cal_9845/hdr_reg.c \
	cal_9855/decon_reg.c cal_9855/dpp_reg.c cal_9855/dqe_reg.c \
	exynos_drm_drv.c exynos_drm_crtc.c exynos_drm_connector.c \
	exynos_drm_fb.c exynos_drm_format.c exynos_drm_gem.c exynos_drm_plane.c \
	exynos_drm_debug.c exynos_drm_dqe.c exynos_drm_hibernation.c \
	exynos_drm_partial.c exynos_drm_recovery.c \
	exynos_drm_decon.c exynos_drm_dpp.c exynos_drm_dsim.c \
	panel/panel-samsung-drv.c panel/panel-samsung-s6e3fc3-p10.c

GS_DRM_SRC := gs_drm/gs_drm_connector.c

SRC_C += $(EXYNOS_DRM_SRC) $(GS_DRM_SRC)

vpath cal_9845/%.c $(SAMSUNG)
vpath cal_9855/%.c $(SAMSUNG)
vpath exynos_drm_%.c $(SAMSUNG)
vpath panel/%.c $(SAMSUNG)
vpath gs_drm/%.c $(COMMON)

INC_DIR += $(SAMSUNG) $(SAMSUNG)/cal_common $(SAMSUNG)/cal_9845 $(SAMSUNG)/cal_9855
INC_DIR += $(SAMSUNG)/include $(SAMSUNG)/include/uapi $(SAMSUNG)/panel
INC_DIR += $(COMMON)/include

# soc/google/exynos-el3_mon.h and other gs SoC headers
INC_DIR += $(call select_from_ports,google_soc_gs)/src/google_soc_gs/include
INC_DIR += $(call select_from_ports,google_soc_gs)/src/google_soc_gs/include/uapi

# PM_SLEEP changes the layout of 'struct dev_pm_info' (embedded in 'struct
# device'): define it for every unit of this driver
CC_OPT += -DCONFIG_PM_SLEEP

# gs201 build of the vendor Kbuild (samsung/Kbuild, panel/Kbuild); TUI,
# writeback, DisplayPort and BTS are off
# (__linux__: the CAL selects its Linux OS layer with it, cal_config.h)
DISP_DEFS := -D__linux__ -DCONFIG_SOC_GS201 -DCONFIG_DRM_SAMSUNG \
             -DCONFIG_DRM_SAMSUNG_DECON -DCONFIG_DRM_SAMSUNG_DPP \
             -DCONFIG_DRM_SAMSUNG_DSI \
             -DCONFIG_DRM_PANEL_SAMSUNG_COMMON \
             -DCONFIG_DRM_PANEL_SAMSUNG_S6E3FC3_P10

# arm64's phys_to_page, not provided by lx_emul (handover framebuffer)
CC_OPT_exynos_drm_fb += '-Dphys_to_page(p)=pfn_to_page(__phys_to_pfn(p))'

$(foreach f,$(EXYNOS_DRM_SRC) $(GS_DRM_SRC),\
  $(eval CC_OPT_$(basename $(f)) += \
    -DKBUILD_MODNAME='"exynos_drm"' -DKBUILD_MODFILE='"exynos_drm"' $(DISP_DEFS)))

#
# Device tree, see google_pixel7.dts
#
$(call check_tool,dtc)

FB_DTB := $(TARGET)-google_pixel7.dtb

CUSTOM_TARGET_DEPS += $(INSTALL_DIR)/$(FB_DTB)
BUILD_ARTIFACTS    := $(TARGET) $(FB_DTB)

$(INSTALL_DIR)/$(FB_DTB): $(FB_DTB)
	$(VERBOSE)cp -f $< $@

$(FB_DTB): $(PRG_DIR)/google_pixel7.dts
	$(MSG_CONVERT)$@
	$(VERBOSE)dtc -q -I dts -O dtb -o $@ $<
