/*
 * lunix-chrdev.c
 *
 * Implementation of character devices
 * for Lunix:TNG
 *
 * Authors:
 * Petros Iakovos Floratos
 * Filippos Giannakopoulos
 *
 */

#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mmzone.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>

#include "lunix.h"
#include "lunix-chrdev.h"
#include "lunix-lookup.h"

/*
 * Global data
 * The `lunix_chrdev_cdev` structure represents the Lunix character device.
 */
struct cdev lunix_chrdev_cdev;

/*
 * Checks whether the cached character device state needs to be updated
 * from the sensor's latest measurements.
 *
 * Returns:
 * - 1 if an update is needed
 * - 0 otherwise
 */
static int lunix_chrdev_state_needs_refresh(struct lunix_chrdev_state_struct *state)
{
	struct lunix_sensor_struct *sensor;

	WARN_ON(!(sensor = state->sensor));

	/* Check if new data is available */
	if (state->buf_timestamp != sensor->msr_data[state->type]->last_update)
		return 1;

	return 0;
}

/*
 * Updates the cached state of the character device using sensor data.
 * Must be called with the `state->lock` semaphore held.
 *
 * Returns:
 * - 0 on success
 * - -EAGAIN if no new data is available
 * - -EINVAL if an invalid measurement type is encountered
 */
static int lunix_chrdev_state_update(struct lunix_chrdev_state_struct *state)
{
	struct lunix_sensor_struct *sensor;
	uint32_t raw_data, last_update;
	long converted_value;
	int ret = 0;

	sensor = state->sensor;
	WARN_ON(!sensor);

	/* Check if update is needed */
	if (!lunix_chrdev_state_needs_refresh(state))
		return -EAGAIN;

	/* Acquire the sensor's spinlock */
	//  to protect shared resources from concurrent access in multiprocessor environments
	spin_lock_irq(&sensor->lock);

	/* Read raw sensor data and timestamp */
	raw_data = sensor->msr_data[state->type]->values[0];
	last_update = sensor->msr_data[state->type]->last_update;

	/* Release the spinlock */
	spin_unlock_irq(&sensor->lock);
	// Ensures that other threads or interrupt handlers waiting to acquire the lock can proceed.

	/* Update the cached timestamp */
	state->buf_timestamp = last_update;

	/* Convert raw data using the lookup tables */
	switch (state->type) {
	case BATT:
		converted_value = lookup_voltage[raw_data];
		break;
	case TEMP:
		converted_value = lookup_temperature[raw_data];
		break;
	case LIGHT:
		converted_value = lookup_light[raw_data];
		break;
	default:
		return -EINVAL;
	}

	/* Format the converted data and store it in state->buf_data */
	state->buf_lim = snprintf(state->buf_data, LUNIX_CHRDEV_BUFSZ, "%ld.%03ld\n",
	                          converted_value / 1000, abs(converted_value % 1000));

	return ret;
}

/*************************************
 * Character device file operations
 *************************************/

/*
 * Opens the character device.
 * Allocates and initializes the private state structure for the file.
 */
static int lunix_chrdev_open(struct inode *inode, struct file *filp)
{
	int ret = 0;
	unsigned int minor_num, type, sensor_num;
	struct lunix_chrdev_state_struct *state;

	debug("entering open\n");

	// Initialize the file operations
	if ((ret = nonseekable_open(inode, filp)) < 0)
		goto out;

	// minor number identifies the specific device.
	minor_num = iminor(inode);
	type = minor_num % 8;      /* Measurement type */
	sensor_num = minor_num / 8; /* Sensor number */

    /* Validate measurement type */
	if (type >= N_LUNIX_MSR) {
		ret = -EINVAL;
		goto out;
	}

    /* Allocate memory for the device state */
	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (!state) {
		ret = -ENOMEM;
		debug("couldn't allocate memory\n");
		goto out;
	}

    /* Initialize the device state */
	state->type = type;
	state->sensor = &lunix_sensors[sensor_num];
	state->buf_lim = 0;
	state->buf_timestamp = 0;
	sema_init(&state->lock, 1);
	// state->lock will protect the state object or associated data from concurrent access by multiple threads or processes
	// A value of 1 means the resource is available.
	// When a thread acquires the semaphore, the value decrements to 0, marking the resource as in use.

	filp->private_data = state;

out:
	debug("leaving open, with ret = %d\n", ret);
	return ret;
}


/*
 * Releases the character device.
 * Frees the allocated private state structure.
 */
static int lunix_chrdev_release(struct inode *inode, struct file *filp)
{
	kfree(filp->private_data);
	debug("released private data successfully\n");
	return 0;
}

/*
 * Handles IOCTL commands for the character device.
 * Currently, no IOCTL commands are supported.
 *
 * Returns:
 * - -EINVAL (Invalid argument)
 */
