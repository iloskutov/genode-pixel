/*
 * \brief  Protocol between the panthor GPU driver and its Gpu-session clients
 * \author Ivan Loskutov
 * \date   2026-10-08
 *
 * The panthor port (src/driver/gpu/panthor) provides Genode's Gpu session.
 * The session interface is too narrow for panthor's uAPI, so that all DRM
 * ioctls but the mapping of a buffer object travel as messages through
 * 'Gpu::Session::execute'. This header is shared by the driver, the test
 * client (src/test/gpu_panthor) and the libdrm back end to come
 * ('ioctl_panthor.cc'). It depends on nothing but fixed-width integers.
 *
 * Use of the Gpu session
 * ----------------------
 *
 * info_dataspace()         'Info' (read-only for the client, written once
 *                          when the session is created)
 * alloc_vram(id, size)     allocates an EXCHANGE BUFFER, plain RAM for
 *                          messages (not a buffer object). 'id' is chosen
 *                          by the client. Up to MAX_EXCHANGE_BUFFERS of at
 *                          most MAX_EXCHANGE_BUFFER_SIZE bytes each. An
 *                          invalid capability is returned on error.
 * free_vram(id)            frees an exchange buffer
 * execute(id, offset)      processes the message that starts at 'offset'
 *                          (a multiple of 8) of exchange buffer 'id' and
 *                          returns its sequence number. One message is in
 *                          flight per session at most. Throws
 *                          'Invalid_state' if the buffer or the offset is
 *                          invalid, or if the previous message is still
 *                          pending.
 * complete(seqno)          true if the message with this sequence number is
 *                          no longer in flight (its response is in the
 *                          buffer)
 * completion_sigh(sigh)    signal for the completion of a message that was
 *                          still pending when 'execute' returned
 * map_cpu(id, attrs)       'id' is the GEM HANDLE of a buffer object
 *                          (Op::BO_CREATE): returns its dataspace. This
 *                          replaces DRM_IOCTL_PANTHOR_BO_MMAP_OFFSET +
 *                          mmap. Invalid capability if the handle is
 *                          unknown, the object was created with
 *                          BO_NO_MMAP, or a message is pending.
 * unmap_cpu(id)            no effect (the client detaches the dataspace;
 *                          the object lives until Op::GEM_CLOSE and the
 *                          end of its GPU mappings)
 * map_gpu, unmap_gpu, set_tiling_gpu, export_vram, import_vram
 *                          not used, fail
 *
 * Message
 * -------
 *
 *   offset 0            Header
 *   offset 32           payload: the argument structure of the ioctl
 *                       (layout of the Linux uAPI, mirrored below)
 *   behind the payload  the data the payload refers to
 *
 * Every pointer member of the uAPI structures (drm_panthor_obj_array.array,
 * drm_panthor_dev_query.pointer, drm_panthor_bo_set_label.label, the
 * 'handles' and 'points' members of the syncobj structures) holds the
 * OFFSET of the data from the start of the message instead of an address.
 * The data must lie inside the message behind the payload, aligned to 8
 * bytes (arrays of 32-bit handles: 4 bytes, strings: none). Offset 0 means
 * a null pointer and is valid where the ioctl accepts one (an array of
 * count 0, a size query, removal of a label).
 *
 * The client sets 'magic', 'version', 'op', 'size', 'flags' and
 * 'state = Header::NEW'. When 'execute' returns, 'state' is either
 *
 *   Header::DONE     the payload and the data carry the ioctl's output,
 *                    'result' is 0 or a negative LINUX errno value (not
 *                    the libc's), 'flush_id' is valid
 *   Header::PENDING  the driver's task blocked (a wait, a lock). The
 *                    client waits for the completion signal (or polls
 *                    'complete') until 'state' is DONE and must not
 *                    execute another message meanwhile.
 *
 * The driver works on a private copy of the message, a client that
 * changes the buffer while the message is pending only loses its response.
 *
 * Differences to the ioctls
 * -------------------------
 *
 * - Time-outs are RELATIVE: 'timeout_nsec' of Op::SYNCOBJ_WAIT and
 *   Op::SYNCOBJ_TIMELINE_WAIT (and 'deadline_nsec' with the flag
 *   WAIT_DEADLINE) count from the moment the driver starts the request.
 *   <= 0 polls, INT64_MAX waits forever. The ioctls take absolute
 *   CLOCK_MONOTONIC values, but client and driver do not share a clock.
 * - File descriptors (Op::SYNCOBJ_HANDLE_TO_FD, Op::SYNCOBJ_FD_TO_HANDLE)
 *   are numbers of a per-session table in the driver, no descriptors of
 *   the client's libc. They are closed with Op::FD_CLOSE or with the
 *   session.
 * - LATEST_FLUSH_ID: the user-MMIO page cannot be mapped. A client either
 *   passes 'latest_flush = 0' in its queue submits (unconditional cache
 *   flush) or sets FLAG_SAMPLE_FLUSH_ID in the header of Op::GROUP_SUBMIT:
 *   the driver then reads the register when it starts the request and
 *   stores the value in every queue submit with a stream. 'flush_id' of a
 *   response is the register's value at completion; it must NOT be used
 *   for a later submit (it is older than the command stream written after
 *   it, which would let the firmware skip a flush that is needed).
 * - Not available: DRM_IOCTL_PANTHOR_BO_MMAP_OFFSET and
 *   SET_USER_MMIO_OFFSET (see above), PRIME (handle <-> fd), the dma-buf
 *   ioctls, DRM_IOCTL_VERSION and DRM_IOCTL_GET_CAP (answered by the
 *   client from 'Info').
 *
 * Cached buffer objects (protocol 1.1)
 * ------------------------------------
 *
 * The GPU is not cache coherent with the CPUs, and the dataspace of a
 * buffer object is uncached memory by default: correct without any cache
 * maintenance, slow to read. If 'Info::bo_cached' is 1, the driver
 * provides what Linux' panthor has since its version 1.7, on top of the
 * kernel driver it is built from:
 *
 * - Op::BO_CREATE accepts the flag BO_WB_MMAP: the dataspace of that
 *   object is cached memory. Objects without the flag stay uncached.
 *   (With 'bo_cached' 0 the flag is refused with EINVAL.)
 * - The client is responsible for the cache maintenance of such an
 *   object: it has to clean the cache for what it wrote before the GPU
 *   reads it, and to clean and invalidate it before it reads (or partly
 *   overwrites) what the GPU wrote, after the GPU job has finished. It
 *   does so either itself (the CPU's cache instructions for its own
 *   mapping; no request needed, the cheap way) or with Op::BO_SYNC, which
 *   lets the driver do it for ranges of objects.
 * - Op::BO_QUERY_INFO returns the flags an object was created with.
 *
 * The driver itself never accesses the content of a buffer object of a
 * client with the CPU, with one exception of the kernel driver: the
 * synchronization object a command stream waits for (SYNC_WAIT
 * instructions) is read through the driver's mapping. Such objects must
 * not live in a cached buffer object.
 *
 * A fresh cached object is zeroed and its cache cleaned and invalidated
 * by the driver.
 *
 * Compatibility: a client of protocol 1.0 does not know the flag, gets
 * uncached objects and works unchanged. A client of 1.1 at a driver of 1.0
 * sees 'bo_cached' 0.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _INCLUDE__GPU__PANTHOR_SESSION_H_
#define _INCLUDE__GPU__PANTHOR_SESSION_H_

#include <base/fixed_stdint.h>

namespace Gpu { namespace Panthor {

	using uint8_t  = genode_uint8_t;
	using uint32_t = genode_uint32_t;
	using int32_t  = genode_int32_t;
	using uint64_t = genode_uint64_t;
	using int64_t  = genode_int64_t;

	enum : uint32_t {
		MAGIC         = 0x52485450,   /* "PTHR" */
		VERSION_MAJOR = 1,            /* incompatible changes */
		VERSION_MINOR = 1,            /* additions */
		VERSION       = (VERSION_MAJOR << 16) | VERSION_MINOR,
	};

	enum : uint32_t {
		MAX_MESSAGE_SIZE         = 64u*1024,
		MAX_EXCHANGE_BUFFER_SIZE = 1024u*1024,
		MAX_EXCHANGE_BUFFERS     = 4,
	};

	/*
	 * Upper bound of the sum of the range sizes of one Op::BO_SYNC: the
	 * driver serves all its clients with one thread
	 */
	enum : uint64_t { MAX_BO_SYNC_BYTES = 64ull*1024*1024 };

	/**
	 * Request types
	 *
	 * 0x100 + n is the panthor ioctl DRM_PANTHOR_<n>, 0x200... are ioctls
	 * of the DRM core, 0x300... exist only in this protocol. The payload
	 * is named behind each type.
	 */
	enum class Op : uint32_t {

		NOP                     = 0,       /* none; the response carries 'flush_id' */

		DEV_QUERY               = 0x100,   /* Dev_query */
		VM_CREATE               = 0x101,   /* Vm_create */
		VM_DESTROY              = 0x102,   /* Vm_destroy */
		VM_BIND                 = 0x103,   /* Vm_bind */
		VM_GET_STATE            = 0x104,   /* Vm_get_state */
		BO_CREATE               = 0x105,   /* Bo_create */
		/* 0x106 BO_MMAP_OFFSET: 'map_cpu' */
		GROUP_CREATE            = 0x107,   /* Group_create */
		GROUP_DESTROY           = 0x108,   /* Group_destroy */
		GROUP_SUBMIT            = 0x109,   /* Group_submit */
		GROUP_GET_STATE         = 0x10a,   /* Group_get_state */
		TILER_HEAP_CREATE       = 0x10b,   /* Tiler_heap_create */
		TILER_HEAP_DESTROY      = 0x10c,   /* Tiler_heap_destroy */
		BO_SET_LABEL            = 0x10d,   /* Bo_set_label */
		/* 0x10e SET_USER_MMIO_OFFSET: not available */

		GEM_CLOSE               = 0x200,   /* Gem_close */
		SYNCOBJ_CREATE          = 0x201,   /* Syncobj_create */
		SYNCOBJ_DESTROY         = 0x202,   /* Syncobj_destroy */
		SYNCOBJ_HANDLE_TO_FD    = 0x203,   /* Syncobj_handle */
		SYNCOBJ_FD_TO_HANDLE    = 0x204,   /* Syncobj_handle */
		SYNCOBJ_WAIT            = 0x205,   /* Syncobj_wait */
		SYNCOBJ_RESET           = 0x206,   /* Syncobj_array */
		SYNCOBJ_SIGNAL          = 0x207,   /* Syncobj_array */
		SYNCOBJ_TIMELINE_WAIT   = 0x208,   /* Syncobj_timeline_wait */
		SYNCOBJ_QUERY           = 0x209,   /* Syncobj_timeline_array */
		SYNCOBJ_TRANSFER        = 0x20a,   /* Syncobj_transfer */
		SYNCOBJ_TIMELINE_SIGNAL = 0x20b,   /* Syncobj_timeline_array */

		FD_CLOSE                = 0x300,   /* Fd_close */
		BO_SYNC                 = 0x301,   /* Bo_sync */
		TEST_SLEEP              = 0x302,   /* Test_sleep, driver config test_ops="yes" */
		BO_QUERY_INFO           = 0x303,   /* Bo_query_info (protocol 1.1) */
	};

	/**
	 * Linux errno values a response may carry in 'Header::result' (negated)
	 *
	 * The numbers differ from the ones of Genode's libc (FreeBSD), a libdrm
	 * back end has to translate them.
	 */
	enum Linux_errno : int32_t {
		LX_ENOENT      = 2,     /* unknown handle */
		LX_EINTR       = 4,
		LX_EBADF       = 9,     /* unknown file descriptor */
		LX_EAGAIN      = 11,
		LX_ENOMEM      = 12,
		LX_EFAULT      = 14,
		LX_EBUSY       = 16,
		LX_ENODEV      = 19,    /* the driver has no GPU (protocol test) */
		LX_EINVAL      = 22,
		LX_EMFILE      = 24,    /* file-descriptor table of the session full */
		LX_ENOSPC      = 28,
		LX_ENOSYS      = 38,    /* request type unknown to the driver */
		LX_ETIME       = 62,    /* syncobj wait timed out */
		LX_EPROTO      = 71,    /* malformed message */
		LX_EOPNOTSUPP  = 95,
		LX_ECANCELED   = 125,
		LX_ERESTARTSYS = 512,   /* wait interrupted (session is closing) */
	};

	struct Header;
	struct Info;
	struct Obj_array;
	struct Sync_op;
	struct Gpu_info;
	struct Csif_info;
	struct Timestamp_info;
	struct Group_priorities_info;
	struct Dev_query;
	struct Vm_create;
	struct Vm_destroy;
	struct Vm_bind_op;
	struct Vm_bind;
	struct Vm_get_state;
	struct Bo_create;
	struct Queue_create;
	struct Group_create;
	struct Group_destroy;
	struct Queue_submit;
	struct Group_submit;
	struct Group_get_state;
	struct Tiler_heap_create;
	struct Tiler_heap_destroy;
	struct Bo_set_label;
	struct Gem_close;
	struct Syncobj_create;
	struct Syncobj_destroy;
	struct Syncobj_handle;
	struct Syncobj_transfer;
	struct Syncobj_wait;
	struct Syncobj_timeline_wait;
	struct Syncobj_array;
	struct Syncobj_timeline_array;
	struct Fd_close;
	struct Bo_sync_op;
	struct Bo_sync;
	struct Test_sleep;
	struct Bo_query_info;

	class Message;

	/* enum drm_panthor_dev_query_type */
	enum : uint32_t {
		DEV_QUERY_GPU_INFO              = 0,
		DEV_QUERY_CSIF_INFO             = 1,
		DEV_QUERY_TIMESTAMP_INFO        = 2,
		DEV_QUERY_GROUP_PRIORITIES_INFO = 3,
	};

	/* enum drm_panthor_sync_op_flags */
	enum : uint32_t {
		SYNC_OP_HANDLE_TYPE_SYNCOBJ          = 0,
		SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ = 1,
		SYNC_OP_WAIT                         = 0,
		SYNC_OP_SIGNAL                       = 1u << 31,
	};

	/* enum drm_panthor_vm_bind_op_flags, drm_panthor_vm_bind_flags */
	enum : uint32_t {
		VM_BIND_OP_MAP_READONLY   = 1u << 0,
		VM_BIND_OP_MAP_NOEXEC     = 1u << 1,
		VM_BIND_OP_MAP_UNCACHED   = 1u << 2,
		VM_BIND_OP_TYPE_MAP       = 0u << 28,
		VM_BIND_OP_TYPE_UNMAP     = 1u << 28,
		VM_BIND_OP_TYPE_SYNC_ONLY = 2u << 28,
		VM_BIND_ASYNC             = 1u << 0,
	};

	/* enum drm_panthor_vm_state, drm_panthor_bo_flags, group priority and state */
	enum : uint32_t {
		VM_STATE_USABLE         = 0,
		VM_STATE_UNUSABLE       = 1,
		BO_NO_MMAP              = 1u << 0,
		BO_WB_MMAP              = 1u << 1,   /* panthor 1.7, see 'Info::bo_cached' */
		GROUP_PRIORITY_LOW      = 0,
		GROUP_PRIORITY_MEDIUM   = 1,
		GROUP_PRIORITY_HIGH     = 2,
		GROUP_PRIORITY_REALTIME = 3,
		GROUP_STATE_TIMEDOUT    = 1u << 0,
		GROUP_STATE_FATAL_FAULT = 1u << 1,
		GROUP_STATE_INNOCENT    = 1u << 2,
	};

	/* flags of the DRM core's syncobj ioctls */
	enum : uint32_t {
		SYNCOBJ_CREATE_SIGNALED                     = 1u << 0,
		SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE = 1u << 0,
		SYNCOBJ_FD_TO_HANDLE_FLAGS_TIMELINE         = 1u << 1,
		SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE = 1u << 0,
		SYNCOBJ_HANDLE_TO_FD_FLAGS_TIMELINE         = 1u << 1,
		SYNCOBJ_WAIT_FLAGS_WAIT_ALL                 = 1u << 0,
		SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT          = 1u << 1,
		SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE           = 1u << 2,
		SYNCOBJ_WAIT_FLAGS_WAIT_DEADLINE            = 1u << 3,
	};

	/* 'Bo_sync_op::type', enum drm_panthor_bo_sync_op_type (panthor 1.7) */
	enum : uint32_t {
		BO_SYNC_CPU_CACHE_FLUSH                = 0,
		BO_SYNC_CPU_CACHE_FLUSH_AND_INVALIDATE = 1,
	};

	/* 'Bo_query_info::extra_flags', enum drm_panthor_bo_extra_flags (panthor 1.7) */
	enum : uint32_t { BO_IS_IMPORTED = 1u << 0 };

	/*
	 * enum drm_panthor_gpu_coherency (panthor 1.7), the value of
	 * 'drm_panthor_gpu_info::selected_coherency' ('Gpu_info::pad0' here)
	 * that a client of 'bo_cached' objects has to assume
	 */
	enum : uint32_t { GPU_COHERENCY_NONE = 31 };

} /* namespace Panthor */ } /* namespace Gpu */


