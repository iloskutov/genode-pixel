/*
 * \brief  Linux emulation environment specific to this driver
 * \author Ivan Loskutov
 * \date   2026-09-30
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <lx_emul.h>
#include <lx_emul/alloc.h>


/*
 * Pseudo file system
 *
 * exynos-drm allocates every buffer as a dma-buf (dma-heap "system", then
 * imported as GEM object), so dma-buf files must have a working life cycle:
 * the last fput() releases the file and, through the dentry operations of
 * the dma-buf pseudo fs (init_pseudo), the dma-buf itself.
 */

#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/mount.h>
#include <linux/pseudo_fs.h>
#include <linux/file.h>
#include <linux/slab.h>

struct pseudo_fs_context *init_pseudo(struct fs_context *fc, unsigned long magic)
{
	struct pseudo_fs_context *ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);

	if (ctx) {
		ctx->magic      = magic;
		fc->fs_private  = ctx;
	}
	return ctx;
}


struct vfsmount *kern_mount(struct file_system_type *type)
{
	struct vfsmount   *mnt = kzalloc(sizeof(*mnt), GFP_KERNEL);
	struct super_block *sb = kzalloc(sizeof(*sb),  GFP_KERNEL);
	struct fs_context   fc = { .fs_type = type };

	if (!mnt || !sb) {
		kfree(mnt);
		kfree(sb);
		return ERR_PTR(-ENOMEM);
	}

	if (type->init_fs_context && type->init_fs_context(&fc) == 0 && fc.fs_private) {
		struct pseudo_fs_context *ctx = fc.fs_private;

		sb->s_d_op  = ctx->dops;
		sb->s_magic = ctx->magic;
		kfree(ctx);
	}

	mnt->mnt_sb = sb;
	return mnt;
}


/* the DRM pseudo fs (drm_fs_inode_new) is pinned, never released */
int simple_pin_fs(struct file_system_type *type, struct vfsmount **mount, int *count)
{
	if (!*mount) {
		struct vfsmount *mnt = kern_mount(type);

		if (IS_ERR(mnt))
			return PTR_ERR(mnt);

		*mount = mnt;
	}
	++*count;
	return 0;
}


void simple_release_fs(struct vfsmount **mount, int *count)
{
	--*count;
}


struct inode *alloc_anon_inode(struct super_block *sb)
{
	struct inode *inode = kzalloc(sizeof(*inode), GFP_KERNEL);

	if (!inode)
		return ERR_PTR(-ENOMEM);

	inode->i_sb = sb;
	atomic_set(&inode->i_count, 1);
	return inode;
}


void iput(struct inode *inode)
{
	if (inode && atomic_dec_and_test(&inode->i_count))
		kfree(inode);
}


void inode_set_bytes(struct inode *inode, loff_t bytes)
{
	inode->i_blocks = bytes >> 9;
	inode->i_bytes  = bytes & 511;
}


struct file *alloc_file_pseudo(struct inode *inode, struct vfsmount *mnt,
                               const char *name, int flags,
                               const struct file_operations *fops)
{
	struct file   *file   = kzalloc(sizeof(*file),   GFP_KERNEL);
	struct dentry *dentry = kzalloc(sizeof(*dentry), GFP_KERNEL);

	if (!file || !dentry) {
		kfree(file);
		kfree(dentry);
		return ERR_PTR(-ENOMEM);
	}

	dentry->d_inode = inode;
	dentry->d_sb    = mnt ? mnt->mnt_sb : NULL;
	dentry->d_op    = dentry->d_sb ? dentry->d_sb->s_d_op : NULL;

	atomic_long_set(&file->f_count, 1);
	file->f_path.mnt    = mnt;
	file->f_path.dentry = dentry;
	file->f_inode       = inode;
	file->f_op          = fops;
	file->f_flags       = flags;
	file->f_mode        = OPEN_FMODE(flags);
	return file;
}


