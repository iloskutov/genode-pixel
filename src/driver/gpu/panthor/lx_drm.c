/*
 * \brief  DRM side of the Gpu session of the panthor GPU driver
 * \author Ivan Loskutov
 * \date   2026-10-08
 *
 * One pseudo DRM file per Gpu session (as in the lima port). The requests
 * of a session arrive as argument structures of DRM ioctls whose pointer
 * members were validated and relocated by the session (tunnel.h), and are
 * handed to 'drm_ioctl'. In addition: the file-descriptor table of a
 * session, the lookup of a buffer object's backing store, and the value of
 * the LATEST_FLUSH_ID register.
 */

/*
 * Copyright (C) 2026 Ivan Loskutov
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#include <lx_emul.h>

#include <linux/anon_inodes.h>
#include <linux/delay.h>
#include <linux/dma-resv.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kdev_t.h>
#include <linux/ktime.h>
#include <linux/scatterlist.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>

#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_prime.h>
#include <uapi/drm/drm.h>
#include <uapi/drm/panthor_drm.h>

#include <../drivers/gpu/drm/panthor/panthor_device.h>
#include <../drivers/gpu/drm/panthor/panthor_gem.h>
#include <../drivers/gpu/drm/panthor/panthor_regs.h>

#include <lx_emul/alloc.h>
#include <lx_emul/task.h>

#include "bo_sync.h"
#include "emul.h"
#include "lx_drm.h"


/* lx_emul.c */
extern struct file_operations const *lx_drm_fops;


/*
 * Argument structures of the request types that are no ioctls
 * ('Fd_close', 'Bo_sync_op', 'Bo_sync', 'Test_sleep', 'Bo_query_info' of
 * include/gpu/panthor_session.h)
 */

struct lx_drm_fd_close
{
	__s32 fd;
	__u32 pad;
};


struct lx_drm_bo_sync_op
{
	__u32 handle;
	__u32 type;
	__u64 offset;
	__u64 size;
};


struct lx_drm_bo_sync
{
	struct drm_panthor_obj_array ops;
};


struct lx_drm_test_sleep
{
	__u32 ms;
	__u32 pad;
};


struct lx_drm_bo_query_info
{
	__u32 handle;
	__u32 extra_flags;
	__u32 create_flags;
	__u32 pad;
};


struct lx_drm_private
{
	struct file           *file;
	struct inode          *inode;
	struct emul_fd_table   fds;
};


/*
 * The render node of the first DRM device (minors 128...), then its primary
 * node: the ioctls the sessions use are allowed on both.
 */
static unsigned const _minors[] = { 128, 0 };


void *lx_drm_open(void)
{
	struct lx_drm_private *prv;
	unsigned i;
	int err = -ENODEV;

	if (!lx_drm_fops || !lx_drm_fops->open)
		return NULL;

	prv = kzalloc(sizeof(*prv), GFP_KERNEL);
	if (!prv)
		return NULL;

	prv->inode = kzalloc(sizeof(struct inode), GFP_KERNEL);
	if (!prv->inode)
		goto free_private;

	prv->file = kzalloc(sizeof(struct file), GFP_KERNEL);
	if (!prv->file)
		goto free_inode;

	/*
	 * Never released by 'fput': the file is owned by the session. The
	 * operations are the ones of the DRM core's character device, its
	 * open function ('drm_stub_open') looks up the minor and installs the
	 * operations of the driver.
	 */
	file_ref_init(&prv->file->f_ref, 1);
	prv->file->f_op    = lx_drm_fops;
	prv->file->f_inode = prv->inode;
	prv->file->f_flags = O_RDWR;
	prv->file->f_mode  = FMODE_READ | FMODE_WRITE | FMODE_OPENED;

	for (i = 0; i < ARRAY_SIZE(_minors) && err; i++) {
		prv->inode->i_rdev = MKDEV(DRM_MAJOR, _minors[i]);
		err = lx_drm_fops->open(prv->inode, prv->file);
	}

	if (err) {
		printk("lx_drm_open: no DRM device (error %d)\n", err);
		goto free_file;
	}

	emul_fd_table_attach(&prv->fds);

	return prv;

free_file:
	kfree(prv->file);
free_inode:
	kfree(prv->inode);
free_private:
	kfree(prv);
	return NULL;
}


