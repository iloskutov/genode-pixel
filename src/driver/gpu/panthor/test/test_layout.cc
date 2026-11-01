/*
 * \brief  Host test: the protocol's structures against the Linux uAPI
 * \author Ivan Loskutov
 * \date   2026-10-08
 *
 * The payload of a message is handed to 'drm_ioctl' as it is. This test
 * compares size and member offsets of every mirror structure of
 * include/gpu/panthor_session.h with the structure of the Linux tree the
 * driver is built from, and the constants that have a counterpart.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <cstddef>
#include <cstdint>

/* Linux uAPI (contrib/linux-<hash>/src/linux/include/uapi/drm) */
#include <drm.h>
#include <panthor_drm.h>

#include <gpu/panthor_session.h>

#include "check.h"

using namespace Gpu::Panthor;

#define SAME_SIZE(mirror, linux_type) \
	CHECK_EQ(sizeof(mirror), sizeof(struct linux_type))

#define SAME_MEMBER(mirror, linux_type, member) do { \
	CHECK_EQ(offsetof(mirror, member), offsetof(struct linux_type, member)); \
	CHECK_EQ(sizeof(((mirror *)nullptr)->member), \
	         sizeof(((struct linux_type *)nullptr)->member)); } while (0)


static void test_panthor_structs()
{
	SAME_SIZE  (Obj_array, drm_panthor_obj_array);
	SAME_MEMBER(Obj_array, drm_panthor_obj_array, stride);
	SAME_MEMBER(Obj_array, drm_panthor_obj_array, count);
	SAME_MEMBER(Obj_array, drm_panthor_obj_array, array);

	SAME_SIZE  (Sync_op, drm_panthor_sync_op);
	SAME_MEMBER(Sync_op, drm_panthor_sync_op, flags);
	SAME_MEMBER(Sync_op, drm_panthor_sync_op, handle);
	SAME_MEMBER(Sync_op, drm_panthor_sync_op, timeline_value);

	SAME_SIZE  (Gpu_info, drm_panthor_gpu_info);
	SAME_MEMBER(Gpu_info, drm_panthor_gpu_info, gpu_id);
	SAME_MEMBER(Gpu_info, drm_panthor_gpu_info, gpu_rev);
	SAME_MEMBER(Gpu_info, drm_panthor_gpu_info, csf_id);
	SAME_MEMBER(Gpu_info, drm_panthor_gpu_info, l2_features);
	SAME_MEMBER(Gpu_info, drm_panthor_gpu_info, tiler_features);
	SAME_MEMBER(Gpu_info, drm_panthor_gpu_info, mem_features);
	SAME_MEMBER(Gpu_info, drm_panthor_gpu_info, mmu_features);
	SAME_MEMBER(Gpu_info, drm_panthor_gpu_info, thread_features);
	SAME_MEMBER(Gpu_info, drm_panthor_gpu_info, max_threads);
	SAME_MEMBER(Gpu_info, drm_panthor_gpu_info, thread_max_workgroup_size);
	SAME_MEMBER(Gpu_info, drm_panthor_gpu_info, thread_max_barrier_size);
	SAME_MEMBER(Gpu_info, drm_panthor_gpu_info, coherency_features);
	SAME_MEMBER(Gpu_info, drm_panthor_gpu_info, texture_features);
	SAME_MEMBER(Gpu_info, drm_panthor_gpu_info, as_present);
	SAME_MEMBER(Gpu_info, drm_panthor_gpu_info, pad0);
	SAME_MEMBER(Gpu_info, drm_panthor_gpu_info, shader_present);
	SAME_MEMBER(Gpu_info, drm_panthor_gpu_info, l2_present);
	SAME_MEMBER(Gpu_info, drm_panthor_gpu_info, tiler_present);
	SAME_MEMBER(Gpu_info, drm_panthor_gpu_info, core_features);
	SAME_MEMBER(Gpu_info, drm_panthor_gpu_info, pad);
	SAME_MEMBER(Gpu_info, drm_panthor_gpu_info, gpu_features);

	SAME_SIZE  (Csif_info, drm_panthor_csif_info);
	SAME_MEMBER(Csif_info, drm_panthor_csif_info, csg_slot_count);
	SAME_MEMBER(Csif_info, drm_panthor_csif_info, cs_slot_count);
	SAME_MEMBER(Csif_info, drm_panthor_csif_info, cs_reg_count);
	SAME_MEMBER(Csif_info, drm_panthor_csif_info, scoreboard_slot_count);
	SAME_MEMBER(Csif_info, drm_panthor_csif_info, unpreserved_cs_reg_count);
	SAME_MEMBER(Csif_info, drm_panthor_csif_info, pad);

	SAME_SIZE  (Timestamp_info, drm_panthor_timestamp_info);
	SAME_MEMBER(Timestamp_info, drm_panthor_timestamp_info, timestamp_frequency);
	SAME_MEMBER(Timestamp_info, drm_panthor_timestamp_info, current_timestamp);
	SAME_MEMBER(Timestamp_info, drm_panthor_timestamp_info, timestamp_offset);

	SAME_SIZE  (Group_priorities_info, drm_panthor_group_priorities_info);
	SAME_MEMBER(Group_priorities_info, drm_panthor_group_priorities_info, allowed_mask);
	SAME_MEMBER(Group_priorities_info, drm_panthor_group_priorities_info, pad);

	SAME_SIZE  (Dev_query, drm_panthor_dev_query);
	SAME_MEMBER(Dev_query, drm_panthor_dev_query, type);
	SAME_MEMBER(Dev_query, drm_panthor_dev_query, size);
	SAME_MEMBER(Dev_query, drm_panthor_dev_query, pointer);

	SAME_SIZE  (Vm_create, drm_panthor_vm_create);
	SAME_MEMBER(Vm_create, drm_panthor_vm_create, flags);
	SAME_MEMBER(Vm_create, drm_panthor_vm_create, id);
	SAME_MEMBER(Vm_create, drm_panthor_vm_create, user_va_range);

	SAME_SIZE  (Vm_destroy, drm_panthor_vm_destroy);
	SAME_MEMBER(Vm_destroy, drm_panthor_vm_destroy, id);
	SAME_MEMBER(Vm_destroy, drm_panthor_vm_destroy, pad);

	SAME_SIZE  (Vm_bind_op, drm_panthor_vm_bind_op);
	SAME_MEMBER(Vm_bind_op, drm_panthor_vm_bind_op, flags);
	SAME_MEMBER(Vm_bind_op, drm_panthor_vm_bind_op, bo_handle);
	SAME_MEMBER(Vm_bind_op, drm_panthor_vm_bind_op, bo_offset);
	SAME_MEMBER(Vm_bind_op, drm_panthor_vm_bind_op, va);
	SAME_MEMBER(Vm_bind_op, drm_panthor_vm_bind_op, size);
	SAME_MEMBER(Vm_bind_op, drm_panthor_vm_bind_op, syncs);

	SAME_SIZE  (Vm_bind, drm_panthor_vm_bind);
	SAME_MEMBER(Vm_bind, drm_panthor_vm_bind, vm_id);
	SAME_MEMBER(Vm_bind, drm_panthor_vm_bind, flags);
	SAME_MEMBER(Vm_bind, drm_panthor_vm_bind, ops);

	SAME_SIZE  (Vm_get_state, drm_panthor_vm_get_state);
	SAME_MEMBER(Vm_get_state, drm_panthor_vm_get_state, vm_id);
	SAME_MEMBER(Vm_get_state, drm_panthor_vm_get_state, state);

	SAME_SIZE  (Bo_create, drm_panthor_bo_create);
	SAME_MEMBER(Bo_create, drm_panthor_bo_create, size);
	SAME_MEMBER(Bo_create, drm_panthor_bo_create, flags);
	SAME_MEMBER(Bo_create, drm_panthor_bo_create, exclusive_vm_id);
	SAME_MEMBER(Bo_create, drm_panthor_bo_create, handle);
	SAME_MEMBER(Bo_create, drm_panthor_bo_create, pad);

	SAME_SIZE  (Queue_create, drm_panthor_queue_create);
	SAME_MEMBER(Queue_create, drm_panthor_queue_create, priority);
	SAME_MEMBER(Queue_create, drm_panthor_queue_create, pad);
	SAME_MEMBER(Queue_create, drm_panthor_queue_create, ringbuf_size);

	SAME_SIZE  (Group_create, drm_panthor_group_create);
	SAME_MEMBER(Group_create, drm_panthor_group_create, queues);
	SAME_MEMBER(Group_create, drm_panthor_group_create, max_compute_cores);
	SAME_MEMBER(Group_create, drm_panthor_group_create, max_fragment_cores);
	SAME_MEMBER(Group_create, drm_panthor_group_create, max_tiler_cores);
	SAME_MEMBER(Group_create, drm_panthor_group_create, priority);
	SAME_MEMBER(Group_create, drm_panthor_group_create, pad);
	SAME_MEMBER(Group_create, drm_panthor_group_create, compute_core_mask);
	SAME_MEMBER(Group_create, drm_panthor_group_create, fragment_core_mask);
	SAME_MEMBER(Group_create, drm_panthor_group_create, tiler_core_mask);
	SAME_MEMBER(Group_create, drm_panthor_group_create, vm_id);
	SAME_MEMBER(Group_create, drm_panthor_group_create, group_handle);

	SAME_SIZE  (Group_destroy, drm_panthor_group_destroy);
	SAME_MEMBER(Group_destroy, drm_panthor_group_destroy, group_handle);
	SAME_MEMBER(Group_destroy, drm_panthor_group_destroy, pad);

	SAME_SIZE  (Queue_submit, drm_panthor_queue_submit);
	SAME_MEMBER(Queue_submit, drm_panthor_queue_submit, queue_index);
	SAME_MEMBER(Queue_submit, drm_panthor_queue_submit, stream_size);
	SAME_MEMBER(Queue_submit, drm_panthor_queue_submit, stream_addr);
	SAME_MEMBER(Queue_submit, drm_panthor_queue_submit, latest_flush);
	SAME_MEMBER(Queue_submit, drm_panthor_queue_submit, pad);
	SAME_MEMBER(Queue_submit, drm_panthor_queue_submit, syncs);

	SAME_SIZE  (Group_submit, drm_panthor_group_submit);
	SAME_MEMBER(Group_submit, drm_panthor_group_submit, group_handle);
	SAME_MEMBER(Group_submit, drm_panthor_group_submit, pad);
	SAME_MEMBER(Group_submit, drm_panthor_group_submit, queue_submits);

	SAME_SIZE  (Group_get_state, drm_panthor_group_get_state);
	SAME_MEMBER(Group_get_state, drm_panthor_group_get_state, group_handle);
	SAME_MEMBER(Group_get_state, drm_panthor_group_get_state, state);
	SAME_MEMBER(Group_get_state, drm_panthor_group_get_state, fatal_queues);
	SAME_MEMBER(Group_get_state, drm_panthor_group_get_state, pad);

	SAME_SIZE  (Tiler_heap_create, drm_panthor_tiler_heap_create);
	SAME_MEMBER(Tiler_heap_create, drm_panthor_tiler_heap_create, vm_id);
	SAME_MEMBER(Tiler_heap_create, drm_panthor_tiler_heap_create, initial_chunk_count);
	SAME_MEMBER(Tiler_heap_create, drm_panthor_tiler_heap_create, chunk_size);
	SAME_MEMBER(Tiler_heap_create, drm_panthor_tiler_heap_create, max_chunks);
	SAME_MEMBER(Tiler_heap_create, drm_panthor_tiler_heap_create, target_in_flight);
	SAME_MEMBER(Tiler_heap_create, drm_panthor_tiler_heap_create, handle);
	SAME_MEMBER(Tiler_heap_create, drm_panthor_tiler_heap_create, tiler_heap_ctx_gpu_va);
	SAME_MEMBER(Tiler_heap_create, drm_panthor_tiler_heap_create, first_heap_chunk_gpu_va);

	SAME_SIZE  (Tiler_heap_destroy, drm_panthor_tiler_heap_destroy);
	SAME_MEMBER(Tiler_heap_destroy, drm_panthor_tiler_heap_destroy, handle);
	SAME_MEMBER(Tiler_heap_destroy, drm_panthor_tiler_heap_destroy, pad);

	SAME_SIZE  (Bo_set_label, drm_panthor_bo_set_label);
	SAME_MEMBER(Bo_set_label, drm_panthor_bo_set_label, handle);
	SAME_MEMBER(Bo_set_label, drm_panthor_bo_set_label, pad);
	SAME_MEMBER(Bo_set_label, drm_panthor_bo_set_label, label);
}


