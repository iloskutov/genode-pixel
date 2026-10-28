/*
 * \brief  Slim Exynos glue of the GS201's DWC3 (device mode only)
 * \author Ivan Loskutov
 * \date   2026-10-23
 *
 * Instead of the vendor's dwc3-exynos.c with its OTG state machine,
 * extcon, Type-C and power-supply plumbing: the platform driver of the
 * glue node ("samsung,exynos9-dwusb") only creates the dwc3 child, and
 * 'exynos_dwc3_config' applies the vendor's register setup
 * (dwc3_exynos_phy_setup, dwc3_core_config, dwc3_exynos_core_init of
 * google_soc_gs drivers/usb/dwc3/dwc3-exynos.c), which the vendor runs on
 * every connect before the gadget is connected. Its extras for other
 * revisions than DWC_usb31 1.70a and 1.80a..1.90a (the GS201 has 1.80a)
 * are left out.
 */

/*
 * Derived from drivers/usb/dwc3/dwc3-exynos.c of the google_soc_gs port:
 *
 * Copyright (C) 2022 Samsung Electronics Co., Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#include <core.h>        /* drivers/usb/dwc3 */
#include <exynos_glue.h>

/* registers and bits of the vendor's core-exynos.h not in upstream core.h */
#define EXYNOS_GSBUSCFG0_INCRBRSTEN        BIT(0)
#define EXYNOS_GSBUSCFG0_INCR4BRSTEN       BIT(1)
#define EXYNOS_GSBUSCFG0_INCR8BRSTEN       BIT(2)
#define EXYNOS_GSBUSCFG0_INCR16BRSTEN      BIT(3)
#define EXYNOS_GSBUSCFG0_DESWRREQINFO      (2 << 16)
#define EXYNOS_GSBUSCFG0_DATWRREQINFO      (2 << 20)
#define EXYNOS_GSBUSCFG0_DESRDREQINFO      (2 << 24)
#define EXYNOS_GSBUSCFG0_DATRDREQINFO      (2 << 28)
#define EXYNOS_GSBUSCFG1_BREQLIMIT(n)      ((n) << 8)
#define EXYNOS_GUCTL_REFCLKPER(n)          ((n) << 22)
#define EXYNOS_GUCTL_REFCLKPER_MASK        EXYNOS_GUCTL_REFCLKPER(0x3ff)
#define EXYNOS_GUCTL_NOEXTRDL              BIT(21)
#define EXYNOS_GUCTL_SPRSCTRLTRANSEN       BIT(17)
#define EXYNOS_GUCTL_USBHSTINAUTORETRYEN   BIT(14)
#define EXYNOS_GUCTL_DTOUT(n)              (n)
#define EXYNOS_GUCTL_DTOUT_MASK            (0x7ff)
#define EXYNOS_GUSB3PIPECTL_U1U2EXITFAIL_TO_RECOV BIT(25)
#define EXYNOS_ELASTIC_BUFFER_MODE         BIT(0)
#define EXYNOS_LSKIPFREQ                   0xd020
#define EXYNOS_PENDING_HP_TIMER_US(n)      ((n) << 16)
#define EXYNOS_EN_US_HP_TIMER              BIT(15)
#define EXYNOS_FORCE_GEN1                  BIT(10)
#define EXYNOS_PM_ENTRY_TIMER_US(n)        ((n) << 20)
#define EXYNOS_PM_LC_TIMER_US(n)           ((n) << 24)
#define EXYNOS_EN_PM_TIMER_US              BIT(27)

