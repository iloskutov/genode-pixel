/*
 * \brief  Dummy definitions of Linux Kernel functions
 * \author Automatically generated file - do no edit
 * \date   2026-11-07
 */

#include <lx_emul.h>


const struct attribute_group aspm_ctrl_attr_group = {};


#include <linux/filter.h>

DEFINE_STATIC_KEY_FALSE(bpf_master_redirect_enabled_key);


#include <linux/filter.h>

void bpf_prog_change_xdp(struct bpf_prog * prev_prog,struct bpf_prog * prog)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/filter.h>

DEFINE_STATIC_KEY_FALSE(bpf_stats_enabled_key);


#include <linux/filter.h>

void bpf_warn_invalid_xdp_action(struct net_device * dev,struct bpf_prog * prog,u32 act)
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


#include <asm-generic/checksum.h>

__wsum csum_partial(const void * buff,int len,__wsum sum)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/ethtool.h>

int ethtool_op_get_ts_info(struct net_device * dev,struct ethtool_ts_info * eti)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/capability.h>

bool file_ns_capable(const struct file * file,struct user_namespace * ns,int cap)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/sched.h>

struct task_struct * find_task_by_vpid(pid_t nr)
{
	lx_emul_trace_and_stop(__func__);
}


#include <net/gen_stats.h>

void gen_kill_estimator(struct net_rate_estimator __rcu ** ptr)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/pseudo_fs.h>

struct pseudo_fs_context * init_pseudo(struct fs_context * fc,unsigned long magic)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/cred.h>

struct user_namespace init_user_ns = {};


#include <linux/io.h>

int ioremap_page_range(unsigned long addr,unsigned long end,phys_addr_t phys_addr,pgprot_t prot)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/uio.h>

void iov_iter_revert(struct iov_iter * i,size_t bytes)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/net.h>

int kernel_sendmsg(struct socket * sock,struct msghdr * msg,struct kvec * vec,size_t num,size_t len)
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


#include <linux/mmzone.h>

struct mem_section **mem_section = {};


#include <linux/preempt.h>

void migrate_disable(void)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/preempt.h>

void migrate_enable(void)
{
	lx_emul_trace_and_stop(__func__);
}


#include <net/neighbour.h>

const struct nla_policy nda_policy[] = {};


#include <linux/clk.h>

struct clk * of_clk_get(struct device_node * np,int index)
{
	lx_emul_trace_and_stop(__func__);
}


const struct attribute_group pci_dev_vpd_attr_group = {};


#include <linux/pci.h>

int pci_mmap_resource_range(struct pci_dev * dev,int bar,struct vm_area_struct * vma,enum pci_mmap_state mmap_state,int write_combine)
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


#include <linux/pm.h>

void pm_generic_complete(struct device * dev)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/socket.h>

int put_cmsg(struct msghdr *,int level,int type,int len,void * data)
{
	lx_emul_trace_and_stop(__func__);
}


#include <net/sock_reuseport.h>

void reuseport_detach_sock(struct sock * sk)
{
	lx_emul_trace_and_stop(__func__);
}


#include <net/scm.h>

void __scm_destroy(struct scm_cookie * scm)
{
	lx_emul_trace_and_stop(__func__);
}


#include <net/scm.h>

void scm_detach_fds(struct msghdr * msg,struct scm_cookie * scm)
{
	lx_emul_trace_and_stop(__func__);
}


#include <net/scm.h>

int __scm_send(struct socket * sock,struct msghdr * msg,struct scm_cookie * scm)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/filter.h>

int sk_filter_trim_cap(struct sock * sk,struct sk_buff * skb,unsigned int cap)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/filter.h>

void sk_filter_uncharge(struct sock * sk,struct sk_filter * fp)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/sock_diag.h>

void sock_diag_broadcast_destroy(struct sock * sk)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/net.h>

int sock_wake_async(struct socket_wq * sk_wq,int how,int band)
{
	lx_emul_trace_and_stop(__func__);
}


#include <net/busy_poll.h>

unsigned int sysctl_net_busy_read = {};


#include <linux/vmalloc.h>

void vfree_atomic(const void * addr)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/vmalloc.h>

void vunmap_range(unsigned long addr,unsigned long end)
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


#include <linux/sched.h>

void yield(void)
{
	lx_emul_trace_and_stop(__func__);
}

