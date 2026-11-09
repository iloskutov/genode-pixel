/*
 * \brief  The device nodes of the cpif for Genode
 * \author Ivan Loskutov
 * \date   2026-11-07
 *
 * The CP is booted and driven by its clients through the device nodes of
 * the cpif: every operation on an Android_chardev session ends in the
 * file_operations of the io_device of that name, as a system call does on
 * Linux.
 *
 * Differences to Linux, all at the system-call boundary:
 *
 * - A read of an empty queue reports 'would_block' instead of waiting in
 *   the kernel. The client waits for READABLE.
 * - Pointers into the client do not exist here. Arguments live in the
 *   session buffer and only the commands listed in 'bridge_ioctl' pass.
 * - One 'struct file' per node, shared by all sessions of that node.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <lx_user/init.h>
#include <lx_user/io.h>
#include <lx_emul/task.h>

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/sched/task.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/wait.h>

#include "modem_prj.h"
#include "modem_utils.h"

#include "lx_bridge.h"
#include "uplink.h"


/*********************
 ** Character nodes **
 *********************/

struct bridge_node
{
	struct list_head               list;
	struct io_device              *iod;
	struct file_operations const  *fops;
	struct inode                   inode;
	struct file                    file;
	struct wait_queue_entry        wait;
	unsigned volatile              ready;
	unsigned                       opens;   /* sessions of the node */
};


static LIST_HEAD(_nodes);

static struct modem_shared *_msd;


unsigned lx_bridge_ready(void *node)
{
	return ((struct bridge_node *)node)->ready;
}


static void node_update_ready(struct bridge_node *n)
{
	__poll_t const mask = n->fops->poll(&n->file, NULL);
	unsigned ready = 0;

	/* Linux read() takes what is queued in any modem state, so READABLE does */
	if ((mask & EPOLLIN) || !skb_queue_empty(&n->iod->sk_rx_q))
		ready |= LX_BRIDGE_READABLE;
	if (mask & EPOLLHUP)
		ready |= LX_BRIDGE_HANGUP;

	if (ready == n->ready)
		return;

	n->ready = ready;
	genode_bridge_changed();
}


static int node_woken(struct wait_queue_entry *wait, unsigned mode, int sync, void *key)
{
	node_update_ready(container_of(wait, struct bridge_node, wait));
	return 0;
}


struct find_iod_args { char const *name; struct io_device *iod; };

static void find_iod(struct io_device *iod, void *args)
{
	struct find_iod_args *find = args;

	if (iod && (iod->io_typ == IODEV_BOOTDUMP || iod->io_typ == IODEV_IPC)
	 && !strcmp(iod->name, find->name))
		find->iod = iod;
}


static struct bridge_node *node_by_name(char const *name)
{
	struct bridge_node *n;
	struct find_iod_args find = { .name = name, .iod = NULL };

	list_for_each_entry(n, &_nodes, list)
		if (!strcmp(n->iod->name, name))
			return n;

	iodevs_for_each(_msd, find_iod, &find);
	if (!find.iod)
		return NULL;

	n = kzalloc(sizeof(*n), GFP_KERNEL);
	if (!n)
		return NULL;

	/* the cdev keeps no operations, they are the ones of the io_device type */
	n->iod  = find.iod;
	n->fops = find.iod->io_typ == IODEV_BOOTDUMP ? get_bootdump_io_fops()
	                                             : get_ipc_io_fops();
	n->inode.i_cdev      = &find.iod->cdev;
	n->file.private_data = find.iod;

	/* for good: the node is never freed, 'lx_bridge_ready' may look at it */
	init_waitqueue_func_entry(&n->wait, node_woken);
	add_wait_queue(&find.iod->wq, &n->wait);
	list_add_tail(&n->list, &_nodes);

	return n;
}


static void bridge_open(struct lx_bridge_request *r)
{
	struct bridge_node *n = node_by_name(r->name);

	if (!n) {
		r->result = -ENOENT;
		return;
	}

	r->node   = n;
	r->result = n->fops->open(&n->inode, &n->file);
	if (r->result == 0)
		n->opens++;
	node_update_ready(n);
}