static void test_drm_structs()
{
	SAME_SIZE  (Gem_close, drm_gem_close);
	SAME_MEMBER(Gem_close, drm_gem_close, handle);
	SAME_MEMBER(Gem_close, drm_gem_close, pad);

	SAME_SIZE  (Syncobj_create, drm_syncobj_create);
	SAME_MEMBER(Syncobj_create, drm_syncobj_create, handle);
	SAME_MEMBER(Syncobj_create, drm_syncobj_create, flags);

	SAME_SIZE  (Syncobj_destroy, drm_syncobj_destroy);
	SAME_MEMBER(Syncobj_destroy, drm_syncobj_destroy, handle);
	SAME_MEMBER(Syncobj_destroy, drm_syncobj_destroy, pad);

	SAME_SIZE  (Syncobj_handle, drm_syncobj_handle);
	SAME_MEMBER(Syncobj_handle, drm_syncobj_handle, handle);
	SAME_MEMBER(Syncobj_handle, drm_syncobj_handle, flags);
	SAME_MEMBER(Syncobj_handle, drm_syncobj_handle, fd);
	SAME_MEMBER(Syncobj_handle, drm_syncobj_handle, pad);
	SAME_MEMBER(Syncobj_handle, drm_syncobj_handle, point);

	SAME_SIZE  (Syncobj_transfer, drm_syncobj_transfer);
	SAME_MEMBER(Syncobj_transfer, drm_syncobj_transfer, src_handle);
	SAME_MEMBER(Syncobj_transfer, drm_syncobj_transfer, dst_handle);
	SAME_MEMBER(Syncobj_transfer, drm_syncobj_transfer, src_point);
	SAME_MEMBER(Syncobj_transfer, drm_syncobj_transfer, dst_point);
	SAME_MEMBER(Syncobj_transfer, drm_syncobj_transfer, flags);
	SAME_MEMBER(Syncobj_transfer, drm_syncobj_transfer, pad);

	SAME_SIZE  (Syncobj_wait, drm_syncobj_wait);
	SAME_MEMBER(Syncobj_wait, drm_syncobj_wait, handles);
	SAME_MEMBER(Syncobj_wait, drm_syncobj_wait, timeout_nsec);
	SAME_MEMBER(Syncobj_wait, drm_syncobj_wait, count_handles);
	SAME_MEMBER(Syncobj_wait, drm_syncobj_wait, flags);
	SAME_MEMBER(Syncobj_wait, drm_syncobj_wait, first_signaled);
	SAME_MEMBER(Syncobj_wait, drm_syncobj_wait, pad);
	SAME_MEMBER(Syncobj_wait, drm_syncobj_wait, deadline_nsec);

	SAME_SIZE  (Syncobj_timeline_wait, drm_syncobj_timeline_wait);
	SAME_MEMBER(Syncobj_timeline_wait, drm_syncobj_timeline_wait, handles);
	SAME_MEMBER(Syncobj_timeline_wait, drm_syncobj_timeline_wait, points);
	SAME_MEMBER(Syncobj_timeline_wait, drm_syncobj_timeline_wait, timeout_nsec);
	SAME_MEMBER(Syncobj_timeline_wait, drm_syncobj_timeline_wait, count_handles);
	SAME_MEMBER(Syncobj_timeline_wait, drm_syncobj_timeline_wait, flags);
	SAME_MEMBER(Syncobj_timeline_wait, drm_syncobj_timeline_wait, first_signaled);
	SAME_MEMBER(Syncobj_timeline_wait, drm_syncobj_timeline_wait, pad);
	SAME_MEMBER(Syncobj_timeline_wait, drm_syncobj_timeline_wait, deadline_nsec);

	SAME_SIZE  (Syncobj_array, drm_syncobj_array);
	SAME_MEMBER(Syncobj_array, drm_syncobj_array, handles);
	SAME_MEMBER(Syncobj_array, drm_syncobj_array, count_handles);
	SAME_MEMBER(Syncobj_array, drm_syncobj_array, pad);

	SAME_SIZE  (Syncobj_timeline_array, drm_syncobj_timeline_array);
	SAME_MEMBER(Syncobj_timeline_array, drm_syncobj_timeline_array, handles);
	SAME_MEMBER(Syncobj_timeline_array, drm_syncobj_timeline_array, points);
	SAME_MEMBER(Syncobj_timeline_array, drm_syncobj_timeline_array, count_handles);
	SAME_MEMBER(Syncobj_timeline_array, drm_syncobj_timeline_array, flags);
}


