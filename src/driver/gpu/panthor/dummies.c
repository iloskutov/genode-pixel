/*
 * \brief  Dummy definitions of Linux Kernel functions - handled manually
 * \author Ivan Loskutov
 * \date   2026-10-07
 */

/*
 * Copyright (C) 2026 Ivan Loskutov
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#include <lx_emul.h>
#include <linux/slab.h>
#include <linux/string.h>


/*
 * Symbols normally provided by the linker script or by start-up code
 */

#include <linux/of_reserved_mem.h>
#include <linux/mod_devicetable.h>

const struct of_device_id __reservedmem_of_table[] = {};


#include <asm-generic/sections.h>

char __start_rodata[] = {};
char __end_rodata[]   = {};


/* drivers/of/empty_root.dts, only used if no device tree is given */
char __dtb_empty_root_begin[] = {};
char __dtb_empty_root_end[]   = {};


#include <linux/tracepoint-defs.h>

const struct trace_print_flags gfpflag_names[]  = { {0, NULL} };
const struct trace_print_flags pageflag_names[] = { {0, NULL} };
const struct trace_print_flags vmaflag_names[]  = { {0, NULL} };


#include <asm/memory.h>

u64 vabits_actual;


#include <linux/jump_label.h>

bool static_key_initialized = true;


#include <linux/percpu.h>

/* kernel/sched/sched.h */
bool sched_smp_initialized = true;

DEFINE_PER_CPU(unsigned long, cpu_scale);


/*
 * User-memory access: the arguments of the DRM ioctls are local memory
 * (include/lx_emul/shadow/asm/uaccess.h)
 */

unsigned long __must_check __arch_clear_user(void __user *to, unsigned long n)
{
	memset(to, 0, n);
	return 0;
}


/*
 * Kernel start-up
 */

#include <linux/async.h>

void async_init(void) { }


#include <linux/skbuff.h>

void skb_init(void) { }


#include <linux/cpuhotplug.h>

int __cpuhp_setup_state(enum cpuhp_state state, const char *name, bool invoke,
                        int (*startup)(unsigned int cpu),
                        int (*teardown)(unsigned int cpu), bool multi_instance)
{
	lx_emul_trace(__func__);
	return 0;
}


#include <linux/syscore_ops.h>

void register_syscore_ops(struct syscore_ops *ops)
{
	lx_emul_trace(__func__);
}


#include <linux/sysctl.h>

void __init __register_sysctl_init(const char *path,
                                   const struct ctl_table *table,
                                   const char *table_name, size_t table_size)
{
	lx_emul_trace(__func__);
}


struct ctl_table_header *register_sysctl_sz(const char *path,
                                            const struct ctl_table *table,
                                            size_t table_size)
{
	lx_emul_trace(__func__);
	return NULL;
}


#include <linux/random.h>

int __cold execute_with_initialized_rng(struct notifier_block *nb)
{
	lx_emul_trace(__func__);
	return 0;
}


void add_interrupt_randomness(int irq) { }


/*
 * Time, scheduler, RCU
 */

#include <linux/kernel_stat.h>

void account_process_tick(struct task_struct *p, int user_tick) { }


#include <linux/sched.h>

void sched_tick(void) { }


#include <linux/timekeeper_internal.h>

void update_vsyscall(struct timekeeper *tk) { }


#include <linux/sched/signal.h>

void ignore_signals(struct task_struct *t)
{
	lx_emul_trace(__func__);
}


#include <linux/context_tracking_irq.h>

noinstr void ct_irq_enter(void) { }


noinstr void ct_irq_exit(void) { }


#include <linux/rcupdate.h>

void rcu_sched_clock_irq(int user) { }


/*
 * The emulation runs the Linux code cooperatively on one CPU: there is no
 * concurrent reader that a grace period would have to wait for
 */
void synchronize_rcu(void)
{
	lx_emul_trace(__func__);
}


void rcu_barrier(void)
{
	lx_emul_trace(__func__);
}


#include <linux/srcu.h>

void synchronize_srcu(struct srcu_struct *ssp)
{
	lx_emul_trace(__func__);
}


/*
 * Interrupts
 */

struct irq_desc;
struct irqaction;

