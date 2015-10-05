lsnvme is a utility to list the attached NVMe controllers and the various
properties of their attached drives both local and remote.

Much of lsnvme is modeled after lsscsi written by Doug Gilbert.
Some features and options from lsblk and lspci are included as well.

**Installation**

lsnvme requires libudev (-devel for building) for walking through sysfs.
On RHEL distros this is a part of the systemd-devel package.

```
$ make
```

**Usage**

```
# NVMe controller number, namespace, device type, vendor, model, revision, device node, size
$ lsnvme
[0:1]	disk	NVMe	QEMU_NVMe_Ctrl	1.0	/dev/nvme0n1	128.00M
[0:2]	disk	NVMe	QEMU_NVMe_Ctrl	1.0	/dev/nvme0n2	128.00M

# NVMe controller number, vendor, model, parent subsystem, device node, kernel driver
$ lsnvme -H
[0]          Intel Corporation             QEMU Virtual Machine  pci          /dev/nvme0      nvme
[1]          Intel Corporation             QEMU Virtual Machine  pci          /dev/nvme1      nvme
```

**Known issues**

This initial release is not intended for production.

* Display of controllers with no namespaces is a TODO (need hardware)
* No RDMA backed devices supported (aftr milestone 3?)
* strip slashes from given paths
* take paths from /dev or /sys, fugure out which and do the right thing.
* sort entries with natural sort
* Test with non-default /sys and /dev classes
* sort entries properly
* this should work:  lsblk --output NAME,VENDOR,REV,MODEL,SERIAL,TYPE,WWN,SUBSYSTEMS

**How it should work**

```
+-HOST-SYSTEM----+                      +--TARGET-SYSTEM---------------+
|                |                      |                              |
|                |                      |                              |
| | RDMA Ctrl |  |                      |  | RDMA Ctrl |               |
|                |<=====(fabric)=======>|  | NVMe Ctrl |               |
+----------------+                      |  | disk1, disk2, ... |       |
                                        |                              |
                                        +------------------------------+

The output will look like the following for the above scenario:

# HOST SYSTEM
$ lsnvme -H
disk1 -> transport mapping
disk2 -> transport mapping

$ lsnvme -T
disk1   info  /dev/nvme0n1
disk2   info  /dev/nvme0n2

# TARGET SYSTEM
$ lsnvme -H
(empty?)

$ lsnvme -T
disk1   info  /dev/nvme0n1
disk2   info  /dev/nvme0n2

```
