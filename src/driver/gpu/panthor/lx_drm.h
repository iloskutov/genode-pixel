/*
 * \brief  Interface between the Gpu session and the DRM side of the driver
 * \author Ivan Loskutov
 * \date   2026-10-08
 *
 * All functions but 'lx_drm_cancel' and 'lx_drm_payload_size' must be
 * called by the Linux task of the session.
 */

/*
 * Copyright (C) 2026 Ivan Loskutov
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#ifndef _LX_DRM_H_
#define _LX_DRM_H_

#ifdef __cplusplus
extern "C" {
#endif

struct task_struct;
struct genode_shared_dataspace;

/*
 * Request types, the values of 'Gpu::Panthor::Op'
 * (include/gpu/panthor_session.h; compared at compile time in session.cc)
 */
enum lx_drm_op {
	LX_DRM_NOP                     = 0,
	LX_DRM_DEV_QUERY               = 0x100,
	LX_DRM_VM_CREATE               = 0x101,
	LX_DRM_VM_DESTROY              = 0x102,
	LX_DRM_VM_BIND                 = 0x103,
	LX_DRM_VM_GET_STATE            = 0x104,
	LX_DRM_BO_CREATE               = 0x105,
	LX_DRM_GROUP_CREATE            = 0x107,
	LX_DRM_GROUP_DESTROY           = 0x108,
	LX_DRM_GROUP_SUBMIT            = 0x109,
	LX_DRM_GROUP_GET_STATE         = 0x10a,
	LX_DRM_TILER_HEAP_CREATE       = 0x10b,
	LX_DRM_TILER_HEAP_DESTROY      = 0x10c,
	LX_DRM_BO_SET_LABEL            = 0x10d,
	LX_DRM_GEM_CLOSE               = 0x200,
	LX_DRM_SYNCOBJ_CREATE          = 0x201,
	LX_DRM_SYNCOBJ_DESTROY         = 0x202,
	LX_DRM_SYNCOBJ_HANDLE_TO_FD    = 0x203,
	LX_DRM_SYNCOBJ_FD_TO_HANDLE    = 0x204,
	LX_DRM_SYNCOBJ_WAIT            = 0x205,
	LX_DRM_SYNCOBJ_RESET           = 0x206,
	LX_DRM_SYNCOBJ_SIGNAL          = 0x207,
	LX_DRM_SYNCOBJ_TIMELINE_WAIT   = 0x208,
	LX_DRM_SYNCOBJ_QUERY           = 0x209,
	LX_DRM_SYNCOBJ_TRANSFER        = 0x20a,
	LX_DRM_SYNCOBJ_TIMELINE_SIGNAL = 0x20b,
	LX_DRM_FD_CLOSE                = 0x300,
	LX_DRM_BO_SYNC                 = 0x301,
	LX_DRM_TEST_SLEEP              = 0x302,
	LX_DRM_BO_QUERY_INFO           = 0x303,
};

/* 'Gpu::Panthor::Header::FLAG_SAMPLE_FLUSH_ID' */
enum { LX_DRM_FLAG_SAMPLE_FLUSH_ID = 1 };

/**
 * Open the DRM device for the calling task
 *
 * \return session handle, NULL if there is no DRM device
 */
void *lx_drm_open(void);

/**
 * Close the session's file descriptors and its DRM file
 */
void lx_drm_close(void *drm);

/**
 * Size of the argument structure the kernel expects for a request type
 *
 * \return ~0u for an unknown type
 */
unsigned lx_drm_payload_size(unsigned op);

/**
 * Execute a request
 *
 * \param drm      session handle, NULL without DRM device
 * \param payload  argument structure, its pointer members are valid
 *                 addresses of memory that stays in place until the
 *                 function returns
 *
 * \return 0 or negative errno
 */
int lx_drm_tunnel(void *drm, unsigned op, unsigned flags, void *payload);

/**
 * Current value of the GPU's LATEST_FLUSH_ID register
 */
unsigned lx_drm_flush_id(void *drm);

/**
 * Driver version and device information for the session's info dataspace
 *
 * \param gpu_info   buffer for 'struct drm_panthor_gpu_info'
 * \param csif_info  buffer for 'struct drm_panthor_csif_info'
 *
 * Without GPU only the version is provided.
 *
 * \return 0 on success
 */
int lx_drm_info(void *drm, unsigned *major, unsigned *minor, unsigned *patch,
                void *gpu_info, unsigned gpu_info_size,
                void *csif_info, unsigned csif_info_size);

/**
 * Backing store of a buffer object that may be mapped by the client
 *
 * \return NULL if the handle is unknown or the object must not be mapped
 */
struct genode_shared_dataspace *lx_drm_bo_dataspace(void *drm, unsigned handle,
                                                    unsigned long *size);

/**
 * Make a blocking request of 'task' return with -ERESTARTSYS
 *
 * Covers the interruptible waits (syncobj waits). Called from outside of
 * the task; the caller has to run the scheduler afterwards.
 */
void lx_drm_cancel(struct task_struct *task);

/**
 * Take back 'lx_drm_cancel' (called by the task once its request returned)
 */
void lx_drm_cancel_done(void);

#ifdef __cplusplus
}
#endif

#endif /* _LX_DRM_H_ */
