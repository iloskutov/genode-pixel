/*
 * \brief  Linux emulation environment specific to the panthor GPU driver
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
#include "emul.h"


/**************
 ** Firmware **
 **************/

#include <linux/firmware.h>
#include <linux/slab.h>

int request_firmware(const struct firmware **firmware_p, const char *name,
                     struct device *device)
{
	struct firmware *fw;
	void const      *data = NULL;
	unsigned long    size = 0;

	if (!firmware_p)
		return -EINVAL;

	*firmware_p = NULL;

	fw = kzalloc(sizeof(*fw), GFP_KERNEL);
	if (!fw)
		return -ENOMEM;

	if (emul_firmware_request(name, &data, &size)) {
		kfree(fw);
		return -ENOENT;
	}

	fw->data = data;
	fw->size = size;

	*firmware_p = fw;
	return 0;
}


void release_firmware(const struct firmware *fw)
{
	if (!fw)
		return;

	emul_firmware_release(fw->data);
	kfree(fw);
}


/*********
 ** DRM **
 *********/

#include <linux/fs.h>

/*
 * The file operations of the DRM character device, the entry of the Gpu
 * sessions (lx_drm.c)
 */
struct file_operations const *lx_drm_fops;

int __register_chrdev(unsigned int major, unsigned int baseminor,
                      unsigned int count, char const *name,
                      struct file_operations const *fops)
{
	lx_drm_fops = fops;
	return 0;
}


/***********************************************
 ** Backing store of the GEM objects (shmem) **
 ***********************************************/

/*
 * Local variant of dde_linux's 'lx_emul/shmem_file.h': one buffer per
 * object, never swapped. Differences: the buffer comes from
 * 'emul_buffer_alloc' with the class the creating task announced
 * ('emul_shmem_class_for_current'), and 'fput' knows the other kinds of
 * files of this driver (sync files, syncobj files).
 */

#include <linux/file.h>
#include <linux/pagevec.h>
#include <linux/shmem_fs.h>
#include <lx_emul/page_virt.h>

/* marks the files created by 'shmem_file_setup' */
static struct file_operations const _shmem_fops;

static struct task_struct     *_shmem_class_task;
static enum emul_buffer_class  _shmem_class = EMUL_BUFFER_KERNEL;


void emul_shmem_class_for_current(enum emul_buffer_class buffer_class)
{
	_shmem_class_task = (buffer_class == EMUL_BUFFER_KERNEL) ? NULL : current;
	_shmem_class      = buffer_class;
}


struct file *shmem_file_setup(char const *name, loff_t size,
                              unsigned long flags)
{
	struct file              *f;
	struct inode             *inode;
	struct address_space     *mapping;
	struct emul_shmem_buffer *buffer;
	loff_t const nrpages = DIV_ROUND_UP(size, PAGE_SIZE);

	if (!size)
		return (struct file *)ERR_PTR(-EINVAL);

	f = kzalloc(sizeof(struct file), GFP_KERNEL);
	if (!f)
		return (struct file *)ERR_PTR(-ENOMEM);

	inode = kzalloc(sizeof(struct inode), GFP_KERNEL);
	if (!inode)
		goto err_inode;

	mapping = kzalloc(sizeof(struct address_space), GFP_KERNEL);
	if (!mapping)
		goto err_mapping;

	buffer = kzalloc(sizeof(struct emul_shmem_buffer), GFP_KERNEL);
	if (!buffer)
		goto err_buffer;

	buffer->buffer_class = (_shmem_class_task == current) ? _shmem_class
	                                                      : EMUL_BUFFER_KERNEL;

	buffer->dataspace = emul_buffer_alloc(nrpages * PAGE_SIZE, buffer->buffer_class);
	if (!buffer->dataspace)
		goto err_dataspace;

	buffer->addr  = emul_buffer_virt_addr(buffer->dataspace);
	buffer->pages = lx_emul_virt_to_page(buffer->addr);

	mapping->i_private_data = buffer;
	mapping->nrpages        = nrpages;

	inode->i_mapping = mapping;

	file_ref_init(&f->f_ref, 1);
	f->f_op      = &_shmem_fops;
	f->f_inode   = inode;
	f->f_mapping = mapping;
	f->f_flags   = flags;
	f->f_mode    = OPEN_FMODE(flags);
	f->f_mode   |= FMODE_OPENED;

	return f;

err_dataspace:
	kfree(buffer);
err_buffer:
	kfree(mapping);
err_mapping:
	kfree(inode);
err_inode:
	kfree(f);
	return (struct file *)ERR_PTR(-ENOMEM);
}