struct Gpu::Panthor::Header
{
	enum State : uint32_t { NEW = 0, PENDING = 1, DONE = 2 };

	enum Flags : uint32_t {

		/*
		 * Op::GROUP_SUBMIT: the driver stores the current LATEST_FLUSH_ID
		 * in every queue submit that has a command stream
		 */
		FLAG_SAMPLE_FLUSH_ID = 1u << 0,

		KNOWN_FLAGS = FLAG_SAMPLE_FLUSH_ID,
	};

	uint32_t magic;      /* in:  MAGIC */
	uint32_t version;    /* in:  VERSION of the client */
	uint32_t op;         /* in:  Op */
	uint32_t size;       /* in:  header + payload + data, in bytes */
	uint32_t flags;      /* in:  Flags, unknown bits are refused */
	uint32_t state;      /* out: State */
	int32_t  result;     /* out: 0 or negative Linux errno */
	uint32_t flush_id;   /* out: LATEST_FLUSH_ID at completion */
};


/*
 * Mirrors of the uAPI structures (Linux 6.18, panthor 1.5:
 * include/uapi/drm/panthor_drm.h, include/uapi/drm/drm.h). The layouts are
 * compared with the kernel's headers by the host test
 * src/driver/gpu/panthor/test and the sizes by the driver at start.
 */

