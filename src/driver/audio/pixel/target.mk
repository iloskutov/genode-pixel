#
# DDE audio driver for the Pixel 7 (README). One component on the 6.1
# lx_emul core (pixel_lx_emul) holds
#
#   - the Trusty client (google_trusty port: SMC core, virtio transport, TIPC),
#     with the 'smc' instruction, the forwarded-interrupt driver and the
#     scheduler-share page replaced (trusty_smc.c),
#   - the GSA client (google_soc_gs port: non-secure mailbox + TIPC channel to
#     the hardware manager),
#   - the AoC core (google_aoc port: aoc, mailbox-wc, service core, aoc_ipc),
#   - the sound card: ALSA and ASoC of the kernel, aoc_alsa (google_aoc port),
#     the CS35L41 amplifiers with wm_adsp (google_amplifiers port) on the
#     vendor SPI controller driver (google_soc_gs port),
#   - the Genode side: Record/Play sessions, config and mixer report
#     (audio.cc), driven by the card task (card.c).
#
# The bring-up is staged by the 'stage' attribute of the 'settings' ROM
# (lx_user.c, card.c, amp.c, README).
#

TARGET   = pixel_audio
REQUIRES = arm_v8a
LIBS     = base jitterentropy pixel_lx_emul

INC_DIR += $(PRG_DIR)
INC_DIR += $(LX_SRC_DIR)/scripts/dtc/libfdt

SRC_CC  += main.cc
SRC_C   += lx_user.c lx_emul.c trusty_smc.c s2mpu.c aoc_log.c dummies.c
SRC_C   += $(notdir $(wildcard $(PRG_DIR)/generated_dummies.c))

# common 6.1 kernel dummies and the ROM-backed firmware loader
SRC_C   += lx_emul/gs201/common_dummies.c
SRC_C   += lx_emul/gs201/rom_firmware_lx.c
SRC_CC  += lx_emul/gs201/rom_firmware.cc

vpath %.c  $(PRG_DIR)
vpath %.cc $(PRG_DIR)
vpath lx_emul/gs201/%.c  $(REP_DIR)/src/lib
vpath lx_emul/gs201/%.cc $(REP_DIR)/src/lib

# PM_SLEEP changes the layout of 'struct dev_pm_info', as in all pixel DDEs
CC_OPT += -DCONFIG_PM_SLEEP

#
# Trusty client (google_trusty port). Not built: trusty-irq.c (forwarded
# interrupts cannot reach a component), trusty-log.c, trusty-test.c,
# trusty-sched-share.c (stubbed in trusty_smc.c) and trusty-smc-arm64.S
# (replaced by trusty_smc.c).
#
TRUSTY_DIR := $(call select_from_ports,google_trusty)/src/google_trusty
TRUSTY_SRC := trusty.c trusty-mem.c trusty-virtio.c trusty-ipc.c

SRC_C += $(TRUSTY_SRC)
vpath trusty%.c $(TRUSTY_DIR)/drivers/trusty

# public headers and the directory of the trace headers (TRACE_INCLUDE_PATH .)
INC_DIR += $(TRUSTY_DIR)/include $(TRUSTY_DIR)/drivers/trusty

TRUSTY_DEFS := -DCONFIG_TRUSTY=1 -DCONFIG_TRUSTY_VIRTIO=1 \
               -DCONFIG_TRUSTY_VIRTIO_IPC=1

$(foreach f,$(TRUSTY_SRC),\
  $(eval CC_OPT_$(basename $(f)) += \
    -DKBUILD_MODNAME='"$(subst -,_,$(basename $(f)))"' \
    -DKBUILD_MODFILE='"$(subst -,_,$(basename $(f)))"' $(TRUSTY_DEFS)))

