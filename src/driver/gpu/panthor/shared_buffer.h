/*
 * \brief  Backing store of the GEM objects: access for the Gpu session
 * \author Ivan Loskutov
 * \date   2026-10-08
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _SHARED_BUFFER_H_
#define _SHARED_BUFFER_H_

#include <dataspace/capability.h>

struct genode_shared_dataspace;

/**
 * Dataspace of a GEM object's backing store
 */
Genode::Dataspace_capability
genode_shared_dataspace_capability(struct genode_shared_dataspace *);

#endif /* _SHARED_BUFFER_H_ */