struct Gpu::Panthor::Obj_array      /* drm_panthor_obj_array */
{
	uint32_t stride;   /* size of one element */
	uint32_t count;
	uint64_t array;    /* offset of the first element in the message */
};


struct Gpu::Panthor::Sync_op        /* drm_panthor_sync_op */
{
	uint32_t flags;
	uint32_t handle;
	uint64_t timeline_value;
};


struct Gpu::Panthor::Gpu_info       /* drm_panthor_gpu_info */
{
	uint32_t gpu_id;
	uint32_t gpu_rev;
	uint32_t csf_id;
	uint32_t l2_features;
	uint32_t tiler_features;
	uint32_t mem_features;
	uint32_t mmu_features;
	uint32_t thread_features;
	uint32_t max_threads;
	uint32_t thread_max_workgroup_size;
	uint32_t thread_max_barrier_size;
	uint32_t coherency_features;
	uint32_t texture_features[4];
	uint32_t as_present;
	uint32_t pad0;                /* panthor 1.7: selected_coherency */
	uint64_t shader_present;
	uint64_t l2_present;
	uint64_t tiler_present;
	uint32_t core_features;
	uint32_t pad;
	uint64_t gpu_features;
};


struct Gpu::Panthor::Csif_info      /* drm_panthor_csif_info */
{
	uint32_t csg_slot_count;
	uint32_t cs_slot_count;
	uint32_t cs_reg_count;
	uint32_t scoreboard_slot_count;
	uint32_t unpreserved_cs_reg_count;
	uint32_t pad;
};


