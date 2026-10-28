/*
 * \brief  Hand-written dummies of the DDE gadget driver
 * \author Ivan Loskutov
 * \date   2026-10-23
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <lx_emul.h>

#include <linux/clk.h>

struct clk *of_clk_get(struct device_node *np, int index) { return ERR_PTR(-ENOENT); }

struct clk *devm_clk_get(struct device *dev, const char *id) { return clk_get(dev, id); }

/*
 * As Linux: a clock that does not exist is no error for an optional one.
 * The shared shadow (clk-devres.c) answers -ENOENT, and the dwc3 core,
 * whose node has none of its optional clocks, would not probe.
 */
struct clk *devm_clk_get_optional(struct device *dev, const char *id)
{
	struct clk *clk = clk_get(dev, id);
	return PTR_ERR_OR_ZERO(clk) == -ENOENT ? NULL : clk;
}


#include <linux/delay.h>

void __ndelay(unsigned long nsecs) { udelay(DIV_ROUND_UP(nsecs, 1000)); }


#include <linux/pm_runtime.h>
#include <linux/pm_wakeup.h>

int  pm_runtime_force_suspend(struct device *dev) { return 0; }
int  pm_runtime_force_resume(struct device *dev) { return 0; }
void pm_relax(struct device *dev) { }


#include <linux/pinctrl/consumer.h>

/* no pin control here: pinctrl_bind_pins() accepts that for every device */
struct pinctrl *devm_pinctrl_get(struct device *dev) { return ERR_PTR(-ENODEV); }


#include <linux/pinctrl/consumer.h>

void devm_pinctrl_put(struct pinctrl *p) { }

struct pinctrl_state *pinctrl_lookup_state(struct pinctrl *p, const char *name)
{
	return ERR_PTR(-ENODEV);
}

int pinctrl_select_state(struct pinctrl *p, struct pinctrl_state *s) { return 0; }
int pinctrl_pm_select_default_state(struct device *dev) { return 0; }
int pinctrl_pm_select_sleep_state(struct device *dev)   { return 0; }


/* runtime PM as always active, see lx_emul.c */
void pm_runtime_allow(struct device *dev)  { }
void pm_runtime_forbid(struct device *dev) { }


#include <linux/usb/of.h>

/* the DT has no 'phy_type': dwc3 keeps its UTMI default */
enum usb_phy_interface of_usb_get_phy_mode(struct device_node *np)
{
	return USBPHY_INTERFACE_MODE_UNKNOWN;
}


#include <linux/extcon.h>

/* no extcon devices (the DT links are removed, lx_emul.c answers states) */
struct extcon_dev *extcon_get_edev_by_phandle(struct device *dev, int index)
{
	return ERR_PTR(-ENODEV);
}

struct extcon_dev *extcon_find_edev_by_node(struct device_node *node)
{
	return ERR_PTR(-ENODEV);
}

struct extcon_dev *extcon_get_extcon_dev(const char *extcon_name)
{
	return ERR_PTR(-ENODEV);
}

int extcon_register_notifier(struct extcon_dev *edev, unsigned int id,
                             struct notifier_block *nb)
{
	return -ENODEV;
}


#include <linux/power_supply.h>

/* no power-supply class (the DT's usb-psy-name is removed) */
struct power_supply *power_supply_get_by_name(const char *name) { return NULL; }
void power_supply_put(struct power_supply *psy) { }

int power_supply_set_property(struct power_supply *psy, enum power_supply_property psp,
                              const union power_supply_propval *val)
{
	return -ENODEV;
}


#include <linux/nls.h>

/*
 * UTF-8 to UTF-16 for the USB string descriptors (usbstring.c), as
 * fs/nls/nls_base.c for code points below 0x10000
 */
int utf8s_to_utf16s(const u8 *s, int inlen, enum utf16_endian endian,
                    wchar_t *pwcs, int maxout)
{
	int n = 0;

	while (inlen > 0 && *s && n < maxout) {
		unsigned c = *s;
		int len = 1;

		if (c >= 0xe0 && inlen >= 3) {
			c = ((s[0] & 0x0f) << 12) | ((s[1] & 0x3f) << 6) | (s[2] & 0x3f);
			len = 3;
		} else if (c >= 0xc0 && inlen >= 2) {
			c = ((s[0] & 0x1f) << 6) | (s[1] & 0x3f);
			len = 2;
		} else if (c >= 0x80) {
			return -EINVAL;
		}

		pwcs[n++] = endian == UTF16_BIG_ENDIAN ? cpu_to_be16(c) : cpu_to_le16(c);
		s += len;
		inlen -= len;
	}
	return n;
}