static void bridge_close(struct lx_bridge_request *r)
{
	struct bridge_node *n = r->node;
	char comm[sizeof(current->comm)];
	bool last = n->opens == 0 || --n->opens == 0;

	/*
	 * bootdump_release purges the receive queue only when it runs in cbd's
	 * context. The sessions of a node share its queue, so only the last
	 * close purges: a status query of another daemon must not drop the
	 * acknowledgements cbd waits for while it streams the firmware.
	 */
	memcpy(comm, current->comm, sizeof(comm));
	if (n->iod->io_typ == IODEV_BOOTDUMP && last)
		strscpy(current->comm, "cbd", sizeof(current->comm));

	r->result = n->fops->release(&n->inode, &n->file);

	memcpy(current->comm, comm, sizeof(comm));
	node_update_ready(n);
}


static void bridge_read(struct lx_bridge_request *r)
{
	struct bridge_node *n = r->node;
	loff_t pos = 0;

	if (skb_queue_empty(&n->iod->sk_rx_q)) {
		r->would_block = 1;
		return;
	}

	r->result = n->fops->read(&n->file, r->buf, r->len, &pos);
	node_update_ready(n);
}


static void bridge_write(struct lx_bridge_request *r)
{
	struct bridge_node *n = r->node;
	loff_t pos = 0;

	r->result = n->fops->write(&n->file, r->buf, r->len, &pos);
	node_update_ready(n);
}


/**
 * Check the input and output bytes a command with a size needs
 */
static bool ioctl_sized(struct lx_bridge_request *r, unsigned long *arg)
{
	unsigned const dir  = _IOC_DIR(r->cmd);
	unsigned const size = _IOC_SIZE(r->cmd);

	if (((dir & _IOC_WRITE) && r->len < size) || ((dir & _IOC_READ) && r->out_cap < size))
		return false;

	*arg = (unsigned long)r->buf;
	return true;
}


/*
 * The commands are spelled out because the client controls the buffer and
 * the value argument, and an argument of a cpif ioctl is usually an
 * address: nothing from the client may reach Linux as one. Hence
 *
 * - no client value at all, 'arg' is 0 or points into the session buffer,
 * - no command whose struct holds a pointer (the GNSS images),
 * - struct cp_image, which does hold one, is a private copy: the client can
 *   change the session buffer while this runs.
 *
 * A command not listed here is ENOTTY.
 */
static void bridge_ioctl(struct lx_bridge_request *r)
{
	struct bridge_node *n = r->node;
	struct cp_image img;
	unsigned long arg = 0;
	bool ok = true;

	switch (r->cmd) {

	case IOCTL_POWER_ON:
	case IOCTL_POWER_OFF:
	case IOCTL_SILENT_RESET:
	case IOCTL_COMPLETE_NORMAL_BOOTUP:
	case IOCTL_GET_CP_STATUS:
	case IOCTL_TRIGGER_CP_CRASH:   /* its value is the address of a reason string */
		break;

	case IOCTL_CLR_CP_BOOTLOG:     /* no boot log (cp_btl) in this build */
		r->result = 0;
		return;

	case IOCTL_HANDOVER_BLOCK_INFO:
		ok  = r->len >= sizeof(struct t_handover_block_info);
		arg = (unsigned long)r->buf;
		break;

	case IOCTL_LOAD_CP_IMAGE:
		/* the client's 'binary' points into the client, the image follows the struct */
		if (r->len < sizeof(img)) {
			ok = false;
			break;
		}
		memcpy(&img, r->buf, sizeof(img));
		ok = img.size == img.len && img.len <= r->len - sizeof(img);
		img.binary = (unsigned long)r->buf + sizeof(img);
		arg = (unsigned long)&img;
		break;

	case IOCTL_START_CP_BOOTLOADER:
	case IOCTL_POWER_RESET:
	case IOCTL_GET_CP_CRASH_REASON:
	case IOCTL_GET_CPIF_VERSION:
	case IOCTL_GET_OPENED_STATUS:
		ok = ioctl_sized(r, &arg);
		break;

	default:
		pr_info("modem: %s: ioctl 0x%lx not supported\n", n->iod->name, r->cmd);
		r->result = -ENOTTY;
		return;
	}

	if (!ok) {
		pr_info("modem: %s: ioctl 0x%lx with %lu input and %lu output bytes refused\n",
		        n->iod->name, r->cmd, r->len, r->out_cap);
		r->result = -EINVAL;
		return;
	}

	r->result = n->fops->unlocked_ioctl(&n->file, r->cmd, arg);

	if (r->cmd != IOCTL_GET_CP_STATUS || r->result < 0)
		pr_info("modem: %s: ioctl 0x%lx -> %ld\n", n->iod->name, r->cmd, r->result);

	node_update_ready(n);
}