struct Gpu::Panthor::Timestamp_info /* drm_panthor_timestamp_info */
{
	uint64_t timestamp_frequency;
	uint64_t current_timestamp;
	uint64_t timestamp_offset;
};


struct Gpu::Panthor::Group_priorities_info /* drm_panthor_group_priorities_info */
{
	uint8_t allowed_mask;
	uint8_t pad[3];
};


struct Gpu::Panthor::Dev_query      /* drm_panthor_dev_query */
{
	uint32_t type;
	uint32_t size;      /* size of the result buffer */
	uint64_t pointer;   /* offset of the result buffer, 0: query the size */
};


struct Gpu::Panthor::Vm_create      /* drm_panthor_vm_create */
{
	uint32_t flags;
	uint32_t id;              /* out */
	uint64_t user_va_range;   /* in/out */
};


struct Gpu::Panthor::Vm_destroy     /* drm_panthor_vm_destroy */
{
	uint32_t id;
	uint32_t pad;
};


struct Gpu::Panthor::Vm_bind_op     /* drm_panthor_vm_bind_op */
{
	uint32_t  flags;
	uint32_t  bo_handle;
	uint64_t  bo_offset;
	uint64_t  va;
	uint64_t  size;
	Obj_array syncs;   /* Sync_op, only with VM_BIND_ASYNC */
};


