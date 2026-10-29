/*
 * \brief  Dummy definitions of Linux Kernel functions
 * \author Automatically generated file - do no edit
 * \date   2026-10-01
 */

#include <lx_emul.h>


#include <linux/mm.h>

void __show_mem(unsigned int filter,nodemask_t * nodemask,int max_zone_idx)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/uio.h>

size_t _copy_from_iter(void * addr,size_t bytes,struct iov_iter * i)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/uio.h>

size_t _copy_to_iter(const void * addr,size_t bytes,struct iov_iter * i)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/amba/bus.h>

struct bus_type amba_bustype;


#include <linux/amba/bus.h>

void amba_device_unregister(struct amba_device * dev)
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


#include <linux/io.h>

void devm_iounmap(struct device * dev,void __iomem * addr)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dma-buf.h>

struct dma_buf_attachment * dma_buf_attach(struct dma_buf * dmabuf,struct device * dev)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dma-buf.h>

void dma_buf_detach(struct dma_buf * dmabuf,struct dma_buf_attachment * attach)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dma-buf.h>

struct dma_buf * dma_buf_get(int fd)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dma-buf.h>

struct sg_table * dma_buf_map_attachment(struct dma_buf_attachment * attach,enum dma_data_direction direction)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dma-buf.h>

void dma_buf_put(struct dma_buf * dmabuf)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dma-buf.h>

void dma_buf_unmap_attachment(struct dma_buf_attachment * attach,struct sg_table * sg_table,enum dma_data_direction direction)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/reboot.h>

void emergency_restart(void)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/file.h>

struct file * fget(unsigned int fd)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/file.h>

void fput(struct file * file)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/uio.h>

ssize_t import_iovec(int type,const struct iovec __user * uvec,unsigned nr_segs,unsigned fast_segs,struct iovec ** iovp,struct iov_iter * i)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/kmsg_dump.h>

void kmsg_dump(enum kmsg_dump_reason reason)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/reboot.h>

enum reboot_mode panic_reboot_mode;


#include <linux/moduleparam.h>

const struct kernel_param_ops param_ops_bool;


#include <linux/moduleparam.h>

const struct kernel_param_ops param_ops_int;


#include <linux/moduleparam.h>

const struct kernel_param_ops param_ops_ulong;


#include <linux/reboot.h>

enum reboot_mode reboot_mode;


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

long strncpy_from_user(char * dst,const char __user * src,long count)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/printk.h>

int suppress_printk;


#include <linux/vmalloc.h>

void * vmap(struct page ** pages,unsigned int count,unsigned long flags,pgprot_t prot)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/vmalloc.h>

void vunmap(const void * addr)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/file.h>

unsigned long __fdget(unsigned int fd)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/anon_inodes.h>

struct file * anon_inode_getfile(const char * name,const struct file_operations * fops,void * priv,int flags)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/property.h>

int device_add_software_node(struct device * dev,const struct software_node * node)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/property.h>

void device_remove_software_node(struct device * dev)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dma-mapping.h>

struct sg_table * dma_alloc_noncontiguous(struct device * dev,size_t size,enum dma_data_direction dir,gfp_t gfp,unsigned long attrs)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dma-mapping.h>

struct page * dma_alloc_pages(struct device * dev,size_t size,dma_addr_t * dma_handle,enum dma_data_direction dir,gfp_t gfp)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dma-mapping.h>

bool dma_can_mmap(struct device * dev)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dma-mapping.h>

void dma_free_noncontiguous(struct device * dev,size_t size,struct sg_table * sgt,enum dma_data_direction dir)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dma-mapping.h>

void dma_free_pages(struct device * dev,size_t size,struct page * page,dma_addr_t dma_handle,enum dma_data_direction dir)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dma-mapping.h>

int dma_map_sgtable(struct device * dev,struct sg_table * sgt,enum dma_data_direction dir,unsigned long attrs)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dma-mapping.h>

int dma_mmap_attrs(struct device * dev,struct vm_area_struct * vma,void * cpu_addr,dma_addr_t dma_addr,size_t size,unsigned long attrs)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dma-mapping.h>

int dma_mmap_noncontiguous(struct device * dev,struct vm_area_struct * vma,size_t size,struct sg_table * sgt)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dma-mapping.h>

int dma_mmap_pages(struct device * dev,struct vm_area_struct * vma,size_t size,struct page * page)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dma-mapping.h>

bool dma_need_sync(struct device * dev,dma_addr_t dma_addr)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dma-mapping.h>

void dma_sync_sg_for_cpu(struct device * dev,struct scatterlist * sg,int nelems,enum dma_data_direction dir)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dma-mapping.h>

void dma_sync_sg_for_device(struct device * dev,struct scatterlist * sg,int nelems,enum dma_data_direction dir)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dma-mapping.h>

void * dma_vmap_noncontiguous(struct device * dev,size_t size,struct sg_table * sgt)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/dma-mapping.h>

void dma_vunmap_noncontiguous(struct device * dev,void * vaddr)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/file.h>

void fd_install(unsigned int fd,struct file * file)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/genalloc.h>

void * gen_pool_dma_alloc_align(struct gen_pool * pool,size_t size,dma_addr_t * dma,int align)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/genalloc.h>

void gen_pool_free_owner(struct gen_pool * pool,unsigned long addr,size_t size,void ** owner)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/file.h>

int get_unused_fd_flags(unsigned flags)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/input.h>

struct input_dev * input_allocate_device(void)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/input.h>

void input_event(struct input_dev * dev,unsigned int type,unsigned int code,int value)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/input.h>

void input_free_device(struct input_dev * dev)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/input.h>

int input_register_device(struct input_dev * dev)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/input.h>

void input_set_capability(struct input_dev * dev,unsigned int type,unsigned int code)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/input.h>

void input_unregister_device(struct input_dev * dev)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/genalloc.h>

struct gen_pool * of_gen_pool_get(struct device_node * np,const char * propname,int index)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/moduleparam.h>

const struct kernel_param_ops param_array_ops;


#include <linux/moduleparam.h>

const struct kernel_param_ops param_ops_charp;


#include <linux/pid.h>

pid_t pid_vnr(struct pid * pid)
{
	lx_emul_trace_and_stop(__func__);
}


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


#include <linux/file.h>

void put_unused_fd(unsigned int fd)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/mm.h>

int remap_pfn_range(struct vm_area_struct * vma,unsigned long addr,unsigned long pfn,unsigned long size,pgprot_t prot)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/vmalloc.h>

int remap_vmalloc_range(struct vm_area_struct * vma,void * addr,unsigned long pgoff)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/mm.h>

pgprot_t vm_get_page_prot(unsigned long vm_flags)
{
	lx_emul_trace_and_stop(__func__);
}

