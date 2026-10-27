/*
 * \brief  Hand-written dummies of the FT3658 touchscreen driver
 * \author Ivan Loskutov
 * \date   2026-09-30
 *
 * The dummies shared with other drivers are in src/lib/lx_emul/gs201/
 * common_dummies.c, these are specific to the touch stack.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <lx_emul.h>


#include <linux/mfd/syscon.h>

/*
 * The protocol of the SPI controller's USI is selected by the platform
 * driver (clock 'usi0-spi-mode'). Without the syscon, the SPI driver leaves
 * it alone.
 */
struct regmap *syscon_regmap_lookup_by_phandle(struct device_node *np,
                                               const char *property)
{
	return ERR_PTR(-ENODEV);
}


#include <linux/clk.h>

/* clocks are looked up by name (clkdev shadow), not by index */
struct clk *of_clk_get(struct device_node *np, int index) { return ERR_PTR(-ENOENT); }


#include <linux/delay.h>

void __ndelay(unsigned long nsecs) { udelay(DIV_ROUND_UP(nsecs, 1000)); }


#include <linux/pm_runtime.h>
#include <linux/pm_wakeup.h>

int  pm_runtime_force_suspend(struct device *dev) { return 0; }
int  pm_runtime_force_resume(struct device *dev) { return 0; }
void pm_relax(struct device *dev) { }


#include <linux/fs.h>

int register_chrdev_region(dev_t from, unsigned count, const char *name) { return 0; }


/*
 * FocalTech driver
 */

/* defined by the self-test module, which is disabled by a patch of the port */
int int_test_has_interrupt;


/*
 * Heatmap (touch/common heatmap.c, V4L2): not provided, the driver's heatmap
 * code stays unused. It is enabled only because the vendor sources do not
 * build without CONFIG_TOUCHSCREEN_HEATMAP.
 */
#include <heatmap.h>

int heatmap_probe(struct v4l2_heatmap *v4l2) { return 0; }
void heatmap_remove(struct v4l2_heatmap *v4l2) { }
void heatmap_read(struct v4l2_heatmap *v4l2, uint64_t timestamp) { }


/*
 * Panel bridge (display on/off notifications to the touch driver): there is
 * no DRM in this component, the bridge is never attached. It is enabled only
 * because the vendor sources do not build without
 * CONFIG_TOUCHSCREEN_PANEL_BRIDGE.
 */
#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_modes.h>
#include <drm/drm_modeset_lock.h>

void drm_bridge_add(struct drm_bridge *bridge) { }
void drm_bridge_remove(struct drm_bridge *bridge) { }

void drm_connector_list_iter_begin(struct drm_device *dev,
                                   struct drm_connector_list_iter *iter) { }
void drm_connector_list_iter_end(struct drm_connector_list_iter *iter) { }

struct drm_connector *drm_connector_list_iter_next(struct drm_connector_list_iter *iter)
{
	return NULL;
}

int drm_modeset_lock(struct drm_modeset_lock *lock, struct drm_modeset_acquire_ctx *ctx)
{
	return 0;
}

void drm_modeset_unlock(struct drm_modeset_lock *lock) { }

int drm_mode_vrefresh(const struct drm_display_mode *mode) { return 60; }


/*
 * Defined by the self-test module (focaltech_test.c, not built). The
 * interrupt handler transposes the mutual-sensing heatmap with it, but the
 * result feeds only the touch offload and the V4L2 heatmap, which are not
 * provided. The touch coordinates do not depend on it.
 */
void transpose_raw(u8 *src, u8 *dist, int tx, int rx, bool big_endian) { }