void lx_drm_close(void *p)
{
	struct lx_drm_private *prv = p;

	if (!prv)
		return;

	emul_fd_table_release(&prv->fds);

	/*
	 * 'lx_drm_fops' are the operations of the DRM core's character device,
	 * whose open function replaced the file's operations by the driver's
	 */
	if (prv->file->f_op && prv->file->f_op->release)
		(void)prv->file->f_op->release(prv->inode, prv->file);
	else
		printk("lx_drm_close: DRM file has no release function\n");

	kfree(prv->file);
	kfree(prv->inode);
	kfree(prv);
}


static struct drm_file *_drm_file(struct lx_drm_private *prv)
{
	return prv->file->private_data;
}


static struct panthor_device *_ptdev(struct lx_drm_private *prv)
{
	return container_of(_drm_file(prv)->minor->dev, struct panthor_device, base);
}


unsigned lx_drm_flush_id(void *p)
{
	struct lx_drm_private *prv = p;
	struct panthor_device *ptdev;

	if (!prv)
		return 0;

	ptdev = _ptdev(prv);

	/*
	 * As the user-MMIO page of Linux: the register while the device is
	 * powered, else the content of panthor's dummy page (1: no flush is
	 * needed after a power cycle). Without CONFIG_PM the device is always
	 * active once the probe function passed.
	 */
	if (atomic_read(&ptdev->pm.state) != PANTHOR_DEVICE_PM_STATE_ACTIVE)
		return 1;

	return gpu_read(ptdev, CSF_GPU_LATEST_FLUSH_ID);
}


int lx_drm_info(void *p, unsigned *major, unsigned *minor, unsigned *patch,
                void *gpu_info, unsigned gpu_info_size,
                void *csif_info, unsigned csif_info_size)
{
	struct lx_drm_private *prv = p;
	struct drm_device *dev;
	struct drm_panthor_dev_query query;
	int err;

	if (!prv)
		return -ENODEV;

	dev = _drm_file(prv)->minor->dev;

	*major = dev->driver->major;
	*minor = dev->driver->minor;
	*patch = dev->driver->patchlevel;

	query =(struct drm_panthor_dev_query) {
		.type    = DRM_PANTHOR_DEV_QUERY_GPU_INFO,
		.size    = gpu_info_size,
		.pointer = (unsigned long)gpu_info,
	};
	err = drm_ioctl(prv->file, DRM_IOCTL_PANTHOR_DEV_QUERY, (unsigned long)&query);
	if (err)
		return err;

	query = (struct drm_panthor_dev_query) {
		.type    = DRM_PANTHOR_DEV_QUERY_CSIF_INFO,
		.size    = csif_info_size,
		.pointer = (unsigned long)csif_info,
	};
	return drm_ioctl(prv->file, DRM_IOCTL_PANTHOR_DEV_QUERY, (unsigned long)&query);
}


/*
 * Backing store of a buffer object (lx_emul.c, 'shmem_file_setup')
 */
static struct emul_shmem_buffer *_bo_buffer(struct drm_gem_object *obj)
{
	if (!obj->filp || !obj->filp->f_mapping)
		return NULL;

	return obj->filp->f_mapping->i_private_data;
}


