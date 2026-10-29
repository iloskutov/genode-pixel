/*
 * \brief  Linux emulation of the pixel_audio driver (code with behaviour)
 * \author Ivan Loskutov
 * \date   2026-10-01
 *
 * Plain no-op stubs are in dummies.c. Every function here deviates from or
 * stands in for a Linux mechanism, the reason is given at each one.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <lx_emul.h>
#include <lx_emul/io_mem.h>

#include <linux/dma-map-ops.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pm_wakeup.h>
#include <linux/slab.h>
#include <linux/string.h>


/***************************************
 ** Staged creation of the DT devices **
 ***************************************/

/*
 * The drivers of this component register during the initcalls. Their
 * devices must not appear at that time: the probes issue SMCs, GSA mailbox
 * commands and start the AoC, and the bring-up wants them one at a time with
 * a log line in between (lx_user.c). The nodes are therefore marked as
 * already populated before the platform bus is filled from the device tree
 * (of_platform_default_populate_init, arch_initcall_sync) and their devices
 * are created later by 'pixel_audio_create_device'.
 */

static char const * const _held_nodes[] = {
	PIXEL_AUDIO_DT_TRUSTY, PIXEL_AUDIO_DT_GSA,   PIXEL_AUDIO_DT_MBOX0,
	PIXEL_AUDIO_DT_MBOX1,  PIXEL_AUDIO_DT_MBOX2, PIXEL_AUDIO_DT_AOC,

	/*
	 * The AoC's SysMMU: aoc.c only reads the node (phandle 'iommus' and its
	 * interrupt specifiers). No driver for it is built and this component
	 * is not granted the device, so no platform device is created.
	 */
	"/sysmmu@1A090000",

	/*
	 * Stage B. The SPI controller's probe registers the amplifiers, whose
	 * probe resets the chips and reads them over SPI; the sound card's
	 * probe needs the AoC online.
	 */
	PIXEL_AUDIO_DT_SPI7,   PIXEL_AUDIO_DT_SOUND,
};


static int __init pixel_audio_hold_devices(void)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(_held_nodes); i++) {
		struct device_node *np = of_find_node_by_path(_held_nodes[i]);

		if (!np) {
			printk("pixel_audio: device-tree node %s missing\n",
			       _held_nodes[i]);
			continue;
		}
		of_node_set_flag(np, OF_POPULATED);
		of_node_put(np);
	}
	return 0;
}

postcore_initcall(pixel_audio_hold_devices);


struct platform_device *pixel_audio_create_device(char const *path)
{
	struct device_node     *np = of_find_node_by_path(path);
	struct platform_device *pdev;

	if (!np)
		return NULL;

	of_node_clear_flag(np, OF_POPULATED);

	pdev = of_platform_device_create(np, NULL, NULL);

	of_node_put(np);
	return pdev;
}


/***********************
 ** Module parameters **
 ***********************/

/*
 * Built-in modules keep their parameters in the '__param' section, Linux
 * sets them from the kernel command line ("aoc_core.aoc_disable_restart=1").
 * There is no command line here, so the table is searched directly. Used for
 * the two parameters of aoc.c that make its crash path harmless.
 */

extern const struct kernel_param __start___param[], __stop___param[];


int pixel_audio_set_bool_param(char const *name, bool value)
{
	const struct kernel_param *kp;

	for (kp = __start___param; kp < __stop___param; kp++) {

		char const *dot = strchr(kp->name, '.');
		char const *kp_name = dot ? dot + 1 : kp->name;

		if (strcmp(kp_name, name) != 0)
			continue;

		*(bool *)kp->arg = value;
		return 0;
	}
	return -ENOENT;
}


/*****************************************
 ** DMA ops of the Trusty virtio device **
 *****************************************/

/*
 * trusty-virtio.c installs DMA ops on its platform device whose 'map_page'
 * returns the shared-memory id of a TIPC buffer (trusty_virtio_shim.h). The
 * virtio ring maps every buffer it queues through the DMA API of that
 * device (VIRTIO_F_ACCESS_PLATFORM), so the ring must carry that id. One
 * device uses this, a single slot is enough.
 */

static struct device            *_dma_ops_dev;
static const struct dma_map_ops *_dma_ops;


void pixel_audio_set_dma_ops(struct device *dev, const struct dma_map_ops *ops)
{
	if (_dma_ops_dev && _dma_ops_dev != dev)
		printk("pixel_audio: second device with private DMA ops (%s), "
		       "replacing %s\n", dev_name(dev), dev_name(_dma_ops_dev));

	_dma_ops_dev = dev;
	_dma_ops     = ops;
}


/* 'dma_map_page_attrs' of drivers/virtio/virtio_ring.c */
dma_addr_t pixel_audio_dma_map_page_attrs(struct device *dev, struct page *page,
                                          size_t offset, size_t size,
                                          enum dma_data_direction dir,
                                          unsigned long attrs)
{
	if (dev && dev == _dma_ops_dev && _dma_ops && _dma_ops->map_page)
		return _dma_ops->map_page(dev, page, offset, size, dir, attrs);

	return dma_map_page_attrs(dev, page, offset, size, dir, attrs);
}


/*
 * 'dma_unmap_page_attrs' of drivers/virtio/virtio_ring.c. As in Linux, a
 * device with private ops that have no 'unmap_page' needs no unmapping (the
 * "address" is a shared-memory id, the shadow code would look it up as a
 * DMA address and complain).
 */
void pixel_audio_dma_unmap_page_attrs(struct device *dev, dma_addr_t addr,
                                      size_t size, enum dma_data_direction dir,
                                      unsigned long attrs)
{
	if (dev && dev == _dma_ops_dev && _dma_ops) {
		if (_dma_ops->unmap_page)
			_dma_ops->unmap_page(dev, addr, size, dir, attrs);
		return;
	}

	dma_unmap_page_attrs(dev, addr, size, dir, attrs);
}


