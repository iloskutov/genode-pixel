/*
 * \brief  Post kernel userland activity: DRM client driving the panel
 * \author Ivan Loskutov
 * \date   2026-09-30
 *
 * The vendor exynos-drm has no fbdev support and its GEM objects cannot be
 * vmapped through DRM (no gem vmap op), so the generic fbdev emulation is
 * not usable. Instead, an in-kernel DRM client allocates one dumb buffer in
 * the panel's preferred mode, maps it through its dma-buf, and shows it on
 * all connected outputs.
 *
 * The s6e3fc3 panel runs in MIPI command mode: it keeps showing its own
 * frame memory until DECON sends a new frame, which exynos-drm does on an
 * atomic commit. As in Genode's Intel display driver, one task paints the
 * captured changes (Genode side) and then commits, round after round (10 ms
 * apart); after a few rounds without changes it stops capturing and sleeps
 * until nitpicker's wakeup.
 *
 * Display off/on is DPMS (drm_client_modeset_dpms): an atomic commit with
 * the CRTC inactive/active, i.e. the vendor's full disable (panel sleep-in,
 * DSIM, DECON off) and enable path. While off, nothing is captured or
 * committed (a plain modeset commit would switch the display on again).
 *
 * The first frame is a copy of the boot loader's framebuffer (reserved-memory
 * node fb_handover, left out of core's RAM by bootstrap), so the screen
 * content stays until the GUI draws.
 *
 * Brightness goes through the panel's backlight device (panel0-backlight,
 * DCS brightness). While the panel is off the driver refuses the update
 * (-EPERM) but keeps the level in the device's properties, and the panel
 * enable path applies it.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/dma-buf.h>
#include <linux/io.h>
#include <linux/iosys-map.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <drm/drm_client.h>
#include <drm/drm_device.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_modes.h>
#include <lx_emul/alloc.h>
#include <lx_emul/fb.h>
#include <lx_emul/task.h>
#include <lx_user/init.h>

/* recorded by the drm_dev_register wrapper (lx_emul.c) */
extern struct drm_device *lx_drm_dev;

static struct task_struct *client_task;
static bool                wakeup_pending;
static unsigned            unchanged;   /* capture rounds without changes */
static bool                power_pending;
static bool                display_on = true;
static bool                display_on_requested = true;
static bool                brightness_pending;
static unsigned            brightness_percent;

static struct drm_client_dev     client;
static struct drm_client_buffer *buffer;


static struct drm_display_mode const *client_mode(void)
{
	struct drm_mode_set *modeset;

	drm_client_for_each_modeset(modeset, &client)
		if (modeset->mode)
			return modeset->mode;

	return NULL;
}


/*
 * Pixel format of the boot loader's layer: DPP L0 (DPU_DMA 0x1c0b0000,
 * drmdpp@1C0B0000) RDMA_IN_CTRL_0 IMG_FORMAT [13:8], cal 9845 codes whose
 * names give the byte order in memory, first byte first (the boot loader of
 * the Pixel 7 uses 0, BGRA). Read before the first commit, when the DPP still
 * has the boot loader's setup.
 */
static unsigned handover_format(void)
{
	enum { DPU_DMA_L0 = 0x1c0b0000, RDMA_IN_CTRL_0 = 0x08 };

	void __iomem *dma = ioremap(DPU_DMA_L0, 0x1000);

	return dma ? (readl(dma + RDMA_IN_CTRL_0) >> 8) & 0x3f : ~0u;
}


/* one pixel of the handover format as XRGB8888, false for other formats */
static bool to_xrgb(unsigned fmt, u32 px, u32 *xrgb)
{
	u32 r, g, b;

	switch (fmt) {
	case 0: case 4: *xrgb = px & 0xffffff; return true;              /* BGRA, BGRX */
	case 1: case 5: r = px;       g = px >> 8;  b = px >> 16; break; /* RGBA, RGBX */
	case 2: case 6: r = px >> 24; g = px >> 16; b = px >> 8;  break; /* ABGR, XBGR */
	case 3: case 7: r = px >> 8;  g = px >> 16; b = px >> 24; break; /* ARGB, XRGB */
	default: return false;
	}
	*xrgb = (r & 0xff) << 16 | (g & 0xff) << 8 | (b & 0xff);
	return true;
}


/*
 * Fill the first frame with the handover framebuffer (w x h, 32 bit,
 * unpadded rows, converted to XRGB8888), or black without one
 */
