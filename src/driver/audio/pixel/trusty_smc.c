/*
 * \brief  Trusty client: replacements for what cannot run in a component
 * \author Ivan Loskutov
 * \date   2026-10-01
 *
 * The vendor Trusty driver (trusty.c, trusty-virtio.c, trusty-ipc.c,
 * trusty-mem.c) is built unmodified. This file provides the parts of the
 * Linux driver that cannot work at EL0 on base-hw:
 *
 * - trusty_smc8 (vendor: trusty-smc-arm64.S, a plain 'smc #0'): issued
 *   through core's system-control interface (arm_smccc_smc ->
 *   pixel_audio_smc in main.cc).
 * - The forwarded-interrupt driver (vendor: trusty-irq.c): Trusty asks the
 *   non-secure side to enter it by raising SGIs/doorbell interrupts, which
 *   never reach a Genode component. One kernel thread signals the NOP worker
 *   of trusty.c periodically instead.
 * - The CPU-hotplug state of trusty.c: its 'online' callback unparks the
 *   NOP worker and must be invoked once.
 * - The scheduler-share page (vendor: trusty-sched-share.c): not registered.
 *
 * The retry and restart rules of standard calls (SM_ERR_BUSY,
 * SM_ERR_INTERRUPTED, SM_ERR_FIQ_INTERRUPTED, SM_ERR_CPU_IDLE,
 * SM_ERR_NOP_INTERRUPTED) are the loops of trusty.c, nothing of them is
 * reimplemented here.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <lx_emul.h>

#include <linux/arm-smccc.h>
#include <linux/cpuhotplug.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/trusty/smcall.h>
#include <linux/trusty/sm_err.h>
#include <linux/trusty/trusty.h>

/* private headers of the vendor driver (drivers/trusty) */
#include <trusty-smc.h>
#include <trusty-sched-share-api.h>

#include "pixel_audio.h"


/*********
 ** SMC **
 *********/

/*
 * Back end of arm_smccc_smc(): the function id and a1..a7 travel in
 * x0..x7, a0..a3 of the result come back in x0..x3.
 */
void __arm_smccc_smc(unsigned long a0, unsigned long a1, unsigned long a2,
                     unsigned long a3, unsigned long a4, unsigned long a5,
                     unsigned long a6, unsigned long a7,
                     struct arm_smccc_res *res, struct arm_smccc_quirk *quirk)
{
	unsigned long const a[8] = { a0, a1, a2, a3, a4, a5, a6, a7 };
	unsigned long r[4] = { };

	pixel_audio_smc(a, r);

	if (res) {
		res->a0 = r[0];
		res->a1 = r[1];
		res->a2 = r[2];
		res->a3 = r[3];
	}
}


/*
 * Replaces trusty-smc-arm64.S. The Trusty drivers pass at most r0..r3 (the
 * rest is 0) and use at most r0..r3 of the result; r4..r7 of the result are
 * not available here and reported as 0.
 */
struct smc_ret8 trusty_smc8(unsigned long r0, unsigned long r1,
                            unsigned long r2, unsigned long r3,
                            unsigned long r4, unsigned long r5,
                            unsigned long r6, unsigned long r7)
{
	struct arm_smccc_res res = { };
	struct smc_ret8      ret = { };

	arm_smccc_smc(r0, r1, r2, r3, r4, r5, r6, r7, &res);

	ret.r0 = res.a0;
	ret.r1 = res.a1;
	ret.r2 = res.a2;
	ret.r3 = res.a3;

	return ret;
}


/*****************
 ** CPU hotplug **
 *****************/

/*
 * trusty.c registers one multi-instance hotplug state. Its startup callback
 * unparks the NOP worker of the CPU that comes online. lx_emul has one CPU
 * that is online from the start, so adding an instance calls the startup
 * callback right away (as Linux does for the CPUs that are already online).
 */

static int (*_cpuhp_startup)(unsigned int cpu, struct hlist_node *node);
static int (*_cpuhp_teardown)(unsigned int cpu, struct hlist_node *node);

#define TRUSTY_CPUHP_SLOT CPUHP_AP_ONLINE_DYN


int pixel_audio_cpuhp_setup_state(enum cpuhp_state state, const char *name,
                                  bool invoke,
                                  int (*startup)(unsigned int cpu),
                                  int (*teardown)(unsigned int cpu),
                                  bool multi_instance)
{
	if (!multi_instance || state != CPUHP_AP_ONLINE_DYN || _cpuhp_startup) {
		printk("pixel_audio: unexpected hotplug state %d (%s)\n", state, name);
		return -EINVAL;
	}

	_cpuhp_startup  = (void *)startup;
	_cpuhp_teardown = (void *)teardown;

	return TRUSTY_CPUHP_SLOT;
}


