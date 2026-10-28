/*
 * \brief  Hand-written dummies of the WiFi driver library
 * \author Ivan Loskutov
 * \date   2026-10-28
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <lx_emul.h>


#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

/*
 * The only regmap of the PCIe driver is the one of the power-management
 * unit. Its registers are reached by the secure monitor only, the driver's
 * accesses are dropped: the isolation of the PHY is released by bootstrap.
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


#include <soc/google/exynos-el3_mon.h>

int rmw_priv_reg(phys_addr_t reg, u32 mask, u32 val) { return 0; }


/*
 * Boot sequence (lx_emul start.c)
 */


#include <linux/timekeeper_internal.h>


void update_vsyscall(struct timekeeper * tk)
{
	lx_emul_trace(__func__);
}


/*
 * The interrupt controllers of the device tree, set up from lx_emul's
 * start.c, so that platform_get_irq and of_irq_get work
 */
#include <linux/irqchip.h>
#include <linux/of_irq.h>


extern struct of_device_id __irqchip_of_table[];


void __init irqchip_init(void)
{
	of_irq_init(__irqchip_of_table);
}


#include <linux/sched_clock.h>


void __init sched_clock_register(u64 (* read)(void),int bits,unsigned long rate)
{
	lx_emul_trace(__func__);
}


void __init generic_sched_clock_init(void)
{
	lx_emul_trace(__func__);
}


/*
 * ARM64 architecture stubs
 */


#include <linux/uaccess.h>


unsigned long __must_check __arch_copy_from_user(void *to, const void __user *from, unsigned long n);
unsigned long __must_check __arch_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	memcpy(to, from, n);
	return 0;
}


unsigned long __must_check __arch_copy_to_user(void __user *to, const void *from, unsigned long n);
unsigned long __must_check __arch_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	memcpy(to, from, n);
	return 0;
}


#include <asm/cpufeature.h>


extern u64 vabits_actual;
u64 vabits_actual;


/*
 * Scheduler / timer stubs
 */


#include <linux/kernel_stat.h>


void account_process_tick(struct task_struct * p,int user_tick)
{
	lx_emul_trace(__func__);
}


#include <linux/rcupdate.h>


void rcu_sched_clock_irq(int user)
{
	lx_emul_trace(__func__);
}


void rcu_barrier(void)
{
	lx_emul_trace(__func__);
}


void synchronize_rcu(void)
{
	lx_emul_trace(__func__);
}


#include <linux/interrupt.h>


void do_softirq_own_stack(void)
{
	__do_softirq();
}


#include <linux/sched/signal.h>


void ignore_signals(struct task_struct * t)
{
	lx_emul_trace(__func__);
}


#include <linux/random.h>


void add_interrupt_randomness(int irq)
{
	lx_emul_trace(__func__);
}


/*
 * Context tracking
 */


#include <linux/context_tracking_irq.h>


noinstr void ct_irq_enter(void)
{
	lx_emul_trace(__func__);
}


noinstr void ct_irq_exit(void)
{
	lx_emul_trace(__func__);
}


void ct_irq_enter_irqson(void)
{
	lx_emul_trace(__func__);
}


void ct_irq_exit_irqson(void)
{
	lx_emul_trace(__func__);
}


/*
 * CPU hotplug
 */


#include <linux/cpuhotplug.h>


int __cpuhp_setup_state(enum cpuhp_state state,const char * name,bool invoke,int (* startup)(unsigned int cpu),int (* teardown)(unsigned int cpu),bool multi_instance)
{
	lx_emul_trace(__func__);
	return 0;
}


/*
 * IRQ subsystem stubs
 */


#include <linux/irq.h>


struct irq_chip no_irq_chip = {};


void note_interrupt(struct irq_desc * desc,irqreturn_t action_ret)
{
	lx_emul_trace(__func__);
}


extern bool irq_wait_for_poll(struct irq_desc *desc);


#include <linux/irq_work.h>


void irq_work_tick(void)
{
	lx_emul_trace(__func__);
}


/*
 * Printk / debug stubs
 */


#include <linux/printk.h>


extern void __printk_safe_enter(void);
void __printk_safe_enter(void)
{
	lx_emul_trace(__func__);
}


extern void __printk_safe_exit(void);
void __printk_safe_exit(void)
{
	lx_emul_trace(__func__);
}


