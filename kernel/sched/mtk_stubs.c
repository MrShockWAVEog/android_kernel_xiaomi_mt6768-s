#include <linux/kernel.h>

int __weak update_eas_uclamp_min(int kicker, int cgroup_idx, int value)
{
	return 0;
}

int __weak sched_forked_ramup_factor(void)
{
	return 0;
}
