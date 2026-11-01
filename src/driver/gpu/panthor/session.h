/*
 * \brief  Gpu session of the panthor GPU driver
 * \author Ivan Loskutov
 * \date   2026-10-08
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _SESSION_H_
#define _SESSION_H_

namespace Gpu {

	/**
	 * Prepare the creation of session tasks
	 *
	 * Must be called before the kernel is started. The service is
	 * announced by the kernel once the driver's probe function is done.
	 */
	void prepare();
}

#endif /* _SESSION_H_ */