void fput(struct file *file)
{
	struct dentry *dentry;

	if (!atomic_long_dec_and_test(&file->f_count))
		return;

	dentry = file->f_path.dentry;

	if (file->f_op && file->f_op->release)
		file->f_op->release(file->f_inode, file);

	if (dentry) {
		if (dentry->d_op && dentry->d_op->d_release)
			dentry->d_op->d_release(dentry);
		kfree(dentry);
	}
	iput(file->f_inode);
	kfree(file);
}


/*
 * DMA-BUF heap "system"
 *
 * Physically contiguous, cached (scan-out) memory from lx_emul: the Genode
 * side paints into it and cleans the painted rows from the data cache before
 * each frame (main.cc). Painting into uncached memory is several times
 * slower. The SysMMU translation of the DPU is off, as the boot loader left
 * it, so the device address is the bus address.
 */

#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/scatterlist.h>
#include <linux/iosys-map.h>

struct dma_heap { char const *name; };

static struct dma_heap system_heap = { .name = "system" };

struct lx_heap_buffer
{
	void         *vaddr;
	unsigned long dma_addr;
	size_t        size;
};


static struct sg_table *lx_heap_map_dma_buf(struct dma_buf_attachment *attach,
                                            enum dma_data_direction dir)
{
	struct lx_heap_buffer *buf = attach->dmabuf->priv;
	struct sg_table       *sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);

	if (!sgt)
		return ERR_PTR(-ENOMEM);

	if (sg_alloc_table(sgt, 1, GFP_KERNEL)) {
		kfree(sgt);
		return ERR_PTR(-ENOMEM);
	}

	sg_set_page(sgt->sgl, virt_to_page(buf->vaddr), buf->size, 0);
	sg_dma_address(sgt->sgl) = buf->dma_addr;
	sg_dma_len(sgt->sgl)     = buf->size;
	return sgt;
}


static void lx_heap_unmap_dma_buf(struct dma_buf_attachment *attach,
                                  struct sg_table *sgt,
                                  enum dma_data_direction dir)
{
	sg_free_table(sgt);
	kfree(sgt);
}


static void lx_heap_release(struct dma_buf *dmabuf)
{
	struct lx_heap_buffer *buf = dmabuf->priv;

	lx_emul_mem_free(buf->vaddr);
	kfree(buf);
}


static int lx_heap_vmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct lx_heap_buffer *buf = dmabuf->priv;

	iosys_map_set_vaddr(map, buf->vaddr);
	return 0;
}


static void lx_heap_vunmap(struct dma_buf *dmabuf, struct iosys_map *map) { }


static const struct dma_buf_ops lx_heap_buf_ops = {
	.map_dma_buf   = lx_heap_map_dma_buf,
	.unmap_dma_buf = lx_heap_unmap_dma_buf,
	.release       = lx_heap_release,
	.vmap          = lx_heap_vmap,
	.vunmap        = lx_heap_vunmap,
};


struct dma_heap *dma_heap_find(const char *name)
{
	return strcmp(name, system_heap.name) == 0 ? &system_heap : NULL;
}


void dma_heap_put(struct dma_heap *heap) { }


struct dma_buf *dma_heap_buffer_alloc(struct dma_heap *heap, size_t len,
                                      unsigned int fd_flags,
                                      unsigned int heap_flags)
{
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct lx_heap_buffer *buf;
	struct dma_buf        *dmabuf;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->size  = PAGE_ALIGN(len);
	buf->vaddr = lx_emul_mem_alloc_aligned(buf->size, PAGE_SIZE);
	if (!buf->vaddr) {
		kfree(buf);
		return ERR_PTR(-ENOMEM);
	}
	buf->dma_addr = lx_emul_mem_dma_addr(buf->vaddr);

	exp_info.exp_name = heap->name;
	exp_info.ops      = &lx_heap_buf_ops;
	exp_info.size     = buf->size;
	exp_info.flags    = fd_flags;
	exp_info.priv     = buf;

	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		lx_emul_mem_free(buf->vaddr);
		kfree(buf);
	}
	return dmabuf;
}


