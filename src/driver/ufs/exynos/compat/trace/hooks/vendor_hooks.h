/*
 * \brief  Android vendor hooks as no-ops
 * \author Ivan Loskutov
 * \date   2026-10-28
 *
 * Replaces include/trace/hooks/vendor_hooks.h of the kernel. Without
 * CONFIG_ANDROID_VENDOR_HOOKS, the kernel's header provides the hook calls
 * as no-ops but not the functions that register a hook, which the vendor's
 * UFS driver calls. Here both are no-ops, so the hooks of the vendor's
 * driver are never called by the UFS core.
 */

/*
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef _COMPAT__TRACE__HOOKS__VENDOR_HOOKS_H_
#define _COMPAT__TRACE__HOOKS__VENDOR_HOOKS_H_

#include <linux/tracepoint.h>

#define DECLARE_HOOK(name, proto, args) \
	static inline void trace_##name(proto) { } \
	static inline bool trace_##name##_enabled(void) { return false; } \
	static inline int register_trace_##name( \
		void (*probe)(void *__data, proto), void *data) { return 0; }

#define DECLARE_RESTRICTED_HOOK(name, proto, args, cond) \
	DECLARE_HOOK(name, PARAMS(proto), PARAMS(args))

#endif /* _COMPAT__TRACE__HOOKS__VENDOR_HOOKS_H_ */
