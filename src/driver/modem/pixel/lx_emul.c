/*
 * \brief  Linux functions of the modem driver with behaviour
 * \author Ivan Loskutov
 * \date   2026-11-07
 *
 * The stubs without behaviour are in dummies.c.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <lx_emul.h>
#include <lx_emul/io_mem.h>

#include <linux/arm-smccc.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/pci.h>
#include <linux/pm_wakeup.h>
#include <linux/shm_ipc.h>
#include <linux/slab.h>
#include <linux/soc/samsung/exynos-smc.h>
#include <soc/google/exynos-el3_mon.h>


/*********
 ** SMC **
 *********/

void pixel_modem_smc(unsigned long const a[8], unsigned long r[4]);

/*
 * Back end of arm_smccc_smc(): issued through core's system-control
 * interface (main.cc)
 */
void __arm_smccc_smc(unsigned long a0, unsigned long a1, unsigned long a2,
                     unsigned long a3, unsigned long a4, unsigned long a5,
                     unsigned long a6, unsigned long a7,
                     struct arm_smccc_res *res, struct arm_smccc_quirk *quirk)
{
	unsigned long const a[8] = { a0, a1, a2, a3, a4, a5, a6, a7 };
	unsigned long r[4] = { };

	pixel_modem_smc(a, r);

	if (res) {
		res->a0 = r[0];
		res->a1 = r[1];
		res->a2 = r[2];
		res->a3 = r[3];
	}
}


/*
 * soc/google/exynos-el3_mon.h: registers of the power-management unit are
 * written by the secure monitor
 */

int rmw_priv_reg(phys_addr_t reg, u32 mask, u32 val)
{
	struct arm_smccc_res res;

	arm_smccc_smc(SMC_CMD_PRIV_REG, reg, PRIV_REG_OPTION_RMW, mask, val,
	              0, 0, 0, &res);
	return (int)res.a0;
}


/*******************************
 ** Memory shared with the CP **
 *******************************/

/* cpif_shim.h: phys_to_virt of the cpif */
void *lx_emul_modem_shared_mem(phys_addr_t phys, size_t size)
{
	return lx_emul_io_mem_map(phys, size, true);
}


/*
 * vmap of shm_ipc.c (shm_ipc_shim.h), whose page pointers are physical
 * addresses. The MSI page holds the CP's doorbell and boot registers and is
 * device memory, as for Linux, the rest is write-combined.
 */
void *lx_emul_modem_shm_vmap(struct page **pages, unsigned int count)
{
	unsigned long const base = (unsigned long)pages[0];

	return lx_emul_io_mem_map(base, count*PAGE_SIZE, base != shm_get_msi_base());
}


/*
 * Replaces the one of shm_ipc.c, which hands the packet buffers of the
 * reserved memory to the page allocator. They stay reserved.
 */
void cp_shmem_release_rmem(u32 cp, u32 idx, u32 headroom) { }


/*
 * DDE Linux does not set up the reserved memory in the early boot, the
 * node's region is read from the device tree. shm_ipc.c copies the result
 * before the next lookup.
 */
struct reserved_mem *of_reserved_mem_lookup(struct device_node *np)
{
	static struct reserved_mem rmem;

	struct resource res;

	if (!np || of_address_to_resource(np, 0, &res))
		return NULL;

	rmem.name    = np->full_name;
	rmem.phandle = np->phandle;
	rmem.base    = res.start;
	rmem.size    = resource_size(&res);

	return &rmem;
}


/********************
 ** Wakeup sources **
 ********************/

/*
 * cpif_shim.h: the cpif refuses to probe without its wakeup sources, which
 * the common dummies do not register. There is no suspend, they are kept
 * for their names only.
 */
struct wakeup_source *lx_emul_modem_wakeup_source_register(struct device *dev,
                                                           const char *name)
{
	struct wakeup_source *ws = kzalloc(sizeof(*ws), GFP_KERNEL);

	if (ws)
		ws->name = kstrdup_const(name, GFP_KERNEL);

	return ws;
}


void wakeup_source_unregister(struct wakeup_source *ws)
{
	if (!ws)
		return;

	kfree_const(ws->name);
	kfree(ws);
}


/**********
 ** PCIe **
 **********/

/*
 * The ID table of s51xx_pcie.c matches any Samsung device, the root port
 * of the controller as well, whose probe would fail on its missing parent
 * bridge: the driver is registered for the modem only
 */
int lx_emul_s51xx_pci_register_driver(struct pci_driver *drv,
                                      struct module *owner,
                                      const char *mod_name)
{
	static struct pci_device_id const modem_ids[] = {
		{ PCI_DEVICE(PCI_VENDOR_ID_SAMSUNG, 0xa5a5) },
		{ }
	};

	drv->id_table = modem_ids;

	return __pci_register_driver(drv, owner, mod_name);
}


/* private header pcie-exynos-rc.h */
enum { SMC_SECURE_ATU_SETUP = 0x820020d8 };