/* linux/usb/dwc3-exynos.h of the vendor */
#define EXYNOS_LLUCTL_LTSSM_TIMER_OVRRD    BIT(23)
#define EXYNOS_LLUCTL_PIPE_RESET           BIT(7)
#define EXYNOS_LLUCTL_TX_TS1_CNT(n)        ((n) << 0)
#define EXYNOS_LLUCTL_TX_TS1_CNT_MASK      EXYNOS_LLUCTL_TX_TS1_CNT(0x1f)
#define EXYNOS_PM_ENTRY_TIMER_US_MASK      EXYNOS_PM_ENTRY_TIMER_US(0xf)
#define EXYNOS_PM_LC_TIMER_US_MASK         EXYNOS_PM_LC_TIMER_US(0x7)
#define EXYNOS_BU31RHBDBG                  0xd800
#define EXYNOS_BU31RHBDBG_TOUTCTL          (0x1 << 3)
#define EXYNOS_GUCTL1_IP_GAP_ADD_ON(n)     ((n) << 21)
#define EXYNOS_GUCTL1_IP_GAP_ADD_ON_MASK   EXYNOS_GUCTL1_IP_GAP_ADD_ON(0x7)
#define EXYNOS_GFLADJ_REFCLK_240MHZDECR_PLS1 BIT(31)
#define EXYNOS_GFLADJ_REFCLK_240MHZ_DECR(n) ((n) << 24)
#define EXYNOS_GFLADJ_REFCLK_240MHZ_DECR_MASK EXYNOS_GFLADJ_REFCLK_240MHZ_DECR(0x7f)
#define EXYNOS_GFLADJ_REFCLK_FLADJ_MASK    (0x3fff << 8)

struct exynos_config
{
	bool adj_sof_accuracy;
	bool sparse_transfer_control;
	bool no_extra_delay;
	bool ux_exit_in_px_quirk;
	bool elastic_buf_mode_quirk;
	bool force_gen1;
	bool u1u2_exitfail_to_recov_quirk;
	u32  usb_host_device_timeout;
};

static struct exynos_config config;
static bool                config_valid;


static u32 rd(struct dwc3 *dwc, u32 offset)
{
	return readl(dwc->regs + offset - DWC3_GLOBALS_REGS_START);
}


static void wr(struct dwc3 *dwc, u32 offset, u32 value)
{
	writel(value, dwc->regs + offset - DWC3_GLOBALS_REGS_START);
}


/* dwc3_exynos_get_properties */
static void read_config(struct device *dev)
{
	u32 v;

	config.adj_sof_accuracy        = !of_property_read_u32(dev->of_node, "adj-sof-accuracy", &v) && v;
	config.sparse_transfer_control = !of_property_read_u32(dev->of_node, "enable_sprs_transfer", &v) && v;
	if (of_property_read_u32(dev->of_node, "usb_host_device_timeout", &config.usb_host_device_timeout))
		config.usb_host_device_timeout = 0;

	config.no_extra_delay               = device_property_read_bool(dev, "no_extra_delay");
	config.ux_exit_in_px_quirk          = device_property_read_bool(dev, "ux_exit_in_px_quirk");
	config.elastic_buf_mode_quirk       = device_property_read_bool(dev, "elastic_buf_mode_quirk");
	config.force_gen1                   = device_property_read_bool(dev, "force_gen1");
	config.u1u2_exitfail_to_recov_quirk = device_property_read_bool(dev, "u1u2_exitfail_quirk");

	config_valid = true;
}


/* dwc3_exynos_phy_setup */
static void phy_setup(struct dwc3 *dwc)
{
	u32 reg = rd(dwc, DWC3_GUSB3PIPECTL(0));

	if (config.ux_exit_in_px_quirk)
		reg |= DWC3_GUSB3PIPECTL_UX_EXIT_PX;
	if (config.u1u2_exitfail_to_recov_quirk)
		reg |= EXYNOS_GUSB3PIPECTL_U1U2EXITFAIL_TO_RECOV;
	wr(dwc, DWC3_GUSB3PIPECTL(0), reg);

	reg = rd(dwc, DWC3_GUSB2PHYCFG(0));
	if (!dwc->dis_enblslpm_quirk)
		reg |= DWC3_GUSB2PHYCFG_ENBLSLPM;
	if (config.adj_sof_accuracy)
		reg &= ~DWC3_GUSB2PHYCFG_U2_FREECLK_EXISTS;
	wr(dwc, DWC3_GUSB2PHYCFG(0), reg);
}


