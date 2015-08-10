#include <linux/nvme.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <getopt.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <libudev.h>

//#include "lsnvme.h"

#define TAB "\t"

static const char NVME[] = "nvme";

/* opts and default values */
static struct options {
	struct size_spec {
		unsigned int div;
		char suffix;
	} sz;
	int verbose;
	bool disp_ctrl;
	bool disp_devs;
	bool headers;
} opts = {
	{ 1024 * 1000 * 1000, 'G' },
	0,
	false,
	true,
	false,
};

// search parents until grandfather for dirver
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

void lsnvme_printss_header(const char *tab)
{
	printf("%s[dev:ns]\tdevtype\tvendor\tmodel\trevision\tdev\tsize\n", tab);

}

static char *read_str(const char *path)
{
	static char read_str[32] = {0};
	int fd = open(path, O_RDONLY);

	if (fd < 0)
		return NULL;

	if (read(fd, read_str, 32) < 0)
		return NULL;

	close(fd);
	return read_str;
}

/*
 * Get size or retuyrn "-"
 * TODO: write our own atoull to enable more capacity
 * TODO: make it smarter about sizes, choose correct g/m/k by default
 * TODO: keep everything in llu, then conver to to long double?
 */
static char *bd_size(struct udev_device *dev)
{
	static char size_str[32];
	char path[128];
	long long int blocks;
	long long int block_size;
	double total;
	int ret;

	strncpy(path, udev_device_get_syspath(dev), 128);
	strcat(path, "/size");

	blocks = atoll(read_str(path));

	strncpy(path, udev_device_get_syspath(dev), 128);
	strcat(path, "/queue/logical_block_size");

	block_size = atoll(read_str(path));

	total = (blocks * block_size) / opts.sz.div;
	ret = snprintf(size_str, 32, "%.2f%c", total, opts.sz.suffix);

	if (ret >= 32)
		return "-";

	return size_str;
}

/*
 * [dev:ns]  devtype  vendor  model  revision  device_file  size
 */
void lsnvme_printbd(struct udev_device *dev, const char *tab)
{
	printf("%s[%s:%s]\t%s\t%s\t%s\t%s\t%s\t%s\n",
		tab,
		udev_device_get_sysnum(udev_device_get_parent(dev)),
		udev_device_get_sysnum(dev),
		udev_device_get_devtype(dev),
		udev_device_get_property_value(dev, "ID_VENDOR"),
		udev_device_get_property_value(dev, "ID_MODEL"),
		udev_device_get_property_value(dev, "ID_REVISION"),
		udev_device_get_devnode(dev),
		bd_size(dev)
	);
}

/*
 * [dev]  vendor  model  revision  device_file?  driver
 */
void lsnvme_printctrl(struct udev_device *dev)
{
	printf("[%s]\tvendor\tmodel\trevision\t%s\t%s\n",
		udev_device_get_sysnum(dev),
		udev_device_get_devnode(dev),
		find_driver(dev)
	);
}

int lsnvme_enum_ss(const char *custom)
{
	struct udev *udev;
	struct udev_enumerate *enum_parents, *enum_children;
	struct udev_list_entry *devices, *dev_list_entry;
	struct udev_device *dev;
	int count = 0;
	const char *path;

	udev = udev_new();

	if (udev == NULL)
		return -1;

	enum_parents = udev_enumerate_new(udev);
	enum_children = udev_enumerate_new(udev);

	udev_enumerate_add_match_subsystem(enum_parents, NVME);

	udev_enumerate_scan_devices(enum_parents);
	devices = udev_enumerate_get_list_entry(enum_parents);
	udev_list_entry_foreach(dev_list_entry, devices) {
		path = udev_list_entry_get_name(dev_list_entry);
		dev = udev_device_new_from_syspath(udev, path);

		if (opts.disp_ctrl && !opts.disp_devs) {
			lsnvme_printctrl(dev);
			udev_device_unref(dev);
		} else {
			udev_enumerate_add_match_parent(enum_children, dev);
		}
		++count;
	}

	udev_enumerate_unref(enum_parents);

	if (!opts.disp_devs || count == 0)
		goto out;

	udev_enumerate_scan_devices(enum_children);
	devices = udev_enumerate_get_list_entry(enum_children);
	udev_list_entry_foreach(dev_list_entry, devices) {
		path = udev_list_entry_get_name(dev_list_entry);
		dev = udev_device_new_from_syspath(udev, path);
		const char *dt = udev_device_get_devtype(dev);

		if (opts.disp_ctrl && strcmp(udev_device_get_subsystem(dev), NVME) == 0)
			lsnvme_printctrl(dev);
		else if (dt && strcmp(dt, "partition")) {
			lsnvme_printbd(dev, opts.disp_ctrl ? TAB : "");
			++count;
		} else {
			/* skip devtype == NULL for now */
			/* skip partitions for now */
		}

		udev_device_unref(dev);
	}

	udev_enumerate_unref(enum_children);
out:
	udev_unref(udev);

	return count;
}

static int usage(const char *progr)
{
	printf("Usage: %s [<switches>]\n", progr);

	printf("\nDisplay options:\n");
	printf("-s [GMKB]\tShow sizes in specific format (default: auto)\n");
	printf("-v\t\tVerbose output\n");

	printf("\nGeneral options:\n");
	printf("-h\t\tShow this help output and exit\n");
	printf("-v\t\tPrint version and exit\n");
	return EXIT_FAILURE;
}

static int version(const char *progr)
{
	fprintf(stdout, "%s: version\n", progr);
	return EXIT_SUCCESS;
}

static void set_size(char size_spec)
{
	switch (size_spec) {
	case 'b':
	case 'B':
		opts.sz.suffix = ' ';
		opts.sz.div = 1;
		break;
	case 'k':
	case 'K':
		opts.sz.suffix = 'K';
		opts.sz.div = 1024;
		break;
	case 'm':
		opts.sz.suffix = 'm';
		opts.sz.div = 1024 * 1000;
		break;
	case 'M':
		opts.sz.suffix = 'M';
		opts.sz.div = 1024 * 1024;
		break;
	default:
	case 'g':
		opts.sz.suffix = 'g';
		opts.sz.div = 1024 * 1000 * 1000;
		break;
	case 'G':
		opts.sz.suffix = 'G';
		opts.sz.div = 1024 * 1024 * 1024;
		break;
	}
}

int main(int argc, char *argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "Vvs:h")) != -1) {
		switch (opt) {
		case 'V':
			return version(argv[0]);
		case 'v':
			++opts.verbose;
			break;
		case 's':
			set_size(optarg[0]);
			break;
		case 'h':
		default:
			return usage(argv[0]);
		}
	}

	lsnvme_enum_ss(NULL);

	return EXIT_SUCCESS;
}
