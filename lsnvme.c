// only needed for ioctl interface:
//#include <linux/nvme.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <ctype.h>
#include <getopt.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <mntent.h>

#include <libudev.h>

#include <linux/nvme.h>

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

// given absolute path, return udev_device or null
// mostly borrows from udevadm implementation
static struct udev_device *find_device(char *path)
{
	if (!path)
		return NULL;

	if (strncmp(path, DEV, strlen(DEV)) == 0) {
		struct stat statbuf;
		char type;

		if (stat(path, &statbuf) < 0)
			return NULL;

		if (S_ISBLK(statbuf.st_mode))
			type = 'b';
		else if (S_ISCHR(statbuf.st_mode))
			type = 'c';
		else
			return NULL;

		return udev_device_new_from_devnum(udev, type, statbuf.st_rdev);

	} else if (strncmp(path, SYS, strlen(SYS)) == 0) {
		size_t sz = strlen(path);

		while (sz > 0 && path[sz-1] == '/') {
			path[sz-1] = 0;
			--sz;
		}
		return udev_device_new_from_syspath(udev, path);
	} else {
		return NULL;
	}
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
	printf("%s[dev:ns]\tdevtype\tvendor\tmodel\trevision\tdev\tsize\n",
		tab);
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
		ret = snprintf(size_str, 32, "%.2f%c",
				total, disk_sizes[opts.sz].suffix);
	}

	if (ret >= 32)
		return "-";

	return size_str;
}

static int lsnvme_identify_ns(struct udev_device *dev, struct nvme_id_ns *ptr)
{
	const char *devpath = udev_device_get_devnode(dev);
	uint32_t ns = atoi(udev_device_get_sysnum(dev));

	int fd = open(devpath, O_RDONLY|O_NONBLOCK);
	if (fd < 0) {
		perror(devpath);
		return errno;
	}

	struct nvme_admin_cmd cmd = {
		.opcode = nvme_admin_identify,
		.nsid = ns,
		.addr = (uint64_t) ptr,
		.data_len = 4096,
		.cdw10 = 0,
	};

	return ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd);
}

static int lsnvme_identify_ctrl(struct udev_device *dev,
				struct nvme_id_ctrl *ptr)
{
	const char *devpath = udev_device_get_devnode(dev);

	int fd = open(devpath, O_RDONLY|O_NONBLOCK);
	if (fd < 0) {
		perror(devpath);
		return errno;
	}

	struct nvme_admin_cmd cmd = {
		.opcode = nvme_admin_identify,
		.addr = (uint64_t) ptr,
		.data_len = 4096,
		.cdw10 = 1,
	};
	
	return ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd);
}

void lsnvme_printctrl_id(struct nvme_id_ctrl *id)
{
	id->sn[sizeof(id->sn)-1] = 0;
	id->mn[sizeof(id->mn)-1] = 0;
	id->fr[sizeof(id->fr)-1] = 0;

	printf("%sPCI Vendor ID: %x\n", TAB, id->vid);
	printf("%sPCI Subsystem Vendor ID: %x\n", TAB, id->ssvid);
	printf("%sSerial Number: %s\n", TAB, id->sn);
	printf("%sModel Number: %s\n", TAB, id->mn);
	printf("%sFirmware Revision: %s\n", TAB, id->fr);

	printf("%sIEEE OUI Identifier: %02x%02x%02x\n",
		TAB, id->ieee[2], id->ieee[1], id->ieee[0]);

	printf("%sController ID: %x\n", TAB, id->cntlid);
	printf("%sVersion: %x\n", TAB, id->ver);
	printf("%sNumber of Namespaces: %d\n", TAB, id->nn);
}

void lsnvme_printctrl_ns(struct nvme_id_ns *ns)
{
	printf("%sNamespace Size: %#"PRIx64"\n",
		TAB, (uint64_t)le64toh(ns->nsze));
	printf("%sNamespace Capacity: %#"PRIx64"\n",
		TAB, (uint64_t)le64toh(ns->ncap));
	printf("%sNamespace Utilization: %#"PRIx64"\n",
		TAB, (uint64_t)le64toh(ns->nuse));
}
	
/*
 * [dev:ns] device_file devtype size <vendor model revision>
 */
void lsnvme_printbd(struct udev_device *dev, const char *tab)
{
	printf("[%s:%s]\t%s\t%s\t%s\t%s\t%s\t%s\n",
		udev_device_get_sysnum(udev_device_get_parent(dev)),
		udev_device_get_sysnum(dev),
		udev_device_get_devnode(dev),
		udev_device_get_devtype(dev),
		bd_size(dev),
		udev_device_get_property_value(dev, "ID_VENDOR"),
		udev_device_get_property_value(dev, "ID_MODEL"),
		udev_device_get_property_value(dev, "ID_REVISION")
	);

	if (opts.verbose) {
		struct nvme_id_ns ns;
		if(lsnvme_identify_ns(dev, &ns))
			fprintf(stderr, "%sioctl failed on: %s\n",
				TAB, udev_device_get_devnode(dev));
		else
			lsnvme_printctrl_ns(&ns);
	}
}

