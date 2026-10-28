/*
 * \brief  Genode block service provider C-API with a device block size
 * \author Stefan Kalkowski
 * \author Ivan Loskutov
 * \date   2026-10-28
 *
 * The C-API of Genode announces a device with 512-byte blocks only. This
 * file compiles the C-API from the Genode source tree unchanged and adds a
 * variant of 'genode_block_announce_device' with the block size of the
 * device, derived from the following file:
 *
 *   repos/os/src/lib/genode_c_api/block.cc
 */

/*
 * Copyright (C) 2006-2021 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU Affero General Public
 * License version 3, see the file LICENSE.AGPL.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#include "block.cc"


extern "C" void
genode_block_announce_device_with_block_size(const char *       name,
                                             unsigned long long blocks,
                                             unsigned           block_size_log2)
{
	if (!_block_root)
		return;

	/* writeable, as by 'genode_block_announce_device' */
	_block_root->announce_device(name, { 1UL << block_size_log2,
	                             blocks, uint8_t(block_size_log2), true });
}