/*
 * Linker section symbols
 */


#include <asm-generic/sections.h>


char __end_rodata[] = {};
char __start_rodata[] = {};


/*
 * CPU mask / per-cpu
 */


#include <linux/cpumask.h>


struct cpumask __cpu_active_mask;


#include <asm-generic/percpu.h>


unsigned long __per_cpu_offset[NR_CPUS] = {};


/*
 * Static keys / BPF
 */


#include <linux/jump_label.h>


bool static_key_initialized;


/*
 * Boot / init variables
 */


#include <linux/init.h>


char boot_command_line[2048] = {};
bool initcall_debug;


#include <linux/initrd.h>


phys_addr_t phys_initrd_start;
unsigned long phys_initrd_size;


/*
 * Memory / DMA stubs
 */


#include <linux/dma-map-ops.h>


bool dma_default_coherent;


#include <linux/swiotlb.h>


struct io_tlb_mem io_tlb_default_mem;


#include <linux/mm.h>


atomic_long_t _totalram_pages;


#include <linux/vmalloc.h>


void reserve_bootmem_region(phys_addr_t start,phys_addr_t end)
{
	lx_emul_trace(__func__);
}


/*
 * User namespace
 */


#include <linux/user_namespace.h>


struct user_namespace init_user_ns;


/*
 * Scheduler misc
 */


long __sched io_schedule_timeout(long timeout)
{
	lx_emul_trace(__func__);
	return 0;
}


/*
 * Delay
 */


#include <linux/delay.h>


unsigned long lpj_fine;


/*
 * Kobject / uevent stubs
 */


#include <linux/kobject.h>


struct kobject * firmware_kobj;


int add_uevent_var(struct kobj_uevent_env * env,const char * format,...)
{
	lx_emul_trace(__func__);
	return 0;
}


int kobject_uevent(struct kobject * kobj,enum kobject_action action)
{
	lx_emul_trace(__func__);
	return 0;
}


int kobject_uevent_env(struct kobject * kobj,enum kobject_action action,
                       char ** envp_ext)
{
	lx_emul_trace(__func__);
	return 0;
}


/*
 * Device model stubs
 */


extern void software_node_notify(struct device * dev);
void software_node_notify(struct device * dev)
{
	lx_emul_trace(__func__);
}


extern void software_node_notify_remove(struct device * dev);
void software_node_notify_remove(struct device * dev)
{
	lx_emul_trace(__func__);
}


#include <linux/property.h>


bool is_software_node(const struct fwnode_handle * fwnode)
{
	lx_emul_trace(__func__);
	return false;
}


/*
 * DMA / arch setup
 */


void arch_setup_dma_ops(struct device * dev,u64 dma_base,u64 size,
                         const struct iommu_ops * iommu,bool coherent)
{
	lx_emul_trace(__func__);
}


/*
 * Device-tree parsing: no reserved memory, no boot-loader randomness
 */


#include <linux/of_fdt.h>


void __init add_bootloader_randomness(const void * buf,size_t size)
{
	lx_emul_trace(__func__);
}


void fdt_reserved_mem_save_node(unsigned long node,const char * uname,
                                 phys_addr_t base,phys_addr_t size)
{
	lx_emul_trace(__func__);
}


void fdt_init_reserved_mem(void)
{
	lx_emul_trace(__func__);
}


/*
 * OF clock
 */


#include <linux/clk.h>


struct clk * of_clk_get(struct device_node * np,int index)
{
	lx_emul_trace(__func__);
	return ERR_PTR(-ENOENT);
}


#include <linux/clk/clk-conf.h>


int of_clk_set_defaults(struct device_node * node,bool clk_supplier)
{
	lx_emul_trace(__func__);
	return 0;
}


/*
 * I/O memory mapping (needed by PCIe)
 */


#include <lx_emul/io_mem.h>
#include <linux/io.h>


void __iomem * ioremap_prot(phys_addr_t phys_addr,size_t size,
                             unsigned long prot)
{
	return lx_emul_io_mem_map(phys_addr, size, false);
}


void devm_ioremap_release(struct device * dev,void * res)
{
	lx_emul_trace(__func__);
}


/*
 * PCI I/O range stubs (needed by OF address.c)
 */


#include <linux/logic_pio.h>


