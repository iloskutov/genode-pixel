/*
 * \brief  Hand-written dummies of the UFS storage driver
 * \author Ivan Loskutov
 * \date   2026-10-28
 *
 * The dummies shared with other drivers are in src/lib/lx_emul/gs201/
 * common_dummies.c, these are specific to the storage stack.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <lx_emul.h>


#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

/*
 * The only regmap of the driver is the system register with the I/O
 * coherency of the host controller ('ufs-iocc'). The platform driver sets
 * these bits when the device is acquired (clock 'ufs-iocc'), so the lookup
 * returns a placeholder and the driver's write is dropped.
 */
static int dummy_syscon_regmap;

struct regmap *syscon_regmap_lookup_by_phandle(struct device_node *np,
                                               const char *property)
{
	return (struct regmap *)&dummy_syscon_regmap;
}

int regmap_update_bits_base(struct regmap *map, unsigned int reg,
                            unsigned int mask, unsigned int val,
                            bool *change, bool async, bool force)
{
	if (change)
		*change = false;
	return 0;
}


#include <linux/pinctrl/devinfo.h>

/* the pins of the host controller are set up by the boot loader */
int pinctrl_bind_pins(struct device *dev) { return 0; }


#include <linux/cpuhotplug.h>

/* there is no CPU hotplug, the per-CPU caches of bio sets are not flushed */
int __cpuhp_state_add_instance(enum cpuhp_state state, struct hlist_node *node,
                               bool invoke) { return 0; }

int __cpuhp_state_remove_instance(enum cpuhp_state state,
                                  struct hlist_node *node, bool invoke) { return 0; }


#include <linux/arch_topology.h>

struct cpu_topology cpu_topology[NR_CPUS];


#include <linux/rcupdate.h>

void rcu_barrier(void) { }


#include <linux/pm_runtime.h>

void pm_runtime_allow(struct device *dev) { }
void pm_runtime_forbid(struct device *dev) { }
void pm_runtime_set_memalloc_noio(struct device *dev, bool enable) { }


#include <linux/dma-mapping.h>

/*
 * Managed DMA memory, which is not released with the device: the device is
 * never removed
 */
void *dmam_alloc_attrs(struct device *dev, size_t size, dma_addr_t *dma_handle,
                       gfp_t gfp, unsigned long attrs)
{
	return dma_alloc_attrs(dev, size, dma_handle, gfp, attrs);
}

void dmam_free_coherent(struct device *dev, size_t size, void *vaddr,
                        dma_addr_t dma_handle)
{
	dma_free_attrs(dev, size, vaddr, dma_handle, 0);
}

/* DMA is not bounced, so the size of a mapping is not limited */
size_t dma_max_mapping_size(struct device *dev) { return SIZE_MAX; }


#include <linux/io.h>

void devm_iounmap(struct device *dev, void __iomem *addr) { }


/*
 * Block layer: no sysfs, no runtime power management, no writeback, no
 * partitions (partitions are left to Genode's part_block)
 */

#include <linux/backing-dev.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/ioprio.h>
#include <linux/writeback.h>

struct backing_dev_info *bdi_alloc(int node_id)
{
	return kzalloc(sizeof(struct backing_dev_info), GFP_KERNEL);
}

int  bdi_register(struct backing_dev_info *bdi, const char *fmt, ...) { return 0; }
void bdi_set_owner(struct backing_dev_info *bdi, struct device *owner) { }
void bdi_unregister(struct backing_dev_info *bdi) { }
void bdi_put(struct backing_dev_info *bdi) { }

struct kobj_type blk_queue_ktype;

extern void blk_mq_sysfs_init(struct request_queue *q);
void blk_mq_sysfs_init(struct request_queue *q) { }

extern void blk_mq_sysfs_deinit(struct request_queue *q);
void blk_mq_sysfs_deinit(struct request_queue *q) { }

extern void blk_mq_hctx_kobj_init(struct blk_mq_hw_ctx *hctx);
void blk_mq_hctx_kobj_init(struct blk_mq_hw_ctx *hctx) { }

void blk_pm_runtime_init(struct request_queue *q, struct device *dev) { }

extern int blk_drop_partitions(struct block_device *bdev);
int blk_drop_partitions(struct block_device *bdev) { return 0; }

int  bd_prepare_to_claim(struct block_device *bdev, void *holder) { return 0; }
void bd_abort_claiming(struct block_device *bdev, void *holder) { }
void blkdev_put(struct block_device *bdev, fmode_t mode) { }

int fsync_bdev(struct block_device *bdev) { return 0; }

int __get_task_ioprio(struct task_struct *p) { return IOPRIO_DEFAULT; }

int laptop_mode;


#include <linux/fs.h>

void iput(struct inode *inode) { }
void __remove_inode_hash(struct inode *inode) { }


#include <linux/random.h>

void rand_initialize_disk(struct gendisk *disk) { }
void add_disk_randomness(struct gendisk *disk) { }


/*
 * SCSI: no runtime power management, no transport class, no bsg device
 */

#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

extern const struct dev_pm_ops scsi_bus_pm_ops;
const struct dev_pm_ops scsi_bus_pm_ops;

extern int scsi_autopm_get_device(struct scsi_device *sdev);
int scsi_autopm_get_device(struct scsi_device *sdev) { return 0; }

extern void scsi_autopm_put_device(struct scsi_device *sdev);
void scsi_autopm_put_device(struct scsi_device *sdev) { }

extern int scsi_autopm_get_target(struct scsi_target *starget);
int scsi_autopm_get_target(struct scsi_target *starget) { return 0; }

extern void scsi_autopm_put_target(struct scsi_target *starget);
void scsi_autopm_put_target(struct scsi_target *starget) { }

extern int scsi_autopm_get_host(struct Scsi_Host *shost);
int scsi_autopm_get_host(struct Scsi_Host *shost) { return 0; }

extern void scsi_autopm_put_host(struct Scsi_Host *shost);
void scsi_autopm_put_host(struct Scsi_Host *shost) { }


#include <linux/transport_class.h>

void transport_setup_device(struct device *dev) { }
int  transport_add_device(struct device *dev) { return 0; }
void transport_configure_device(struct device *dev) { }
void transport_remove_device(struct device *dev) { }
void transport_destroy_device(struct device *dev) { }


extern struct bsg_device *scsi_bsg_register_queue(struct scsi_device *sdev);
struct bsg_device *scsi_bsg_register_queue(struct scsi_device *sdev)
{
	return NULL;
}


/*
 * Vendor UFS driver: the inline encryption is not built, its operations
 * are not used without it
 */

#include <ufs/ufshcd.h>
#include <ufs-pixel.h>

extern const struct pixel_crypto_ops exynos_crypto_ops;
const struct pixel_crypto_ops exynos_crypto_ops;