/***********
 ** Tasks **
 ***********/

/* more than requests can block at a time: reads, writes and status stay answered */
enum { NUM_WORKERS = 8 };

struct bridge_worker
{
	struct task_struct       *task;
	struct lx_bridge_request *request;
	bool                      idle;
};

static struct bridge_worker _workers[NUM_WORKERS];


static void bridge_execute(struct lx_bridge_request *r)
{
	switch (r->op) {
	case LX_BRIDGE_OPEN:  bridge_open(r);  break;
	case LX_BRIDGE_CLOSE: bridge_close(r); break;
	case LX_BRIDGE_READ:  bridge_read(r);  break;
	case LX_BRIDGE_WRITE: bridge_write(r); break;
	case LX_BRIDGE_IOCTL: bridge_ioctl(r); break;
	}
}


static int bridge_worker_task(void *arg)
{
	struct bridge_worker *w = arg;

	for (;;) {
		if (!w->request)
			w->request = genode_bridge_next_request();

		if (!w->request) {
			w->idle = true;
			lx_emul_task_schedule(true);
			w->idle = false;
			continue;
		}

		bridge_execute(w->request);
		genode_bridge_request_done(w->request);
		w->request = NULL;
	}
	return 0;
}


/*
 * Waits for the probe of the cpif, then keeps the readiness up to date for
 * the state changes that do not wake the io_device's wait queue
 */
static int bridge_start_task(void *arg)
{
	struct modem_ctl *mc = NULL;
	struct bridge_node *n;

	while (!mc) {
		struct device_node *np = of_find_compatible_node(NULL, NULL, "samsung,exynos-cp");
		struct platform_device *pdev = np ? of_find_device_by_node(np) : NULL;

		of_node_put(np);
		mc = pdev ? platform_get_drvdata(pdev) : NULL;
		if (!mc)
			msleep(100);
	}

	_msd = mc->msd;
	pr_info("modem: cpif probed, %d channels\n", _msd->num_channels);
	genode_bridge_driver_ready();

	for (;;) {
		msleep(1000);
		list_for_each_entry(n, &_nodes, list)
			node_update_ready(n);
	}
	return 0;
}


static struct task_struct *_spawn(int (*fn)(void *), void *arg)
{
	return find_task_by_pid_ns(kernel_thread(fn, arg, CLONE_FS | CLONE_FILES), NULL);
}


void lx_user_init(void)
{
	unsigned i;

	for (i = 0; i < NUM_WORKERS; i++)
		_workers[i].task = _spawn(bridge_worker_task, &_workers[i]);

	_spawn(bridge_start_task, NULL);

	uplink_init();
}


void lx_user_handle_io(void)
{
	unsigned i;

	for (i = 0; i < NUM_WORKERS; i++) {
		struct bridge_worker *w = &_workers[i];

		if (!w->idle)
			continue;

		w->request = genode_bridge_next_request();
		if (!w->request)
			break;

		w->idle = false;
		lx_emul_task_unblock(w->task);
	}
}