struct genode_shared_dataspace *lx_drm_bo_dataspace(void *p, unsigned handle,
                                                    unsigned long *size)
{
	struct lx_drm_private          *prv = p;
	struct drm_gem_object          *obj;
	struct emul_shmem_buffer       *buffer;
	struct genode_shared_dataspace *ds = NULL;

	if (!prv)
		return NULL;

	obj = drm_gem_object_lookup(_drm_file(prv), handle);
	if (!obj)
		return NULL;

	/* what 'panthor_gem_mmap' and 'drm_gem_shmem_mmap' refuse */
	if ((to_panthor_bo(obj)->flags & DRM_PANTHOR_BO_NO_MMAP)
	 || drm_gem_is_imported(obj))
		goto out;

	buffer = _bo_buffer(obj);
	if (buffer) {
		ds    = buffer->dataspace;
		*size = obj->size;
	}
out:
	drm_gem_object_put(obj);
	return ds;
}


unsigned lx_drm_payload_size(unsigned op)
{
	switch ((enum lx_drm_op)op) {
	case LX_DRM_NOP:                     return 0;
	case LX_DRM_DEV_QUERY:               return sizeof(struct drm_panthor_dev_query);
	case LX_DRM_VM_CREATE:               return sizeof(struct drm_panthor_vm_create);
	case LX_DRM_VM_DESTROY:              return sizeof(struct drm_panthor_vm_destroy);
	case LX_DRM_VM_BIND:                 return sizeof(struct drm_panthor_vm_bind);
	case LX_DRM_VM_GET_STATE:            return sizeof(struct drm_panthor_vm_get_state);
	case LX_DRM_BO_CREATE:               return sizeof(struct drm_panthor_bo_create);
	case LX_DRM_GROUP_CREATE:            return sizeof(struct drm_panthor_group_create);
	case LX_DRM_GROUP_DESTROY:           return sizeof(struct drm_panthor_group_destroy);
	case LX_DRM_GROUP_SUBMIT:            return sizeof(struct drm_panthor_group_submit);
	case LX_DRM_GROUP_GET_STATE:         return sizeof(struct drm_panthor_group_get_state);
	case LX_DRM_TILER_HEAP_CREATE:       return sizeof(struct drm_panthor_tiler_heap_create);
	case LX_DRM_TILER_HEAP_DESTROY:      return sizeof(struct drm_panthor_tiler_heap_destroy);
	case LX_DRM_BO_SET_LABEL:            return sizeof(struct drm_panthor_bo_set_label);
	case LX_DRM_GEM_CLOSE:               return sizeof(struct drm_gem_close);
	case LX_DRM_SYNCOBJ_CREATE:          return sizeof(struct drm_syncobj_create);
	case LX_DRM_SYNCOBJ_DESTROY:         return sizeof(struct drm_syncobj_destroy);
	case LX_DRM_SYNCOBJ_HANDLE_TO_FD:    return sizeof(struct drm_syncobj_handle);
	case LX_DRM_SYNCOBJ_FD_TO_HANDLE:    return sizeof(struct drm_syncobj_handle);
	case LX_DRM_SYNCOBJ_WAIT:            return sizeof(struct drm_syncobj_wait);
	case LX_DRM_SYNCOBJ_RESET:           return sizeof(struct drm_syncobj_array);
	case LX_DRM_SYNCOBJ_SIGNAL:          return sizeof(struct drm_syncobj_array);
	case LX_DRM_SYNCOBJ_TIMELINE_WAIT:   return sizeof(struct drm_syncobj_timeline_wait);
	case LX_DRM_SYNCOBJ_QUERY:           return sizeof(struct drm_syncobj_timeline_array);
	case LX_DRM_SYNCOBJ_TRANSFER:        return sizeof(struct drm_syncobj_transfer);
	case LX_DRM_SYNCOBJ_TIMELINE_SIGNAL: return sizeof(struct drm_syncobj_timeline_array);
	case LX_DRM_FD_CLOSE:                return sizeof(struct lx_drm_fd_close);
	case LX_DRM_BO_SYNC:                 return sizeof(struct lx_drm_bo_sync);
	case LX_DRM_TEST_SLEEP:              return sizeof(struct lx_drm_test_sleep);
	case LX_DRM_BO_QUERY_INFO:           return sizeof(struct lx_drm_bo_query_info);
	}
	return ~0u;
}


/*
 * The time-outs of a message are relative, the ones of the ioctls absolute
 * values of the kernel's monotonic clock
 */