static void test_constants()
{
	/* request type = 0x100 + number of the panthor ioctl */
	CHECK_EQ((unsigned)Op::DEV_QUERY,          0x100u + DRM_PANTHOR_DEV_QUERY);
	CHECK_EQ((unsigned)Op::VM_CREATE,          0x100u + DRM_PANTHOR_VM_CREATE);
	CHECK_EQ((unsigned)Op::VM_DESTROY,         0x100u + DRM_PANTHOR_VM_DESTROY);
	CHECK_EQ((unsigned)Op::VM_BIND,            0x100u + DRM_PANTHOR_VM_BIND);
	CHECK_EQ((unsigned)Op::VM_GET_STATE,       0x100u + DRM_PANTHOR_VM_GET_STATE);
	CHECK_EQ((unsigned)Op::BO_CREATE,          0x100u + DRM_PANTHOR_BO_CREATE);
	CHECK_EQ((unsigned)Op::GROUP_CREATE,       0x100u + DRM_PANTHOR_GROUP_CREATE);
	CHECK_EQ((unsigned)Op::GROUP_DESTROY,      0x100u + DRM_PANTHOR_GROUP_DESTROY);
	CHECK_EQ((unsigned)Op::GROUP_SUBMIT,       0x100u + DRM_PANTHOR_GROUP_SUBMIT);
	CHECK_EQ((unsigned)Op::GROUP_GET_STATE,    0x100u + DRM_PANTHOR_GROUP_GET_STATE);
	CHECK_EQ((unsigned)Op::TILER_HEAP_CREATE,  0x100u + DRM_PANTHOR_TILER_HEAP_CREATE);
	CHECK_EQ((unsigned)Op::TILER_HEAP_DESTROY, 0x100u + DRM_PANTHOR_TILER_HEAP_DESTROY);
	CHECK_EQ((unsigned)Op::BO_SET_LABEL,       0x100u + DRM_PANTHOR_BO_SET_LABEL);

	CHECK_EQ((uint32_t)DEV_QUERY_GPU_INFO,       (uint32_t)DRM_PANTHOR_DEV_QUERY_GPU_INFO);
	CHECK_EQ((uint32_t)DEV_QUERY_CSIF_INFO,      (uint32_t)DRM_PANTHOR_DEV_QUERY_CSIF_INFO);
	CHECK_EQ((uint32_t)DEV_QUERY_TIMESTAMP_INFO, (uint32_t)DRM_PANTHOR_DEV_QUERY_TIMESTAMP_INFO);
	CHECK_EQ((uint32_t)DEV_QUERY_GROUP_PRIORITIES_INFO,
	         (uint32_t)DRM_PANTHOR_DEV_QUERY_GROUP_PRIORITIES_INFO);

	CHECK_EQ((uint32_t)SYNC_OP_HANDLE_TYPE_SYNCOBJ,
	         (uint32_t)DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_SYNCOBJ);
	CHECK_EQ((uint32_t)SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ,
	         (uint32_t)DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ);
	CHECK_EQ((uint32_t)SYNC_OP_WAIT,   (uint32_t)DRM_PANTHOR_SYNC_OP_WAIT);
	CHECK_EQ((uint32_t)SYNC_OP_SIGNAL, (uint32_t)DRM_PANTHOR_SYNC_OP_SIGNAL);

	CHECK_EQ((uint32_t)VM_BIND_OP_MAP_READONLY,   (uint32_t)DRM_PANTHOR_VM_BIND_OP_MAP_READONLY);
	CHECK_EQ((uint32_t)VM_BIND_OP_MAP_NOEXEC,     (uint32_t)DRM_PANTHOR_VM_BIND_OP_MAP_NOEXEC);
	CHECK_EQ((uint32_t)VM_BIND_OP_MAP_UNCACHED,   (uint32_t)DRM_PANTHOR_VM_BIND_OP_MAP_UNCACHED);
	CHECK_EQ((uint32_t)VM_BIND_OP_TYPE_MAP,       (uint32_t)DRM_PANTHOR_VM_BIND_OP_TYPE_MAP);
	CHECK_EQ((uint32_t)VM_BIND_OP_TYPE_UNMAP,     (uint32_t)DRM_PANTHOR_VM_BIND_OP_TYPE_UNMAP);
	CHECK_EQ((uint32_t)VM_BIND_OP_TYPE_SYNC_ONLY, (uint32_t)DRM_PANTHOR_VM_BIND_OP_TYPE_SYNC_ONLY);
	CHECK_EQ((uint32_t)VM_BIND_ASYNC,             (uint32_t)DRM_PANTHOR_VM_BIND_ASYNC);

	CHECK_EQ((uint32_t)VM_STATE_USABLE,   (uint32_t)DRM_PANTHOR_VM_STATE_USABLE);
	CHECK_EQ((uint32_t)VM_STATE_UNUSABLE, (uint32_t)DRM_PANTHOR_VM_STATE_UNUSABLE);
	CHECK_EQ((uint32_t)BO_NO_MMAP,        (uint32_t)DRM_PANTHOR_BO_NO_MMAP);

	CHECK_EQ((uint32_t)GROUP_PRIORITY_LOW,      (uint32_t)PANTHOR_GROUP_PRIORITY_LOW);
	CHECK_EQ((uint32_t)GROUP_PRIORITY_MEDIUM,   (uint32_t)PANTHOR_GROUP_PRIORITY_MEDIUM);
	CHECK_EQ((uint32_t)GROUP_PRIORITY_HIGH,     (uint32_t)PANTHOR_GROUP_PRIORITY_HIGH);
	CHECK_EQ((uint32_t)GROUP_PRIORITY_REALTIME, (uint32_t)PANTHOR_GROUP_PRIORITY_REALTIME);
	CHECK_EQ((uint32_t)GROUP_STATE_TIMEDOUT,    (uint32_t)DRM_PANTHOR_GROUP_STATE_TIMEDOUT);
	CHECK_EQ((uint32_t)GROUP_STATE_FATAL_FAULT, (uint32_t)DRM_PANTHOR_GROUP_STATE_FATAL_FAULT);
	CHECK_EQ((uint32_t)GROUP_STATE_INNOCENT,    (uint32_t)DRM_PANTHOR_GROUP_STATE_INNOCENT);

	CHECK_EQ((uint32_t)SYNCOBJ_CREATE_SIGNALED, (uint32_t)DRM_SYNCOBJ_CREATE_SIGNALED);
	CHECK_EQ((uint32_t)SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE,
	         (uint32_t)DRM_SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE);
	CHECK_EQ((uint32_t)SYNCOBJ_FD_TO_HANDLE_FLAGS_TIMELINE,
	         (uint32_t)DRM_SYNCOBJ_FD_TO_HANDLE_FLAGS_TIMELINE);
	CHECK_EQ((uint32_t)SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE,
	         (uint32_t)DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE);
	CHECK_EQ((uint32_t)SYNCOBJ_HANDLE_TO_FD_FLAGS_TIMELINE,
	         (uint32_t)DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_TIMELINE);
	CHECK_EQ((uint32_t)SYNCOBJ_WAIT_FLAGS_WAIT_ALL,
	         (uint32_t)DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL);
	CHECK_EQ((uint32_t)SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,
	         (uint32_t)DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT);
	CHECK_EQ((uint32_t)SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE,
	         (uint32_t)DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE);
	CHECK_EQ((uint32_t)SYNCOBJ_WAIT_FLAGS_WAIT_DEADLINE,
	         (uint32_t)DRM_SYNCOBJ_WAIT_FLAGS_WAIT_DEADLINE);

	/* errno values named by the protocol (asm-generic) */
	CHECK_EQ((int)LX_ENOENT, 2);   CHECK_EQ((int)LX_EBADF,  9);
	CHECK_EQ((int)LX_ENOMEM, 12);  CHECK_EQ((int)LX_EINVAL, 22);
	CHECK_EQ((int)LX_ENODEV, 19);  CHECK_EQ((int)LX_ENOSYS, 38);
	CHECK_EQ((int)LX_ETIME,  62);  CHECK_EQ((int)LX_EPROTO, 71);
}


int main()
{
	test_panthor_structs();
	test_drm_structs();
	test_constants();

	return check_result("test_layout");
}
