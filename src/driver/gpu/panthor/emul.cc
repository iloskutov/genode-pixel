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

/* Genode includes */
#include <util/string.h>

/* local includes */
#include "emul.h"


unsigned long emul_user_copy(void *to, void const *from, unsigned long n)
{
	Genode::memcpy(to, from, n);
	return 0;
}