static s64 _absolute_ns(s64 relative_ns)
{
	s64 const now = (s64)ktime_get_ns();

	if (relative_ns <= 0)
		return 0;

	if (relative_ns > S64_MAX - now)
		return S64_MAX;

	return now + relative_ns;
}


static int _syncobj_wait(struct lx_drm_private *prv, struct drm_syncobj_wait *wait)
{
	s64 const timeout  = wait->timeout_nsec;
	u64 const deadline = wait->deadline_nsec;
	int err;

	wait->timeout_nsec = _absolute_ns(timeout);
	if (wait->flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_DEADLINE)
		wait->deadline_nsec = _absolute_ns((s64)min_t(u64, deadline, S64_MAX));

	err = drm_ioctl(prv->file, DRM_IOCTL_SYNCOBJ_WAIT, (unsigned long)wait);

	wait->timeout_nsec  = timeout;
	wait->deadline_nsec = deadline;
	return err;
}


static int _syncobj_timeline_wait(struct lx_drm_private *prv,
                                  struct drm_syncobj_timeline_wait *wait)
{
	s64 const timeout  = wait->timeout_nsec;
	u64 const deadline = wait->deadline_nsec;
	int err;

	wait->timeout_nsec = _absolute_ns(timeout);
	if (wait->flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_DEADLINE)
		wait->deadline_nsec = _absolute_ns((s64)min_t(u64, deadline, S64_MAX));

	err = drm_ioctl(prv->file, DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT, (unsigned long)wait);

	wait->timeout_nsec  = timeout;
	wait->deadline_nsec = deadline;
	return err;
}


/*
 * Store the current LATEST_FLUSH_ID in the queue submits, what a Linux
 * client does by reading the user-MMIO page right before the ioctl
 */
static void _sample_flush_id(struct lx_drm_private *prv,
                             struct drm_panthor_group_submit *submit)
{
	struct drm_panthor_obj_array const *array = &submit->queue_submits;
	u32 const flush_id = lx_drm_flush_id(prv);
	u32 i;

	/* the session checked: stride >= sizeof(struct drm_panthor_queue_submit) */
	for (i = 0; i < array->count; i++) {
		struct drm_panthor_queue_submit *qsubmit =
			(void *)(unsigned long)(array->array + (u64)i*array->stride);

		if (qsubmit->stream_size)
			qsubmit->latest_flush = flush_id;
	}
}


/*
 * Cache maintenance for ranges of buffer objects (panthor 1.7's BO_SYNC,
 * which the kernel driver of this port does not have)
 *
 * Everything in the request is a client's: the array was bounded by the
 * session (tunnel.h), handles are looked up in the session's DRM file,
 * ranges are checked against the object's size without wrap, and the
 * amount of work per request is bounded because this task shares one
 * thread with the kernel driver and all other sessions. The request is
 * checked as a whole first, so that a refused request has no effect.
 *
 * Both types clean and invalidate: there is no plain clean in the
 * emulation, and dropping the cached copy of data that was written back is
 * always correct.
 */
static int _bo_sync(struct lx_drm_private *prv, struct lx_drm_bo_sync *sync)
{
	unsigned long long total = 0;
	unsigned pass;
	u32 i;

	if (!emul_config_bo_cached())
		return -ENOSYS;

	for (pass = 0; pass < 2; pass++) {
		for (i = 0; i < sync->ops.count; i++) {
			struct lx_drm_bo_sync_op const *op =
				(void *)(unsigned long)(sync->ops.array + (u64)i*sync->ops.stride);

			struct drm_gem_object    *obj;
			struct emul_shmem_buffer *buffer;
			int err = 0;

			if (op->type >= BO_SYNC_TYPES)
				return -EINVAL;

			obj = drm_gem_object_lookup(_drm_file(prv), op->handle);
			if (!obj)
				return -ENOENT;

			buffer = _bo_buffer(obj);

			if (!buffer || !bo_sync_range_valid(op->offset, op->size, obj->size))
				err = -EINVAL;

			else if (pass == 0 && !bo_sync_account(&total, op->size))
				err = -EINVAL;

			/* objects without WB_MMAP are uncached: nothing to do */
			else if (pass == 1 && op->size
			      && buffer->buffer_class == EMUL_BUFFER_CLIENT_CACHED)
				lx_emul_mem_cache_clean_invalidate(buffer->addr + op->offset,
				                                   op->size);

			drm_gem_object_put(obj);

			if (err)
				return err;
		}
	}
	return 0;
}