/* dwc3_core_config, the parts for DWC_usb31 */
static void core_config(struct dwc3 *dwc)
{
	u32 reg = rd(dwc, DWC3_GSBUSCFG0);
	reg |= EXYNOS_GSBUSCFG0_INCRBRSTEN | EXYNOS_GSBUSCFG0_INCR16BRSTEN;
	if (!DWC3_VER_IS_PRIOR(DWC3, 220A))
		reg |= EXYNOS_GSBUSCFG0_DESWRREQINFO | EXYNOS_GSBUSCFG0_DATWRREQINFO
		     | EXYNOS_GSBUSCFG0_DESRDREQINFO | EXYNOS_GSBUSCFG0_DATRDREQINFO;
	wr(dwc, DWC3_GSBUSCFG0, reg);

	if (DWC3_VER_IS(DWC31, 170A))
		wr(dwc, DWC3_GSBUSCFG1, rd(dwc, DWC3_GSBUSCFG1) | EXYNOS_GSBUSCFG1_BREQLIMIT(0x8));

	reg = rd(dwc, DWC3_GUCTL);
	reg |= EXYNOS_GUCTL_USBHSTINAUTORETRYEN;
	if (DWC3_VER_IS(DWC31, 170A)) {
		reg &= ~EXYNOS_GUCTL_REFCLKPER_MASK;
		reg |= EXYNOS_GUCTL_REFCLKPER(0xf);
	} else if (DWC3_VER_IS_WITHIN(DWC31, 180A, 190A)) {
		reg &= ~EXYNOS_GUCTL_REFCLKPER_MASK;
		reg |= EXYNOS_GUCTL_REFCLKPER(0x34);
	}
	if (config.sparse_transfer_control)
		reg |= EXYNOS_GUCTL_SPRSCTRLTRANSEN;
	if (config.no_extra_delay)
		reg |= EXYNOS_GUCTL_NOEXTRDL;
	if (config.usb_host_device_timeout) {
		reg &= ~EXYNOS_GUCTL_DTOUT_MASK;
		reg |= EXYNOS_GUCTL_DTOUT(config.usb_host_device_timeout);
	}
	wr(dwc, DWC3_GUCTL, reg);

	if (!DWC3_VER_IS_PRIOR(DWC3, 250A))
		wr(dwc, DWC3_GUSB3PIPECTL(0),
		   rd(dwc, DWC3_GUSB3PIPECTL(0)) | DWC3_GUSB3PIPECTL_DISRXDETINP3);

	reg = rd(dwc, DWC3_GUSB3PIPECTL(0));
	if (config.ux_exit_in_px_quirk)
		reg |= DWC3_GUSB3PIPECTL_UX_EXIT_PX;
	if (config.elastic_buf_mode_quirk)
		reg |= EXYNOS_ELASTIC_BUFFER_MODE;
	wr(dwc, DWC3_GUSB3PIPECTL(0), reg);

	if (DWC3_VER_IS(DWC31, 170A)) {
		wr(dwc, DWC3_LLUCTL, rd(dwc, DWC3_LLUCTL)
		   | EXYNOS_PENDING_HP_TIMER_US(0xb) | EXYNOS_EN_US_HP_TIMER | EXYNOS_FORCE_GEN1);
		wr(dwc, EXYNOS_LSKIPFREQ, rd(dwc, EXYNOS_LSKIPFREQ)
		   | EXYNOS_PM_ENTRY_TIMER_US(0x9) | EXYNOS_PM_LC_TIMER_US(0x5) | EXYNOS_EN_PM_TIMER_US);
	} else if (DWC3_VER_IS_WITHIN(DWC31, 180A, 190A)) {
		reg = rd(dwc, DWC3_LLUCTL);
		reg &= ~EXYNOS_LLUCTL_TX_TS1_CNT_MASK;
		reg |= EXYNOS_PENDING_HP_TIMER_US(0xb) | EXYNOS_EN_US_HP_TIMER
		     | EXYNOS_LLUCTL_PIPE_RESET | EXYNOS_LLUCTL_LTSSM_TIMER_OVRRD
		     | EXYNOS_LLUCTL_TX_TS1_CNT(0x0);
		if (config.force_gen1)
			reg |= EXYNOS_FORCE_GEN1;
		wr(dwc, DWC3_LLUCTL, reg);

		reg = rd(dwc, EXYNOS_LSKIPFREQ);
		reg &= ~(EXYNOS_PM_LC_TIMER_US_MASK | EXYNOS_PM_ENTRY_TIMER_US_MASK);
		reg |= EXYNOS_PM_ENTRY_TIMER_US(0x9) | EXYNOS_PM_LC_TIMER_US(0x5)
		     | EXYNOS_EN_PM_TIMER_US;
		wr(dwc, EXYNOS_LSKIPFREQ, reg);

		reg = rd(dwc, DWC3_GUSB3PIPECTL(0));
		reg &= ~(DWC3_GUSB3PIPECTL_DISRXDETINP3 | DWC3_GUSB3PIPECTL_RX_DETOPOLL);
		wr(dwc, DWC3_GUSB3PIPECTL(0), reg);

		wr(dwc, DWC3_GSBUSCFG0, rd(dwc, DWC3_GSBUSCFG0)
		   | EXYNOS_GSBUSCFG0_INCR8BRSTEN | EXYNOS_GSBUSCFG0_INCR4BRSTEN);

		wr(dwc, EXYNOS_BU31RHBDBG, rd(dwc, EXYNOS_BU31RHBDBG) | EXYNOS_BU31RHBDBG_TOUTCTL);

		reg = rd(dwc, DWC3_GUCTL1);
		reg &= ~EXYNOS_GUCTL1_IP_GAP_ADD_ON_MASK;
		reg |= EXYNOS_GUCTL1_IP_GAP_ADD_ON(0x1);
		wr(dwc, DWC3_GUCTL1, reg);
	}
}