static long lunix_chrdev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	/* No ioctl commands are supported */
	return -EINVAL;
}


/*
 * Reads data from the character device into the user buffer.
 */
static ssize_t lunix_chrdev_read(struct file *filp, char __user *usrbuf, size_t cnt, loff_t *f_pos)
{
	ssize_t ret = 0;
	struct lunix_chrdev_state_struct *state;
	struct lunix_sensor_struct *sensor;
	ssize_t available_bytes;

	state = filp->private_data;
	WARN_ON(!state);

	sensor = state->sensor;
	WARN_ON(!sensor);

    /* Acquire the state lock */
	// Attempt to acquire the semaphore (state->lock) to prevent concurrent access to the device state.
	if (down_interruptible(&state->lock))
		return -ERESTARTSYS;

	/* Update state if necessary */
	if (*f_pos == 0) {
		while (lunix_chrdev_state_update(state) == -EAGAIN) { // refresh the device state
			// Releases the lock
			up(&state->lock);

			/* Wait until new data is available */
			if (wait_event_interruptible(sensor->wq, lunix_chrdev_state_needs_refresh(state)))
				return -ERESTARTSYS;

			if (down_interruptible(&state->lock))
				return -ERESTARTSYS;
		}
	}

	/* Determine the number of bytes to copy */
	available_bytes = state->buf_lim - *f_pos;  // number of bytes available for reading
	if (available_bytes < 0)
		available_bytes = 0;

	if (cnt > available_bytes) // ensures that the number of bytes to read (cnt) does not exceed the available bytes.
		cnt = available_bytes;

	if (cnt == 0) {
		/* End of file */
		ret = 0;
		goto out;
	}

	/* Copy data to user-space */
	if (copy_to_user(usrbuf, state->buf_data + *f_pos, cnt)) {
		ret = -EFAULT;
		goto out;
	}

	*f_pos += cnt;
	ret = cnt;

	/* Auto-rewind on EOF */
	if (*f_pos >= state->buf_lim)
		*f_pos = 0;

out:
	up(&state->lock); // Releases the lock
	return ret;
}


/*
 * Memory mapping is not supported for the character device.
 */
static int lunix_chrdev_mmap(struct file *filp, struct vm_area_struct *vma)
{
	/* Memory mapping is not supported */
	return -EINVAL;
}


/*
 * File operations structure for the Lunix character device.
 */
static const struct file_operations lunix_chrdev_fops = {
	.owner          = THIS_MODULE,
	.open           = lunix_chrdev_open,
	.release        = lunix_chrdev_release,
	.read           = lunix_chrdev_read,
	.unlocked_ioctl = lunix_chrdev_ioctl,
	.mmap           = lunix_chrdev_mmap,
};


/*
 * Initializes the Lunix character device.
 */
int lunix_chrdev_init(void)
{
	int ret;
	dev_t dev_no; // device number for the Lunix character device
	unsigned int lunix_minor_cnt = lunix_sensor_cnt << 3; // number of minor device number

	debug("initializing character device\n");
	cdev_init(&lunix_chrdev_cdev, &lunix_chrdev_fops); // initialize the lunix_chrdev_cdev structure, linking it to the file operations
	lunix_chrdev_cdev.owner = THIS_MODULE;

	// combines the major device number (LUNIX_CHRDEV_MAJOR) and the base minor number (0) into a device number using MKDEV
	dev_no = MKDEV(LUNIX_CHRDEV_MAJOR, 0);

	// reserves a range of device numbers for the Lunix character device
	ret = register_chrdev_region(dev_no, lunix_minor_cnt, "lunix");
	if (ret < 0) {
		debug("failed to register region, ret = %d\n", ret);
		goto out;
	}

	// registers the lunix_chrdev_cdev structure with the kernel, associating it with the reserved device numbers.
	ret = cdev_add(&lunix_chrdev_cdev, dev_no, lunix_minor_cnt);
	if (ret < 0) {
		debug("failed to add character device\n");
		goto out_with_chrdev_region;
	}

	debug("completed successfully\n");
	return 0;

out_with_chrdev_region:
	unregister_chrdev_region(dev_no, lunix_minor_cnt);
out:
	return ret;
}


/*
 * Destroys the Lunix character device.
 */
void lunix_chrdev_destroy(void)
{
	dev_t dev_no;
	unsigned int lunix_minor_cnt = lunix_sensor_cnt << 3;

	debug("entering destroy\n");
	dev_no = MKDEV(LUNIX_CHRDEV_MAJOR, 0);
	cdev_del(&lunix_chrdev_cdev); // remove the lunix_chrdev_cdev structure from the kernel
	unregister_chrdev_region(dev_no, lunix_minor_cnt);
	debug("leaving destroy\n");
}
