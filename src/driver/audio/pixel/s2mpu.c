/*
 * \brief  S2MPU of the security chip (GSA): programming as pKVM does
 * \author Ivan Loskutov
 * \date   2026-10-01
 *
 * GSA reaches AP memory through a stage-2 memory protection unit
 * (DT node s2mpu_gsa@17c60000), which the boot loader leaves enabled without
 * access. On Android it is programmed by pKVM each time a GSA data-transfer
 * command is issued: the GSA driver takes the GSA wakelock (the S2MPU sits
 * in GSA's core power domain), resumes the S2MPU (hypercall), sends the
 * command, suspends the S2MPU and drops the wakelock (gsa_mbox.c,
 * CONFIG_GSA_PKVM). There is no pKVM here: the functions below do the
 * hardware part of it from this component, around the authentication of
 * the AoC firmware (lx_user.c).
 *
 * EVERY FUNCTION HERE MUST BE CALLED WITH THE GSA WAKELOCK HELD. The block
 * is powered only then, an access to an unpowered block is an SError, which
 * stops the system.
 */

/*
 * Register map and sequences derived from drivers/soc/google/pkvm-s2mpu of
 * the google_soc_gs port (common/kvm_s2mpu.h, common/hyp/s2mpu.c,
 * common/hyp/io-mpt-s2mpu.c, common/pkvm_s2mpu.c):
 *
 * Copyright (C) 2021 - Google LLC
 * Author: David Brazdil <dbrazdil@google.com>
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <lx_emul.h>

#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#define S2LOG(fmt, ...) printk("pixel_audio: s2mpu: " fmt "\n", ##__VA_ARGS__)

/***************
 ** Registers **
 ***************/

/* register block version 1 and 2 (kvm_s2mpu.h, "REG_NS_*") */
enum {
	REG_CTRL0                 = 0x0000,
	REG_CTRL1                 = 0x0004,
	REG_CFG                   = 0x0010,
	REG_IRQ_ENABLE_VID_SET    = 0x0020,
	REG_IRQ_CLEAR             = 0x002c,
	REG_VERSION               = 0x0060,
	REG_STATUS                = 0x0068,
	REG_NUM_CONTEXT           = 0x0100,  /* version 2 only */
	REG_CONTEXT_CFG_VALID_VID = 0x0104,  /* version 2 only */
	REG_ALL_INVALIDATION      = 0x1000,
	REG_FAULT_STATUS          = 0x2000,
};

enum {
	NR_VIDS      = 8,
	ALL_VIDS     = 0xff,
	NR_GIGABYTES = 64,

	/*
	 * The L1 entries of 4..33 GiB are read-only registers with the value
	 * "no access" (kvm_s2mpu.h, for_each_gb): no device is mapped there.
	 * The RAM of the phone is at 2..4 GiB and from 34 GiB on.
	 */
	RO_GIGABYTES_FIRST = 4,
	RO_GIGABYTES_LAST  = 33,
};

enum {
	CTRL0_ENABLE           = 1u << 0,
	CTRL0_INTERRUPT_ENABLE = 1u << 1,
	CTRL0_FAULT_RESP_ERR   = 1u << 2,  /* SLVERR (v1) / DECERR (v2) on a fault */

	/* value written by the hypervisor (hyp/s2mpu.c, __set_control_regs) */
	CTRL0_PKVM = CTRL0_ENABLE | CTRL0_INTERRUPT_ENABLE | CTRL0_FAULT_RESP_ERR,

	STATUS_BUSY            = 1u << 0,
	STATUS_ON_INVALIDATING = 1u << 1,

	NUM_CONTEXT_MASK = 0xf,

	INVALIDATION_INVALIDATE = 1u << 0,

	/* L1ENTRY_ATTR: bit 0 = L2 table, bits 2:1 = access of a whole GiB */
	L1_ATTR_PROT_SHIFT = 1,
	L1_PROT_RW         = 3,   /* bit 0 read, bit 1 write */
	L1_ATTR_1G_RW      = L1_PROT_RW << L1_ATTR_PROT_SHIFT,

	FAULT_INFO_RW_BIT = 1u << 20,
};

#define VERSION_CHECK_MASK 0xffff0000u   /* without the RTL version */
#define VERSION_1          0x11000000u
#define VERSION_2          0x20000000u

static unsigned reg_fault_pa_low(unsigned vid)  { return 0x2004 + vid*0x20; }
static unsigned reg_fault_pa_high(unsigned vid) { return 0x2008 + vid*0x20; }
static unsigned reg_fault_info(unsigned vid)    { return 0x2010 + vid*0x20; }

static unsigned reg_l1_attr(unsigned vid, unsigned gb)
{
	return 0x4004 + vid*0x200 + gb*0x8;
}

static bool gb_writeable(unsigned gb)
{
	return gb < RO_GIGABYTES_FIRST || gb > RO_GIGABYTES_LAST;
}


/************
 ** Access **
 ************/

static void __iomem *_base;
static u32           _version;


/*
 * The register range comes from the device-tree node, the mapping from the
 * platform device 's2mpu_gsa' of this component (no Linux driver is bound
 * to the node, it only describes the range).
 */