struct Gpu::Panthor::Vm_bind        /* drm_panthor_vm_bind */
{
	uint32_t  vm_id;
	uint32_t  flags;
	Obj_array ops;     /* Vm_bind_op */
};


struct Gpu::Panthor::Vm_get_state   /* drm_panthor_vm_get_state */
{
	uint32_t vm_id;
	uint32_t state;   /* out */
};


struct Gpu::Panthor::Bo_create      /* drm_panthor_bo_create */
{
	uint64_t size;              /* in/out */
	uint32_t flags;
	uint32_t exclusive_vm_id;
	uint32_t handle;            /* out, the 'Vram_id' for 'map_cpu' */
	uint32_t pad;
};


struct Gpu::Panthor::Queue_create   /* drm_panthor_queue_create */
{
	uint8_t  priority;
	uint8_t  pad[3];
	uint32_t ringbuf_size;
};


struct Gpu::Panthor::Group_create   /* drm_panthor_group_create */
{
	Obj_array queues;   /* Queue_create */
	uint8_t   max_compute_cores;
	uint8_t   max_fragment_cores;
	uint8_t   max_tiler_cores;
	uint8_t   priority;
	uint32_t  pad;
	uint64_t  compute_core_mask;
	uint64_t  fragment_core_mask;
	uint64_t  tiler_core_mask;
	uint32_t  vm_id;
	uint32_t  group_handle;   /* out */
};