struct logic_pio_hwaddr * find_io_range_by_fwnode(struct fwnode_handle * fwnode)
{
	lx_emul_trace(__func__);
	return NULL;
}


unsigned long logic_pio_trans_hwaddr(struct fwnode_handle * fwnode,
                                     resource_size_t addr,resource_size_t size)
{
	lx_emul_trace(__func__);
	return (unsigned long)-1;
}


/*
 * AMBA bus stubs (needed by drivers/of/platform.c)
 */


#include <linux/amba/bus.h>


struct amba_device * amba_device_alloc(const char * name,resource_size_t base,
                                       size_t size)
{
	lx_emul_trace(__func__);
	return NULL;
}


int amba_device_add(struct amba_device * dev,struct resource * parent)
{
	lx_emul_trace(__func__);
	return -ENODEV;
}


void amba_device_put(struct amba_device * dev)
{
	lx_emul_trace(__func__);
}


/*
 * Syscore ops
 */


#include <linux/syscore_ops.h>


void register_syscore_ops(struct syscore_ops * ops)
{
	lx_emul_trace(__func__);
}


/*
 * RCU wait
 */


#include <linux/rcuwait.h>


void finish_rcuwait(struct rcuwait * w)
{
	lx_emul_trace(__func__);
}


/*
 * Rate limiting
 */


#include <linux/ratelimit_types.h>


int ___ratelimit(struct ratelimit_state * rs,const char * func)
{
	lx_emul_trace(__func__);
	return 0;
}


/*
 * Cmdline parsing
 */


#include <linux/kernel.h>


bool parse_option_str(const char * str,const char * option)
{
	lx_emul_trace(__func__);
	return false;
}


/*
 * SMP stubs
 */


#include <linux/smp.h>


void smp_call_function_many(const struct cpumask * mask,smp_call_func_t func,
                            void * info,bool wait)
{
	lx_emul_trace(__func__);
}


/*
 * Wake queue
 */


#include <linux/sched/wake_q.h>


void wake_q_add_safe(struct wake_q_head * head,struct task_struct * task)
{
	lx_emul_trace(__func__);
}


#include <net/flow_dissector.h>


struct flow_dissector flow_keys_basic_dissector;


#include <net/neighbour.h>


const struct nla_policy nda_policy[] = {};


/*
 * Timer stubs
 */


#include <linux/clockchips.h>


void tick_broadcast(const struct cpumask * mask)
{
	lx_emul_trace(__func__);
}


/*
 * UUID
 */


#include <linux/uuid.h>


const u8 guid_index[16] = {};
const u8 uuid_index[16] = {};


/*
 * PCI-specific stubs
 */


#include <linux/pci.h>


extern const struct attribute_group aspm_ctrl_attr_group;
const struct attribute_group aspm_ctrl_attr_group;


extern const struct attribute_group pci_dev_vpd_attr_group;
const struct attribute_group pci_dev_vpd_attr_group;


int pci_dev_specific_reset(struct pci_dev * dev,bool probe)
{
	lx_emul_trace(__func__);
	return -ENOTTY;
}


/*
 * WiFi regulatory database stubs
 */


const u8 shipped_regdb_certs[] = {};


unsigned int shipped_regdb_certs_len;


/*
 * Trace flag names
 */


#include <linux/trace_events.h>


const struct trace_print_flags gfpflag_names[] = { {0, NULL} };
const struct trace_print_flags pageflag_names[] = { {0, NULL} };
const struct trace_print_flags vmaflag_names[] = { {0, NULL} };


/*
 * SRCU stubs
 */


#include <linux/srcu.h>


void synchronize_srcu(struct srcu_struct * ssp)
{
	lx_emul_trace(__func__);
}


/*
 * Memory zone
 */


#include <linux/mmzone.h>


extern struct pglist_data contig_page_data;


struct pglist_data * first_online_pgdat(void)
{
	return &contig_page_data;
}


struct pglist_data * next_online_pgdat(struct pglist_data * pgdat)
{
	return NULL;
}


/*
 * Runtime power management, power domains, system sleep
 */


void pm_runtime_enable(struct device * dev) { }


int __pm_runtime_set_status(struct device * dev,unsigned int status)
{
	return 0;
}


void pm_runtime_set_memalloc_noio(struct device * dev,bool enable) { }


void pm_runtime_forbid(struct device * dev) { }