static bool mapped(void)
{
	struct device_node *np;
	struct resource     res;

	if (_base)
		return true;

	np = of_find_node_by_path(PIXEL_AUDIO_DT_S2MPU);
	if (!np) {
		S2LOG("device-tree node %s missing", PIXEL_AUDIO_DT_S2MPU);
		return false;
	}

	if (of_address_to_resource(np, 0, &res)) {
		S2LOG("device-tree node %s has no usable 'reg'", PIXEL_AUDIO_DT_S2MPU);
		of_node_put(np);
		return false;
	}
	of_node_put(np);

	_base = ioremap(res.start, resource_size(&res));
	if (!_base) {
		S2LOG("%pR not mapped: platform device \"s2mpu_gsa\" not granted "
		      "to this component?", &res);
		return false;
	}

	return true;
}


static u32 rd(unsigned off) { return readl(_base + off); }


/* write a register and return what it reads back as */
static u32 wr(unsigned off, u32 value)
{
	writel(value, _base + off);
	return readl(_base + off);
}


static bool version_is(u32 version)
{
	return (_version & VERSION_CHECK_MASK) == version;
}


/************
 ** Faults **
 ************/

void pixel_audio_s2mpu_faults(char const *when)
{
	unsigned vid;
	u32      status;

	if (!_base)
		return;

	status = rd(REG_FAULT_STATUS);

	for (vid = 0; vid < NR_VIDS; vid++) {

		u32 lo, hi, info;

		if (!(status & (1u << vid)))
			continue;

		lo   = rd(reg_fault_pa_low(vid));
		hi   = rd(reg_fault_pa_high(vid));
		info = rd(reg_fault_info(vid));

		S2LOG("fault %s: VID %u, %s of address 0x%llx, type %u "
		      "(1 table walk, 2 access permission, 4 context), length %u, "
		      "AXI id 0x%x", when, vid,
		      (info & FAULT_INFO_RW_BIT) ? "write" : "read",
		      ((unsigned long long)(hi & 0xf) << 32) | lo,
		      (info >> 21) & 7, (info >> 16) & 0xf, info & 0xffff);

		/* as the fault handler of the Linux host does (pkvm-s2mpu-faults.c) */
		writel(1u << vid, _base + REG_IRQ_CLEAR);
	}
}


/**********
 ** Open **
 **********/

/*
 * Version 2 ignores writes to the L1 entries of a VID that has no context:
 * one context per VID, as far as the block has contexts (hyp/s2mpu.c,
 * __initialize_ctx with all VIDs)
 */
static int assign_contexts(void)
{
	unsigned const num = rd(REG_NUM_CONTEXT) & NUM_CONTEXT_MASK;

	unsigned ctx;
	u32      cfg = 0;

	for (ctx = 0; ctx < NR_VIDS && ctx < num; ctx++)
		cfg |= (ctx << (4*ctx)) | (1u << (4*ctx + 3));

	if (!cfg) {
		S2LOG("version 2 block without contexts, nothing written");
		return -EINVAL;
	}

	wr(REG_CONTEXT_CFG_VALID_VID, cfg);
	return 0;
}


/*
 * What the hypervisor does when the Linux host resumes the S2MPU
 * (hyp/s2mpu.c: s2mpu_resume -> initialize_with_mpt), for a host that owns
 * all memory: every GiB of every VID read-write at 1 GiB granularity, the
 * cached entries invalidated, then the control registers with the block
 * enabled. The L2 table addresses, which the hypervisor writes too, are
 * left alone: they are not used by an entry without the L2 flag. The
 * "SysMMU sync" device the hypervisor waits for after the invalidation is
 * not accessed (nothing is in flight: GSA transfers data only on request).
 */
int pixel_audio_s2mpu_open(void)
{
	unsigned vid, gb, bad = 0;
	u32      ctrl0;

	if (!mapped())
		return -ENODEV;

	_version = rd(REG_VERSION);

	if (!version_is(VERSION_1) && !version_is(VERSION_2)) {
		S2LOG("VERSION 0x%08x is not a version 1 or 2 block, nothing written",
		      _version);
		return -ENODEV;
	}

	if (version_is(VERSION_2)) {
		int const rc = assign_contexts();
		if (rc)
			return rc;
	}

	for (vid = 0; vid < NR_VIDS; vid++)
		for (gb = 0; gb < NR_GIGABYTES; gb++)
			if (gb_writeable(gb)
			 && wr(reg_l1_attr(vid, gb), L1_ATTR_1G_RW) != L1_ATTR_1G_RW)
				bad++;

	writel(INVALIDATION_INVALIDATE, _base + REG_ALL_INVALIDATION);

	/* version 2: no register access while the invalidation is in progress */
	if (version_is(VERSION_2)) {
		unsigned const mask = STATUS_BUSY | STATUS_ON_INVALIDATING;
		unsigned polls = 0;

		while ((rd(REG_STATUS) & mask) == mask && polls < 100000)
			polls++;

		if (polls == 100000)
			S2LOG("invalidation still busy after %u polls of STATUS", polls);
	}

	wr(REG_IRQ_ENABLE_VID_SET, ALL_VIDS);
	wr(REG_CFG,   0);
	wr(REG_CTRL1, 0);

	ctrl0 = wr(REG_CTRL0, CTRL0_PKVM);

	if (ctrl0 == CTRL0_PKVM && !bad)
		return 0;

	S2LOG("programming incomplete: CTRL0 0x%x (wanted 0x%x), %u L1 entries "
	      "did not take the value", ctrl0, CTRL0_PKVM, bad);
	return -EIO;
}
