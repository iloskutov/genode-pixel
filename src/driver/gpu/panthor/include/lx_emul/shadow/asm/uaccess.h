/*
 * \brief  Shadow copy of asm/uaccess.h
 * \author Ivan Loskutov
 * \date   2026-10-07
 *
 * The arguments of the DRM ioctls are local memory, as in the lima port.
 */

/*
 * Copyright (C) 2026 Ivan Loskutov
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#pragma once

extern unsigned long emul_user_copy(void *, void const *, unsigned long);

#include_next <asm/uaccess.h>

#undef access_ok
#define access_ok(addr, size) 1

#undef raw_copy_from_user
#define raw_copy_from_user(to, from, n) emul_user_copy((to), (from), (n))

#undef raw_copy_to_user
#define raw_copy_to_user(to, from, n) emul_user_copy((to), (from), (n))