extern void register_irq_proc(unsigned int irq, struct irq_desc *desc);
void register_irq_proc(unsigned int irq, struct irq_desc *desc) { }


extern void unregister_irq_proc(unsigned int irq, struct irq_desc *desc);
void unregister_irq_proc(unsigned int irq, struct irq_desc *desc) { }


extern void register_handler_proc(unsigned int irq, struct irqaction *action);
void register_handler_proc(unsigned int irq, struct irqaction *action) { }


extern void unregister_handler_proc(unsigned int irq, struct irqaction *action);
void unregister_handler_proc(unsigned int irq, struct irqaction *action) { }


/*
 * Device model: no sysfs, no procfs, no uevents, no software nodes
 */

#include <linux/kobject.h>

int kobject_uevent(struct kobject *kobj, enum kobject_action action)
{
	return 0;
}


int kobject_uevent_env(struct kobject *kobj, enum kobject_action action,
                       char *envp[])
{
	return 0;
}


#include <linux/property.h>

bool is_software_node(const struct fwnode_handle *fwnode)
{
	return false;
}


extern void software_node_notify(struct device *dev);
void software_node_notify(struct device *dev) { }


extern void software_node_notify_remove(struct device *dev);
void software_node_notify_remove(struct device *dev) { }


#include <linux/kernfs.h>

struct kernfs_node *kernfs_find_and_get_ns(struct kernfs_node *parent,
                                           const char *name, const void *ns)
{
	return NULL;
}


void kernfs_get(struct kernfs_node *kn) { }


void kernfs_put(struct kernfs_node *kn) { }


#include <linux/sysfs.h>

int sysfs_create_dir_ns(struct kobject *kobj, const void *ns)
{
	if (!kobj)
		return -EINVAL;

	kobj->sd = kzalloc(sizeof(*kobj->sd), GFP_KERNEL);
	return kobj->sd ? 0 : -ENOMEM;
}


void sysfs_remove_dir(struct kobject *kobj)
{
	if (!kobj)
		return;

	kfree(kobj->sd);
	kobj->sd = NULL;
}


int sysfs_create_groups(struct kobject *kobj,
                        const struct attribute_group **groups)
{
	return 0;
}


void sysfs_remove_groups(struct kobject *kobj,
                         const struct attribute_group **groups) { }


int sysfs_create_bin_file(struct kobject *kobj, const struct bin_attribute *attr)
{
	return 0;
}


int sysfs_create_file_ns(struct kobject *kobj, const struct attribute *attr,
                         const void *ns)
{
	return 0;
}


void sysfs_remove_file_ns(struct kobject *kobj, const struct attribute *attr,
                          const void *ns) { }


int sysfs_add_file_to_group(struct kobject *kobj, const struct attribute *attr,
                            const char *group)
{
	return 0;
}


int sysfs_create_link(struct kobject *kobj, struct kobject *target,
                      const char *name)
{
	return 0;
}


void sysfs_remove_link(struct kobject *kobj, const char *name) { }


void sysfs_delete_link(struct kobject *kobj, struct kobject *targ,
                       const char *name) { }


int sysfs_emit(char *buf, const char *fmt, ...)
{
	lx_emul_trace(__func__);
	return 0;
}


int sysfs_emit_at(char *buf, int at, const char *fmt, ...)
{
	lx_emul_trace(__func__);
	return 0;
}


#include <linux/proc_fs.h>

struct proc_dir_entry { int dummy; };

struct proc_dir_entry *proc_create_seq_private(const char *name, umode_t mode,
                                               struct proc_dir_entry *parent,
                                               const struct seq_operations *ops,
                                               unsigned int state_size,
                                               void *data)
{
	static struct proc_dir_entry ret;
	return &ret;
}


struct proc_dir_entry *proc_symlink(const char *name,
                                    struct proc_dir_entry *parent,
                                    const char *dest)
{
	return NULL;
}


#include <linux/printk.h>

asmlinkage __visible void dump_stack_lvl(const char *log_lvl)
{
	printk("%sdump_stack: no backtrace available\n", log_lvl);
}


asmlinkage __visible void dump_stack(void)
{
	dump_stack_lvl(KERN_DEFAULT);
}


