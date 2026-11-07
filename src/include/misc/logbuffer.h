/*
 * \brief  Log buffer of the Pixel kernel, written to the kernel log
 * \author Ivan Loskutov
 * \date   2026-10-28
 *
 * The vendor's PCIe driver reports the training of the link (lock of the
 * PHY, link speed, power states), the modem interface driver the power
 * sequence of the modem, only to a log buffer of Google's kernel modules,
 * which is not part of the ports.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _MISC__LOGBUFFER_H_
#define _MISC__LOGBUFFER_H_

#include <linux/printk.h>

struct logbuffer;

static inline struct logbuffer *logbuffer_register(const char *name)
{
	/* not NULL, the handle is only passed back to the functions below */
	return (struct logbuffer *)1;
}

static inline void logbuffer_unregister(struct logbuffer *instance) { }

/*
 * A driver that defines LOGBUFFER_DROP drops the messages, as Linux keeps
 * them out of the kernel log: the modem's PCIe link powers off and on
 * hundreds of times an hour, about 30 messages each time.
 */
#ifdef LOGBUFFER_DROP

#define logbuffer_log(instance, fmt, ...) \
	no_printk(fmt, ##__VA_ARGS__)
#define logbuffer_logk(instance, loglevel, fmt, ...) \
	no_printk(fmt, ##__VA_ARGS__)
#define dev_logbuffer_logk(dev, instance, loglevel, fmt, ...) \
	no_printk(fmt, ##__VA_ARGS__)

#else

/* the log level (LOGLEVEL_*) is not a prefix of printk, the messages are info */
#define logbuffer_log(instance, fmt, ...) \
	printk(KERN_INFO fmt "\n", ##__VA_ARGS__)
#define logbuffer_logk(instance, loglevel, fmt, ...) \
	printk(KERN_INFO fmt "\n", ##__VA_ARGS__)
#define dev_logbuffer_logk(dev, instance, loglevel, fmt, ...) \
	dev_info(dev, fmt "\n", ##__VA_ARGS__)

#endif

#endif /* _MISC__LOGBUFFER_H_ */
