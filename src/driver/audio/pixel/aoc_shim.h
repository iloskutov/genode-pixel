/*
 * \brief  Compile-time adaptation of the vendor aoc.c (force-included)
 * \author Ivan Loskutov
 * \date   2026-10-01
 *
 * aoc.c runs unmodified. The calls below are redirected to lx_emul.c
 * (reasons there):
 *
 * - IOMMU domain and fault handler: with CONFIG_IOMMU_API off the kernel's
 *   inline stubs return NULL / -ENODEV and the probe fails. On the GSA start
 *   path the AP maps nothing, GSA programs the AoC SysMMU.
 * - devm_ioremap_resource: the 16 MiB 'blk_aoc' window is not handed to this
 *   component (its end overlaps the modem's voice window).
 * - devm_ioremap_wc: the AoC DRAM is mapped write-combined.
 * - wakeup_source_register: the common dummy returns NULL, the watchdog path
 *   of aoc.c dereferences the result.
 * - gsa_load_aoc_fw_image / gsa_send_aoc_cmd: the GSA wakelock and the
 *   S2MPU around the firmware authentication, and the 'stage' gate.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _PIXEL_AUDIO__AOC_SHIM_H_
#define _PIXEL_AUDIO__AOC_SHIM_H_

/* as aoc.c, which defines it in front of its own includes */
#define pr_fmt(fmt) "aoc: " fmt

#include <linux/device.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/pm_wakeup.h>
#include <linux/gsa/gsa_aoc.h>

struct iommu_domain *pixel_audio_iommu_get_domain_for_dev(struct device *dev);

int pixel_audio_iommu_register_device_fault_handler(struct device *dev,
                                                    iommu_dev_fault_handler_t handler,
                                                    void *data);

void __iomem *pixel_audio_devm_ioremap_resource(struct device *dev,
                                                const struct resource *res);

void __iomem *pixel_audio_devm_ioremap_wc(struct device *dev,
                                          resource_size_t offset,
                                          resource_size_t size);

struct wakeup_source *pixel_audio_wakeup_source_register(struct device *dev,
                                                         const char *name);

int pixel_audio_gsa_load_aoc_fw_image(struct device *gsa, dma_addr_t img_meta,
                                      phys_addr_t img_body);

int pixel_audio_gsa_send_aoc_cmd(struct device *gsa, enum gsa_aoc_cmd cmd);

#define iommu_get_domain_for_dev            pixel_audio_iommu_get_domain_for_dev
#define iommu_register_device_fault_handler pixel_audio_iommu_register_device_fault_handler
#define devm_ioremap_resource               pixel_audio_devm_ioremap_resource
#define devm_ioremap_wc                     pixel_audio_devm_ioremap_wc
#define wakeup_source_register              pixel_audio_wakeup_source_register
#define gsa_load_aoc_fw_image               pixel_audio_gsa_load_aoc_fw_image
#define gsa_send_aoc_cmd                    pixel_audio_gsa_send_aoc_cmd

/*
 * aoc_watchdog() maps the AoC DRAM page-wise for a core dump. The code is
 * never reached here: no subsystem-core-dump driver sets 'sscd_report', the
 * watchdog work leaves before ("aoc coredump failed: no sscd driver").
 * 'phys_to_page' is not provided by the lx_emul memory shadow.
 */
#define phys_to_page(phys) ((struct page *)NULL)

#endif /* _PIXEL_AUDIO__AOC_SHIM_H_ */
