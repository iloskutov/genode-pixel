/*
 * \brief  Post kernel activity of the panthor GPU driver
 * \author Ivan Loskutov
 * \date   2026-10-07
 *
 * The driver's probe function runs from the initcalls. Started here, once
 * the initcalls are done: the task that runs irq works and the task that
 * creates the tasks of the Gpu sessions (as in the lima port). Finally the
 * Gpu service is announced.
 */

/*
 * Copyright (C) 2026 Ivan Loskutov
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#include <linux/sched/task.h>

#include <lx_emul/task.h>
#include <lx_user/init.h>
#include <lx_user/io.h>

#include "emul.h"


/*
 * Creation of the session tasks (session.cc)
 */

struct task_struct *lx_user_task;
void               *lx_user_task_args;

extern int  lx_user_task_func(void *);
extern void lx_emul_announce_gpu_session(void);


struct task_struct *lx_user_new_gpu_task(int (*func)(void *), void *args)
{
	int const pid = kernel_thread(func, args, "gpu_task", CLONE_FS | CLONE_FILES);

	return find_task_by_pid_ns(pid, NULL);
}


void lx_user_destroy_gpu_task(struct task_struct *task)
{
	if (task != current) {
		printk("%s: task %px is not current %px\n", __func__, task, current);
		return;
	}

	do_exit(0);
}


void lx_user_handle_io(void) { }


void lx_user_init(void)
{
	int pid;

	emul_irq_work_init();

	pid =kernel_thread(lx_user_task_func, lx_user_task_args, "user_task",
	                    CLONE_FS | CLONE_FILES);
	lx_user_task = find_task_by_pid_ns(pid, NULL);

	lx_emul_announce_gpu_session();
}
