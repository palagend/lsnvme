#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <libudev.h>

static struct udev *udev;

// search parents until grandfather for driver
static const char *find_driver(struct udev_device *node)
{
	static unsigned int count = 3;
	const char *p = udev_device_get_driver(node);
	struct udev_device *parent = node;

	while (p == NULL && count > 0) {
		parent = udev_device_get_parent(parent);
		p = udev_device_get_driver(parent);
		--count;
	}

	return p;
}

static const char *pr_devname(struct udev_device *dev)
{
	return udev_device_get_devnode(dev);
}

static void pr_devinfo(struct udev_device *dev)
{
	struct udev_device *pdev;
	struct udev_list_entry *head, *current;

	printf("Device Node Path: %s\n", udev_device_get_devnode(dev));
	printf("Sys Path: %s\n", udev_device_get_syspath(dev));
	printf("Sys Name: %s\n", udev_device_get_sysname(dev));
	printf("Subsystem: %s\n", udev_device_get_subsystem(dev));
	printf("devtype: %s\n", udev_device_get_devtype(dev));
	printf("sysnum: %s\n", udev_device_get_sysnum(dev));
	printf("driver: %s\n", find_driver(dev));

	pdev = udev_device_get_parent(dev);
	printf("parent_sysname: %s\n", udev_device_get_subsystem(pdev));
	printf("parent_sysnnum: %s\n", udev_device_get_sysnum(pdev));

	/* From here, we can call get_sysattr_value() for each file
 	 *in the device's /sys entry. The strings passed into these
	 *functions (idProduct, idVendor, serial, etc.) correspond
	 *directly to the files in the directory which represents
	 *the USB device. Note that USB strings are Unicode, UCS2
	 *encoded, but the strings returned from
	 *udev_device_get_sysattr_value() are UTF-8 encoded. */

	head = udev_device_get_sysattr_list_entry(dev);
	printf("attributes:\n");
	udev_list_entry_foreach(current, head) {
		const char *key = udev_list_entry_get_name(current);

		printf("\t%s\n", key);
	//	printf("\t%s : %s\n", key, udev_device_get_sysattr_value(dev, key));
	}

	head = udev_device_get_properties_list_entry(dev);
	printf("properties:\n");
	udev_list_entry_foreach(current, head) {
		printf("\t%s : %s\n",
		udev_list_entry_get_name(current),
		udev_list_entry_get_value(current));
	}
}

int main (int argc, char *argv[])
{
	struct udev_enumerate *enumerate, *e2;
	struct udev_list_entry *devices, *dev_list_entry;
	struct udev_device *dev, *pdev, *cdev;
	const char *path;
	
	/* Create the udev object */
	udev = udev_new();
	assert (udev);
	
	enumerate = udev_enumerate_new(udev);
	e2 = udev_enumerate_new(udev);

	while (argc > 1)
		udev_enumerate_add_match_subsystem(enumerate, argv[--argc]);

	udev_enumerate_scan_devices(enumerate);
	//udev_enumerate_scan_subsystems(enumerate);
	devices = udev_enumerate_get_list_entry(enumerate);
	udev_list_entry_foreach(dev_list_entry, devices) {
		
		/* Get the filename of the /sys entry for the device
		 * and create a udev_device object (dev) representing it */
		path = udev_list_entry_get_name(dev_list_entry);
		dev = udev_device_new_from_syspath(udev, path);
	
		// enumerate over all descendents of first enumeration
		udev_enumerate_add_match_parent(e2, dev);	
		udev_enumerate_scan_devices(e2);
		devices = udev_enumerate_get_list_entry(e2);
		udev_list_entry_foreach(dev_list_entry, devices) {
		
			/* Get the filename of the /sys entry for the device
			 * and create a udev_device object (dev) representing it */
			path = udev_list_entry_get_name(dev_list_entry);
			cdev = udev_device_new_from_syspath(udev, path);
			pr_devinfo(cdev);
			udev_device_unref(cdev);
		}
	}

	udev_enumerate_unref(e2);
	udev_enumerate_unref(enumerate);

	udev_unref(udev);

	return 0;       
}
