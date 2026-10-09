/*
 * \brief  Dummies shared by the Linux-based drivers for the GS201 SoC
 * \author Ivan Loskutov
 * \date   2026-09-30
 *
 * Kernel-internal symbols every driver built on pixel_lx_emul needs and no
 * source.list provides.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <lx_emul.h>

/*
 * kernel/sched/cputime.c
 */
#include <linux/kernel_stat.h>

void account_process_tick(struct task_struct * tsk,int user)
{
	lx_emul_trace(__func__);
}


/*
 * drivers/char/random.c
 */
#include <linux/random.h>

void add_interrupt_randomness(int irq)
{
	lx_emul_trace(__func__);
}


/*
 * lib/kobject_uevent.c
 */
#include <linux/kobject.h>

int add_uevent_var(struct kobj_uevent_env * env,const char * format,...)
{
	lx_emul_trace(__func__);
	return 0;
}


struct cpumask __cpu_active_mask;


/*
 * arch/arm64/lib/copy_to_user.S
 */
unsigned long __must_check __arch_copy_to_user(void __user *to, const void *from, unsigned long n);
unsigned long __must_check __arch_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	lx_emul_trace_and_stop(__func__);
}


/*
 * kernel/rcu/srcutree.c
 *
 * The emulation does not track RCU readers and ends a grace period at once
 * ('call_rcu' runs the callback directly), so there is nothing to wait for.
 * Called when the driver core releases a device link.
 */
void synchronize_srcu(struct srcu_struct * ssp) { }


/*
 * kernel/cpu.c — cpuhotplug
 */
#include <linux/cpuhotplug.h>

int __cpuhp_setup_state(enum cpuhp_state state,const char * name,bool invoke,
                        int (*startup)(unsigned int cpu),
                        int (*teardown)(unsigned int cpu),bool multi_instance)
{
	lx_emul_trace(__func__);
	return 0;
}


/*
 * include/linux/context_tracking_irq.h
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


/*
 * include/linux/dma-map-ops.h
 */
#include <linux/dma-map-ops.h>

bool dma_default_coherent;


/*
 * asm-generic/softirq_stack.h
 */
#include <asm-generic/softirq_stack.h>

void do_softirq_own_stack(void)
{
	lx_emul_trace(__func__);
}


/*
 * lib/dump_stack.c
 */
#include <linux/printk.h>

asmlinkage __visible void dump_stack(void)
{
	lx_emul_trace_and_stop(__func__);
}


/*
 * asm-generic/sections.h — linker symbols
 */
#include <asm-generic/sections.h>

char __start_rodata[] = {};
char __end_rodata[] = {};


/*
 * include/linux/rcuwait.h
 */
#include <linux/rcuwait.h>

void finish_rcuwait(struct rcuwait * w)
{
	lx_emul_trace(__func__);
}


/*
 * mm/debug.c — trace_print_flags arrays
 */
#include <linux/trace_events.h>

const struct trace_print_flags gfpflag_names[] = {
	{0, NULL}
};


const struct trace_print_flags pageflag_names[] = {
	{0, NULL}
};


const struct trace_print_flags vmaflag_names[] = {
	{0, NULL}
};


/*
 * include/linux/gfp.h
 */
#include <linux/gfp.h>

bool gfp_pfmemalloc_allowed(gfp_t gfp_mask)
{
	lx_emul_trace_and_stop(__func__);
}


/*
 * include/linux/uuid.h
 */
#include <linux/uuid.h>

const u8 guid_index[16] = {};
const u8 uuid_index[16] = {};


/*
 * kernel/signal.c
 */
#include <linux/sched/signal.h>

void ignore_signals(struct task_struct * t)
{
	lx_emul_trace(__func__);
}


/*
 * init/main.c
 */
#include <linux/init.h>

bool initcall_debug;


/*
 * kernel/resource.c
 */
#include <linux/ioport.h>

int insert_resource(struct resource * parent,struct resource * new)
{
	lx_emul_trace(__func__);
	return 0;
}


int release_resource(struct resource * new)
{
	lx_emul_trace(__func__);
	return 0;
}


struct resource iomem_resource = {};
struct resource ioport_resource = {};


/*
 * kernel/sched/core.c
 */
#include <linux/sched.h>

void __sched io_schedule(void)
{
	lx_emul_trace_and_stop(__func__);
}


void io_schedule_finish(int token)
{
	lx_emul_trace_and_stop(__func__);
}