struct Gpu::Panthor::Group_destroy  /* drm_panthor_group_destroy */
{
	uint32_t group_handle;
	uint32_t pad;
};


struct Gpu::Panthor::Queue_submit   /* drm_panthor_queue_submit */
{
	uint32_t  queue_index;
	uint32_t  stream_size;
	uint64_t  stream_addr;
	uint32_t  latest_flush;
	uint32_t  pad;
	Obj_array syncs;   /* Sync_op */
};


struct Gpu::Panthor::Group_submit   /* drm_panthor_group_submit */
{
	uint32_t  group_handle;
	uint32_t  pad;
	Obj_array queue_submits;   /* Queue_submit */
};


struct Gpu::Panthor::Group_get_state /* drm_panthor_group_get_state */
{
	uint32_t group_handle;
	uint32_t state;          /* out */
	uint32_t fatal_queues;   /* out */
	uint32_t pad;
};


struct Gpu::Panthor::Tiler_heap_create /* drm_panthor_tiler_heap_create */
{
	uint32_t vm_id;
	uint32_t initial_chunk_count;
	uint32_t chunk_size;
	uint32_t max_chunks;
	uint32_t target_in_flight;
	uint32_t handle;                    /* out */
	uint64_t tiler_heap_ctx_gpu_va;     /* out */
	uint64_t first_heap_chunk_gpu_va;   /* out */
};


struct Gpu::Panthor::Tiler_heap_destroy /* drm_panthor_tiler_heap_destroy */
{
	uint32_t handle;
	uint32_t pad;
};


struct Gpu::Panthor::Bo_set_label   /* drm_panthor_bo_set_label */
{
	uint32_t handle;
	uint32_t pad;
	uint64_t label;   /* offset of a null-terminated string, 0: no label */
};


struct Gpu::Panthor::Gem_close      /* drm_gem_close */
{
	uint32_t handle;
	uint32_t pad;
};


struct Gpu::Panthor::Syncobj_create /* drm_syncobj_create */
{
	uint32_t handle;   /* out */
	uint32_t flags;
};


struct Gpu::Panthor::Syncobj_destroy /* drm_syncobj_destroy */
{
	uint32_t handle;
	uint32_t pad;
};


struct Gpu::Panthor::Syncobj_handle /* drm_syncobj_handle */
{
	uint32_t handle;
	uint32_t flags;
	int32_t  fd;      /* descriptor of the session's table in the driver */
	uint32_t pad;
	uint64_t point;
};


struct Gpu::Panthor::Syncobj_transfer /* drm_syncobj_transfer */
{
	uint32_t src_handle;
	uint32_t dst_handle;
	uint64_t src_point;
	uint64_t dst_point;
	uint32_t flags;
	uint32_t pad;
};


struct Gpu::Panthor::Syncobj_wait   /* drm_syncobj_wait */
{
	uint64_t handles;          /* offset of 'count_handles' uint32_t */
	int64_t  timeout_nsec;     /* RELATIVE */
	uint32_t count_handles;
	uint32_t flags;
	uint32_t first_signaled;   /* out */
	uint32_t pad;
	uint64_t deadline_nsec;    /* RELATIVE, with WAIT_DEADLINE */
};


