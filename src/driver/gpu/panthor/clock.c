/*
 * \brief  Stand-in for the Linux clock interface of the panthor GPU driver
 * \author Ivan Loskutov
 * \date   2026-10-07
 *
 * The gs201 has no clock the GPU driver could own: the rates of the GPU
 * are set through ACPM DVFS domains. The driver gets one
 * clock object for its "core" clock; enabling and disabling it does
 * nothing, its rate is the "clock-frequency" property of the fixed-clock
 * node the device tree refers to. The optional clocks "stacks" and
 * "coregroup" do not exist.
 */

/*
 * Copyright (C) 2026 Ivan Loskutov
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clk/clk-conf.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_clk.h>
#include <lx_emul/debug.h>

struct clk { unsigned long rate; };

static struct clk _core_clk;


struct clk *clk_get(struct device *dev, const char *con_id)
{
	struct device_node *np;
	u32 rate = 0;

	if (!dev || !dev->of_node)
		return ERR_PTR(-ENOENT);

	/* the driver asks for its first clock without a name */
	if (con_id && strcmp(con_id, "core") != 0)
		return ERR_PTR(-ENOENT);

	np = of_parse_phandle(dev->of_node, "clocks", 0);
	if (!np)
		return ERR_PTR(-ENOENT);

	if (of_property_read_u32(np, "clock-frequency", &rate))
		printk("clk_get: clock node without clock-frequency\n");

	of_node_put(np);

	_core_clk.rate = rate;
	return &_core_clk;
}


void clk_put(struct clk *clk) { }


struct clk *devm_clk_get(struct device *dev, const char *id)
{
	return clk_get(dev, id);
}


struct clk *devm_clk_get_optional(struct device *dev, const char *id)
{
	struct clk *clk = clk_get(dev, id);

	return (clk == ERR_PTR(-ENOENT)) ? NULL : clk;
}


unsigned long clk_get_rate(struct clk *clk)
{
	return clk ? clk->rate : 0;
}


int clk_set_rate(struct clk *clk, unsigned long rate)
{
	if (clk && clk->rate != rate)
		printk("clk_set_rate: %lu Hz requested, rate stays at %lu Hz\n",
		       rate, clk->rate);
	return 0;
}


int clk_prepare(struct clk *clk) { return 0; }


int clk_enable(struct clk *clk) { return 0; }


void clk_disable(struct clk *clk) { }


void clk_unprepare(struct clk *clk) { }


/*
 * What the clock framework does for a clock provider of the device tree:
 * no platform device for the node, and the node counts as initialized, so
 * that the driver core does not wait for a driver of the "supplier"
 * (fw_devlink) before it probes the GPU.
 */
void of_clk_init(const struct of_device_id *matches)
{
	struct device_node *np;

	for_each_node_with_property(np, "#clock-cells") {
		of_node_set_flag(np, OF_POPULATED);
		fwnode_dev_initialized(of_fwnode_handle(np), true);
	}
}


int of_clk_set_defaults(struct device_node *node, bool clk_supplier)
{
	return 0;
}


const char *__clk_get_name(const struct clk *clk)
{
	return "gpu";
}


/*******************************
 ** Operating points, devfreq **
 *******************************/

/*
 * No OPP table, no regulator, no devfreq device: the GPU stays at the rate
 * and voltage it has when the driver starts. The one "operating point" the
 * driver gets to see is the rate of the stand-in clock.
 */

#include <linux/pm_opp.h>
#include <linux/devfreq.h>

/* reached via devm_pm_opp_set_regulators (the "mali" supply) */
int devm_pm_opp_set_config(struct device *dev, struct dev_pm_opp_config *config)
{
	return 0;
}


int devm_pm_opp_of_add_table(struct device *dev)
{
	return 0;
}


struct dev_pm_opp *devfreq_recommended_opp(struct device *dev,
                                           unsigned long *freq, u32 flags)
{
	*freq = _core_clk.rate;
	return NULL;
}


struct dev_pm_opp *dev_pm_opp_find_freq_floor(struct device *dev,
                                              unsigned long *freq)
{
	*freq = _core_clk.rate;
	return NULL;
}


int dev_pm_opp_set_opp(struct device *dev, struct dev_pm_opp *opp)
{
	return 0;
}


int dev_pm_opp_set_rate(struct device *dev, unsigned long target_freq)
{
	return 0;
}


void dev_pm_opp_put(struct dev_pm_opp *opp) { }


/*
 * NULL is no error for the driver: it then skips every devfreq operation
 * (load tracking, suspend, resume)
 */
struct devfreq *devm_devfreq_add_device(struct device *dev,
                                        struct devfreq_dev_profile *profile,
                                        const char *governor_name, void *data)
{
	return NULL;
}


int devfreq_suspend_device(struct devfreq *devfreq)
{
	return 0;
}


int devfreq_resume_device(struct devfreq *devfreq)
{
	return 0;
}