void pm_runtime_release_supplier(struct device_link * link) { }


#include <linux/pm_domain.h>


int dev_pm_domain_attach(struct device * dev,bool power_on)
{
	return 0;
}


void dev_pm_domain_detach(struct device * dev,bool power_off) { }


int dpm_sysfs_add(struct device * dev) { return 0; }


void device_pm_sleep_init(struct device * dev) { }


void device_pm_add(struct device * dev) { }


void device_pm_remove(struct device * dev) { }


void device_pm_check_callbacks(struct device * dev) { }


#include <linux/irqdesc.h>
#include <linux/interrupt.h>


void irq_pm_install_action(struct irq_desc * desc, struct irqaction * action) { }


void irq_pm_remove_action(struct irq_desc * desc, struct irqaction * action) { }


void device_pm_lock(void) { }


void device_pm_unlock(void) { }


#include <linux/pm_wakeup.h>


void dpm_sysfs_remove(struct device * dev) { }


/*
 * Pinctrl stubs
 */


#include <linux/pinctrl/devinfo.h>
#include <linux/pinctrl/consumer.h>


int pinctrl_bind_pins(struct device * dev)
{
	lx_emul_trace(__func__);
	return 0;
}


/*
 * PCI: no virtual channels, no ACS, no quirks, no ASPM, no VPD
 */


void pci_allocate_vc_save_buffers(struct pci_dev * dev)
{
	lx_emul_trace(__func__);
}


void pci_dev_specific_disable_acs_redir(struct pci_dev * dev)
{
	lx_emul_trace(__func__);
}


int pci_dev_specific_enable_acs(struct pci_dev * dev)
{
	lx_emul_trace(__func__);
	return -ENOTTY;
}


void pcie_aspm_exit_link_state(struct pci_dev * pdev)
{
	lx_emul_trace(__func__);
}


void pcie_aspm_init_link_state(struct pci_dev * pdev)
{
	lx_emul_trace(__func__);
}


void pcie_aspm_pm_state_change(struct pci_dev * pdev,bool locked)
{
	lx_emul_trace(__func__);
}


void pcie_aspm_powersave_config_link(struct pci_dev * pdev)
{
	lx_emul_trace(__func__);
}


void pci_fixup_device(enum pci_fixup_pass pass,struct pci_dev * dev)
{
	lx_emul_trace(__func__);
}


void pci_idt_bus_quirk(struct pci_dev * pdev)
{
	lx_emul_trace(__func__);
}


int pci_restore_vc_state(struct pci_dev * dev)
{
	lx_emul_trace(__func__);
	return 0;
}


int pci_save_vc_state(struct pci_dev * dev)
{
	lx_emul_trace(__func__);
	return 0;
}


void pci_vpd_init(struct pci_dev * dev)
{
	lx_emul_trace(__func__);
}


/*
 * Wakeup sources: CONFIG_PM_SLEEP is defined for the layout of 'struct
 * device', the system never suspends
 */
#include <linux/pm_wakeup.h>

static struct wakeup_source _dummy_wakeup_source;


void device_set_wakeup_capable(struct device * dev, bool capable)
{
	dev->power.can_wakeup = capable;
}


struct wakeup_source * wakeup_source_register(struct device * dev,
					      const char * name)
{
	lx_emul_trace(__func__);
	return &_dummy_wakeup_source;
}


void wakeup_source_unregister(struct wakeup_source * ws)
{
	lx_emul_trace(__func__);
}


void __pm_stay_awake(struct wakeup_source * ws)
{
	lx_emul_trace(__func__);
}


void __pm_relax(struct wakeup_source * ws)
{
	lx_emul_trace(__func__);
}


void pm_wakeup_ws_event(struct wakeup_source * ws, unsigned int msec, bool hard)
{
	lx_emul_trace(__func__);
}


#include <linux/suspend.h>


int register_pm_notifier(struct notifier_block * nb)
{
	lx_emul_trace(__func__);
	return 0;
}


int unregister_pm_notifier(struct notifier_block * nb)
{
	lx_emul_trace(__func__);
	return 0;
}


struct irq_affinity_desc * irq_create_affinity_masks(unsigned int nvecs,
                                                      struct irq_affinity * affd)
{
	lx_emul_trace(__func__);
	return NULL;
}