/*********************
 ** IOMMU of aoc.c  **
 *********************/

/*
 * The AoC probe fails without an IOMMU domain. On the GSA start path the AP
 * maps nothing into it (aoc_configure_iommu is the non-GSA branch): GSA
 * programs the AoC SysMMU from the signed image header. The domain is a
 * placeholder that is never passed to an IOMMU function with an effect
 * (iommu_map/iommu_unmap are the kernel's inline stubs, CONFIG_IOMMU_API is
 * off).
 */
struct iommu_domain *pixel_audio_iommu_get_domain_for_dev(struct device *dev)
{
	static struct iommu_domain domain;

	return &domain;
}


/* IOMMU faults of the AoC cannot be reported to this component */
int pixel_audio_iommu_register_device_fault_handler(struct device *dev,
                                                    iommu_dev_fault_handler_t handler,
                                                    void *data)
{
	return 0;
}


/*************************
 ** I/O memory of aoc.c **
 *************************/

/*
 * 'blk_aoc' (0x19000000 + 16 MiB, SFR/SRAM window of the AoC) is not a
 * device of this component: its end is the modem's voice window owned by
 * pixel_cpif, and on the GSA start path with restart disabled aoc.c never
 * accesses it (only the non-GSA reset trampoline, aoc_release_from_reset
 * and the PCU watchdog registers of the restart path do). The probe insists
 * on a mapping, so it gets a window of the right size that is backed by
 * nothing: an access would stop the driver with an unresolvable page fault
 * instead of silently hitting foreign registers.
 */
void __iomem *pixel_audio_devm_ioremap_resource(struct device *dev,
                                                const struct resource *res)
{
	void *window;

	if (!res)
		return IOMEM_ERR_PTR(-EINVAL);

	if (!res->name || strcmp(res->name, "blk_aoc") != 0)
		return devm_ioremap_resource(dev, res);

	window = pixel_audio_unbacked_window(resource_size(res));
	if (!window)
		return IOMEM_ERR_PTR(-ENOMEM);

	dev_info(dev, "%pR (blk_aoc) not mapped, placeholder window at %px: "
	         "any access is a page fault\n", res, window);

	return (void __iomem *)window;
}


/*
 * The AoC DRAM (firmware image and IPC rings) is accessed with memcpy and
 * unaligned loads, which need a normal-memory mapping: the platform device
 * declares the range write-combined, and the mapping is requested that way
 * (the common 'ioremap_prot' asks for an uncached device mapping).
 */
void __iomem *pixel_audio_devm_ioremap_wc(struct device *dev,
                                          resource_size_t offset,
                                          resource_size_t size)
{
	return lx_emul_io_mem_map(offset, size, true);
}


/*******************
 ** Wakeup source **
 *******************/

/*
 * aoc_watchdog() prints the name of the wakeup source it registers; the
 * common dummy returns NULL. There is no suspend, the source has no effect.
 */
struct wakeup_source *pixel_audio_wakeup_source_register(struct device *dev,
                                                         const char *name)
{
	static struct wakeup_source ws;

	ws.name = name;
	return &ws;
}


/************
 ** CHIPID **
 ************/

/*
 * gs-chipid.c is not built and the CHIPID block (0x10000000) is not a
 * device of this component. aoc.c passes the three values to the AoC
 * firmware: product id and revision of the Pixel 7's GS201 B0 as logged by
 * LineageOS ("CPU[GS201]B0 CPU_REV[0x10]"), the type as gs-chipid.c
 * derives it.
 */
u32 gs_chipid_get_revision(void)   { return 0x10; }
u32 gs_chipid_get_type(void)       { return 0x1; }
u32 gs_chipid_get_product_id(void) { return 0x09855000; }


/*************************
 ** Managed DMA memory  **
 *************************/

/*
 * 'dmam_alloc_coherent' of gsa_core.c (one-page bounce buffer). The shadow
 * DMA code has no managed variant; the buffer lives as long as the device,
 * which is never removed, so the release on driver detach is left out.
 */
void *dmam_alloc_attrs(struct device *dev, size_t size, dma_addr_t *dma_handle,
                       gfp_t gfp, unsigned long attrs)
{
	return dma_alloc_attrs(dev, size, dma_handle, gfp, attrs);
}


/***************************************
 ** User access from inside the kernel **
 ***************************************/

/*
 * The ALSA devices are used through their file operations by code of this
 * component (card.c), the "user" pointers of the ioctls are kernel pointers.
 * arm64's raw_copy_to/from_user end in '__arch_copy_to/from_user', which the
 * common dummies define as trace-and-stop; this target is compiled with the
 * two names redirected to the plain copies below (target.mk).
 */
unsigned long pixel_audio_copy_to_user(void *to, const void *from,
                                       unsigned long n)
{
	memcpy(to, from, n);
	return 0;
}


unsigned long pixel_audio_copy_from_user(void *to, const void *from,
                                         unsigned long n)
{
	memcpy(to, from, n);
	return 0;
}


/*****************
 ** Async calls **
 *****************/

#include <linux/async.h>

/*
 * kernel/async.c is not built (the common dummies run async calls
 * synchronously). DAPM powers the widgets of the DAPM contexts of a card
 * with 'async_schedule_domain' and waits with
 * 'async_synchronize_full_domain' (soc-dapm.c, dapm_power_widgets): here
 * each context is handled in the caller, one after the other.
 */
async_cookie_t async_schedule_node_domain(async_func_t func, void *data,
                                          int node, struct async_domain *domain)
{
	func(data, 0);
	return 0;
}


void async_synchronize_full_domain(struct async_domain *domain) { }
