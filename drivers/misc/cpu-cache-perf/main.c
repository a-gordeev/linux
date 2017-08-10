/*
 *  Copyright (C) 2017 Alexander Gordeev <agordeev@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <linux/compat.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/interrupt.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mutex.h>

#include <linux/atomic.h>
#include <asm/io.h>

#define CPU_CACHE_PERF_VERSION		"0.0.0"

#define CPU_CACHE_PERF_MAX_MINORS	1
#define CHRDEV_NAME			"cpu-cache-perf"
#define PFX				CHRDEV_NAME

static struct class	*class;
static int		major;

static CLASS_ATTR_STRING(version, 0444, CPU_CACHE_PERF_VERSION);

static int __init cpu_cache_perf_init(void)
{
	int retval;
	dev_t dev;

	class = class_create(THIS_MODULE, CHRDEV_NAME);
	if (IS_ERR(class)) {
		retval = PTR_ERR(class);
		printk(KERN_ERR PFX ": can't register " CHRDEV_NAME " class\n");
		goto err;
	}
	retval = class_create_file(class, &class_attr_version.attr);
	if (retval) {
		printk(KERN_ERR PFX ": can't create sysfs version file\n");
		goto err_class;
	}

	retval = alloc_chrdev_region(&dev, 0, CPU_CACHE_PERF_MAX_MINORS, CHRDEV_NAME);
	if (retval) {
		printk(KERN_ERR PFX ": can't register character device\n");
		goto err_attr;
	}

	major = MAJOR(dev);

	/*
	 * other things initialization
	 */

	printk(KERN_INFO CHRDEV_NAME " version " CPU_CACHE_PERF_VERSION);

	return 0;

err_chrdev:
	unregister_chrdev_region(dev, CPU_CACHE_PERF_MAX_MINORS);
err_attr:
	class_remove_file(class, &class_attr_version.attr);
err_class:
	class_destroy(class);
err:
	return retval;
}

static void __exit cpu_cache_perf_exit(void)
{
	unregister_chrdev_region(MKDEV(major, 0), CPU_CACHE_PERF_MAX_MINORS);

	class_remove_file(class, &class_attr_version.attr);
	class_destroy(class);

	pr_debug(PFX ": module successfully removed\n");
}

module_init(cpu_cache_perf_init);
module_exit(cpu_cache_perf_exit);

MODULE_AUTHOR("Alexander Gordeev <agordeev@redhat.com>");
MODULE_DESCRIPTION("CPU cache performance driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(CPU_CACHE_PERF_VERSION);