struct folio *shmem_read_folio_gfp(struct address_space *mapping,
                                   pgoff_t index, gfp_t gfp)
{
	struct emul_shmem_buffer *buffer;

	if (index >= mapping->nrpages)
		return (struct folio *)ERR_PTR(-EINVAL);

	buffer = mapping->i_private_data;

	return (struct folio *)(buffer->pages + index);
}


void __folio_batch_release(struct folio_batch *fbatch)
{
	folio_batch_reinit(fbatch);
}


static void _free_shmem_file(struct file *file)
{
	struct address_space *mapping = file->f_mapping;

	if (mapping) {
		struct emul_shmem_buffer *buffer = mapping->i_private_data;

		emul_buffer_free(buffer->dataspace, buffer->buffer_class);

		kfree(buffer);
		kfree(mapping);
	}

	kfree(file->f_inode);
	kfree(file);
}


/******************
 ** Pseudo files **
 ******************/

#include <linux/anon_inodes.h>
#include <linux/mount.h>

/* called by the initcall of dma-buf */
struct vfsmount *kern_mount(struct file_system_type *type)
{
	struct vfsmount *m = kzalloc(sizeof(struct vfsmount), GFP_KERNEL);

	return m ? m : (struct vfsmount *)ERR_PTR(-ENOMEM);
}


/*
 * The only inodes are the ones of 'alloc_anon_inode' (the DRM device's
 * address space, dma-buf), plain allocations of lx_emul/shadow/fs/libfs.c.
 * Called when a DRM device is released, e.g., after a failed probe.
 */
void iput(struct inode *inode)
{
	kfree(inode);
}


/*
 * Files without a file system behind them: sync files ('sync_file_create')
 * and the files of exported syncobjs. All share one inode, as in Linux.
 */
struct file *anon_inode_getfile(char const *name,
                                struct file_operations const *fops,
                                void *priv, int flags)
{
	static struct inode anon_inode;

	struct file *file = kzalloc(sizeof(struct file), GFP_KERNEL);
	if (!file)
		return (struct file *)ERR_PTR(-ENOMEM);

	file_ref_init(&file->f_ref, 1);
	file->f_op         = fops;
	file->f_inode      = &anon_inode;
	file->private_data = priv;
	file->f_flags      = flags & (O_ACCMODE | O_NONBLOCK);
	file->f_mode       = OPEN_FMODE(flags) | FMODE_OPENED;

	return file;
}


/* called by 'file_ref_put' when the counter dropped below zero */
bool __file_ref_put(file_ref_t *ref, unsigned long cnt)
{
	return cnt == FILE_REF_NOREF;
}


void fput(struct file *file)
{
	if (!file)
		return;

	if (!file_ref_put(&file->f_ref))
		return;

	if (file->f_op == &_shmem_fops) {
		_free_shmem_file(file);
		return;
	}

	/* pseudo file: its release function drops what the file refers to */
	if (file->f_op && file->f_op->release)
		(void)file->f_op->release(file->f_inode, file);

	kfree(file);
}


/**********************
 ** File descriptors **
 **********************/

/*
 * Each Gpu session has a descriptor table, bound to the session's task via
 * the otherwise unused 'task_struct::files'. Other tasks (workers) have no
 * table and cannot create descriptors. The numbers start at 3 and mean
 * nothing outside of the driver.
 */

#include <linux/fdtable.h>

