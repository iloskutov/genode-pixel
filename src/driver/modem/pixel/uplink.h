/*
 * \brief  Uplink of the modem's mobile data
 * \author Ivan Loskutov
 * \date   2026-11-09
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPLINK_H_
#define _UPLINK_H_

#ifdef __cplusplus
extern "C" {
#endif

/* start the task of the uplink, with the kernel */
void uplink_init(void);

/* the interface of the cpif whose packets the Uplink session carries, "" for none */
void uplink_interface(char const *name);

/* the session has packets or room for them */
void uplink_handle_io(void);

#ifdef __cplusplus
}
#endif

#endif /* _UPLINK_H_ */
