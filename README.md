lsnvme is a utility to list the attached NVMe controllers and the various
properties of their attached drives both local and remote.

Much of lsnvme is modeled after lsscsi written by Doug Gilbert.

** Installation **

lsnvme requires libudev (-devel for building) for walking through sysfs.
On RHEL distros this is a part of the systemd-devel package.

```
$ make
```

** Usage **

```
$ lsnvme -s M
[0:1]	disk	NVMe	QEMU_NVMe_Ctrl	1.0	/dev/nvme0n1	128.00M
[0:2]	disk	NVMe	QEMU_NVMe_Ctrl	1.0	/dev/nvme0n2	128.00M
```

** Known issues **

This initial release is not intended for production.

* Front end arg parsing isn't hooked up, almost no arguments are supported.
* Display of controllers (including ones with no namespaces) is a TODO
* No RDMA backed devices supported (need hardware)
* Many small bugs.