static void show_handover(void *dst, unsigned pitch, unsigned w, unsigned h)
{
	struct device_node *np = of_find_node_by_name(NULL, "fb_handover");
	unsigned const src_pitch = w * 4;
	unsigned const fmt = handover_format();
	struct resource res;
	void __iomem *src;
	unsigned x, y;
	u32 probe, *row;

	memset(dst, 0, pitch * h);

	if (!to_xrgb(fmt, 0, &probe)) {
		printk("genode_fb: handover format %u not supported\n", fmt);
		return;
	}

	if (!np || of_address_to_resource(np, 0, &res)
	 || resource_size(&res) < (resource_size_t)src_pitch * h) {
		printk("genode_fb: no handover framebuffer\n");
		of_node_put(np);
		return;
	}
	of_node_put(np);

	src = ioremap(res.start, resource_size(&res));
	if (!src) {
		printk("genode_fb: handover framebuffer not mapped\n");
		return;
	}

	/* convert through a cached row: dst is uncached DMA memory */
	row = kmalloc(src_pitch, GFP_KERNEL);
	if (!row) {
		printk("genode_fb: no memory for the handover copy\n");
		return;
	}
	for (y = 0; y < h; y++) {
		u32 *line = dst + y * pitch;
		memcpy_fromio(row, src + y * src_pitch, src_pitch);
		for (x = 0; x < w; x++)
			to_xrgb(fmt, row[x], &line[x]);
	}
	kfree(row);

	/* no iounmap: lx_emul keeps its io_mem mappings */
}


/* drivers/gpu/drm/drm_crtc_internal.h */
int drm_mode_addfb2(struct drm_device *dev, void *data, struct drm_file *file_priv);
int drm_mode_rmfb(struct drm_device *dev, u32 fb_id, struct drm_file *file_priv);


/*
 * Genode's pixels are 0x00RRGGBB, bytes B G R X in memory. exynos-drm maps
 * DRM XRGB8888 to the DPP code "XRGB", which this hardware reads as bytes
 * X R G B; DRM BGRX8888 maps to the code "BGRX" = bytes B G R X, Genode's
 * order.
 *
 * The 6.1 drm_client_framebuffer_create() adds its framebuffer with the
 * legacy drm_mode_addfb (bpp 32, depth 24 -> always XRGB8888), whatever
 * format is asked for. So the client buffer's framebuffer is replaced by
 * one added with drm_mode_addfb2 and the real format, as newer kernels do.
 */
static int use_genode_byte_order(struct drm_client_buffer *b,
                                 unsigned w, unsigned h)
{
	struct drm_mode_fb_cmd2 req = { };
	struct drm_framebuffer *fb;
	int err;

	req.width        = w;
	req.height       = h;
	req.pixel_format = DRM_FORMAT_BGRX8888;
	req.handles[0]   = b->handle;
	req.pitches[0]   = b->pitch;

	err = drm_mode_addfb2(client.dev, &req, client.file);
	if (err)
		return err;

	fb = drm_framebuffer_lookup(client.dev, client.file, req.fb_id);
	if (!fb)
		return -ENOENT;

	/* drop the lookup reference, the file keeps the framebuffer */
	drm_framebuffer_put(fb);

	drm_mode_rmfb(client.dev, b->fb->base.id, client.file);
	b->fb = fb;
	return 0;
}


static int client_setup(void)
{
	struct drm_display_mode const *mode;
	struct drm_mode_set *modeset;
	struct iosys_map map;
	int err;

	if (!lx_drm_dev) {
		printk("genode_fb: no DRM device\n");
		return -ENODEV;
	}

	err = drm_client_init(lx_drm_dev, &client, "genode_fb", NULL);
	if (err) {
		printk("genode_fb: drm_client_init failed: %d\n", err);
		return err;
	}

	err = drm_client_modeset_probe(&client, 0, 0);
	if (err) {
		printk("genode_fb: modeset probe failed: %d\n", err);
		return err;
	}

	mode = client_mode();
	if (!mode) {
		printk("genode_fb: no connected output\n");
		return -ENODEV;
	}

	buffer = drm_client_framebuffer_create(&client, mode->hdisplay,
	                                       mode->vdisplay, DRM_FORMAT_XRGB8888);
	if (IS_ERR(buffer)) {
		printk("genode_fb: framebuffer creation failed: %ld\n", PTR_ERR(buffer));
		return PTR_ERR(buffer);
	}

	err = use_genode_byte_order(buffer, mode->hdisplay, mode->vdisplay);
	if (err) {
		printk("genode_fb: BGRX8888 framebuffer failed: %d\n", err);
		return err;
	}

	/* exynos GEM objects are imported dma-bufs, map the dma-buf itself */
	if (!buffer->gem->import_attach) {
		printk("genode_fb: framebuffer is no dma-buf\n");
		return -EINVAL;
	}
	err = dma_buf_vmap(buffer->gem->import_attach->dmabuf, &map);
	if (err) {
		printk("genode_fb: dma_buf_vmap failed: %d\n", err);
		return err;
	}
	show_handover(map.vaddr, buffer->pitch, mode->hdisplay, mode->vdisplay);

	/* the buffer is cached memory, DECON reads it by DMA */
	lx_emul_mem_cache_clean_invalidate(map.vaddr, buffer->pitch * mode->vdisplay);

	mutex_lock(&client.modeset_mutex);
	drm_client_for_each_modeset(modeset, &client)
		if (modeset->num_connectors)
			modeset->fb = buffer->fb;
	mutex_unlock(&client.modeset_mutex);

	err = drm_client_modeset_commit(&client);
	if (err) {
		printk("genode_fb: initial commit failed: %d\n", err);
		return err;
	}

	lx_emul_framebuffer_ready(map.vaddr, buffer->pitch * mode->vdisplay,
	                          mode->hdisplay, mode->vdisplay);
	return 0;
}


