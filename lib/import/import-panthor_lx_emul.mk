#
# Linux emulation environment of the panthor GPU driver
#
# Genode's stock dde_linux emulation (repos/dde_linux: lx_emul, lx_kit) with
# the stock 'linux' port (6.18.19) and a kernel configuration of its own
# (lib/mk/spec/arm_v8/panthor_linux_generated.mk). Nothing here is shared
# with pixel_lx_emul, the 6.1-based environment of the other Pixel drivers:
# no include path and no source of src/lx_6_1 or src/include/lx_emul is used.
# Layout as allwinner's import-a64_lx_emul.mk.
#

# check for installation of device-tree compiler
$(call check_tool,dtc)

LIBS    += panthor_linux_generated

INC_DIR += $(PRG_DIR)

# lx_emul/initcall_order.h of this kernel configuration
INC_DIR += $(call select_from_repositories,src/include/panthor_linux)

LX_SRC_DIR := $(call select_from_ports,linux)/src/linux
ifeq ($(wildcard $(LX_SRC_DIR)),)
LX_SRC_DIR := $(call select_from_repositories,src/linux)
endif

LX_GEN_DIR := $(LIB_CACHE_DIR)/panthor_linux_generated

LINUX_VERSION_CODE := 61819

# only repos/dde_linux provides this file (the 6.1 one is src/lx_6_1/lx_emul_common.inc)
include $(call select_from_repositories,lib/import/import-lx_emul_common.inc)

INC_DIR += $(LX_SRC_DIR)/scripts/dtc/libfdt

#
# Additional Lx_emul + Lx_kit definitions
#

SRC_CC  += lx_emul/io_mem.cc
SRC_CC  += lx_emul/io_port.cc
SRC_CC  += lx_emul/irq.cc
SRC_C   += lx_emul/shadow/arch/arm64/kernel/irq.c
SRC_C   += lx_emul/shadow/arch/arm64/kernel/smp.c
SRC_C   += lx_emul/shadow/arch/arm64/mm/ioremap.c
SRC_C   += lx_emul/shadow/drivers/char/random.c
SRC_C   += lx_emul/shadow/fs/libfs.c
SRC_C   += lx_emul/shadow/kernel/dma/mapping.c
SRC_C   += lx_emul/shadow/kernel/irq/spurious.c
SRC_C   += lx_emul/shadow/kernel/rcu/srcutree.c
SRC_C   += lx_emul/shadow/lib/devres.c
SRC_C   += lx_emul/shadow/lib/smp_processor_id.c
SRC_C   += lx_emul/shadow/mm/memblock.c
SRC_C   += lx_emul/shadow/mm/page_alloc.c
SRC_C   += lx_emul/shadow/mm/vmalloc.c
SRC_CC  += lx_kit/device.cc
SRC_CC  += lx_kit/memory_dma.cc
