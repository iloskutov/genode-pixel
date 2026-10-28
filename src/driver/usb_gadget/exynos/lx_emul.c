/*
 * \brief  Stand-ins of the DDE gadget driver
 * \author Ivan Loskutov
 * \date   2026-10-23
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <lx_emul.h>
#include <linux/extcon.h>
#include <linux/mfd/syscon.h>
#include <linux/pm_runtime.h>

#include <type_c.h>


/*
 * Runtime PM: lx_emul's '__pm_runtime_resume' answers 0 ("resumed"), and
 * dwc3_gadget_pullup() then returns without connecting (it leaves the
 * run/stop to the runtime-resume path, drivers/usb/dwc3/gadget.c).
 * Linux answers 1 for a device that is active already, as it always is
 * here. Linked with --wrap, the shared shadow stays as it is.
 */
int __wrap___pm_runtime_resume(struct device *dev, int rpmflags)
{
	return 1;
}


/*
 * Secure read-modify-write of a PMU register (soc/google/exynos-el3_mon.h),
 * used by the PHY for its isolation. Only the secure monitor writes these
 * registers: bootstrap releases the isolation of the USB PHYs once, and it
 * is never set again, so the requests are dropped.
 */
#include <soc/google/exynos-el3_mon.h>

int rmw_priv_reg(phys_addr_t reg, u32 mask, u32 val) { return 0; }


/*
 * The PHY keeps the PMU regmap only to know that its isolation is
 * controlled (rmw_priv_reg does the access), a dummy suffices
 */
static int pmu_regmap_dummy;

struct regmap *syscon_regmap_lookup_by_phandle(struct device_node *np,
                                               const char *property)
{
	return (struct regmap *)&pmu_regmap_dummy;
}


/*
 * extcon of the Type-C controller (the DT 'extcon' properties are removed,
 * edev stays NULL): the phone is always attached as a USB device, the
 * cable orientation comes from the Type-C controller (type_c.h)
 */
int extcon_get_state(struct extcon_dev *edev, unsigned int id)
{
	return id == EXTCON_USB;
}


int extcon_get_property(struct extcon_dev *edev, unsigned int id,
                        unsigned int prop, union extcon_property_value *val)
{
	val->intval = 0;

	if (id == EXTCON_USB && prop == EXTCON_PROP_USB_TYPEC_POLARITY)
		val->intval = usb_gadget_cc_polarity();

	return 0;
}