/*
 * Percent of the panel's normal range: s6e3fc3 levels 4..2047, above is
 * high brightness mode (brightness_capability in the panel driver). The
 * levels are about linear in light (2..500 nits), the eye is not: the
 * percentage is squared (20 % -> level 86, 50 % -> 514).
 */
static void set_brightness(unsigned percent)
{
	enum { LEVEL_MIN = 4, LEVEL_NORMAL_MAX = 2047 };

	struct backlight_device *bl = backlight_device_get_by_name("panel0-backlight");
	int level, ret;

	if (!bl) {
		printk("genode_fb: no panel backlight\n");
		return;
	}

	level = LEVEL_MIN + (LEVEL_NORMAL_MAX - LEVEL_MIN) * (int)(percent * percent) / 10000;
	ret   = backlight_device_set_brightness(bl, level);
	if (ret && ret != -EPERM)
		printk("genode_fb: brightness %d failed: %d\n", level, ret);
	put_device(&bl->dev);
}


enum { CAPTURE_RATE_MS = 10, ATTEMPTS_BEFORE_STOP = 7 };


static int client_task_func(void *data)
{
	int const err  = client_setup();
	bool      idle = false;

	for (;;) {
		if (!err && power_pending) {
			int ret;

			power_pending = false;
			if (display_on_requested != display_on) {
				ret = drm_client_modeset_dpms(&client, display_on_requested
				                              ? DRM_MODE_DPMS_ON : DRM_MODE_DPMS_OFF);
				if (ret)
					printk("genode_fb: dpms %s failed: %d\n",
					       display_on_requested ? "on" : "off", ret);
				else
					display_on = display_on_requested;

				/* capture again after switching on */
				unchanged = 0;
			}
		}
		if (!err && brightness_pending) {
			brightness_pending = false;
			set_brightness(brightness_percent);
		}

		/*
		 * Update the panel as the Intel display driver does: paint the
		 * captured changes, then commit (one command-mode frame), then the
		 * next round - changes made meanwhile are painted together, a
		 * frame is never queued behind stale paints. After a few rounds
		 * without changes the capture stops until nitpicker's wakeup.
		 */
		wakeup_pending = false;

		if (err || !display_on) {
			idle = true;
		} else {
			bool const may_stop = ++unchanged >= ATTEMPTS_BEFORE_STOP;

			if (lx_emul_framebuffer_capture(may_stop)) {
				int const ret = drm_client_modeset_commit(&client);

				if (ret)
					printk("genode_fb: flush commit failed: %d\n", ret);
				unchanged = 0;
				idle      = false;
			} else {
				idle = may_stop;
			}
		}

		/*
		 * A request that arrived during a commit must not be lost: block
		 * only when nothing is pending
		 */
		if (power_pending || brightness_pending || wakeup_pending)
			continue;

		if (idle)
			lx_emul_task_schedule(true);
		else
			msleep(CAPTURE_RATE_MS);
	}
	return 0;
}


void lx_emul_framebuffer_power(bool on)
{
	display_on_requested = on;
	power_pending        = true;
	if (client_task)
		lx_emul_task_unblock(client_task);
}


void lx_emul_framebuffer_brightness(unsigned percent)
{
	brightness_percent = percent;
	brightness_pending = true;
	if (client_task)
		lx_emul_task_unblock(client_task);
}


void lx_emul_framebuffer_wakeup(void)
{
	unchanged      = 0;
	wakeup_pending = true;
	if (client_task)
		lx_emul_task_unblock(client_task);
}


void lx_user_init(void)
{
	int pid = kernel_thread(client_task_func, NULL, CLONE_FS | CLONE_FILES);
	client_task = find_task_by_pid_ns(pid, NULL);
}
