#
# Linux emulation environment for drivers that are based on the Linux 6.1
# kernel of the Pixel 7 (port 'pixel_linux')
#
# The emulation environment is the one of Genode's 'dde_linux' repository,
# which follows a much newer kernel. The Linux-facing files that the 6.1
# kernel needs in another form are provided by 'src/lx_emul_6_1' of this
# repository and take precedence. Everything else, in particular all
# Genode-facing code, comes from 'dde_linux'. See src/lx_emul_6_1/README.
#

LIBS += pixel_linux_generated

LX_SRC_DIR := $(call select_from_ports,pixel_linux)/src/linux
LX_GEN_DIR := $(LIB_CACHE_DIR)/pixel_linux_generated

INC_DIR += $(PRG_DIR)

# lx_emul/initcall_order.h
INC_DIR += $(REP_DIR)/src/include

# shadow headers specific to the kernel of the Pixel 7
INC_DIR += $(REP_DIR)/src/include/lx_emul/shadow/arch/arm64/include

# files for the 6.1 kernel, searched before the ones of the dde_linux repository
LX_EMUL_6_1_DIR := $(call select_from_repositories,src/lx_emul_6_1)

INC_DIR += $(LX_EMUL_6_1_DIR)/include/lx_emul/shadow/arch/arm64/include
INC_DIR += $(LX_EMUL_6_1_DIR)/include/lx_emul/shadow/include

vpath lx_emul/% $(LX_EMUL_6_1_DIR)/lib

include $(call select_from_repositories,lib/import/import-lx_emul_common.inc)

#
# Additional Lx_emul + Lx_kit definitions
#

SRC_CC += lx_emul/clock.cc
SRC_CC += lx_emul/io_mem.cc
SRC_C  += lx_emul/shadow/drivers/clk/clk.c
SRC_C  += lx_emul/shadow/mm/vmalloc.c
SRC_CC += lx_emul/io_port.cc
SRC_CC += lx_emul/irq.cc
SRC_CC += lx_emul/random.cc
SRC_C  += lx_emul/shadow/arch/arm64/kernel/smp.c
SRC_C  += lx_emul/shadow/arch/arm64/mm/ioremap.c
SRC_C  += lx_emul/shadow/drivers/char/random.c
SRC_C  += lx_emul/shadow/kernel/dma/mapping.c
SRC_C  += lx_emul/shadow/kernel/irq/spurious.c
SRC_C  += lx_emul/shadow/kernel/rcu/srcutree.c
SRC_C  += lx_emul/shadow/kernel/rcu/tree.c
SRC_C  += lx_emul/shadow/kernel/smp.c
SRC_C  += lx_emul/shadow/kernel/stop_machine.c
SRC_C  += lx_emul/shadow/lib/devres.c
SRC_C  += lx_emul/shadow/lib/smp_processor_id.c
SRC_C  += lx_emul/shadow/drivers/base/power/runtime.c
SRC_C  += lx_emul/shadow/mm/page_alloc.c
SRC_CC += lx_kit/device.cc
SRC_CC += lx_kit/memory_dma.cc
