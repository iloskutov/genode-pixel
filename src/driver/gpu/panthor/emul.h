/*
 * \brief  Backend functions of the panthor GPU driver
 * \author Ivan Loskutov
 * \date   2026-10-07
 */

/*
 * Copyright (C) 2026 Ivan Loskutov
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#ifndef _EMUL_H_
#define _EMUL_H_

#ifdef __cplusplus
extern "C" {
#endif

struct file;
struct page;
struct genode_shared_dataspace;

unsigned long emul_user_copy(void *, void const *, unsigned long);

/**
 * Obtain a firmware image from the ROM named like the last element of 'path'
 *
 * \return 0 on success
 */
int emul_firmware_request(char const *path, void const **data,
                          unsigned long *size);

void emul_firmware_release(void const *data);


/*
 * Configuration of the component (main.cc)
 */

/* config attribute 'test_ops': the request type TEST_SLEEP is served */
int emul_config_test_ops(void);

/*
 * config attribute 'bo_cached' off: no cached buffer objects (BO_CREATE refuses
 * WB_MMAP, BO_SYNC and BO_QUERY_INFO are not served). Default "yes".
 */
int emul_config_bo_cached(void);


/*
 * Backing store of the GEM objects (shared_buffer.cc)
 *
 * The class tells who accesses the memory with the CPU and how it is
 * backed:
 *
 * KERNEL         the driver (and the firmware, without synchronization
 *                points): uncached
 * CLIENT         a client through the dataspace handed out by
 *                'Gpu::Session::map_cpu': uncached, no cache maintenance
 * CLIENT_CACHED  a client that asked for a cached mapping (BO_CREATE with
 *                WB_MMAP) and does the cache maintenance: cached
 */

enum emul_buffer_class {
	EMUL_BUFFER_KERNEL,
	EMUL_BUFFER_CLIENT,
	EMUL_BUFFER_CLIENT_CACHED,
};

struct genode_shared_dataspace *emul_buffer_alloc(unsigned long size,
                                                  enum emul_buffer_class);

void emul_buffer_free(struct genode_shared_dataspace *, enum emul_buffer_class);

void *emul_buffer_virt_addr(struct genode_shared_dataspace *);

/* 1 if the class is backed by cached memory */
int emul_buffer_cached(enum emul_buffer_class);


/*
 * Private data of the address space of a shmem file (lx_emul.c)
 */
struct emul_shmem_buffer
{
	struct genode_shared_dataspace *dataspace;
	void                           *addr;
	struct page                    *pages;
	enum emul_buffer_class          buffer_class;
};

/**
 * Class of the shmem files the calling task creates from now on
 */
void emul_shmem_class_for_current(enum emul_buffer_class);


/*
 * File descriptors (lx_emul.c): one table per Gpu session, bound to the
 * session's task
 */

enum { EMUL_FD_MAX = 64 };

struct emul_fd_table
{
	struct file *file[EMUL_FD_MAX];
};

/* make 'table' the descriptor table of the calling task */
void emul_fd_table_attach(struct emul_fd_table *table);

/* close all descriptors of the calling task's table and detach it */
void emul_fd_table_release(struct emul_fd_table *table);

/* close a descriptor of the calling task, 0 or -EBADF */
int emul_fd_close(int fd);


/* start the task that runs the queued irq works (lx_user.c) */
void emul_irq_work_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _EMUL_H_ */