struct Gpu::Panthor::Syncobj_timeline_wait /* drm_syncobj_timeline_wait */
{
	uint64_t handles;          /* offset of 'count_handles' uint32_t */
	uint64_t points;           /* offset of 'count_handles' uint64_t */
	int64_t  timeout_nsec;     /* RELATIVE */
	uint32_t count_handles;
	uint32_t flags;
	uint32_t first_signaled;   /* out */
	uint32_t pad;
	uint64_t deadline_nsec;    /* RELATIVE, with WAIT_DEADLINE */
};


struct Gpu::Panthor::Syncobj_array  /* drm_syncobj_array */
{
	uint64_t handles;          /* offset of 'count_handles' uint32_t */
	uint32_t count_handles;
	uint32_t pad;
};


struct Gpu::Panthor::Syncobj_timeline_array /* drm_syncobj_timeline_array */
{
	uint64_t handles;          /* offset of 'count_handles' uint32_t */
	uint64_t points;           /* offset of 'count_handles' uint64_t (QUERY: out) */
	uint32_t count_handles;
	uint32_t flags;
};


/*
 * Structures of this protocol
 */

struct Gpu::Panthor::Fd_close
{
	int32_t  fd;
	uint32_t pad;
};


/*
 * Layout of drm_panthor_bo_sync_op / drm_panthor_bo_sync (panthor 1.7).
 * Cache maintenance for ranges of buffer objects, done by the driver:
 * BO_SYNC_CPU_CACHE_FLUSH writes the CPU's cached data back to memory,
 * ..._FLUSH_AND_INVALIDATE in addition drops the cached copy (the driver
 * may do the latter for both types). A range must lie inside its object
 * ('offset + size' <= size of the object, else EINVAL; nothing is
 * clamped), the sizes of one request must not sum up to more than
 * MAX_BO_SYNC_BYTES (EINVAL). A range of an object that was created
 * without BO_WB_MMAP is checked and otherwise ignored. The request is
 * validated as a whole before anything is done.
 */
struct Gpu::Panthor::Bo_sync_op
{
	uint32_t handle;
	uint32_t type;
	uint64_t offset;
	uint64_t size;
};


struct Gpu::Panthor::Bo_sync
{
	Obj_array ops;   /* Bo_sync_op */
};


struct Gpu::Panthor::Test_sleep
{
	uint32_t ms;    /* the driver's task sleeps that long, at most 10000 */
	uint32_t pad;
};


struct Gpu::Panthor::Bo_query_info  /* drm_panthor_bo_query_info (panthor 1.7) */
{
	uint32_t handle;
	uint32_t extra_flags;    /* out: BO_IS_IMPORTED */
	uint32_t create_flags;   /* out: BO_NO_MMAP, BO_WB_MMAP */
	uint32_t pad;            /* must be 0 */
};


/**
 * Content of the session's info dataspace
 */
struct Gpu::Panthor::Info
{
	uint32_t magic;     /* MAGIC */
	uint32_t version;   /* VERSION of the driver */

	/* 1: the GPU is behind the session, always the case */
	uint32_t device;

	/* 1: the requests of the DRM core (0x200...0x2ff, FD_CLOSE) are served,
	 * always the case */
	uint32_t drm_core;

	/* DRM_IOCTL_VERSION: name "panthor" */
	uint32_t drm_version_major;
	uint32_t drm_version_minor;
	uint32_t drm_version_patch;

	/*
	 * 0: the dataspaces of all buffer objects are uncached memory, no
	 *    cache maintenance is needed (and reading is slow). BO_WB_MMAP is
	 *    refused.
	 * 1: Op::BO_CREATE accepts BO_WB_MMAP (cached dataspace, the client
	 *    does the cache maintenance), Op::BO_SYNC and Op::BO_QUERY_INFO
	 *    are served: the buffer-object interface of panthor 1.7. See
	 *    "Cached buffer objects" at the top. Driver config
	 *    bo_cached="no" switches it off.
	 */
	uint32_t bo_cached;

	uint32_t max_message_size;
	uint32_t max_exchange_buffer_size;
	uint32_t max_exchange_buffers;

	/* DRM_IOCTL_GET_CAP: DRM_CAP_SYNCOBJ, DRM_CAP_SYNCOBJ_TIMELINE */
	uint32_t cap_syncobj;
	uint32_t cap_syncobj_timeline;
	uint32_t pad;

	Gpu_info  gpu;    /* DEV_QUERY_GPU_INFO at session creation */
	Csif_info csif;   /* DEV_QUERY_CSIF_INFO */
};