int io_schedule_prepare(void)
{
	lx_emul_trace_and_stop(__func__);
}


long __sched io_schedule_timeout(long timeout)
{
	return schedule_timeout(timeout);
}


/*
 * kernel/irq/spurious.c
 */
#include <linux/irq.h>

struct irq_chip no_irq_chip = {};

void note_interrupt(struct irq_desc * desc,irqreturn_t action_ret)
{
	lx_emul_trace_and_stop(__func__);
}


/*
 * kernel/irq/manage.c (internal)
 */
extern bool irq_wait_for_poll(struct irq_desc *desc);
bool irq_wait_for_poll(struct irq_desc * desc)
{
	lx_emul_trace_and_stop(__func__);
}


void irq_work_tick(void)
{
	lx_emul_trace_and_stop(__func__);
}


/*
 * include/linux/property.h
 */
#include <linux/property.h>

bool is_software_node(const struct fwnode_handle * fwnode)
{
	lx_emul_trace_and_stop(__func__);
}


/*
 * lib/kobject_uevent.c
 */
int kobject_synth_uevent(struct kobject * kobj,const char * buf,size_t count)
{
	lx_emul_trace_and_stop(__func__);
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
 * include/linux/delay.h
 */
#include <linux/delay.h>

/*
 * Drivers that time out register polls in loop counts (msecs_to_loops()
 * in spi-s3c64xx: loops_per_jiffy / 1000 * HZ * ms) need a realistic value:
 * with 0 a flush 'times out' at once and a 'while (... && --val)' poll
 * wraps and spins practically forever. 40000 at HZ=250 gives 10000 polls
 * per millisecond, about the rate of device-register reads on the A55.
 * (udelay and friends of lx_emul do not use it.)
 */
unsigned long loops_per_jiffy = 40000;
unsigned long lpj_fine;


/*
 * drivers/clk/clk-conf.c
 */
#include <linux/clk/clk-conf.h>

int of_clk_set_defaults(struct device_node * node,bool clk_supplier)
{
	lx_emul_trace(__func__);
	return 0;
}


void smp_call_function_many(const struct cpumask * mask,smp_call_func_t func,
                            void * info,bool wait)
{
	lx_emul_trace_and_stop(__func__);
}


/*
 * include/asm-generic/percpu.h
 */
#include <asm-generic/percpu.h>

unsigned long __per_cpu_offset[NR_CPUS] = {};


void rcu_sched_clock_irq(int user)
{
	lx_emul_trace(__func__);
}


/*
 * include/linux/syscore_ops.h
 */
#include <linux/syscore_ops.h>

void register_syscore_ops(struct syscore_ops * ops)
{
	lx_emul_trace(__func__);
}


/*
 * net/core/skbuff.c
 */
#include <linux/skbuff.h>

void skb_init(void)
{
	lx_emul_trace(__func__);
}


/*
 * drivers/base/base.h — software node notification (internal)
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


/*
 * include/linux/jump_label.h
 */
#include <linux/jump_label.h>

bool static_key_initialized;


/*
 * kernel/time/tick-broadcast.c
 */
#include <linux/clockchips.h>

void tick_broadcast(const struct cpumask * mask)
{
	lx_emul_trace_and_stop(__func__);
}


/*
 * include/linux/timekeeper_internal.h
 */
#include <linux/timekeeper_internal.h>

void update_vsyscall(struct timekeeper * tk)
{
	lx_emul_trace(__func__);
}


/*
 * kernel/dma/swiotlb.c — SWIOTLB default memory
 */
#include <linux/swiotlb.h>

struct io_tlb_mem io_tlb_default_mem = {};


/*
 * arch/arm64/kernel/cpufeature.c — virtual address bits
 */
u64 vabits_actual = 48;


/*
 * kernel/sched/core.c — wake queue
 */
#include <linux/sched/wake_q.h>

void wake_q_add_safe(struct wake_q_head * head,struct task_struct * task)
{
	lx_emul_trace(__func__);
}


/*
 * include/linux/context_tracking_irq.h — context tracking IRQ
 */
void ct_irq_enter_irqson(void)
{
	lx_emul_trace(__func__);
}


void ct_irq_exit_irqson(void)
{
	lx_emul_trace(__func__);
}


/*
 * kernel/printk/printk_safe.c
 */
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
 * drivers/amba/bus.c — AMBA stubs needed by drivers/of/platform.c
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
 * drivers/of/address.c — PCI I/O range stubs (not used on Pixel 7)
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
 * arch/arm64/mm/ioremap.c — ioremap_prot via DDE Linux io_mem_map
 */
#include <lx_emul/io_mem.h>

void __iomem * ioremap_prot(phys_addr_t phys_addr,size_t size,
                             unsigned long prot)
{
	return lx_emul_io_mem_map(phys_addr, size, false);
}


/*
 * drivers/of/base.c — kobject for /sys/firmware
 */
struct kobject * firmware_kobj;


/*
 * drivers/of/device.c — ARM64 DMA ops
 */
void arch_setup_dma_ops(struct device * dev,u64 dma_base,u64 size,
                         const struct iommu_ops * iommu,bool coherent)
{
	lx_emul_trace(__func__);
}


/*
 * drivers/of/fdt.c — FDT init stubs
 */
#include <linux/of_fdt.h>

void __init add_bootloader_randomness(const void * buf,size_t size)
{
	lx_emul_trace(__func__);
}


char boot_command_line[2048] = {};


void fdt_reserved_mem_save_node(unsigned long node,const char * uname,
                                 phys_addr_t base,phys_addr_t size)
{
	lx_emul_trace(__func__);
}


void fdt_init_reserved_mem(void)
{
	lx_emul_trace(__func__);
}


phys_addr_t phys_initrd_start;
phys_addr_t phys_initrd_size;


/*
 * mm/memblock.c — memory region stubs
 */
#include <linux/mmzone.h>

atomic_long_t _totalram_pages;


void reserve_bootmem_region(phys_addr_t start,phys_addr_t end)
{
	lx_emul_trace(__func__);
}


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
 * drivers/base/power/runtime.c — additional pm_runtime stubs
 * (the base stubs are in lx_emul/shadow/drivers/base/power/runtime.c)
 */
#include <linux/pm_runtime.h>

void pm_runtime_enable(struct device * dev)
{
	lx_emul_trace(__func__);
}


void pm_runtime_set_autosuspend_delay(struct device * dev,int delay)
{
	lx_emul_trace(__func__);
}


int __pm_runtime_set_status(struct device * dev,unsigned int status)
{
	lx_emul_trace(__func__);
	return 0;
}


void __pm_runtime_disable(struct device * dev,bool check_resume)
{
	lx_emul_trace(__func__);
}


void pm_runtime_release_supplier(struct device_link * link)
{
	lx_emul_trace(__func__);
}


/*
 * drivers/base/power/sysfs.c — device power management sysfs
 */
int dpm_sysfs_add(struct device * dev)
{
	lx_emul_trace(__func__);
	return 0;
}


void dpm_sysfs_remove(struct device * dev)
{
	lx_emul_trace(__func__);
}


void pinctrl_init_done(struct device * dev)
{
	lx_emul_trace(__func__);
}


/*
 * drivers/base/power/common.c — PM domain attach/detach
 */
int dev_pm_domain_attach(struct device * dev,bool power_on)
{
	lx_emul_trace(__func__);
	return 0;
}


void dev_pm_domain_detach(struct device * dev,bool power_off)
{
	lx_emul_trace(__func__);
}


/*
 * lib/logic_pio.c — PCI I/O address translation
 */
unsigned long pci_address_to_pio(phys_addr_t address)
{
	lx_emul_trace(__func__);
	return (unsigned long)-1;
}


/*
 * net/core/net_namespace.c — network namespace init
 */
int __init net_ns_init(void)
{
	lx_emul_trace(__func__);
	return 0;
}


#include <linux/pm_runtime.h>

/* runtime PM is not modelled: devices are active (handover), callbacks unused */
int  __pm_runtime_suspend(struct device *dev, int rpmflags) { return 0; }
void __pm_runtime_use_autosuspend(struct device *dev, bool use) { }
int  pm_runtime_get_if_active(struct device *dev, bool ign_usage_count) { return 1; }
void pm_runtime_no_callbacks(struct device *dev) { }


#include <linux/pm.h>

/*
 * No dpm list, but its membership flag: device_link_add() refuses suppliers
 * that are not 'device_pm_initialized' (dev->power.in_dpm_list with
 * PM_SLEEP), and the DSIM links itself to DECON on every enable.
 */
void device_pm_add(struct device *dev) { dev->power.in_dpm_list = true; }
void device_pm_remove(struct device *dev) { dev->power.in_dpm_list = false; }
void device_pm_check_callbacks(struct device *dev) { }
void device_pm_lock(void) { }
void device_pm_unlock(void) { }
void device_pm_move_last(struct device *dev) { }
void device_pm_sleep_init(struct device *dev) { }
void pm_wakeup_ws_event(struct wakeup_source *ws, unsigned int msec, bool hard) { }
struct wakeup_source *wakeup_source_register(struct device *dev, const char *name) { return NULL; }


#include <linux/interrupt.h>

void irq_pm_install_action(struct irq_desc *desc, struct irqaction *action) { }
void irq_pm_remove_action(struct irq_desc *desc, struct irqaction *action) { }
bool irq_pm_check_wakeup(struct irq_desc *desc) { return false; }


#include <linux/async.h>

async_cookie_t async_schedule_node(async_func_t func, void *data, int node)
{
	func(data, 0);
	return 0;
}


void async_synchronize_full(void) { }


#include <linux/property.h>

int device_create_managed_software_node(struct device *dev,
                                        const struct property_entry *properties,
                                        const struct software_node *parent)
{
	return 0;
}


#include <linux/fs.h>
#include <linux/cdev.h>

int alloc_chrdev_region(dev_t *dev, unsigned baseminor, unsigned count, const char *name)
{
	static dev_t counter = 0;
	*dev = counter++;
	return 0;
}


int __register_chrdev(unsigned int major, unsigned int baseminor, unsigned int count,
                      const char *name, const struct file_operations *fops)
{
	return 0;
}


void __unregister_chrdev(unsigned int major, unsigned int baseminor,
                         unsigned int count, const char *name) { }
void cdev_init(struct cdev *cdev, const struct file_operations *fops) { }
int  cdev_device_add(struct cdev *cdev, struct device *dev) { return 0; }
void cdev_device_del(struct cdev *cdev, struct device *dev) { }


#include <linux/console.h>

/* no console: drm_fb_helper takes the lock around fb_set_suspend only */
void console_lock(void) { }
void console_unlock(void) { }

int oops_in_progress;


#include <linux/io.h>

void __memcpy_fromio(void *to, const volatile void __iomem *from, size_t count)
{
	memcpy(to, (void const *)from, count);
}


void __memcpy_toio(volatile void __iomem *to, const void *from, size_t count)
{
	memcpy((void *)to, from, count);
}


pgprot_t pgprot_writecombine(pgprot_t prot) { return prot; }


/* gpiolib's pinctrl hooks: the gs201 GPIO banks have no gpio-ranges */
struct pinctrl_dev;
bool pinctrl_gpio_can_use_line(unsigned gpio) { return true; }
struct pinctrl_dev *of_pinctrl_get(struct device_node *np) { return NULL; }


unsigned long __must_check __arch_copy_from_user(void *to, const void __user *from, unsigned long n);
unsigned long __must_check __arch_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dcache.h>

/*
 * dentries only exist in the pseudo fs (lx_emul.c), freed with their file.
 * The panel also dput()s the ERR_PTR from the debugfs_lookup() stub.
 */
void dput(struct dentry *dentry) { }


#include <linux/irq_work.h>

/* no hard-IRQ context to defer from in lx_emul: run the work right away */
bool irq_work_queue(struct irq_work *work)
{
	work->func(work);
	return true;
}


asmlinkage int _printk_deferred(const char *fmt, ...)
{
	va_list args;
	int r;

	va_start(args, fmt);
	r = vprintk(fmt, args);
	va_end(args);
	return r;
}


#include <linux/pid.h>
#include <linux/sched.h>

pid_t __task_pid_nr_ns(struct task_struct *task, enum pid_type type,
                       struct pid_namespace *ns)
{
	return task ? task->pid : 0;
}


void kick_process(struct task_struct *p) { }


void put_pid(struct pid *pid) { }


#include <linux/seq_file.h>

void seq_printf(struct seq_file *m, const char *fmt, ...) { }


/* the DPU event log records callers, no unwinder here */
void *return_address(unsigned int level) { return NULL; }


#include <linux/rcupdate.h>

/* one CPU, cooperative tasks: every grace period has already elapsed */
void synchronize_rcu(void)
{
	lx_emul_trace(__func__);
}


#include <linux/mm.h>

/* dead code: is_vmalloc_addr() is always false in DDE Linux */
struct page *vmalloc_to_page(const void *vmalloc_addr)
{
	lx_emul_trace_and_stop(__func__);
}