void exynos_dwc3_config(struct dwc3 *dwc)
{
	u32 reg;

	if (!config_valid) {
		pr_err("usb_gadget: glue configuration missing\n");
		return;
	}

	pr_info("usb_gadget: DWC_usb31 revision %08x, Exynos setup\n", dwc->revision);

	phy_setup(dwc);
	core_config(dwc);

	/* dwc3_exynos_core_init: reference clock 19.2 MHz */
	if (DWC3_VER_IS_WITHIN(DWC31, 180A, 190A)) {
		reg = rd(dwc, DWC3_GFLADJ);
		if ((reg & DWC3_GFLADJ_30MHZ_MASK) != dwc->fladj) {
			reg &= ~DWC3_GFLADJ_30MHZ_MASK;
			reg |= dwc->fladj;
		}
		reg &= ~EXYNOS_GFLADJ_REFCLK_240MHZ_DECR_MASK;
		reg |= EXYNOS_GFLADJ_REFCLK_240MHZ_DECR(0xc);
		reg |= EXYNOS_GFLADJ_REFCLK_240MHZDECR_PLS1;
		reg |= DWC3_GFLADJ_REFCLK_LPM_SEL;
		reg &= ~EXYNOS_GFLADJ_REFCLK_FLADJ_MASK;
		reg |= DWC3_GFLADJ_30MHZ_SDBND_SEL;
		wr(dwc, DWC3_GFLADJ, reg);
	}

	reg = rd(dwc, DWC3_GCTL);
	if (dwc->dis_u2_freeclk_exists_quirk)
		reg |= DWC3_GCTL_SOFITPSYNC;
	else
		reg &= ~DWC3_GCTL_SOFITPSYNC;
	if (config.adj_sof_accuracy)
		reg &= ~DWC3_GCTL_SOFITPSYNC;
	wr(dwc, DWC3_GCTL, reg);
}


static int exynos_dwc3_glue_probe(struct platform_device *pdev)
{
	int err;

	read_config(&pdev->dev);

	err = of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
	if (err)
		dev_err(&pdev->dev, "creating the dwc3 core device failed: %d\n", err);
	return err;
}


static const struct of_device_id exynos_dwc3_glue_match[] = {
	{ .compatible = "samsung,exynos9-dwusb" },
	{ }
};


static struct platform_driver exynos_dwc3_glue_driver = {
	.probe  = exynos_dwc3_glue_probe,
	.driver = {
		.name           = "exynos-dwc3-glue",
		.of_match_table = exynos_dwc3_glue_match,
	},
};

module_platform_driver(exynos_dwc3_glue_driver);