int  vendor_exynos_pcie_poweron(int ch_num, int spd);
void exynos_pcie_rc_set_num_lanes(int ch_num, int lanes);

/*
 * The cpif powers the link on with the speed and the width it needs (Gen1
 * x1 for the boot loader of the CP, Gen3 x2 afterwards), the driver takes
 * the speed only.
 *
 * The address translation of the controller, including the inbound windows
 * through which the CP reads the AP's memory, is programmed by the secure
 * monitor (SMC_SECURE_ATU_SETUP). On Linux every configuration access to
 * the endpoint renews it, here it is renewed after each power-on: without,
 * the CP's reads after the link's second power-on are lost while its writes
 * pass.
 */
int exynos_pcie_poweron(int ch_num, int spd, int width)
{
	int ret;

	exynos_pcie_rc_set_num_lanes(ch_num, width < 1 ? 1 : width);

	ret = vendor_exynos_pcie_poweron(ch_num, spd);
	if (ret == 0)
		exynos_smc(SMC_SECURE_ATU_SETUP, 0, 0, 0);

	return ret;
}


/***********
 ** Pages **
 ***********/

/*
 * Receive buffers of the packet processing (link_device_pcie_iommu.c): the
 * IOMMU maps the page of a fragment once, keyed on the cache's current
 * page, so the fragments are cut from one page until it is used up. The
 * page holds one reference for the cache and one per fragment: it is freed
 * with the last of them ('page_frag_free' after the driver unmapped it,
 * or the cache moving on), as Linux does.
 */
void *page_frag_alloc_align(struct page_frag_cache *nc, unsigned int fragsz,
                            gfp_t gfp_mask, unsigned int align_mask)
{
	unsigned int offset;
	struct page *page;

	fragsz = ALIGN(fragsz, sizeof(long));

	if (!nc->va || (unsigned int)nc->offset < fragsz) {
		if (nc->va)
			__free_pages(virt_to_page(nc->va), 0);   /* the cache's reference */

		page = alloc_pages(gfp_mask, 0);              /* one reference: the cache's */
		if (!page)
			return NULL;

		nc->va     = page_address(page);
		nc->offset = PAGE_SIZE;
	}

	offset = ((unsigned int)nc->offset - fragsz) & align_mask;
	nc->offset = offset;
	page_ref_inc(virt_to_page(nc->va));               /* the fragment's reference */

	return (char *)nc->va + offset;
}


void page_frag_free(void *addr)
{
	__free_pages(virt_to_page(addr), 0);   /* frees the page with the last reference */
}


void __page_frag_cache_drain(struct page *page, unsigned int count)
{
	if (count > 1)
		page_ref_sub(page, count - 1);
	__free_pages(page, 0);
}


unsigned long get_zeroed_page(gfp_t gfp_mask)
{
	return __get_free_pages(gfp_mask | __GFP_ZERO, 0);
}


/**********
 ** Misc **
 **********/

/* target.mk: the "user" pointers of the device nodes are kernel pointers */
unsigned long lx_emul_modem_copy_to_user(void *to, const void *from,
                                         unsigned long n)
{
	memcpy(to, from, n);
	return 0;
}


unsigned long lx_emul_modem_copy_from_user(void *to, const void *from,
                                           unsigned long n)
{
	memcpy(to, from, n);
	return 0;
}


#include <linux/panic_notifier.h>

ATOMIC_NOTIFIER_HEAD(panic_notifier_list);

/* the vendor drivers' notifiers of a panic are not called */
void panic(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vprintk(fmt, args);
	va_end(args);

	lx_emul_trace_and_stop(__func__);
}


#include <linux/net.h>
#include <linux/slab.h>

/* net/socket.c: the kernel sockets of netlink */
int sock_create_lite(int family, int type, int protocol, struct socket **res)
{
	struct socket *sock = kzalloc(sizeof(*sock), GFP_KERNEL);

	if (!sock)
		return -ENOMEM;

	sock->type = type;
	*res = sock;
	return 0;
}


void sock_release(struct socket *sock)
{
	kfree(sock);
}


void *dmam_alloc_attrs(struct device *dev, size_t size, dma_addr_t *dma_handle,
                       gfp_t gfp, unsigned long attrs)
{
	return dma_alloc_attrs(dev, size, dma_handle, gfp, attrs);
}


void timer_usleep_range_state(unsigned long min, unsigned long max,
                              unsigned int state);

/*
 * The PCIe link training sleeps 10 us up to thousands of times per attempt,
 * each sleep would take a scheduling round. Short sleeps busy-wait, they
 * must still take their time: the training measures the link's speed
 * change by them. A busy wait stalls the whole driver (interrupts, the
 * sessions), so from a millisecond on the sleep is a real one.
 */
void usleep_range_state(unsigned long min, unsigned long max, unsigned int state)
{
	if (max < 1000) {
		udelay(min);
		return;
	}
	timer_usleep_range_state(min, max, state);
}
