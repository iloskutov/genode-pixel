/*
 * \brief  Hand-written dummies of the pixel_modem driver
 * \author Ivan Loskutov
 * \date   2026-11-07
 *
 * The common 6.1 kernel dummies are in src/lib/lx_emul/gs201/
 * common_dummies.c, code with behaviour is in lx_emul.c. Each stub here
 * names what it stands in for.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <lx_emul.h>


#include <linux/pinctrl/devinfo.h>

/*
 * drivers/base/pinctrl.c: pin functions are gs201_pio configuration, the
 * driver core must not look for default pin states
 */
int pinctrl_bind_pins(struct device *dev) { return 0; }


#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>

/*
 * Character devices of the cpif: lx_user.c serves them by the name of their
 * io_device, there are no device files. Registration succeeds so that the
 * probe goes on.
 */
int  cdev_add(struct cdev *p, dev_t dev, unsigned count) { return 0; }
void cdev_del(struct cdev *p) { }
void unregister_chrdev_region(dev_t from, unsigned count) { }
int  misc_register(struct miscdevice *misc) { return 0; }
void misc_deregister(struct miscdevice *misc) { }


#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

/*
 * The only regmap of the PCIe driver is the one of the power-management
 * unit, a fallback of its accesses through the secure monitor
 * (rmw_priv_reg, lx_emul.c). The accesses are dropped.
 */
static int dummy_syscon_regmap;

struct regmap *syscon_regmap_lookup_by_phandle(struct device_node *np,
                                               const char *property)
{
	return (struct regmap *)&dummy_syscon_regmap;
}


int regmap_update_bits_base(struct regmap *map, unsigned int reg,
                            unsigned int mask, unsigned int val,
                            bool *change, bool async, bool force)
{
	if (change)
		*change = false;
	return 0;
}


int regmap_read(struct regmap *map, unsigned int reg, unsigned int *val)
{
	*val = 0;
	return 0;
}


/* soc/google/exynos-pm.h: no CPU idle driver asks for the link state */
int register_pcie_is_connect(u32 (*func)(void)) { return 0; }


#include <linux/pm_wakeup.h>

/* there is no suspend, wakeup sources are never active */
void __pm_stay_awake(struct wakeup_source *ws) { }
void __pm_relax(struct wakeup_source *ws) { }
void pm_wakeup_dev_event(struct device *dev, unsigned int msec, bool hard) { }

void device_set_wakeup_capable(struct device *dev, bool capable)
{
	dev->power.can_wakeup = capable;
}


#include <linux/pm_runtime.h>

/* runtime PM of the PCI and network devices: never suspended */
void pm_runtime_forbid(struct device *dev) { }
void pm_runtime_set_memalloc_noio(struct device *dev, bool enable) { }


#include <linux/interrupt.h>

/* the MSI vectors of the modem have no CPU affinity, there is one CPU */
unsigned int irq_calc_affinity_vectors(unsigned int minvec, unsigned int maxvec,
                                       const struct irq_affinity *affd)
{
	return maxvec;
}


struct irq_affinity_desc *irq_create_affinity_masks(unsigned int nvec,
                                                    struct irq_affinity *affd)
{
	return NULL;
}


#include <linux/cred.h>
#include <linux/pid.h>

/* kernel threads only: one set of credentials, one PID namespace */
void  __put_cred(struct cred *cred) { }
pid_t pid_vnr(struct pid *pid) { return 0; }


/*
 * Boot sequence (lx_emul start.c): the interrupt controllers of the device
 * tree are set up by lx_emul's GIC, there is no scheduler clock to register
 */
#include <linux/irqchip.h>
#include <linux/of_irq.h>
#include <linux/sched_clock.h>

extern struct of_device_id __irqchip_of_table[];

void __init irqchip_init(void) { of_irq_init(__irqchip_of_table); }
void __init sched_clock_register(u64 (*read)(void), int bits, unsigned long rate) { }
void __init generic_sched_clock_init(void) { }


#include <linux/rcupdate.h>

/* one CPU, cooperative tasks: every grace period has already elapsed */
void rcu_barrier(void) { }
void synchronize_rcu_expedited(void) { }


#include <linux/reboot.h>
#include <linux/suspend.h>

/* the cpif's notifiers of suspend and reboot: there is neither */
int register_pm_notifier(struct notifier_block *nb)       { return 0; }
int unregister_pm_notifier(struct notifier_block *nb)     { return 0; }
int register_reboot_notifier(struct notifier_block *nb)   { return 0; }
int unregister_reboot_notifier(struct notifier_block *nb) { return 0; }