/*
 * BO_CREATE with the flag WB_MMAP of panthor 1.7: the flag selects cached
 * backing memory here and is not passed on to the kernel driver, which
 * would refuse it.
 */
static int _bo_create(struct lx_drm_private *prv, struct drm_panthor_bo_create *args)
{
	u32 const flags  = args->flags;
	int const cached = bo_create_cached(flags, emul_config_bo_cached());
	int err;

	if (cached < 0)
		return -EINVAL;

	args->flags = flags & ~(u32)BO_SYNC_FLAG_WB_MMAP;

	/* the object's backing store is for a client (lx_emul.c) */
	emul_shmem_class_for_current(cached ? EMUL_BUFFER_CLIENT_CACHED
	                                    : EMUL_BUFFER_CLIENT);

	err = drm_ioctl(prv->file, DRM_IOCTL_PANTHOR_BO_CREATE, (unsigned long)args);

	emul_shmem_class_for_current(EMUL_BUFFER_KERNEL);

	args->flags = flags;
	return err;
}


/*
 * BO_QUERY_INFO of panthor 1.7
 */
static int _bo_query_info(struct lx_drm_private *prv,
                          struct lx_drm_bo_query_info *info)
{
	struct drm_gem_object    *obj;
	struct emul_shmem_buffer *buffer;

	if (!emul_config_bo_cached())
		return -ENOSYS;

	if (info->pad)
		return -EINVAL;

	obj = drm_gem_object_lookup(_drm_file(prv), info->handle);
	if (!obj)
		return -ENOENT;

	buffer = _bo_buffer(obj);

	info->extra_flags  = drm_gem_is_imported(obj) ? 1 /* BO_IS_IMPORTED */ : 0;
	info->create_flags = to_panthor_bo(obj)->flags;

	if (buffer && buffer->buffer_class == EMUL_BUFFER_CLIENT_CACHED)
		info->create_flags |= BO_SYNC_FLAG_WB_MMAP;

	drm_gem_object_put(obj);
	return 0;
}