unsigned int irq_calc_affinity_vectors(unsigned int minvec,unsigned int maxvec,
                                        const struct irq_affinity * affd)
{
	lx_emul_trace(__func__);
	return maxvec;
}


/*
 * Device stubs
 */


#include <linux/property.h>


int device_create_managed_software_node(struct device * dev,
                                         const struct property_entry * properties,
                                         const struct software_node * parent)
{
	lx_emul_trace(__func__);
	return 0;
}


#include <linux/filter.h>
#include <net/dst.h>


void dst_release(struct dst_entry * dst)
{
	lx_emul_trace(__func__);
}


int net_ratelimit(void)
{
	return 0;
}


int sk_filter_trim_cap(struct sock * sk,struct sk_buff * skb,unsigned int cap)
{
	lx_emul_trace(__func__);
	return 0;
}


void sk_filter_uncharge(struct sock * sk,struct sk_filter * fp)
{
	lx_emul_trace(__func__);
}


bool __skb_flow_dissect(const struct net * net,const struct sk_buff * skb,
                         struct flow_dissector * flow_dissector,
                         void * target_container,const void * data,
                         __be16 proto,int nhoff,int hlen,unsigned int flags)
{
	lx_emul_trace(__func__);
	return false;
}


void __skb_get_hash(struct sk_buff * skb)
{
	lx_emul_trace(__func__);
}


u32 __skb_get_hash_symmetric(const struct sk_buff * skb)
{
	lx_emul_trace(__func__);
	return 0;
}


#include <linux/ethtool.h>

int __ethtool_get_link_ksettings(struct net_device * dev,
                                  struct ethtool_link_ksettings * link_ksettings)
{
	lx_emul_trace(__func__);
	return -EOPNOTSUPP;
}


void sock_diag_broadcast_destroy(struct sock * sk)
{
	lx_emul_trace(__func__);
}


void __scm_destroy(struct scm_cookie * scm)
{
	lx_emul_trace(__func__);
}


int sk_detach_filter(struct sock * sk)
{
	lx_emul_trace(__func__);
	return 0;
}


void reuseport_detach_prog(struct sock * sk)
{
	lx_emul_trace(__func__);
}


void reuseport_detach_sock(struct sock * sk)
{
	lx_emul_trace(__func__);
}


void reuseport_update_incoming_cpu(struct sock * sk,int val)
{
	lx_emul_trace(__func__);
}


void bpf_prog_destroy(struct bpf_prog * fp)
{
	lx_emul_trace(__func__);
}


void bpf_warn_invalid_xdp_action(struct net_device * dev,struct bpf_prog * prog,
                                   u32 act)
{
	lx_emul_trace(__func__);
}


void bpf_prog_change_xdp(struct bpf_prog * prev_prog,struct bpf_prog * prog)
{
	lx_emul_trace(__func__);
}


int copy_bpf_fprog_from_user(struct sock_fprog * dst,
                               sockptr_t src,int len)
{
	lx_emul_trace(__func__);
	return -EFAULT;
}


/*
 * Gen stats stubs
 */


#include <net/gen_stats.h>


void gen_kill_estimator(struct net_rate_estimator __rcu ** rate_est)
{
	lx_emul_trace(__func__);
}


void gnet_stats_add_basic(struct gnet_stats_basic_sync * bstats,
                           struct gnet_stats_basic_sync __percpu * cpu,
                           struct gnet_stats_basic_sync * b,bool running)
{
	lx_emul_trace(__func__);
}


void gnet_stats_add_queue(struct gnet_stats_queue * qstats,
                           const struct gnet_stats_queue __percpu * cpu_q,
                           const struct gnet_stats_queue * q)
{
	lx_emul_trace(__func__);
}


void gnet_stats_basic_sync_init(struct gnet_stats_basic_sync * b)
{
	lx_emul_trace(__func__);
}


int gnet_stats_copy_basic(struct gnet_dump * d,
                           struct gnet_stats_basic_sync __percpu * cpu,
                           struct gnet_stats_basic_sync * b,bool running)
{
	lx_emul_trace(__func__);
	return 0;
}


int gnet_stats_copy_queue(struct gnet_dump * d,
                           struct gnet_stats_queue __percpu * cpu_q,
                           struct gnet_stats_queue * q,__u32 qlen)
{
	lx_emul_trace(__func__);
	return 0;
}