#include <linux/gpio/driver.h>

/*
 * drivers/gpio/gpiolib-cdev.c: no character device for the GPIO banks of
 * pio.c
 */
struct gpio_device;
int  gpiolib_cdev_register(struct gpio_device *gdev, dev_t devt);
void gpiolib_cdev_unregister(struct gpio_device *gdev);

int  gpiolib_cdev_register(struct gpio_device *gdev, dev_t devt) { return 0; }
void gpiolib_cdev_unregister(struct gpio_device *gdev) { }


#include <linux/io.h>
#include <linux/logic_pio.h>
#include <linux/pci.h>

/*
 * PCI core: no quirks (quirks.c), no vital product data (vpd.c), no
 * virtual channels (vc.c), no link power management (aspm.c), no I/O
 * ports (logic_pio.c), releases of devres mappings do nothing, the I/O
 * memory stays mapped
 */
void pci_fixup_device(enum pci_fixup_pass pass, struct pci_dev *dev) { }
void pci_vpd_init(struct pci_dev *dev) { }

void pci_allocate_vc_save_buffers(struct pci_dev *dev);
int  pci_save_vc_state(struct pci_dev *dev);
void pci_restore_vc_state(struct pci_dev *dev);
void pcie_aspm_init_link_state(struct pci_dev *pdev);
void pcie_aspm_exit_link_state(struct pci_dev *pdev);
void pcie_aspm_pm_state_change(struct pci_dev *pdev);
void pcie_aspm_powersave_config_link(struct pci_dev *pdev);
int  pci_dev_specific_reset(struct pci_dev *dev, bool probe);
int  pci_dev_specific_enable_acs(struct pci_dev *dev);
int  pci_dev_specific_disable_acs_redir(struct pci_dev *dev);
bool pci_idt_bus_quirk(struct pci_bus *bus, int devfn, u32 *pl, int timeout);

void pci_allocate_vc_save_buffers(struct pci_dev *dev) { }
int  pci_save_vc_state(struct pci_dev *dev) { return 0; }
void pci_restore_vc_state(struct pci_dev *dev) { }
void pcie_aspm_init_link_state(struct pci_dev *pdev) { }
void pcie_aspm_exit_link_state(struct pci_dev *pdev) { }
void pcie_aspm_pm_state_change(struct pci_dev *pdev) { }
void pcie_aspm_powersave_config_link(struct pci_dev *pdev) { }
int  pci_dev_specific_reset(struct pci_dev *dev, bool probe) { return -ENOTTY; }
int  pci_dev_specific_enable_acs(struct pci_dev *dev) { return -ENOTTY; }
int  pci_dev_specific_disable_acs_redir(struct pci_dev *dev) { return -ENOTTY; }
bool pci_idt_bus_quirk(struct pci_bus *bus, int devfn, u32 *pl, int timeout) { return false; }

int  logic_pio_register_range(struct logic_pio_hwaddr *newrange) { return 0; }
void devm_ioremap_release(struct device *dev, void *res) { }


#include <linux/net.h>
#include <linux/random.h>
#include <linux/skbuff.h>
#include <linux/stringhash.h>
#include <net/dst.h>
#include <net/xdp.h>

/*
 * Network core, for the cpif's network devices: no random pool to feed,
 * no routing, no XDP, a hash of names that keeps all names in one bucket
 */
void add_device_randomness(const void *buf, size_t len) { }
unsigned int full_name_hash(const void *salt, const char *name, unsigned int len) { return 0; }
int  net_ratelimit(void) { return 1; }
void dst_release(struct dst_entry *dst) { }
void __skb_get_hash(struct sk_buff *skb) { }

int __xdp_rxq_info_reg(struct xdp_rxq_info *xdp_rxq, struct net_device *dev,
                       u32 queue_index, unsigned int napi_id, u32 frag_size)
{
	return 0;
}

void xdp_rxq_info_unreg(struct xdp_rxq_info *xdp_rxq) { }


/*
 * net/socket.c: the kernel sockets of netlink, no socket family of user
 * space
 */
int sock_register(const struct net_proto_family *fam) { return 0; }


#include <linux/vmalloc.h>

/* the cpif's mappings of the shared memory are I/O memory, they stay */
void vunmap(const void *addr) { }