/**
 * Utility for composing a message in an exchange buffer
 *
 *   Message msg { buffer_addr, buffer_size, Op::VM_BIND };
 *   Vm_bind    &bind = msg.payload<Vm_bind>();
 *   Vm_bind_op *ops  = msg.append<Vm_bind_op>(n);
 *   if (!ops) ...message does not fit...
 *   bind.ops = msg.obj_array(ops, n);
 *   if (!msg.finish()) ...message does not fit...
 *
 * 'payload' must be called before 'append'. All memory handed out is
 * zeroed. 'append' and 'append_string' return a null pointer if the data
 * does not fit; the message is unusable from then on ('finish' fails).
 * 'payload' and 'header' never fail: without room they refer to a scratch
 * area inside of the object that is large enough for any payload.
 */
class Gpu::Panthor::Message
{
	private:

		enum : uint64_t { MAX_PAYLOAD = 64 };

		char * const _base;
		uint64_t     _capacity;
		uint64_t     _used { sizeof(Header) };
		bool         _overflow { false };

		/* stands in for header and payload after an overflow */
		alignas(8) char _scratch[MAX_PAYLOAD] { };

		/**
		 * \return null pointer if 'size' bytes do not fit
		 */
		void *_alloc(uint64_t size, uint64_t align)
		{
			if (_overflow)
				return nullptr;

			uint64_t const start = (_used + align - 1) & ~(align - 1);

			if (start > _capacity || size > _capacity - start) {
				_overflow = true;
				return nullptr;
			}

			for (uint64_t i = _used; i < start + size; i++)
				_base[i] = 0;

			_used = start + size;
			return _base + start;
		}

	public:

		Message(void *base, uint64_t capacity, Op op, uint32_t flags = 0)
		:
			_base((char *)base),
			_capacity(capacity < (uint64_t)MAX_MESSAGE_SIZE ? capacity
			                                               : (uint64_t)MAX_MESSAGE_SIZE)
		{
			static_assert(sizeof(Header) <= MAX_PAYLOAD);

			if (!_base || _capacity < sizeof(Header))
				_overflow = true;

			header() = Header { .magic    = MAGIC,
			                    .version  = VERSION,
			                    .op       = (uint32_t)op,
			                    .size     = 0,
			                    .flags    = flags,
			                    .state    = Header::NEW,
			                    .result   = 0,
			                    .flush_id = 0 };
		}

		Header &header()
		{
			return _overflow ? *(Header *)_scratch : *(Header *)_base;
		}

		/**
		 * Reserve the payload
		 */
		template <typename T>
		T &payload()
		{
			static_assert(sizeof(T) <= MAX_PAYLOAD && alignof(T) <= 8);

			if (void * const ptr = _alloc(sizeof(T), 8))
				return *(T *)ptr;

			for (char &c : _scratch) c = 0;
			return *(T *)_scratch;
		}

		/**
		 * Reserve 'count' elements behind the payload
		 *
		 * \return null pointer if the elements do not fit
		 */
		template <typename T>
		T *append(uint64_t count, uint64_t align = 8)
		{
			/* decide before any size is computed: no wrap of sizeof(T)*count */
			if (count > MAX_MESSAGE_SIZE/sizeof(T)) {
				_overflow = true;
				return nullptr;
			}

			return (T *)_alloc(sizeof(T)*count, align);
		}

		/**
		 * Append a null-terminated string
		 *
		 * \return null pointer if the string does not fit
		 */
		char *append_string(char const *s)
		{
			uint64_t len = 0;
			while (s[len] && len < MAX_MESSAGE_SIZE) len++;

			char * const dst = append<char>(len + 1, 1);
			if (!dst)
				return nullptr;

			for (uint64_t i = 0; i < len; i++) dst[i] = s[i];

			return dst;
		}

		/**
		 * Value of a pointer member that refers to 'ptr' (0 for nullptr)
		 */
		uint64_t offset(void const *ptr) const
		{
			return (ptr && !_overflow) ? (uint64_t)((char const *)ptr - _base) : 0;
		}

		template <typename T>
		Obj_array obj_array(T const *first, uint32_t count) const
		{
			return { .stride = sizeof(T),
			         .count  = count,
			         .array  = count ? offset(first) : 0 };
		}

		/**
		 * Set the size of the message
		 *
		 * \return false if the message did not fit into the buffer
		 */
		bool finish()
		{
			if (_overflow)
				return false;

			header().size = (uint32_t)_used;
			return true;
		}

		uint64_t size() const { return _used; }
};

#endif /* _INCLUDE__GPU__PANTHOR_SESSION_H_ */