/*
 * DMA and IOMMU
 *
 * The device is not behind an IOMMU the emulation would know of: DMA
 * addresses are the physical addresses of the Genode DMA buffers. panthor
 * programs the GPU's own MMU with them (io-pgtable-arm is used as a page-table
 * library only).
 */

#include <linux/dma-map-ops.h>

void arch_setup_dma_ops(struct device *dev, bool coherent)
{
	lx_emul_trace(__func__);
}


#include <linux/of_iommu.h>

int of_iommu_configure(struct device *dev, struct device_node *master_np,
                       const u32 *id)
{
	return -ENODEV;
}


#include <linux/iommu.h>

int iommu_device_use_default_domain(struct device *dev)
{
	return 0;
}


void iommu_device_unuse_default_domain(struct device *dev) { }


#include <linux/swiotlb.h>

void swiotlb_dev_init(struct device *dev) { }


bool is_swiotlb_allocated(void)
{
	return false;
}


#include <linux/dma-mapping.h>

size_t dma_max_mapping_size(struct device *dev)
{
	return SIZE_MAX;
}


/*
 * Memory management
 */

#include <linux/mm.h>

/*
 * No user mapping exists in the emulation (panthor drops the mappings of
 * its user MMIO page whenever the device changes its power state)
 */
void unmap_mapping_range(struct address_space *mapping, loff_t const holebegin,
                         loff_t const holelen, int even_cows)
{
	lx_emul_trace(__func__);
}


/* the backing store of a GEM object is a dataspace that is never swapped */

#include <linux/swap.h>

void check_move_unevictable_folios(struct folio_batch *fbatch) { }


void folio_mark_accessed(struct folio *folio) { }


#include <linux/pagemap.h>

bool folio_mark_dirty(struct folio *folio)
{
	return false;
}


/*
 * Symbols without a declaration in a public header (the dummy generator
 * cannot create them)
 */

/* kernel/irq/spurious.c: CPU that polls a misrouted interrupt, none */
int irq_poll_cpu = -1;


#include <asm/memory.h>

/*
 * Asked by 'lpa2_is_enabled' (TCR_DS) and 'vabits_actual' (T1SZ) when the
 * driver builds protection bits for a kernel mapping (first device run:
 * reached from the firmware load). Answer: 48-bit addresses, no LPA2.
 */
u64 read_tcr(void)
{
	return 16UL << 16;
}


/* kernel/sched/sched.h */
struct dl_bw;
extern void init_dl_bw(struct dl_bw *dl_b);
void init_dl_bw(struct dl_bw *dl_b)
{
	lx_emul_trace_and_stop(__func__);
}


struct sched_dl_entity;
struct rq;
extern void __dl_server_attach_root(struct sched_dl_entity *dl_se, struct rq *rq);
void __dl_server_attach_root(struct sched_dl_entity *dl_se, struct rq *rq)
{
	lx_emul_trace_and_stop(__func__);
}


struct irq_work;
extern void rto_push_irq_work_func(struct irq_work *work);
void rto_push_irq_work_func(struct irq_work *work)
{
	lx_emul_trace_and_stop(__func__);
}


/* kernel/sched/cpudeadline.h */
struct cpudl;
extern int cpudl_init(struct cpudl *cp);
int cpudl_init(struct cpudl *cp)
{
	lx_emul_trace_and_stop(__func__);
}


extern void cpudl_cleanup(struct cpudl *cp);
void cpudl_cleanup(struct cpudl *cp)
{
	lx_emul_trace_and_stop(__func__);
}


/* include/linux/sched/topology.h */
extern int arch_asym_cpu_priority(int cpu);
int arch_asym_cpu_priority(int cpu)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/memblock.h>

/* memory for the unflattened device tree (lx_emul/shadow/mm/memblock.c) */
void * __init __memblock_alloc_or_panic(phys_addr_t size, phys_addr_t align,
                                        const char *func)
{
	void *addr = memblock_alloc_try_nid(size, align, 0, 0, NUMA_NO_NODE);

	if (!addr)
		panic("%s: failed to allocate %llu bytes\n", func,
		      (unsigned long long)size);

	memset(addr, 0, size);
	return addr;
}
