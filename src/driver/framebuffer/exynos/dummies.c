/*
 * \brief  Hand-written dummies of the exynos-drm framebuffer driver
 * \author Ivan Loskutov
 * \date   2026-09-30
 *
 * The dummies shared with other drivers are in src/lib/lx_emul/gs201/
 * common_dummies.c, these are specific to the display stack.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <lx_emul.h>


/*********************************
 ** Display-stack specific parts **
 *********************************/

#include <linux/clk.h>

/* DECON's aclk/aclk-disp are optional, the DPU clocks stay as the boot loader set them */
struct clk *clk_get(struct device *dev, const char *id) { return ERR_PTR(-ENOENT); }
struct clk *devm_clk_get(struct device *dev, const char *id) { return ERR_PTR(-ENOENT); }
void clk_put(struct clk *clk) { }


#include <linux/fb.h>

int fb_get_options(const char *name, char **option)
{
	*option = NULL;
	return 0;
}


void framebuffer_release(struct fb_info *info) { kfree(info); }
void unregister_framebuffer(struct fb_info *info) { }
void fb_set_suspend(struct fb_info *info, int state) { }

/* drawing ops are only reached through fbdev ioctls/fbcon, both absent */
void cfb_fillrect(struct fb_info *p, const struct fb_fillrect *rect) { }
void cfb_copyarea(struct fb_info *p, const struct fb_copyarea *area) { }
void cfb_imageblit(struct fb_info *p, const struct fb_image *image) { }
void sys_fillrect(struct fb_info *p, const struct fb_fillrect *rect) { }
void sys_copyarea(struct fb_info *p, const struct fb_copyarea *area) { }
void sys_imageblit(struct fb_info *p, const struct fb_image *image) { }


/* panel debugfs command sets: no debugfs */
struct exynos_panel;
struct exynos_dsi_cmd_set;
void exynos_panel_debugfs_create_cmdset(struct exynos_panel *ctx, struct dentry *parent,
                                        const struct exynos_dsi_cmd_set *cmdset,
                                        const char *name) { }


bool exynos_uart_console_enabled(void) { return false; }


/* secure (TZPC-protected) display registers: would need the EL3 monitor */
int set_priv_reg(phys_addr_t reg, u32 val)
{
	printk("set_priv_reg(%pa, 0x%x): secure register write not supported\n", &reg, val);
	return -EPERM;
}


/* deferred I/O is only used by a shadow framebuffer, exynos-drm has none */
int  fb_deferred_io_init(struct fb_info *info) { lx_emul_trace_and_stop(__func__); }
void fb_deferred_io_cleanup(struct fb_info *info) { }
int  fb_deferred_io_mmap(struct fb_info *info, struct vm_area_struct *vma) { return -ENODEV; }