#
# trusty.c: no DAIF masking at EL0 (trusty_shim.h); the CPU-hotplug state
# machine is the one of lx_emul.c, which calls the 'online' callback that
# unparks the NOP thread (the common dummy drops it)
#
CC_OPT_trusty += -include $(PRG_DIR)/trusty_shim.h \
                 -D__cpuhp_setup_state=pixel_audio_cpuhp_setup_state \
                 -D__cpuhp_remove_state=pixel_audio_cpuhp_remove_state \
                 -D__cpuhp_state_add_instance=pixel_audio_cpuhp_state_add_instance \
                 -D__cpuhp_state_remove_instance=pixel_audio_cpuhp_state_remove_instance

# trusty-mem.c: MAIR_EL1 cannot be read at EL0 (trusty_mem_shim.h)
CC_OPT_trusty-mem += -include $(PRG_DIR)/trusty_mem_shim.h

# trusty-virtio.c: per-device DMA ops are kept by lx_emul.c (no CONFIG_DMA_OPS)
CC_OPT_trusty-virtio += -include $(PRG_DIR)/trusty_virtio_shim.h

#
# virtio ring: map and unmap buffers through the DMA ops registered for the
# device (lx_emul.c), the shadow DMA code knows no per-device ops
#
CC_OPT_drivers/virtio/virtio_ring += \
	-Ddma_map_page_attrs=pixel_audio_dma_map_page_attrs \
	-Ddma_unmap_page_attrs=pixel_audio_dma_unmap_page_attrs

#
# GSA client (google_soc_gs port), without pKVM (no GSA wakelock / s2mpu_gsa
# resume around data transfers), without the log reader, GSC and TZ_PROT
#
GS_DIR  := $(call select_from_ports,google_soc_gs)/src/google_soc_gs
GSA_DIR := $(GS_DIR)/drivers/soc/google/gsa
GSA_SRC := gsa_core.c gsa_mbox.c gsa_tz.c

SRC_C += $(GSA_SRC)
vpath gsa_%.c $(GSA_DIR)

INC_DIR += $(GS_DIR)/include $(GS_DIR)/include/uapi

$(foreach f,$(GSA_SRC),\
  $(eval CC_OPT_$(basename $(f)) += \
    -DKBUILD_MODNAME='"gsa"' -DKBUILD_MODFILE='"gsa"' \
    -DCONFIG_GSA=1 $(TRUSTY_DEFS)))

# gsa_priv.h and gsa_mbox.h for the staged bring-up
CC_OPT_lx_user += -I$(GSA_DIR) $(TRUSTY_DEFS)
CC_OPT_lx_emul += -I$(GSA_DIR) $(TRUSTY_DEFS)

#
# AoC core (google_aoc port). Not built: the service drivers (char, control,
# channel devices), ion_physical_heap.c (stubbed), aoc/alsa and usb. What
# aocd does with the character device of the service "logging" is done by
# aoc_log.c.
#
AOC_DIR     := $(call select_from_ports,google_aoc)/src/google_aoc
AOC_SRC     := aoc.c aoc_v1.c aoc_firmware.c aoc_service_core.c
AOC_IPC_SRC := aoc_ipc_core.c

SRC_C += $(AOC_SRC) $(AOC_IPC_SRC) mailbox-wc.c
vpath aoc.c              $(AOC_DIR)/aoc
vpath aoc_v1.c           $(AOC_DIR)/aoc
vpath aoc_firmware.c     $(AOC_DIR)/aoc
vpath aoc_service_core.c $(AOC_DIR)/aoc
vpath mailbox-wc.c       $(AOC_DIR)/aoc
vpath aoc_ipc_core.c     $(AOC_DIR)/aoc_ipc

AOC_DEFS := -DCONFIG_SOC_GS201=1 -DCONFIG_SOC_GOOGLE=1 \
            -I$(AOC_DIR)/aoc -I$(AOC_DIR)/aoc_ipc \
            -I$(GS_DIR)/drivers/dma-buf/heaps \
            -I$(call select_from_ports,pixel_linux)/src/linux/drivers/dma-buf