void pixel_audio_cpuhp_remove_state(enum cpuhp_state state, bool invoke)
{
	_cpuhp_startup  = NULL;
	_cpuhp_teardown = NULL;
}


int pixel_audio_cpuhp_state_add_instance(enum cpuhp_state state,
                                         struct hlist_node *node, bool invoke)
{
	if (state != TRUSTY_CPUHP_SLOT || !_cpuhp_startup)
		return -EINVAL;

	return invoke ? _cpuhp_startup(0, node) : 0;
}


int pixel_audio_cpuhp_state_remove_instance(enum cpuhp_state state,
                                            struct hlist_node *node,
                                            bool invoke)
{
	if (state != TRUSTY_CPUHP_SLOT || !_cpuhp_teardown)
		return -EINVAL;

	return invoke ? _cpuhp_teardown(0, node) : 0;
}


/****************
 ** NOP poller **
 ****************/

/*
 * Under Linux, Trusty raises a forwarded interrupt when it has work but no
 * CPU (e.g., a reply to a TIPC request became ready after the secure side
 * was left). The handler of trusty-irq.c then calls trusty_enqueue_nop(dev,
 * NULL), which wakes the NOP worker of trusty.c: it enters Trusty with
 * SMC_SC_NOP until Trusty reports SM_ERR_NOP_DONE. Without those
 * interrupts, the same call is made periodically.
 */

static struct task_struct *_poll_task;
static unsigned long       _poll_count;


static int trusty_poll_thread(void *arg)
{
	struct device *trusty_dev = arg;

	for (;;) {
		msleep(pixel_audio_config.nop_poll_ms);

		trusty_enqueue_nop(trusty_dev, NULL);
		_poll_count++;
	}
	return 0;
}


int pixel_audio_trusty_poll_start(struct device *trusty_dev)
{
	if (_poll_task)
		return 0;

	if (!pixel_audio_config.nop_poll_ms) {
		printk("pixel_audio: Trusty NOP poller disabled by config "
		       "(nop_poll_ms=0): Trusty runs only while a call is "
		       "in progress\n");
		return 0;
	}

	_poll_task = kthread_run(trusty_poll_thread, trusty_dev, "trusty-poll");
	if (IS_ERR(_poll_task)) {
		int const err = PTR_ERR(_poll_task);
		_poll_task = NULL;
		return err;
	}
	return 0;
}


unsigned long pixel_audio_trusty_poll_count(void) { return _poll_count; }


/*********************
 ** Scheduler share **
 *********************/

/*
 * The page tells Trusty the priority of the NOP worker per CPU and Linux the
 * priority Trusty asks for. It is indexed by CPU number; lx_emul's single
 * CPU 0 is not the physical CPU the calls are issued from, and Linux
 * tolerates Trusty builds without the feature (trusty.c ignores the result
 * of the registration). Nothing is allocated, shared or registered: the NOP
 * worker runs at the requested "normal" priority all the time.
 */

int trusty_alloc_sched_share(struct device *device,
                             struct trusty_sched_share_state **state)
{
	*state = NULL;
	return 0;
}


void trusty_register_sched_share(struct device *device,
                                 struct trusty_sched_share_state *state)
{
	dev_info(device, "scheduler-share page not registered (stubbed, "
	         "pixel_audio)\n");
}


void trusty_unregister_sched_share(struct trusty_sched_share_state *state) { }
void trusty_free_sched_share(struct trusty_sched_share_state *state) { }


int trusty_get_requested_nice(unsigned int cpu_num,
                              struct trusty_sched_share_state *state)
{
	return LINUX_NICE_FOR_TRUSTY_PRIORITY_NORMAL;
}


void trusty_set_actual_nice(unsigned int cpu_num,
                            struct trusty_sched_share_state *state, int nice) { }


/************
 ** Memory **
 ************/

#include <lx_emul/alloc.h>
#include <lx_emul/page_virt.h>

/*
 * 'phys_to_page' of trusty.c (trusty_shim.h): trusty_transfer_memory() turns
 * the DMA address of a mapped scatterlist entry back into its page to get
 * the physical address from it. DMA addresses of lx_kit memory are physical
 * addresses, the page is the one associated with the buffer's virtual
 * address.
 */
struct page *pixel_audio_phys_to_page(phys_addr_t phys)
{
	unsigned long const virt = lx_emul_mem_virt_addr((void *)phys);

	return virt ? lx_emul_virt_to_page((void *)virt) : NULL;
}
