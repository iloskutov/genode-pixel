/*
 * \brief  Dummy definitions of Linux Kernel functions
 * \author Automatically generated file - do no edit
 * \date   2026-10-28
 */

#include <lx_emul.h>


#include <net/scm.h>

int __scm_send(struct socket * sock,struct msghdr * msg,struct scm_cookie * scm)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/filter.h>

int bpf_prog_create_from_user(struct bpf_prog **pfp,struct sock_fprog * fprog,bpf_aux_classic_check_t trans,bool save_orig)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/uio.h>

size_t copy_page_from_iter(struct page * page,size_t offset,size_t bytes,struct iov_iter * i)
{
	lx_emul_trace_and_stop(__func__);
}



#include <net/checksum.h>

__wsum csum_partial(const void * buff,int len,__wsum sum)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/netdevice.h>

int dev_ifconf(struct net * net,struct ifconf __user * ifc)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/netdevice.h>

int dev_ioctl(struct net * net,unsigned int cmd,struct ifreq * ifr,void __user * data,bool * need_copyout)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/dcache.h>

char * dynamic_dname(char * buffer,int buflen,const char * fmt,...)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/ethtool.h>

int ethtool_op_get_ts_info(struct net_device * dev,struct ethtool_ts_info * eti)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/fs.h>

pid_t f_getown(struct file * filp)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/fs.h>

int f_setown(struct file * filp,unsigned long arg,int force)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/fs.h>

int fasync_helper(int fd,struct file * filp,int on,struct fasync_struct ** fapp)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/sched.h>

struct task_struct * find_task_by_vpid(pid_t nr)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/fs.h>

ssize_t generic_file_splice_read(struct file * in,loff_t * ppos,struct pipe_inode_info * pipe,size_t len,unsigned int flags)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/fs.h>

ssize_t generic_splice_sendpage(struct pipe_inode_info * pipe,struct file * out,loff_t * ppos,size_t len,unsigned int flags)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/sched.h>

void io_schedule(void)
{
	lx_emul_trace_and_stop(__func__);
}



/* from kernel/irq/internals.h */
extern bool irq_wait_for_poll(struct irq_desc * desc);
bool irq_wait_for_poll(struct irq_desc * desc)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/fs.h>

void kill_anon_super(struct super_block * sb)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/logic_pio.h>

resource_size_t logic_pio_to_hwaddr(unsigned long pio)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/logic_pio.h>

unsigned long logic_pio_trans_cpuaddr(resource_size_t hw_addr)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/proc_fs.h>

int open_related_ns(struct ns_common * ns,
                    struct ns_common *(*get_ns)(struct ns_common * ns))
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/panic.h>

void panic(const char * fmt,...)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/pci.h>

int pci_mmap_resource_range(struct pci_dev * dev,int bar,struct vm_area_struct * vma,enum pci_mmap_state mmap_state,int write_combine)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/socket.h>

int put_cmsg(struct msghdr * msg,int level,int type,int len,void * data)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/socket.h>

void put_cmsg_scm_timestamping(struct msghdr * msg,struct scm_timestamping_internal * tss)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/socket.h>

void put_cmsg_scm_timestamping64(struct msghdr * msg,struct scm_timestamping_internal * tss)
{
	lx_emul_trace_and_stop(__func__);
}



#include <net/scm.h>

void scm_detach_fds(struct msghdr * msg,struct scm_cookie * scm)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/fs.h>

int simple_setattr(struct user_namespace * mnt_userns,struct dentry * dentry,struct iattr * iattr)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/fs.h>

int simple_statfs(struct dentry * dentry,struct kstatfs * buf)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/filter.h>

int sk_attach_bpf(u32 ufd,struct sock * sk)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/filter.h>

int sk_attach_filter(struct sock_fprog * fprog,struct sock * sk)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/filter.h>

int sk_reuseport_attach_bpf(u32 ufd,struct sock * sk)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/filter.h>

int sk_reuseport_attach_filter(struct sock_fprog * fprog,struct sock * sk)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/mm.h>

int vm_insert_page(struct vm_area_struct * vma,unsigned long addr,struct page * page)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/mm.h>

struct page * vmalloc_to_page(const void * addr)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/filter.h>

int xdp_do_generic_redirect(struct net_device * dev,struct sk_buff * skb,struct xdp_buff * xdp,struct bpf_prog * prog)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/filter.h>

u32 xdp_master_redirect(struct xdp_buff * xdp)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/io.h>

int ioremap_page_range(unsigned long addr,unsigned long end,phys_addr_t phys_addr,pgprot_t prot)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/logic_pio.h>

int logic_pio_register_range(struct logic_pio_hwaddr * newrange)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/pm.h>

void pm_generic_complete(struct device * dev)
{
	lx_emul_trace_and_stop(__func__);
}



#include <linux/vmalloc.h>

void vunmap_range(unsigned long addr,unsigned long end)
{
	lx_emul_trace_and_stop(__func__);
}