bool file_ns_capable(const struct file * file,struct user_namespace * ns,int cap)
{
	lx_emul_trace(__func__);
	return true;
}


void inode_init_once(struct inode * inode)
{
	lx_emul_trace(__func__);
}


void kill_fasync(struct fasync_struct ** fp,int sig,int band)
{
	lx_emul_trace(__func__);
}


int kobject_synth_uevent(struct kobject * kobj,const char * buf,size_t count)
{
	lx_emul_trace(__func__);
	return 0;
}


int stream_open(struct inode * inode,struct file * filp)
{
	lx_emul_trace(__func__);
	return 0;
}


int alloc_chrdev_region(dev_t * dev,unsigned baseminor,unsigned count,
                         const char * name)
{
	lx_emul_trace(__func__);
	if (dev) *dev = 0;
	return 0;
}


struct gpio_device;  /* drivers/gpio/gpiolib.h */

int gpiolib_cdev_register(struct gpio_device * gdev,dev_t devt)
{
	lx_emul_trace(__func__);
	return 0;
}


void gpiolib_cdev_unregister(struct gpio_device * gdev)
{
	lx_emul_trace(__func__);
}


int __register_chrdev(unsigned int major,unsigned int baseminor,
                       unsigned int count,const char * name,
                       const struct file_operations * fops)
{
	lx_emul_trace(__func__);
	return 0;
}


void __unregister_chrdev(unsigned int major,unsigned int baseminor,
                          unsigned int count,const char * name)
{
	lx_emul_trace(__func__);
}


int unregister_filesystem(struct file_system_type * fs)
{
	lx_emul_trace(__func__);
	return 0;
}


/*
 * Memory stubs
 */


void flush_dcache_page(struct page * page)
{
	lx_emul_trace(__func__);
}


unsigned long get_zeroed_page(gfp_t gfp_mask)
{
	return (unsigned long)kzalloc(PAGE_SIZE, gfp_mask);
}


void vfree_atomic(const void * addr)
{
	lx_emul_trace(__func__);
}


/*
 * I/O stubs
 */


void __memcpy_fromio(void * to,const volatile void __iomem * from,size_t count)
{
	memcpy(to, (const void *)from, count);
}


/*
 * I/O scheduling
 */


int io_schedule_prepare(void)
{
	lx_emul_trace(__func__);
	return 0;
}


void io_schedule_finish(int token)
{
	lx_emul_trace(__func__);
}


void migrate_disable(void) { }


void migrate_enable(void) { }


/*
 * iov_iter
 */


#include <linux/uio.h>


void iov_iter_kvec(struct iov_iter * i,unsigned int direction,
                    const struct kvec * kvec,unsigned long nr_segs,size_t count)
{
	lx_emul_trace(__func__);
}


void iov_iter_revert(struct iov_iter * i,size_t unroll)
{
	lx_emul_trace(__func__);
}


/*
 * Misc stubs
 */


void add_device_randomness(const void * buf,size_t len)
{
	lx_emul_trace(__func__);
}


void __cpuhp_remove_state(enum cpuhp_state state,bool invoke)
{
	lx_emul_trace(__func__);
}


u32 full_name_hash(const void * salt,const char * name,unsigned int len)
{
	lx_emul_trace(__func__);
	return 0;
}


unsigned int get_next_ino(void)
{
	static unsigned int ino;
	lx_emul_trace(__func__);
	return ++ino;
}


int get_option(char ** str,int * pint)
{
	lx_emul_trace(__func__);
	return 0;
}


u8 get_random_u8(void)
{
	u8 val;
	get_random_bytes(&val, sizeof(val));
	return val;
}


bool gfp_pfmemalloc_allowed(gfp_t gfp_mask)
{
	lx_emul_trace(__func__);
	return false;
}


#include <linux/key.h>


key_ref_t key_create_or_update(key_ref_t keyring_ref,const char * type,
                                const char * description,
                                const void * payload,size_t plen,
                                key_perm_t perm,unsigned long flags)
{
	lx_emul_trace(__func__);
	return (key_ref_t)ERR_PTR(-ENOKEY);
}


void key_put(struct key * key)
{
	lx_emul_trace(__func__);
}


struct key * keyring_alloc(const char * description,kuid_t uid,kgid_t gid,
                            const struct cred * cred,key_perm_t perm,
                            unsigned long flags,
                            struct key_restriction * restrict_link,
                            struct key * dest)
{
	lx_emul_trace(__func__);
	return ERR_PTR(-ENOKEY);
}


