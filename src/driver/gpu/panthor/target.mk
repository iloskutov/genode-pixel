#
# DDE port of the upstream 'panthor' DRM driver (Mali CSF GPUs) for the
# Mali-G710 of the Pixel 7. Layout as allwinner's lima port, on Genode's
# stock dde_linux base (Linux 6.18.19, panthor_lx_emul), not on the 6.1
# base of the other Pixel drivers. Provides the Gpu session (session.cc,
# include/gpu/panthor_session.h).
#

DRIVER  := panthor
TARGET   = $(DRIVER)_gpu
REQUIRES = arm_v8a
LIBS     = base panthor_lx_emul

INC_DIR += $(PRG_DIR)/include/lx_emul/shadow

SRC_CC  += main.cc
SRC_CC  += emul.cc
SRC_CC  += firmware.cc
SRC_CC  += lx_emul/random_dummy.cc
SRC_CC  += shared_buffer.cc
SRC_CC  += session.cc
SRC_C   += lx_emul.c
SRC_C   += lx_drm.c
SRC_C   += lx_user.c
SRC_C   += clock.c
SRC_C   += dummies.c
SRC_C   += $(notdir $(wildcard $(PRG_DIR)/generated_dummies.c))

vpath %.c  $(PRG_DIR)
vpath %.cc $(PRG_DIR)

#
# Device tree: written by hand for the binding panthor expects
#
BOARDS := google_pixel7
DTB    := $(DRIVER)-google_pixel7.dtb

CUSTOM_TARGET_DEPS += $(INSTALL_DIR)/$(DTB)
BUILD_ARTIFACTS    := $(TARGET) $(DTB)

$(INSTALL_DIR)/$(DTB): $(DTB)
	$(VERBOSE)cp -f $< $@

$(DTB): $(PRG_DIR)/$(DRIVER)-google_pixel7.dts $(MAKEFILE_LIST)
	$(MSG_CONVERT)$@
	$(VERBOSE)dtc -q -Idts -Odtb $< > $@