$(foreach f,$(AOC_SRC) $(AOC_IPC_SRC),\
  $(eval CC_OPT_$(basename $(f)) += \
    -DKBUILD_MODNAME='"aoc_core"' -DKBUILD_MODFILE='"aoc_core"' $(AOC_DEFS)))

CC_OPT_mailbox-wc += -DKBUILD_MODNAME='"mailbox_wc"' \
                     -DKBUILD_MODFILE='"mailbox_wc"' $(AOC_DEFS)

#
# aoc.c: IOMMU, I/O mapping, wakeup source and the two GSA calls of the
# firmware load go through lx_emul.c (aoc_shim.h)
#
CC_OPT_aoc += -include $(PRG_DIR)/aoc_shim.h

# aoc.h and the GSA interface for the staged bring-up
CC_OPT_lx_user += $(AOC_DEFS)
CC_OPT_lx_emul += $(AOC_DEFS)
CC_OPT_dummies += $(AOC_DEFS)
CC_OPT_aoc_log += $(AOC_DEFS)

#
# Stage B: the sound card
#
# ALSA core, ASoC, regmap-spi, SPI core and gpiolib come from the kernel
# (source.list). GPIO banks and pin interrupts are gs201_pio sessions
# (lx_emul/gs201/pio.c), the SPI7 clocks are clocks of the platform device
# (clkdev shadow).
#
SRC_CC += lx_emul/pin.cc
SRC_C  += lx_emul/gs201/pio.c
SRC_C  += lx_emul/shadow/drivers/clk/clkdev.c
SRC_C  += lx_emul/shadow/drivers/clk/clk-devres.c
SRC_C  += card.c amp.c
SRC_CC += audio.cc

#
# The ALSA devices are used through their file operations from inside the
# kernel (card.c), "user" pointers are kernel pointers: arm64's
# raw_copy_to/from_user become plain copies (lx_emul.c). The common dummies
# define the two functions as trace-and-stop and keep their names.
#
CC_OPT += -D__arch_copy_to_user=pixel_audio_copy_to_user \
          -D__arch_copy_from_user=pixel_audio_copy_from_user
CC_OPT_lx_emul/gs201/common_dummies += -U__arch_copy_to_user \
                                       -U__arch_copy_from_user

CC_OPT_drivers/base/regmap/regmap     += -I$(LX_SRC_DIR)/drivers/base/regmap
CC_OPT_drivers/base/regmap/regmap-spi += -I$(LX_SRC_DIR)/drivers/base/regmap

#
# ALSA registers one set of file operations for all its minors
# (sound/core/sound.c). The common dummy of '__register_chrdev' drops them;
# card.c keeps them to open control and PCM devices from inside the kernel.
#
CC_OPT_sound/core/sound += -D__register_chrdev=pixel_audio_register_chrdev

#
# aoc_alsa (google_aoc port): machine driver, PCM/voice/nohost platforms,
# routing component, controls, AoC command layer. Not built: compress
# offload, in-call, VoIP, USB and DisplayPort platforms (stubs in
# aoc_alsa_stubs.c, the device tree re-points their links).
#
AOC_ALSA_SRC := aoc_alsa_drv.c aoc_alsa_card.c aoc_alsa_pcm.c \
                aoc_alsa_path.c aoc_alsa_ctl.c aoc_alsa_hw.c \
                aoc_alsa_voice.c aoc_alsa_nohost.c

SRC_C += $(AOC_ALSA_SRC) aoc_alsa_stubs.c
vpath aoc_alsa_%.c $(AOC_DIR)/aoc/alsa

AMP_DIR := $(call select_from_ports,google_amplifiers)/src/google_amplifiers

AOC_ALSA_DEFS := $(AOC_DEFS) -I$(AOC_DIR)/aoc/alsa \
                 -I$(call select_from_ports,pixel_linux)/src/linux/sound/usb \
                 -I$(AMP_DIR)/audiometrics

