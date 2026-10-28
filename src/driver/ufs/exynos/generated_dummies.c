/*
 * \brief  Dummy definitions of Linux Kernel functions
 * \author Automatically generated file - do no edit
 * \date   2026-10-28
 */

#include <lx_emul.h>


#include <linux/pagemap.h>

void __folio_lock(struct folio * folio)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/blkdev.h>

int __invalidate_device(struct block_device * bdev,bool kill_dirty)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/badblocks.h>

ssize_t badblocks_show(struct badblocks * bb,char * page,int unack)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/badblocks.h>

ssize_t badblocks_store(struct badblocks * bb,const char * page,size_t len,int unack)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/bsg.h>

void bsg_unregister_queue(struct bsg_device * bcd)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/srcu.h>

void cleanup_srcu_struct(struct srcu_struct * ssp)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/clk.h>

long clk_round_rate(struct clk * clk,unsigned long rate)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/sched/topology.h>

bool cpus_equal_capacity(int this_cpu,int that_cpu)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/sched/topology.h>

bool cpus_share_cache(int this_cpu,int that_cpu)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/pm_opp.h>

int dev_pm_opp_add(struct device * dev,unsigned long freq,unsigned long u_volt)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/pm_opp.h>

void dev_pm_opp_remove(struct device * dev,unsigned long freq)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/devfreq.h>

struct devfreq * devfreq_add_device(struct device * dev,struct devfreq_dev_profile * profile,const char * governor_name,void * data)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/devfreq.h>

int devfreq_remove_device(struct devfreq * devfreq)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/devfreq.h>

int devfreq_resume_device(struct devfreq * devfreq)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/devfreq.h>

int devfreq_suspend_device(struct devfreq * devfreq)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/blkdev.h>

struct blk_independent_access_ranges * disk_alloc_independent_access_ranges(struct gendisk * disk,int nr_ia_ranges)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/blkdev.h>

void disk_set_independent_access_ranges(struct gendisk * disk,struct blk_independent_access_ranges * iars)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/mm.h>

bool folio_mark_dirty(struct folio * folio)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/pagemap.h>

void folio_unlock(struct folio * folio)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/writeback.h>

void laptop_io_completion(struct backing_dev_info * info)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/string.h>

ssize_t memory_read_from_buffer(void * to,size_t count,loff_t * ppos,const void * from,size_t available)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/panic.h>

void panic(const char * fmt,...)
{
	lx_emul_trace_and_stop(__func__);
}


#include <scsi/scsi_ioctl.h>

int scsi_ioctl(struct scsi_device * sdev,fmode_t mode,int cmd,void __user * arg)
{
	lx_emul_trace_and_stop(__func__);
}


#include <scsi/scsi_ioctl.h>

int scsi_ioctl_block_when_processing_errors(struct scsi_device * sdev,int cmd,bool ndelay)
{
	lx_emul_trace_and_stop(__func__);
}


#include <scsi/scsi_device.h>

int scsi_set_medium_removal(struct scsi_device * sdev,char state)
{
	lx_emul_trace_and_stop(__func__);
}


#include <scsi/scsicam.h>

int scsicam_bios_param(struct block_device * bdev,sector_t capacity,int * ip)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/smp.h>

int smp_call_function_single_async(int cpu,struct __call_single_data * csd)
{
	lx_emul_trace_and_stop(__func__);
}


#include <core/ufshcd-priv.h>

int ufshcd_mcq_abort(struct scsi_cmnd * cmd)
{
	lx_emul_trace_and_stop(__func__);
}


#include <core/ufshcd-priv.h>

void ufshcd_mcq_compl_all_cqes_lock(struct ufs_hba * hba,struct ufs_hw_queue * hwq)
{
	lx_emul_trace_and_stop(__func__);
}


#include <ufs/ufshcd.h>

void ufshcd_mcq_config_mac(struct ufs_hba * hba,u32 max_active_cmds)
{
	lx_emul_trace_and_stop(__func__);
}


#include <core/ufshcd-priv.h>

int ufshcd_mcq_decide_queue_depth(struct ufs_hba * hba)
{
	lx_emul_trace_and_stop(__func__);
}


#include <core/ufshcd-priv.h>

int ufshcd_mcq_init(struct ufs_hba * hba)
{
	lx_emul_trace_and_stop(__func__);
}


#include <ufs/ufshcd.h>

void ufshcd_mcq_make_queues_operational(struct ufs_hba * hba)
{
	lx_emul_trace_and_stop(__func__);
}


#include <core/ufshcd-priv.h>

int ufshcd_mcq_memory_alloc(struct ufs_hba * hba)
{
	lx_emul_trace_and_stop(__func__);
}


#include <ufs/ufshcd.h>

unsigned long ufshcd_mcq_poll_cqe_lock(struct ufs_hba * hba,struct ufs_hw_queue * hwq)
{
	lx_emul_trace_and_stop(__func__);
}


#include <ufs/ufshcd.h>

u32 ufshcd_mcq_read_cqis(struct ufs_hba * hba,int i)
{
	lx_emul_trace_and_stop(__func__);
}


#include <core/ufshcd-priv.h>

struct ufs_hw_queue * ufshcd_mcq_req_to_hwq(struct ufs_hba * hba,struct request * req)
{
	lx_emul_trace_and_stop(__func__);
}


#include <core/ufshcd-priv.h>

int ufshcd_mcq_sq_cleanup(struct ufs_hba * hba,int task_tag)
{
	lx_emul_trace_and_stop(__func__);
}


#include <ufs/ufshcd.h>

void ufshcd_mcq_write_cqis(struct ufs_hba * hba,u32 val,int i)
{
	lx_emul_trace_and_stop(__func__);
}


#include <linux/vmalloc.h>

bool vmalloc_dump_obj(void * object)
{
	lx_emul_trace_and_stop(__func__);
}