void kick_process(struct task_struct * p)
{
	lx_emul_trace(__func__);
}


unsigned long long memparse(const char * ptr,char ** retptr)
{
	lx_emul_trace(__func__);
	return 0;
}


pid_t pid_vnr(struct pid * pid)
{
	lx_emul_trace(__func__);
	return 0;
}


void put_pid(struct pid * pid)
{
	lx_emul_trace(__func__);
}


void __put_cred(struct cred * cred)
{
	lx_emul_trace(__func__);
}


int _printk_deferred(const char * fmt,...)
{
	return 0;
}


int register_inetaddr_notifier(struct notifier_block * nb)
{
	lx_emul_trace(__func__);
	return 0;
}


int register_reboot_notifier(struct notifier_block * nb)
{
	lx_emul_trace(__func__);
	return 0;
}


#include <linux/rtc.h>


void rtc_time64_to_tm(time64_t time,struct rtc_time * tm)
{
	lx_emul_trace(__func__);
	memset(tm, 0, sizeof(*tm));
}


void sched_set_normal(struct task_struct * p,int nice)
{
	lx_emul_trace(__func__);
}


void synchronize_rcu_expedited(void)
{
	lx_emul_trace(__func__);
}


int unregister_inetaddr_notifier(struct notifier_block * nb)
{
	lx_emul_trace(__func__);
	return 0;
}


#include <linux/verification.h>


int verify_pkcs7_signature(const void * data,size_t len,
                            const void * raw_pkcs7,size_t pkcs7_len,
                            struct key * trusted_keys,
                            enum key_being_used_for usage,
                            int (* view_content)(void * ctx,const void * data,
                                                  size_t len,size_t asn1hdrlen),
                            void * ctx)
{
	lx_emul_trace(__func__);
	return 0;
}


void yield(void)
{
	lx_emul_trace(__func__);
}


/*
 * Pin control: the GPIO banks are gs201_pio sessions (lx_emul/gs201/pio.c),
 * the pinctrl core is not built. These are references of drivers/base/dd.c
 * and gpiolib, the GS201 banks have no gpio-ranges.
 */

#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/devinfo.h>

int pinctrl_init_done(struct device *dev) { return 0; }


struct pinctrl_dev *of_pinctrl_get(struct device_node *np) { return NULL; }


void pinctrl_add_gpio_range(struct pinctrl_dev *pctldev,
                            struct pinctrl_gpio_range *range)
{
	lx_emul_trace_and_stop(__func__);
}


void pinctrl_remove_gpio_range(struct pinctrl_dev *pctldev,
                               struct pinctrl_gpio_range *range)
{
	lx_emul_trace_and_stop(__func__);
}


struct pinctrl_dev *pinctrl_find_and_add_gpio_range(const char *devname,
                                                    struct pinctrl_gpio_range *range)
{
	lx_emul_trace_and_stop(__func__);
	return NULL;
}


const char *pinctrl_dev_get_devname(struct pinctrl_dev *pctldev)
{
	lx_emul_trace_and_stop(__func__);
	return NULL;
}


int pinctrl_get_group_pins(struct pinctrl_dev *pctldev, const char *pin_group,
                           const unsigned **pins, unsigned *num_pins)
{
	lx_emul_trace_and_stop(__func__);
	return -EINVAL;
}


/*
 * Device links, system sleep, and wakeup events of the PM core
 */

extern void device_pm_move_last(struct device *dev);
void device_pm_move_last(struct device *dev) { }


#include <linux/pm_wakeup.h>

void pm_wakeup_dev_event(struct device *dev, unsigned int msec, bool hard) { }


/* checked for each interrupt, there are no wakeup interrupts */
#include <linux/irqdesc.h>

extern bool irq_pm_check_wakeup(struct irq_desc *desc);
bool irq_pm_check_wakeup(struct irq_desc *desc) { return false; }


/*
 * Static keys of the BPF core (net/core/filter.c, kernel/bpf/core.c), which
 * is not built
 */

DEFINE_STATIC_KEY_FALSE(bpf_master_redirect_enabled_key);
DEFINE_STATIC_KEY_FALSE(bpf_stats_enabled_key);
