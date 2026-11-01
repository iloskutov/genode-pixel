/*
 * \brief  Backing store of the GEM objects: DMA buffers, uncached by default
 * \author Ivan Loskutov
 * \date   2026-10-07
 *
 * Replaces dde_linux's 'shared_dma_buffer.cc' (cached buffers). panthor
 * maps its kernel buffer objects write-combined ('vmap' with
 * 'pgprot_writecombine') and shares some of them with the firmware without
 * any synchronization point: the request and acknowledge words of the
 * firmware interface. With a cached mapping the firmware does not see the
 * driver's requests (first device runs: the firmware boots, but the first
 * ping 12 s later is never answered).
 *
 * The buffer objects of the clients (handed out by
 * 'Gpu::Session::map_cpu') are uncached as well by default (class
 * EMUL_BUFFER_CLIENT): correct without any cache maintenance, slow to
 * read. A client that creates an object with the flag WB_MMAP gets cached
 * memory for it (class EMUL_BUFFER_CLIENT_CACHED) and must clean the cache
 * before the GPU reads and invalidate it before the CPU reads what the GPU
 * wrote, itself or with the request type BO_SYNC ('Info::bo_cached',
 * include/gpu/panthor_session.h). The
 * class is a property of the single object: the kernel's objects never
 * become cached.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <lx_kit/env.h>
#include <lx_kit/dma_buffer.h>
#include <lx_emul/alloc.h>
#include <lx_emul/page_virt.h>

#include "emul.h"
#include "shared_buffer.h"

struct genode_shared_dataspace : Lx_kit::Dma_buffer {};


static bool _cached(enum emul_buffer_class buffer_class)
{
	switch (buffer_class) {
	case EMUL_BUFFER_KERNEL:        return false;   /* shared with the firmware */
	case EMUL_BUFFER_CLIENT:        return false;   /* no cache maintenance */
	case EMUL_BUFFER_CLIENT_CACHED: return true;    /* maintained by the client */
	}
	return false;
}


static Lx_kit::Mem_allocator &_allocator(enum emul_buffer_class buffer_class)
{
	return _cached(buffer_class) ? Lx_kit::env().memory
	                             : Lx_kit::env().uncached_memory;
}


extern "C" int emul_buffer_cached(enum emul_buffer_class buffer_class)
{
	return _cached(buffer_class);
}


extern "C" struct genode_shared_dataspace *
emul_buffer_alloc(unsigned long size, enum emul_buffer_class buffer_class)
{
	/*
	 * The memory comes from the driver's own quota (Dma session of the
	 * platform driver): a client that asks for more than is left gets
	 * ENOMEM instead of taking the driver down.
	 */
	try {
		Lx_kit::Mem_allocator::Buffer &b =
			_allocator(buffer_class).alloc_buffer(size);

		lx_emul_add_page_range((void *)b.virt_addr(), b.size());

		/*
		 * A fresh dataspace is zeroed by core through a cached mapping.
		 * The GPU must see the zeros (not what the memory held before),
		 * and the client's first cache operation must not find anything:
		 * what Linux does when it maps the pages of an object for the
		 * device.
		 */
		if (_cached(buffer_class))
			lx_emul_mem_cache_clean_invalidate((void *)b.virt_addr(), b.size());

		return static_cast<genode_shared_dataspace*>(&b);

	} catch (...) {
		Genode::error("allocation of a ", size, "-byte GEM buffer failed");
		return nullptr;
	}
}


extern "C" void
emul_buffer_free(struct genode_shared_dataspace *ds,
                 enum emul_buffer_class buffer_class)
{
	lx_emul_remove_page_range((void *)ds->virt_addr(), ds->size());
	_allocator(buffer_class).free_buffer((void*)ds->virt_addr());
}


extern "C" void *
emul_buffer_virt_addr(struct genode_shared_dataspace *ds)
{
	return (void *)ds->virt_addr();
}


Genode::Dataspace_capability
genode_shared_dataspace_capability(struct genode_shared_dataspace *ds)
{
	return ds->cap();
}
