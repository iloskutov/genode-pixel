#
# Linux kernel configuration for the Google Pixel 7 (GS201)
#
# The configuration is shared by all Linux-based drivers of this repository
# because the generated kernel headers are built only once.
#

# kernel fundamentals
LX_ENABLE += TTY SERIAL_EARLYCON SERIAL_OF_PLATFORM PRINTK HAS_IOMEM SMP
LX_ENABLE += NO_HZ_IDLE

# initrd support
LX_ENABLE += BINFMT_ELF BLK_DEV_INITRD

# kmem_cache allocator
LX_DISABLE += SLOB SLUB_DEBUG
LX_ENABLE  += SLUB

# block-device support
LX_ENABLE += BLOCK

# SCSI subsystem
LX_ENABLE += SCSI BLK_DEV_SD

# UFS host controller
LX_ENABLE += SCSI_UFSHCD SCSI_UFSHCD_PLATFORM

# device-tree support
LX_ENABLE += OF OF_FLATTREE OF_EARLY_FLATTREE OF_ADDRESS OF_IRQ

# syscon and regmap
LX_ENABLE += MFD_SYSCON REGMAP REGMAP_MMIO

# random-number generator
LX_ENABLE += HW_RANDOM ARCH_RANDOM RANDOM_TRUST_CPU RANDOM_TRUST_BOOTLOADER
LX_ENABLE += CRYPTO_JITTERENTROPY

# disable CPU-frequency scaling
LX_DISABLE += SCHED_MC CPU_FREQ

# disable optimization not possible in PIC code under Genode
LX_DISABLE += CC_HAS_ASM_GOTO

# PCI subsystem
LX_ENABLE += PCI PCI_DOMAINS PCI_DOMAINS_GENERIC
LX_ENABLE += PCIE_DW PCIE_DW_HOST

# network infrastructure
LX_ENABLE += NET NETDEVICES PACKET

# WLAN support
LX_ENABLE += WLAN CFG80211
LX_ENABLE += RFKILL

# GPIO
LX_ENABLE += GPIOLIB OF_GPIO

# power management and pinctrl
LX_ENABLE += PM PM_SLEEP PINCTRL PINMUX PINCONF

#
# Display: DRM core, KMS helpers, MIPI DSI, panels, backlight, and the
# dma-buf/dma-heap infrastructure used by the vendor's GEM and DPP code
#
LX_ENABLE += DRM DRM_KMS_HELPER DRM_MIPI_DSI DRM_PANEL DRM_FBDEV_EMULATION
LX_ENABLE += FB BACKLIGHT_CLASS_DEVICE VIDEOMODE_HELPERS
LX_ENABLE += DMA_SHARED_BUFFER SYNC_FILE DMABUF_HEAPS DMABUF_HEAPS_SYSTEM

#
# DRM_KMS_HELPER, DRM_MIPI_DSI, and DRM_PANEL_BRIDGE are hidden symbols that
# only a driver can select. As the vendor's exynos-drm driver is not part of
# the kernel tree, DRM_CDNS_DSI is enabled merely to select them. Its code
# is not compiled.
#
LX_ENABLE += DRM_BRIDGE DRM_CDNS_DSI

# PHY core, the DSI host uses the MIPI D-PHY via the generic PHY API
LX_ENABLE += GENERIC_PHY

# SPI core and the input touchscreen class
LX_ENABLE += SPI SPI_MASTER INPUT_TOUCHSCREEN

#
# Sound: ALSA core and ASoC. SND_SOC_CS35L41_SPI is enabled merely to select
# hidden symbols (REGMAP_SPI, SND_SOC_WM_ADSP, CS_DSP, SND_SOC_COMPRESS). Its
# code is not compiled.
#
LX_ENABLE += SOUND SND SND_SOC SND_SOC_CS35L41_SPI SND_DYNAMIC_MINORS

#
# Mailbox framework and virtio core. VIRTIO is a hidden symbol, VIRTIO_MMIO
# is enabled merely to select it. Its code is not compiled.
#
LX_ENABLE += MAILBOX VIRTIO_MENU VIRTIO_MMIO

# slim down kernel by removing superfluous drivers
LX_DISABLE += VGA_CONSOLE DUMMY_CONSOLE NLS VGA_ARB DEBUG_LIST
LX_DISABLE += INPUT_KEYBOARD INPUT_MOUSE ETHERNET
LX_DISABLE += USB USB_SUPPORT
LX_DISABLE += IPV6
LX_DISABLE += MAC80211

# vim: set ft=make :
