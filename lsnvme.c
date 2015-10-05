// only needed for ioctl interface:
//#include <linux/nvme.h>

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
#include <mntent.h>

#include <libudev.h>

#define TAB "  "
#define TEE "├─"
#define ELB "└─"

static const char NVME[] = "nvme";
static const char *SYS = "/sys";
static const char *DEV = "/dev";

static struct udev *udev;

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
	bool disp_tree;
	bool disp_machine;
	int headers;
} opts = {
	SZ_AUTO,	/* determine size */
	0,		/* verbosity */
	false,		/* display controllers */
	true,		/* display block devs */
	false,		/* display as tree */
	false,		/* machine readable output */
	0,		/* print headers */
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

// search parents until grandfather for driver
static const char *find_driver(struct udev_device *node)
{
	unsigned int count = 3;
	const char *p = udev_device_get_driver(node);
	struct udev_device *parent = node;

	while (p == NULL && count > 0) {
		parent = udev_device_get_parent(parent);
		p = udev_device_get_driver(parent);
		--count;
	}

	return p;
}

// search parents of device until father for LBS
static int find_lbs(struct udev_device *node)
{
	unsigned int count = 2;
	char *p = NULL;
	char path[128];

	do {
		strncpy(path, udev_device_get_syspath(node), 128);
		strcat(path, "/queue/logical_block_size");

		p = read_str(path);
		if (p)
			return atoi(p);	
		else {
			--count;
			node = udev_device_get_parent(node);
		}
	} while (count > 0);

	return -1;
}

void lsnvme_printss_header(const char *tab)
{
	printf("%s[dev:ns]\tdevtype\tvendor\tmodel\trevision\tdev\tsize\n", tab);
}

static int find_sz(unsigned long long total)
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
	int block_size;
	double total;
	int ret;

	strncpy(path, udev_device_get_syspath(dev), 128);
	strcat(path, "/size");

	nptr = read_str(path);

	if (!nptr)
		return "-";

	blocks = strtoull(nptr, &endptr, 10);

	// no valid digits || overflow
	if ((blocks == 0 && nptr == endptr ) ||
	    (blocks == ULLONG_MAX && errno == ERANGE))
		return "-";

	block_size = find_lbs(dev);	
	if (block_size == -1)
		return "-";

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
 * [dev:ns:pn]  devtype  device_file  size
 */
void lsnvme_printpart(struct udev_device *dev, const char *tab)
{
	struct udev_device *parent = udev_device_get_parent(dev);

	printf("%s[%s:%s:%s]\t%s\t%s\t%s\n",
		tab,
		udev_device_get_sysnum(udev_device_get_parent(parent)),
		udev_device_get_sysnum(udev_device_get_parent(dev)),
		udev_device_get_sysnum(dev),
		udev_device_get_devtype(dev),
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

/*
 * Given a /sys/class/.. path, try figuring out what kind
 * of NVME dev it is and print the appropriate info.
 */
static int lsnvme_dev(const char *path)
{
	struct udev_device *dev = udev_device_new_from_syspath(udev, path);

	if (dev == NULL)
		return EXIT_FAILURE;

	if (strcmp(udev_device_get_subsystem(dev), NVME) == 0)
		lsnvme_printctrl(dev);
	else
		lsnvme_printbd(dev, "");

	return EXIT_SUCCESS;
}

static int lsnvme_ls(const char *path)
{
	// TODO: figure out how to map from /dev -> /sys/class
	return lsnvme_dev(path);
}

static int lsnvme_enum_devs(struct udev_device *parent)
{
	struct udev_enumerate *enum_children = udev_enumerate_new(udev);
	struct udev_list_entry *devices, *dev_list_entry;
	struct udev_device *cdev;
	const char *path;

	udev_enumerate_add_match_parent(enum_children, parent);

	udev_enumerate_scan_devices(enum_children);
	devices = udev_enumerate_get_list_entry(enum_children);

	udev_list_entry_foreach(dev_list_entry, devices) {
		path = udev_list_entry_get_name(dev_list_entry);
		cdev = udev_device_new_from_syspath(udev, path);
		const char *dt = udev_device_get_devtype(cdev);

		/* skip parent */
		if (udev_device_get_devnum(cdev) == udev_device_get_devnum(parent))
			continue;

		if (dt && strcmp(dt, "partition")) {
			lsnvme_printbd(cdev, opts.disp_ctrl ? TAB : "");
		} else {
			lsnvme_printpart(cdev, opts.disp_ctrl ? TAB : "");
		}

		udev_device_unref(cdev);
	}

	udev_enumerate_unref(enum_children);

	return EXIT_SUCCESS;
}

static int lsnvme_enum_ctrl(void)
{
	struct udev_enumerate *enum_parents;
	struct udev_list_entry *devices, *dev_list_entry;
	struct udev_device *dev;
	const char *path;

	enum_parents = udev_enumerate_new(udev);

	udev_enumerate_add_match_subsystem(enum_parents, NVME);

	udev_enumerate_scan_devices(enum_parents);
	devices = udev_enumerate_get_list_entry(enum_parents);
	udev_list_entry_foreach(dev_list_entry, devices) {
		path = udev_list_entry_get_name(dev_list_entry);
		dev = udev_device_new_from_syspath(udev, path);

		if (opts.disp_ctrl)
			lsnvme_printctrl(dev);

		if (opts.disp_devs)
			lsnvme_enum_devs(dev);

		udev_device_unref(dev);
	}

	udev_enumerate_unref(enum_parents);

	return EXIT_SUCCESS;
}

static void lsnvme_get_mount_paths(void)
{
	FILE *fp;
	struct mntent *fs;
	const char *mounts = "/proc/mounts";

	fp = setmntent(mounts, "r");
	if (fp == NULL) {
		fprintf(stderr, "Could not open: %s\n", mounts);
		return;
	}

	while ((fs = getmntent(fp)) != NULL)
		if (strcmp(fs->mnt_type, "sysfs") == 0)
			SYS = strdup(fs->mnt_fsname);
		else if (strcmp(fs->mnt_type, "devtmpfs") == 0)
			DEV = strdup(fs->mnt_fsname);

	endmntent(fp);
}

static int version(const char *progr)
{
	fprintf(stdout, "%s: 0.2\n", progr);
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

static struct option long_options[] = {
	{"size",	required_argument, 0, 's'},
	{"host",	optional_argument, 0, 'H'},
	{"host",	optional_argument, 0, 'C'},
	{"tree",	no_argument, 0, 't'},
	{"m",		no_argument, 0, 'm'},
	{"headers",	no_argument, &opts.headers, 1},
	{"version",	no_argument, 0, 'V'},
	{"verbose",	no_argument, 0, 'v'},
	{"help",	no_argument, 0, 'h'},
	{0,0,0,0}
};

static const char *help_strings[][2] = {
	{"SIZE",	"\tspecific size from [GMKB], default: auto"},
	{"",		"\tdisplay host context"},
	{"",		"\tdisplay tree-like diagram if possible"},
	{"",		"\tmachine readable output"},
	{"",		"\tprint descriptive headers"},
	{"",		"\tdisplay version and exit"},
	{"",		"\tincrease verbosity level"},
	{"",		"\tdisplay this help and exit"},
	{"", ""}
};

static int usage(const char *progr)
{
	const struct option *ptr = long_options;

	printf("Usage: %s [<switches>] [ devices.. ]\n\n", progr);

	for (int i = 0; ptr->name != NULL; ++i, ptr = &long_options[i])
		if (ptr->has_arg == required_argument)
			printf("  -%c, --%s=%s%s\n", ptr->val, ptr->name,
				help_strings[i][0], help_strings[i][1]);
		else if (ptr->flag && ptr->val)
			printf("  --%s\t%s\n", ptr->name,
				help_strings[i][1]);
		else
			printf("  -%c, --%s\t%s\n", ptr->val, ptr->name,
				help_strings[i][1]);

	return EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
	int opt, option_index, ret = EXIT_SUCCESS;

	while ((opt = getopt_long(argc, argv, "s:HCtmVvh", long_options, &option_index)) != -1) {
		switch (opt) {
		case 0: /* longopt only, may not be used */
			printf("option: %s\n", long_options[option_index].name);
			printf("%d\n", opts.headers);
			break;
		case 's':
			set_size(optarg[0]);
			break;
		case 'H':
			opts.disp_ctrl = true;
			opts.disp_devs = false;
			break;
		case 'C':
			opts.disp_ctrl = true;
			opts.disp_devs = false;
			break;
		case 't':
			opts.disp_tree = true;
			break;
		case 'm':
			opts.disp_machine = true;
			break;
		case 'V':
			return version(argv[0]);
		case 'v':
			++opts.verbose;
			break;
		case 'h':
		case '?':
		default:
			return usage(argv[0]);
		}
	}

	udev = udev_new();

	if (udev == NULL) {
		fprintf(stderr, "failed to initialize udev library, exiting\n");
		return EXIT_FAILURE;
	} else {
		lsnvme_get_mount_paths();
	}

	if (optind < argc)
		while (optind < argc)
			ret += lsnvme_ls(argv[optind++]);
	else
		ret = lsnvme_enum_ctrl();

	udev_unref(udev);
	return ret;
}