enum { FD_BASE = 3 };

/* slot handed out by 'get_unused_fd_flags' but not installed yet */
#define FD_RESERVED ((struct file *)1)


static struct emul_fd_table *_fd_table(void)
{
	return (struct emul_fd_table *)current->files;
}


static struct file **_fd_slot(unsigned int fd)
{
	struct emul_fd_table *table = _fd_table();

	if (!table || fd < FD_BASE || fd >= FD_BASE + EMUL_FD_MAX)
		return NULL;

	return &table->file[fd - FD_BASE];
}


void emul_fd_table_attach(struct emul_fd_table *table)
{
	current->files = (struct files_struct *)table;
}


void emul_fd_table_release(struct emul_fd_table *table)
{
	unsigned i;

	for (i = 0; i < EMUL_FD_MAX; i++) {
		struct file *file = table->file[i];

		table->file[i] = NULL;

		if (file && file != FD_RESERVED)
			fput(file);
	}

	if (_fd_table() == table)
		current->files = NULL;
}


int emul_fd_close(int fd)
{
	struct file **slot = (fd < 0) ? NULL : _fd_slot(fd);
	struct file  *file;

	if (!slot || !*slot || *slot == FD_RESERVED)
		return -EBADF;

	file  = *slot;
	*slot = NULL;

	fput(file);
	return 0;
}


int get_unused_fd_flags(unsigned flags)
{
	struct emul_fd_table *table = _fd_table();
	unsigned i;

	if (!table)
		return -EMFILE;

	for (i = 0; i < EMUL_FD_MAX; i++) {
		if (!table->file[i]) {
			table->file[i] = FD_RESERVED;
			return FD_BASE + i;
		}
	}
	return -EMFILE;
}


void put_unused_fd(unsigned int fd)
{
	struct file **slot = _fd_slot(fd);

	if (slot && *slot == FD_RESERVED)
		*slot = NULL;
}


/* takes over the caller's reference to 'file' */
void fd_install(unsigned int fd, struct file *file)
{
	struct file **slot = _fd_slot(fd);

	if (!slot || *slot != FD_RESERVED) {
		printk("fd_install: descriptor %u was not reserved\n", fd);
		fput(file);
		return;
	}

	*slot = file;
}


struct file *fget(unsigned int fd)
{
	struct file **slot = _fd_slot(fd);

	if (!slot || !*slot || *slot == FD_RESERVED)
		return NULL;

	return get_file(*slot);
}


struct fd fdget(unsigned int fd)
{
	struct file *file = fget(fd);

	if (!file)
		return EMPTY_FD;

	/* 'fdput' drops the reference */
	return (struct fd){ (unsigned long)file | FDPUT_FPUT };
}


/**************
 ** IRQ work **
 **************/

/*
 * Used by dma-fence-array and dma-fence-chain to leave the context of a
 * fence callback (the lock of the signalling fence is held) before they
 * signal their own fence or install the next callback. There is no
 * interrupt to raise in the emulation: the works are run by a task.
 */

#include <linux/irq_work.h>
#include <linux/llist.h>
#include <linux/sched/task.h>

static LLIST_HEAD(_irq_works);
static struct task_struct *_irq_work_task;


bool irq_work_queue(struct irq_work *work)
{
	int const oflags = atomic_fetch_or(IRQ_WORK_CLAIMED | CSD_TYPE_IRQ_WORK,
	                                   &work->node.a_flags);

	/* already queued, the pending run covers this request */
	if (oflags & IRQ_WORK_PENDING)
		return false;

	llist_add(&work->node.llist, &_irq_works);

	if (_irq_work_task)
		wake_up_process(_irq_work_task);

	return true;
}