int lx_drm_tunnel(void *p, unsigned op, unsigned flags, void *payload)
{
	struct lx_drm_private *prv = p;
	unsigned long const    arg = (unsigned long)payload;
	unsigned int           cmd = 0;

	/* requests that need no DRM device */
	switch ((enum lx_drm_op)op) {

	case LX_DRM_NOP:
		return 0;

	case LX_DRM_TEST_SLEEP:
		{
			struct lx_drm_test_sleep const *sleep = payload;

			if (!emul_config_test_ops())
				return -ENOSYS;

			if (sleep->ms > 10000)
				return -EINVAL;

			/* interruptible, so that 'lx_drm_cancel' can be tested */
			if (msleep_interruptible(sleep->ms))
				return -ERESTARTSYS;

			return 0;
		}

	default:
		break;
	}

	if (!prv)
		return -ENODEV;

	switch ((enum lx_drm_op)op) {

	case LX_DRM_NOP:
	case LX_DRM_TEST_SLEEP:
		return 0;

	case LX_DRM_SYNCOBJ_WAIT:
		return _syncobj_wait(prv, payload);

	case LX_DRM_SYNCOBJ_TIMELINE_WAIT:
		return _syncobj_timeline_wait(prv, payload);

	case LX_DRM_FD_CLOSE:
		return emul_fd_close(((struct lx_drm_fd_close const *)payload)->fd);

	case LX_DRM_BO_SYNC:
		return _bo_sync(prv, payload);

	case LX_DRM_BO_QUERY_INFO:
		return _bo_query_info(prv, payload);

	case LX_DRM_GROUP_SUBMIT:
		if (flags & LX_DRM_FLAG_SAMPLE_FLUSH_ID)
			_sample_flush_id(prv, payload);
		cmd = DRM_IOCTL_PANTHOR_GROUP_SUBMIT;
		break;

	case LX_DRM_BO_CREATE:
		return _bo_create(prv, payload);

	case LX_DRM_DEV_QUERY:               cmd = DRM_IOCTL_PANTHOR_DEV_QUERY;          break;
	case LX_DRM_VM_CREATE:               cmd = DRM_IOCTL_PANTHOR_VM_CREATE;          break;
	case LX_DRM_VM_DESTROY:              cmd = DRM_IOCTL_PANTHOR_VM_DESTROY;         break;
	case LX_DRM_VM_BIND:                 cmd = DRM_IOCTL_PANTHOR_VM_BIND;            break;
	case LX_DRM_VM_GET_STATE:            cmd = DRM_IOCTL_PANTHOR_VM_GET_STATE;       break;
	case LX_DRM_GROUP_CREATE:            cmd = DRM_IOCTL_PANTHOR_GROUP_CREATE;       break;
	case LX_DRM_GROUP_DESTROY:           cmd = DRM_IOCTL_PANTHOR_GROUP_DESTROY;      break;
	case LX_DRM_GROUP_GET_STATE:         cmd = DRM_IOCTL_PANTHOR_GROUP_GET_STATE;    break;
	case LX_DRM_TILER_HEAP_CREATE:       cmd = DRM_IOCTL_PANTHOR_TILER_HEAP_CREATE;  break;
	case LX_DRM_TILER_HEAP_DESTROY:      cmd = DRM_IOCTL_PANTHOR_TILER_HEAP_DESTROY; break;
	case LX_DRM_BO_SET_LABEL:            cmd = DRM_IOCTL_PANTHOR_BO_SET_LABEL;       break;
	case LX_DRM_GEM_CLOSE:               cmd = DRM_IOCTL_GEM_CLOSE;                  break;
	case LX_DRM_SYNCOBJ_CREATE:          cmd = DRM_IOCTL_SYNCOBJ_CREATE;             break;
	case LX_DRM_SYNCOBJ_DESTROY:         cmd = DRM_IOCTL_SYNCOBJ_DESTROY;            break;
	case LX_DRM_SYNCOBJ_HANDLE_TO_FD:    cmd = DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD;       break;
	case LX_DRM_SYNCOBJ_FD_TO_HANDLE:    cmd = DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE;       break;
	case LX_DRM_SYNCOBJ_RESET:           cmd = DRM_IOCTL_SYNCOBJ_RESET;              break;
	case LX_DRM_SYNCOBJ_SIGNAL:          cmd = DRM_IOCTL_SYNCOBJ_SIGNAL;             break;
	case LX_DRM_SYNCOBJ_QUERY:           cmd = DRM_IOCTL_SYNCOBJ_QUERY;              break;
	case LX_DRM_SYNCOBJ_TRANSFER:        cmd = DRM_IOCTL_SYNCOBJ_TRANSFER;           break;
	case LX_DRM_SYNCOBJ_TIMELINE_SIGNAL: cmd = DRM_IOCTL_SYNCOBJ_TIMELINE_SIGNAL;    break;
	}

	if (!cmd)
		return -ENOSYS;

	return drm_ioctl(prv->file, cmd, arg);
}


void lx_drm_cancel(struct task_struct *task)
{
	set_tsk_thread_flag(task, TIF_SIGPENDING);

	/* not 'wake_up_process': the caller is no task of the emulation */
	lx_emul_task_unblock(task);
}


void lx_drm_cancel_done(void)
{
	clear_tsk_thread_flag(current, TIF_SIGPENDING);
}