/*
 * Reserved memory of the boot loader's framebuffer (display handover)
 *
 * lx_emul does not scan /reserved-memory at boot. The region is looked up
 * from the node's 'reg' when DECON asks for it. It is RAM of Genode's core,
 * not of this driver, so releasing it after the first commit only forgets
 * it (instead of handing its pages to the Linux page allocator).
 */

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>

enum { MAX_RESERVED_MEM = 2 };

static struct reserved_mem reserved_mem[MAX_RESERVED_MEM];


struct reserved_mem *of_reserved_mem_lookup(struct device_node *np)
{
	struct resource res;
	unsigned i;

	for (i = 0; i < MAX_RESERVED_MEM; i++)
		if (reserved_mem[i].phandle == np->phandle && reserved_mem[i].size)
			return &reserved_mem[i];

	if (of_address_to_resource(np, 0, &res))
		return NULL;

	for (i = 0; i < MAX_RESERVED_MEM; i++) {
		struct reserved_mem *rmem = &reserved_mem[i];

		if (rmem->size)
			continue;

		rmem->name    = np->name;
		rmem->phandle = np->phandle;
		rmem->base    = res.start;
		rmem->size    = resource_size(&res);
		return rmem;
	}
	return NULL;
}


int of_reserved_mem_device_init_by_idx(struct device *dev,
                                       struct device_node *np, int idx)
{
	struct device_node  *target = of_parse_phandle(np, "memory-region", idx);
	struct reserved_mem *rmem;

	if (!target)
		return -ENODEV;

	rmem = of_reserved_mem_lookup(target);
	of_node_put(target);

	if (!rmem || !rmem->ops || !rmem->ops->device_init)
		return -EINVAL;

	return rmem->ops->device_init(rmem, dev);
}


void of_reserved_mem_device_release(struct device *dev) { }


/*
 * MIPI D-PHY of DSIM0 ("samsung,mipi-phy-m4m4")
 *
 * The vendor driver only switches the PMU isolation bypass (PMU +0x3eb8,
 * a shared register owned by the platform driver) and a sysreg reset bit.
 * Under display handover the PHY is on, so its provider has no operations.
 */

#include <linux/platform_device.h>
#include <linux/phy/phy.h>

static const struct phy_ops mipi_phy_ops = { .owner = THIS_MODULE };


static int mipi_phy_probe(struct platform_device *pdev)
{
	struct phy          *phy;
	struct phy_provider *provider;

	phy = devm_phy_create(&pdev->dev, NULL, &mipi_phy_ops);
	if (IS_ERR(phy))
		return PTR_ERR(phy);

	provider = devm_of_phy_provider_register(&pdev->dev, of_phy_simple_xlate);
	return PTR_ERR_OR_ZERO(provider);
}


static const struct of_device_id mipi_phy_ids[] = {
	{ .compatible = "samsung,mipi-phy-m4m4" },
	{ }
};


static struct platform_driver mipi_phy_driver = {
	.driver = {
		.name           = "mipi-phy-m4m4",
		.of_match_table = mipi_phy_ids,
	},
	.probe = mipi_phy_probe,
};


static int __init mipi_phy_drv_register(void)
{
	return platform_driver_register(&mipi_phy_driver);
}
module_init(mipi_phy_drv_register);


/*
 * DRM device of exynos-drm, for the DRM client in lx_user.c
 *
 * The vendor driver registers its DRM device from the component bind;
 * the linker option --wrap=drm_dev_register records it.
 */

#include <drm/drm_drv.h>

struct drm_device *lx_drm_dev;

int __real_drm_dev_register(struct drm_device *dev, unsigned long flags);


int __wrap_drm_dev_register(struct drm_device *dev, unsigned long flags)
{
	int const err = __real_drm_dev_register(dev, flags);

	if (!err)
		lx_drm_dev = dev;

	return err;
}