$(foreach f,$(AOC_ALSA_SRC),\
  $(eval CC_OPT_$(basename $(f)) += \
    -DKBUILD_MODNAME='"aoc_alsa_dev_util"' \
    -DKBUILD_MODFILE='"aoc_alsa_dev_util"' $(AOC_ALSA_DEFS)))

#
# aoc_alsa_hw.c: the reply poll of an AoC command must let jiffies advance
# (aoc_alsa_hw_shim.h). CONFIG_GOOGLE_MODEMCTL selects the declarations of
# soc/google/modem_notifier.h, whose inline variants do not compile; no
# function of it is called (CONFIG_EXYNOS_MODEM_IF is off: the voice-call
# notification to the modem driver is left out, it only handles wake locks
# there).
#
CC_OPT_aoc_alsa_hw += -include $(PRG_DIR)/aoc_alsa_hw_shim.h \
                      -DCONFIG_GOOGLE_MODEMCTL=1

CC_OPT_aoc_alsa_stubs += $(AOC_ALSA_DEFS)
CC_OPT_card           += $(AOC_ALSA_DEFS)

#
# CS35L41 amplifiers: Google's fork of the codec driver and of wm_adsp
# (google_amplifiers port; device-tree binding, control names and firmware
# names differ from the in-tree driver). The
# in-tree cs35l41*, wm_adsp and cs_dsp are in no source list.
#
AMP_SRC := cs35l41.c cs35l41-spi.c cs35l41-tables.c

SRC_C += $(AMP_SRC) wm_adsp.c
vpath cs35l41%.c $(AMP_DIR)/cs35l41
vpath wm_adsp.c  $(AMP_DIR)/snd_soc_wm_adsp

AMP_DEFS := -I$(AMP_DIR)/cs35l41 -I$(AMP_DIR)/cs35l41/include \
            -I$(AMP_DIR)/snd_soc_wm_adsp

CC_OPT_cs35l41        += -DKBUILD_MODNAME='"snd_soc_cs35l41"' \
                         -DKBUILD_MODFILE='"snd_soc_cs35l41"' $(AMP_DEFS)
CC_OPT_cs35l41-tables += -DKBUILD_MODNAME='"snd_soc_cs35l41"' \
                         -DKBUILD_MODFILE='"snd_soc_cs35l41"' $(AMP_DEFS)
CC_OPT_cs35l41-spi    += -DKBUILD_MODNAME='"snd_soc_cs35l41_spi"' \
                         -DKBUILD_MODFILE='"snd_soc_cs35l41_spi"' $(AMP_DEFS)
CC_OPT_wm_adsp        += -DKBUILD_MODNAME='"snd_soc_wm_adsp"' \
                         -DKBUILD_MODFILE='"snd_soc_wm_adsp"' $(AMP_DEFS)
CC_OPT_amp            += $(AMP_DEFS)

#
# SPI7 controller: the vendor Exynos SPI driver (google_soc_gs port), CPU
# (FIFO) mode, as in the touch driver
#
SRC_C += spi-s3c64xx.c
vpath spi-s3c64xx.c $(GS_DIR)/drivers/spi

CC_OPT_spi-s3c64xx += -DKBUILD_MODNAME='"spi_s3c64xx"' \
                      -DKBUILD_MODFILE='"spi_s3c64xx"' -DCONFIG_SOC_GS201 \
                      -DCONFIG_SOC_GOOGLE

#
# Device tree, see google_pixel7.dts
#
$(call check_tool,dtc)

AUDIO_DTB := $(TARGET)-google_pixel7.dtb

CUSTOM_TARGET_DEPS += $(INSTALL_DIR)/$(AUDIO_DTB)
BUILD_ARTIFACTS    := $(TARGET) $(AUDIO_DTB)

$(INSTALL_DIR)/$(AUDIO_DTB): $(AUDIO_DTB)
	$(VERBOSE)cp -f $< $@

$(AUDIO_DTB): $(PRG_DIR)/google_pixel7.dts
	$(MSG_CONVERT)$@
	$(VERBOSE)dtc -q -I dts -O dtb -o $@ $<
