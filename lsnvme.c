#include <linux/nvme.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <getopt.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>

#include <libudev.h>

#define TAB "\t"

static const char NVME[] = "nvme";

enum {
	SZ_B,
	SZ_KB,
	SZ_MB,
	SZ_GB,
	SZ_AUTO,
};

/* opts and default values */
static struct options {
	int sz;
	int verbose;
	bool disp_ctrl;
	bool disp_devs;
	bool headers;
} opts = {
	SZ_AUTO,
	0,
	false,
	true,
	false,
};

static struct size_spec {
	unsigned int div;
	char suffix;
} disk_sizes[] = {
	[SZ_B] = { 1, 'B' },
	[SZ_KB] = { 1024, 'K' },
	[SZ_MB] = { 1024 * 1000, 'M' },
	[SZ_GB] = { 1024 * 1000 * 1000, 'G' },
	[SZ_AUTO] = { 0, 0 },
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

/*
 * TODO: make this char buffer the correct size
 */
static char *read_str(const char *path)
{
	static char read_str[32] = {0};
	int fd = open(path, O_RDONLY);

	if (fd < 0)
		return NULL;

	if (read(fd, read_str, 32) < 0) {
		close(fd);
		return NULL;
	}

	close(fd);
	return read_str;
}

int find_sz(unsigned long long total)
{
	int s = SZ_AUTO-1;

	for (; s >= 0 && disk_sizes[s].div > total; --s) {}

	return s;
}


/*
 * Get size or return "-"
 * TODO: support Terabyte size
 */
static char *bd_size(struct udev_device *dev)
{
	static char size_str[32];
	char path[128];
	char *nptr, *endptr;
	unsigned long long int blocks;
	unsigned int block_size;
	double total;
	int ret;

	strncpy(path, udev_device_get_syspath(dev), 128);
	strcat(path, "/size");

	nptr = read_str(path);

	blocks = strtoull(nptr, &endptr, 10);

	// no valid digits || overflow
	if ((blocks == 0 && nptr == endptr ) ||
	    (blocks == ULLONG_MAX && errno == ERANGE))
		return "-";

	strncpy(path, udev_device_get_syspath(dev), 128);
	strcat(path, "/queue/logical_block_size");

	block_size = atoi(read_str(path));

	if (opts.sz == SZ_AUTO)
		opts.sz = find_sz(blocks * block_size);

	if (opts.sz == SZ_B) {
		ret = snprintf(size_str, 32, "%llu", blocks * block_size);
	} else {
		total = (double)(blocks * block_size) / disk_sizes[opts.sz].div;
		ret = snprintf(size_str, 32, "%.2f%c", total, disk_sizes[opts.sz].suffix);
	}

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
 * [dev]  vendor  model  bus  device_file?  driver
 */
void lsnvme_printctrl(struct udev_device *dev)
{
	struct udev_device *pdev = udev_device_get_parent(dev);

	printf("[%s]\t%s\t%s\t%s\t%s\t%s\n",
		udev_device_get_sysnum(dev),
		udev_device_get_property_value(pdev, "ID_VENDOR_FROM_DATABASE"),
		udev_device_get_property_value(pdev, "ID_MODEL_FROM_DATABASE"),
		udev_device_get_subsystem(pdev),
		udev_device_get_devnode(dev),
		find_driver(dev)
	);
}

static int lsnvme_enum_devs(struct udev *udev, struct udev_device *dev)
{
	struct udev_enumerate *enum_children = udev_enumerate_new(udev);
	struct udev_list_entry *devices, *dev_list_entry;
	struct udev_device *cdev;
	const char *path;

	udev_enumerate_add_match_parent(enum_children, dev);

	udev_enumerate_scan_devices(enum_children);
	devices = udev_enumerate_get_list_entry(enum_children);

	udev_list_entry_foreach(dev_list_entry, devices) {
		path = udev_list_entry_get_name(dev_list_entry);
		cdev = udev_device_new_from_syspath(udev, path);
		const char *dt = udev_device_get_devtype(cdev);

		if (opts.disp_ctrl && strcmp(udev_device_get_subsystem(cdev), NVME) == 0) {
			lsnvme_printctrl(dev);
		} else if (opts.disp_devs && dt && strcmp(dt, "partition")) {
			lsnvme_printbd(cdev, opts.disp_ctrl ? TAB : "");
		} else {
			/* skip devtype == NULL for now */
			/* skip partitions for now */
		}

		udev_device_unref(cdev);
	}

	udev_enumerate_unref(enum_children);

	return 0;
}

static int lsnvme_enum_ctrl(const char *custom)
{
	struct udev *udev;
	struct udev_enumerate *enum_parents;
	struct udev_list_entry *devices, *dev_list_entry;
	struct udev_device *dev;
	const char *path;

	udev = udev_new();

	if (udev == NULL)
		return EXIT_FAILURE;

	enum_parents = udev_enumerate_new(udev);

	udev_enumerate_add_match_subsystem(enum_parents, NVME);

	udev_enumerate_scan_devices(enum_parents);
	devices = udev_enumerate_get_list_entry(enum_parents);
	udev_list_entry_foreach(dev_list_entry, devices) {
		path = udev_list_entry_get_name(dev_list_entry);
		dev = udev_device_new_from_syspath(udev, path);

		lsnvme_enum_devs(udev, dev);

		udev_device_unref(dev);
	}

	udev_enumerate_unref(enum_parents);

	udev_unref(udev);

	return EXIT_SUCCESS;
}

static int usage(const char *progr)
{
	printf("Usage: %s [<switches>]\n", progr);

	printf("\nDisplay options:\n");
	printf("-H\t\tDisplay the controllers in addition to the attached devices\n");
	printf("-s [GMKB]\tShow sizes in specific format (default: auto)\n");
	printf("-v\t\tVerbose output\n");

	printf("\nGeneral options:\n");
	printf("-h\t\tShow this help output and exit\n");
	printf("-v\t\tPrint version and exit\n");
	return EXIT_FAILURE;
}

static int version(const char *progr)
{
	fprintf(stdout, "%s: 0.1\n", progr);
	return EXIT_SUCCESS;
}

static void set_size(char size_spec)
{
	switch (size_spec) {
	case 'b':
	case 'B':
		opts.sz = SZ_B;
		break;
	case 'k':
	case 'K':
		opts.sz = SZ_KB;
		break;
	case 'M':
	case 'm':
		opts.sz = SZ_MB;
		break;
	case 'G':
	case 'g':
		opts.sz = SZ_GB;
		break;
	default:
		opts.sz = SZ_AUTO;
		break;
	}
}

int main(int argc, char *argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "HVvs:h")) != -1) {
		switch (opt) {
		case 'H':
			opts.disp_ctrl = true;
			opts.disp_devs = false;
			break;
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

	lsnvme_enum_ctrl(NULL);

	return EXIT_SUCCESS;
}