static int _irq_work_task_func(void *arg)
{
	for (;;) {
		struct llist_node *list;
		struct irq_work   *work, *next;

		set_current_state(TASK_INTERRUPTIBLE);

		if (llist_empty(&_irq_works))
			schedule();

		__set_current_state(TASK_RUNNING);

		/* 'llist_add' puts the newest entry first: restore the order */
		{
			struct llist_node *newest_first = llist_del_all(&_irq_works);

			list = NULL;
			while (newest_first) {
				struct llist_node *node = newest_first;

				newest_first = node->next;
				node->next   = list;
				list         = node;
			}
		}

		llist_for_each_entry_safe(work, next, list, node.llist) {

			/* as 'irq_work_single' of kernel/irq_work.c */
			int flags = atomic_read(&work->node.a_flags);

			flags &= ~IRQ_WORK_PENDING;
			atomic_set(&work->node.a_flags, flags);

			work->func(work);

			(void)atomic_cmpxchg(&work->node.a_flags, flags,
			                     flags & ~IRQ_WORK_BUSY);
		}
	}
	return 0;
}


void emul_irq_work_init(void)
{
	int const pid = kernel_thread(_irq_work_task_func, NULL, "irq_work",
	                              CLONE_FS | CLONE_FILES);

	_irq_work_task = find_task_by_pid_ns(pid, NULL);

	if (!_irq_work_task)
		printk("could not start the irq_work task\n");
}


/************************
 ** Tasks and user data **
 ************************/

#include <linux/pid.h>

/*
 * The tasks of the emulation have no 'struct pid' ('task_tgid' is NULL):
 * the only object that can arrive here is the static 'init_struct_pid'.
 */
void put_pid(struct pid *pid)
{
	if (pid)
		refcount_dec(&pid->count);
}


pid_t __task_pid_nr_ns(struct task_struct *task, enum pid_type type,
                       struct pid_namespace *ns)
{
	return task ? task->pid : 0;
}


#include <linux/uaccess.h>
#include <linux/string.h>

/* 1 if the range is zeroed, 0 if not ('copy_struct_from_user') */
int check_zeroed_user(const void __user *from, size_t size)
{
	return memchr_inv((void const *)from, 0, size) ? 0 : 1;
}


/* length including the terminating null ('strndup_user') */
long strnlen_user(const char __user *str, long count)
{
	return (long)strnlen((char const *)str, count) + 1;
}


/************
 ** Memory **
 ************/

#include <asm/pgtable.h>

/*
 * The cache attributes of a mapping cannot be changed: kernel mappings of
 * GEM objects stay cached. The driver flushes what the GPU must see
 * (dma_sync_sgtable_for_device).
 */
pgprot_t pgprot_writecombine(pgprot_t prot)
{
	return prot;
}


pgprot_t pgprot_noncached(pgprot_t prot)
{
	return prot;
}


#include <linux/vmalloc.h>
#include <linux/mm.h>

/*
 * The pages of a GEM object are one dataspace (lx_emul/shmem_file.h), i.e.,
 * contiguous in the local address space: 'vmap' returns the address of the
 * first page and refuses anything else.
 */
void *vmap(struct page **pages, unsigned int count, unsigned long flags,
           pgprot_t prot)
{
	unsigned int i;
	void *addr;

	if (!pages || !count)
		return NULL;

	addr = page_address(pages[0]);

	for (i = 1; i < count; i++) {
		if (page_address(pages[i]) != addr + i * PAGE_SIZE) {
			printk("vmap: pages are not contiguous (count=%u)\n", count);
			return NULL;
		}
	}
	return addr;
}


void vunmap(const void *addr) { }


#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <lx_emul/alloc.h>

int dma_map_sgtable(struct device *dev, struct sg_table *sgt,
                    enum dma_data_direction dir, unsigned long attrs)
{
	int nents = dma_map_sg_attrs(dev, sgt->sgl, sgt->orig_nents, dir, attrs);

	if (nents <= 0)
		return -EIO;

	sgt->nents = nents;
	return 0;
}


void __dma_sync_sg_for_device(struct device *dev, struct scatterlist *sgl,
                              int nelems, enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nelems, i)
		lx_emul_mem_cache_clean_invalidate(sg_virt(sg), sg->length);
}
