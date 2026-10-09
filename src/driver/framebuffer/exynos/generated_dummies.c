/*
 * \brief  Dummy definitions of Linux Kernel functions
 * \author Automatically generated file - do no edit
 * \date   2026-09-30
 */

#include <lx_emul.h>


#include <linux/dma-fence-unwrap.h>

struct dma_fence * __dma_fence_unwrap_merge(unsigned int num_fences,struct dma_fence ** fences,struct dma_fence_unwrap * iter)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/file.h>

unsigned long __fdget(unsigned int fd)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/mm.h>

void __show_mem(unsigned int filter,nodemask_t * nodemask,int max_zone_idx)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/sched.h>



#include <linux/printk.h>



#include <linux/anon_inodes.h>

struct file * anon_inode_getfile(const char * name,const struct file_operations * fops,void * priv,int flags)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/kernel.h>

void bust_spinlocks(int yes)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/console.h>

void console_flush_on_panic(enum con_flush_mode mode)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/console.h>

void console_unblank(void)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/printk.h>

void console_verbose(void)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/fs.h>

struct file * dentry_open(const struct path * path,int flags,const struct cred * cred)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dma-fence-unwrap.h>

struct dma_fence * dma_fence_unwrap_first(struct dma_fence * head,struct dma_fence_unwrap * cursor)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dma-fence-unwrap.h>

struct dma_fence * dma_fence_unwrap_next(struct dma_fence_unwrap * cursor)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dma-mapping.h>

int dma_map_sgtable(struct device * dev,struct sg_table * sgt,enum dma_data_direction dir,unsigned long attrs)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dcache.h>



#include <drm/drm_modes.h>



extern int drm_pci_set_busid(struct drm_device * dev,struct drm_master * master);
int drm_pci_set_busid(struct drm_device * dev,struct drm_master * master)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dcache.h>

char * dynamic_dname(char * buffer,int buflen,const char * fmt,...)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/reboot.h>

void emergency_restart(void)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/file.h>

void fd_install(unsigned int fd,struct file * file)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/file.h>

struct file * fget(unsigned int fd)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/file.h>

int get_unused_fd_flags(unsigned flags)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/irq_work.h>



#include <linux/sched.h>



#include <linux/fs.h>

void kill_anon_super(struct super_block * sb)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/kmsg_dump.h>

void kmsg_dump(enum kmsg_dump_reason reason)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/fs.h>

int nonseekable_open(struct inode * inode,struct file * filp)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/fs.h>

loff_t noop_llseek(struct file * file,loff_t offset,int whence)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/highuid.h>

int overflowuid;


#include <linux/reboot.h>

enum reboot_mode panic_reboot_mode;


#include <linux/pci.h>

struct bus_type pci_bus_type;


#include <linux/pinctrl/pinctrl.h>

void pinctrl_add_gpio_range(struct pinctrl_dev * pctldev,struct pinctrl_gpio_range * range)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/pinctrl/pinctrl.h>

const char * pinctrl_dev_get_devname(struct pinctrl_dev * pctldev)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/pinctrl/pinctrl.h>

struct pinctrl_dev * pinctrl_find_and_add_gpio_range(const char * devname,struct pinctrl_gpio_range * range)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/pinctrl/pinctrl.h>

int pinctrl_get_group_pins(struct pinctrl_dev * pctldev,const char * pin_group,const unsigned ** pins,unsigned * num_pins)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/pinctrl/pinctrl.h>

void pinctrl_remove_gpio_range(struct pinctrl_dev * pctldev,struct pinctrl_gpio_range * range)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/pm.h>

int pm_generic_freeze(struct device * dev)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/pm.h>

int pm_generic_poweroff(struct device * dev)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/pm.h>

int pm_generic_restore(struct device * dev)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/pm.h>

int pm_generic_resume(struct device * dev)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/pm.h>

int pm_generic_suspend(struct device * dev)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/pm.h>

int pm_generic_thaw(struct device * dev)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/pid.h>



#include <linux/file.h>

void put_unused_fd(unsigned int fd)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/reboot.h>

enum reboot_mode reboot_mode;




#include <linux/seq_file.h>



#include <linux/sched/debug.h>

void show_state_filter(unsigned int state_filter)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/smp.h>

void smp_send_stop(void)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/uaccess.h>

long strnlen_user(const char __user * str,long count)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/printk.h>

int suppress_printk;


#include <linux/mm.h>

pgprot_t vm_get_page_prot(unsigned long vm_flags)
{
	lx_emul_trace_and_stop(__func__);
}