/*
 * [dev:ns:pn] device_file devtype size (part type?)
 */
void lsnvme_printpart(struct udev_device *dev, const char *tab)
{
	struct udev_device *parent = udev_device_get_parent(dev);

	printf("[%s:%s:%s]\t%s\t%s\t%s\n",
		udev_device_get_sysnum(udev_device_get_parent(parent)),
		udev_device_get_sysnum(udev_device_get_parent(dev)),
		udev_device_get_sysnum(dev),
		udev_device_get_devnode(dev),
		udev_device_get_devtype(dev),
		bd_size(dev)
	);
}

/*
 * [dev] device_file vendor  model  bus  driver (transport?)
 */
void lsnvme_printctrl(struct udev_device *dev)
{
	struct udev_device *pdev = udev_device_get_parent(dev);

	printf("[%s]\t%s\t%s\t%s\t%s\t%s\n",
		udev_device_get_sysnum(dev),
		udev_device_get_devnode(dev),
		udev_device_get_property_value(pdev, "ID_VENDOR_FROM_DATABASE"),
		udev_device_get_property_value(pdev, "ID_MODEL_FROM_DATABASE"),
		udev_device_get_subsystem(pdev),
		find_driver(dev)
	);

	if (opts.verbose) {
		struct nvme_id_ctrl id;
		if(lsnvme_identify_ctrl(dev, &id))
			fprintf(stderr, "%sioctl failed on: %s\n",
				TAB, udev_device_get_devnode(dev));
		else
			lsnvme_printctrl_id(&id);
	}
}

static int lsnvme_ls(char *path)
{
	struct udev_device *dev = find_device(path);
	const char *dt;
	if (dev == NULL)
		return EXIT_FAILURE;

	dt = udev_device_get_devtype(dev);

	if (strcmp(udev_device_get_subsystem(dev), NVME) == 0)
		lsnvme_printctrl(dev);
	else if (dt && strcmp(dt, "partition"))
		lsnvme_printbd(dev, "");
	else
		lsnvme_printpart(dev, "");

	return EXIT_SUCCESS;
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
		if (udev_device_get_devnum(cdev) ==
		    udev_device_get_devnum(parent))
			continue;

		if (dt && strcmp(dt, "partition")) {
			lsnvme_printbd(cdev, opts.disp_ctrl ? TAB : "");
		} else {
			lsnvme_printpart(cdev, "");
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

// TODO: don't allocate mem if mount points are equal?
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
			SYS = strdup(fs->mnt_dir);
		else if (strcmp(fs->mnt_type, "devtmpfs") == 0)
			DEV = strdup(fs->mnt_dir);

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
	{"tree",	no_argument, 0, 't'},
	{"targets",	no_argument, 0, 'T'},
	{"discover",	no_argument, 0, 'D'},
	{"m",		no_argument, 0, 'm'},
	{"headers",	no_argument, &opts.headers, 1},
	{"version",	no_argument, 0, 'V'},
	{"verbose",	no_argument, 0, 'v'},
	{"help",	no_argument, 0, 'h'},
	{0,0,0,0}
};

static const char *help_strings[][2] = {
	{"SIZE",	"\tspecific size from [GMKB], default: auto"},
	{"",		"\tdisplay host(s) attached to this target system"},
	{"",		"\tdisplay tree-like diagram if possible"},
	{"",		"\tlist targets attached to this host (WIP)"},
	{"",		"Go 'discover' resources available for a host (WIP)"},
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

	printf("\nUsage: %s [<switches>] [ devices.. ]\n", progr);

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

	printf("\n");
	return EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
	int opt, option_index, ret = EXIT_SUCCESS;

	while ((opt = getopt_long(argc, argv, "s:DHTCtmVvh",
				  long_options, &option_index)) != -1) {
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
		/* TODO: Not sure what -C is doing if it's
		 * the same as -H?
		 */
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
		case 'D':
			printf("lsnvme WIP...\"Go discover NVMe resources ");
			printf("on this target system that\n");
			printf("are available for a host/initiator.\"\n");
			printf("OR\n");
			printf("\"Go discover NVMe resources \'out there\' ");
			printf("on the network for\nthe host/initiator.\"\n");
			return ret;
		case 'T':
			printf("lsnvme WIP...\"Go list the NVMe subsystem ");
			printf("targets and the allocated cntlid's\n");
			printf("in each attached subsystem to ");
			printf("this host/initiator.\"\n");
			return ret;
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

	if (optind < argc) {
		while (optind < argc) 
			if(lsnvme_ls(argv[optind++]))
				fprintf(stderr, 
					"%s: unable to get info for: %s\n",
					argv[0], argv[optind-1]);
	} else
		ret = lsnvme_enum_ctrl();

	udev_unref(udev);
	return ret;
}
