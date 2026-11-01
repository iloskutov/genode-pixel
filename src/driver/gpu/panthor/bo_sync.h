/*
 * \brief  Validation of the buffer-object requests of the session layer
 * \author Ivan Loskutov
 * \date   2026-10-08
 *
 * The checks of the requests the session layer serves itself instead of
 * the kernel driver (panthor 1.5), which has neither the buffer-object
 * flag WB_MMAP nor BO_SYNC: the flags of BO_CREATE and the ranges of
 * BO_SYNC. All values come from a client and are untrusted.
 *
 * Plain C without any dependency, used by lx_drm.c and tested on the host
 * (test/test_bo_sync.cc).
 */

/*
 * Copyright (C) 2026 Ivan Loskutov
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#ifndef _BO_SYNC_H_
#define _BO_SYNC_H_

#ifdef __cplusplus
extern "C" {
#endif

/* 'Gpu::Panthor::BO_NO_MMAP', 'BO_WB_MMAP' (DRM_PANTHOR_BO_*) */
enum {
	BO_SYNC_FLAG_NO_MMAP = 1u << 0,
	BO_SYNC_FLAG_WB_MMAP = 1u << 1,
};

/* 'Gpu::Panthor::MAX_BO_SYNC_BYTES' */
#define BO_SYNC_MAX_BYTES (64ull*1024*1024)

/* number of 'Bo_sync_op::type' values */
enum { BO_SYNC_TYPES = 2 };


/**
 * Check the flags of a BO_CREATE request
 *
 * \param cached_enabled  the driver hands out cached objects
 *
 * \return 1 if the object is to be backed by cached memory,
 *         0 if by uncached memory,
 *         -1 if the request must be refused: WB_MMAP while cached objects
 *         are switched off, or WB_MMAP for an object that is never mapped
 *         (as panthor 1.7). Other flags are left to the kernel driver.
 */
static inline int bo_create_cached(unsigned flags, int cached_enabled)
{
	if (!(flags & BO_SYNC_FLAG_WB_MMAP))
		return 0;

	if (!cached_enabled || (flags & BO_SYNC_FLAG_NO_MMAP))
		return -1;

	return 1;
}


/**
 * Return 1 if [offset, offset + size) lies inside an object of 'obj_size'
 * bytes
 *
 * An empty range is valid at any offset up to the end of the object.
 */
static inline int bo_sync_range_valid(unsigned long long offset,
                                      unsigned long long size,
                                      unsigned long long obj_size)
{
	return offset <= obj_size && size <= obj_size - offset;
}


/**
 * Add the size of a range to the total of a request
 *
 * \return 1 if the total stays within BO_SYNC_MAX_BYTES, else 0 ('total'
 *         is unchanged then)
 */
static inline int bo_sync_account(unsigned long long *total,
                                  unsigned long long size)
{
	if (*total > BO_SYNC_MAX_BYTES || size > BO_SYNC_MAX_BYTES - *total)
		return 0;

	*total += size;
	return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* _BO_SYNC_H_ */
