#
# Linux kernel configuration for the panthor GPU driver (Mali-G710 CSF)
#
# Base: Genode's stock dde_linux Linux (port 'linux', 6.18.19), not the
# Android 6.1 tree of the other Pixel DDE drivers (src/pixel_linux).
#

LX_NR_CPUS = 8

# kernel fundamentals
LX_ENABLE += TTY SERIAL_EARLYCON SERIAL_OF_PLATFORM PRINTK HAS_IOMEM SMP MMU
LX_ENABLE += NO_HZ_IDLE

# initrd support
LX_ENABLE += BINFMT_ELF BLK_DEV_INITRD

# kmem_cache allocator
LX_DISABLE += SLOB SLUB_DEBUG
LX_ENABLE  += SLUB

LX_ENABLE += PROC_FS SYSFS

# GPU driver; selects DRM_EXEC, DRM_GEM_SHMEM_HELPER, DRM_GPUVM, DRM_SCHED,
# IOMMU_SUPPORT, IOMMU_IO_PGTABLE_LPAE, PM_DEVFREQ, DEVFREQ_GOV_SIMPLE_ONDEMAND
LX_ENABLE += DRM DRM_PANTHOR

# 'request_firmware' is a stub returning -EINVAL without it
LX_ENABLE += FW_LOADER

#
# No runtime PM: the emulation's pm_runtime functions do not call the
# driver's callbacks. Without CONFIG_PM panthor resumes the device once in
# its probe function and never suspends it.
#
LX_DISABLE += PM

# disable cpu frequency scaling
LX_DISABLE += SCHED_MC CPU_FREQ

# disable optimization not possible in PIC code under Genode
LX_DISABLE += CC_HAS_ASM_GOTO

# slim down kernel by removing superfluous drivers
LX_DISABLE += VGA_CONSOLE DUMMY_CONSOLE NLS VGA_ARB DEBUG_LIST
