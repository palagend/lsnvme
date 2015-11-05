lsnvme is a utility to list the attached NVMe controllers and the various
properties of their attached drives both local and remote.

Much of lsnvme is modeled after lsscsi written by Doug Gilbert.
Some features and options from lsblk and lspci are included as well.

**Building and Installation**

lsnvme requires libudev (-devel for building) for walking through sysfs.
On RHEL distros this is a part of the systemd-devel package. On earlier or
non systemd distros these are probably in libudev(-devel).

```
$ make
$ make install
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

This initial release is not intended for production. Some TODOs:

* No devices over fabric supported yet (waiting for upstream support).
* Sort entries with natural sort.
* Test with non-default /sys and /dev.
* Tree/machine output.

**Example output**

```
+-HOST-SYSTEM----+                      +--TARGET-SYSTEM---------------+
|                |                      |                              |
|                |                      |                              |
| | RDMA Ctrl |  |                      |  | RDMA Ctrl |               |
|                |<=====(fabric)=======>|  | NVMe Ctrl |               |
+----------------+                      |  | disk1, disk2, ... |       |
                                        |                              |
                                        +------------------------------+
```

The output will look like the following:

```
# FROM HOST SYSTEM
$ lsnvme -H
[0]     /dev/nvme0      Intel Corporation       (null)  pci     nvme    (null)

$ lsnvme -v
[0:1]   /dev/nvme0n1    disk    12502.51G       (null)  (null)  (null)
  Namespace Size: 390703446
  Namespace Capacity: 390703446
  Namespace Utilization: 390703446
  NVM Capacity: 140733787268064

# or specify dev and sysfs entries directly:
$ lsnvme -v /dev/nvme0 /sys/class/nvme/nvme0/nvme0n1/
[0]     /dev/nvme0      Intel Corporation       (null)  pci     nvme    (null)
  PCI Vendor ID: 8086
  PCI Subsystem Vendor ID: 8086
  Serial Number: CVMD433000CL1P6KGN
  Model Number: INTEL SSDPEDME016T4
  Firmware Revision: 8DV1013
  IEEE OUI Identifier: 5cd2e4
  Controller ID: 0
  Version: 0
  Number of Namespaces: 1
[0:1]   /dev/nvme0n1    disk    12502.51G       (null)  (null)  (null)
  Namespace Size: 390703446
  Namespace Capacity: 390703446
  Namespace Utilization: 390703446
  NVM Capacity: 140731497137168

$ lsnvme -T
TODO

```
