/*
 * \brief  Hand-written dummies of the pixel_audio driver
 * \author Ivan Loskutov
 * \date   2026-10-01
 *
 * The common 6.1 kernel dummies are in src/lib/lx_emul/gs201/
 * common_dummies.c, code with behaviour is in lx_emul.c. Each stub here
 * names what it stands in for.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <lx_emul.h>


#include <linux/pinctrl/devinfo.h>

/*
 * drivers/base/pinctrl.c: no pin controller in this component (stage A uses
 * no pins), the driver core must not look for default pin states
 */
int pinctrl_bind_pins(struct device *dev) { return 0; }


#include <linux/cdev.h>
#include <linux/fs.h>

/*
 * Character devices of the vendor drivers (/dev/aoc, /dev/gsa0, the TIPC
 * device node): no user space, nothing can open them. Registration succeeds
 * so that the probes go on.
 */
int  cdev_add(struct cdev *p, dev_t dev, unsigned count) { return 0; }
void cdev_del(struct cdev *p) { }
void unregister_chrdev_region(dev_t from, unsigned count) { }


#include <linux/ioport.h>

/*
 * kernel/resource.c is not built, there is no resource tree: claiming the
 * AoC DRAM range (devm_request_mem_region in aoc.c) always succeeds
 */
struct resource *__devm_request_region(struct device *dev,
                                       struct resource *parent,
                                       resource_size_t start,
                                       resource_size_t n, const char *name)
{
	static struct resource claimed;

	return &claimed;
}


/*
 * GSA log reader (gsa_log.c, not built): the log lives in a reserved-memory
 * region that the derived DTB does not reference ("log region not
 * configured" in the vendor code returns NULL as well)
 */
struct gsa_log;
struct gsa_log *gsa_log_init(struct platform_device *pdev);
ssize_t gsa_log_read(struct gsa_log *log, bool intermediate, char *buf);

struct gsa_log *gsa_log_init(struct platform_device *pdev) { return NULL; }
ssize_t gsa_log_read(struct gsa_log *log, bool intermediate, char *buf) { return 0; }


/*
 * dma-buf heaps of the AoC (ion_physical_heap.c, not built): three heaps
 * carved from the top of the AoC DRAM (sensor 4 MiB, audio playback 16 KiB,
 * audio capture 64 KiB) for buffers shared between user space and the AoC.
 * aoc.c only needs the creation to succeed: it derives the heap addresses
 * itself and passes them to the firmware. No heap is registered, nothing can
 * allocate from them (stage B decides whether aoc_alsa needs them).
 */
#include <ion_physical_heap.h>

struct dma_heap *ion_physical_heap_create(phys_addr_t base, size_t size,
                                          size_t align, const char *name,
                                          ion_physical_heap_allocate_callback alloc_cb,
                                          ion_physical_heap_free_callback free_cb,
                                          void *ctx)
{
	/* opaque to aoc.c, only compared against error pointers */
	static char placeholder;

	return (struct dma_heap *)&placeholder;
}


/*
 * Stage B: SPI7, amplifiers, ASoC
 */

#include <linux/mfd/syscon.h>

/*
 * spi-s3c64xx.c selects the USI protocol (SPI) through the SYSREG_PERIC0
 * syscon. The register belongs to gs201_platform (clock "usi7-spi-mode");
 * without the syscon the driver logs "no lookup for usi-phandle" and skips
 * the write (as for the touch driver's SPI0).
 */
struct regmap *syscon_regmap_lookup_by_phandle(struct device_node *np,
                                               const char *property)
{
	return ERR_PTR(-ENODEV);
}


#include <linux/delay.h>

void __ndelay(unsigned long nsecs) { udelay(DIV_ROUND_UP(nsecs, 1000)); }


#include <linux/pm_runtime.h>

/* system suspend/resume of spi-s3c64xx.c: there is no suspend */
int pm_runtime_force_suspend(struct device *dev) { return 0; }
int pm_runtime_force_resume(struct device *dev)  { return 0; }


#include <linux/pinctrl/consumer.h>

/*
 * ASoC switches the pins of a DAI between its "default" and "sleep" pinctrl
 * states when a stream starts and ends (soc-pcm.c, soc-dapm.c). Pin
 * functions are static gs201_pio configuration.
 */
int pinctrl_pm_select_default_state(struct device *dev) { return 0; }
int pinctrl_pm_select_sleep_state(struct device *dev)   { return 0; }


#include <linux/printk.h>

/* kernel/printk/printk.c ('printk_ratelimit'): never suppress a message */
int __printk_ratelimit(const char *func) { return 1; }
