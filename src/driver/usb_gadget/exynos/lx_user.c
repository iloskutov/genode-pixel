/*
 * \brief  Post kernel userland activity of the gadget driver
 * \author Ivan Loskutov
 * \date   2026-10-23
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <lx_user/init.h>
#include <lx_user/io.h>

#include <gadget_api.h>

extern void usb_gadget_start_task(void);


void lx_user_init(void)
{
	usb_gadget_start_task();
}


void lx_user_handle_io(void)
{
	gadget_wakeup();
}
