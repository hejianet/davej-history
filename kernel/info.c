/*
 * linux/kernel/info.c
 *
 * Copyright (C) 1992 Darren Senn
 */

/* This implements the sysinfo() system call */

#include <linux/sched.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>

asmlinkage int sys_sysinfo(struct sysinfo *info)
{
	struct sysinfo val;

	memset((char *)&val, 0, sizeof(struct sysinfo));

	lock_kernel();
	val.uptime = jiffies / HZ;

	val.loads[0] = avenrun[0] << (SI_LOAD_SHIFT - FSHIFT);
	val.loads[1] = avenrun[1] << (SI_LOAD_SHIFT - FSHIFT);
	val.loads[2] = avenrun[2] << (SI_LOAD_SHIFT - FSHIFT);

	val.procs = nr_tasks-1;

	si_meminfo(&val);
	si_swapinfo(&val);
	unlock_kernel();

	if (copy_to_user(info, &val, sizeof(struct sysinfo)))
		return -EFAULT;
	return 0;
}
