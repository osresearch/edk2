# UefiPayload Package

## Coreboot
TBD
## Slimbootloader
TBD

## LinuxBoot

[Discussion Doc](https://docs.google.com/document/d/1mU6ICHTh0ot8U45uuRENKOGI8cVzizdyWHGYHpEguVg)

### Build UefiPayload
```shell
$ git clone --branch uefipayload --recursive https://github.com/chengchiehhuang/edk2 uefipayload
$ make -C BaseTools
$ source edksetup.sh
$ build -a X64 -p UefiPayloadPkg/UefiPayloadPkg.dsc -b DEBUG -t GCC5 -D BOOTLOADER=LINUXBOOT
# payload will be in Build/UefiPayloadPkgX64/DEBUG_GCC5/FV/UEFIPAYLOAD.fd
```

NOTE: If you want to use QEMU Video Driver, you need to cherry-pick this [patch](https://github.com/chengchiehhuang/edk2/commit/5cacfe275dc3b0dd4ad76ba0a8d2c84f01a0a2c0).

### Build uefiboot

#### Using with u-root

* read u-root [README](https://github.com/u-root/u-root/blob/master/README.md).
```shell
$ export UEFIPAYLOAD_PATH=<path_to_uefipayload>
$ go get github.com/u-root/u-root
$ u-root -files "$UEFIPAYLOAD_PATH:/ext/uefipayload" \
 -uinitcmd="/bbin/uefiboot /ext/uefipayload" core cmds/exp/uefiboot
```

* Testing in QEMU
```shell
$ export KERNEL=<path_to_kernel>
$ qemu-system-x86_64%20-M%20q35,accel=kvm -kernel ${KERNEL} -initrd \
/tmp/initramfs.linux_amd64.cpio -m 4G -device ide-hd,drive=sda -drive \
file=${OS_IMAGE},if=none,id=sda
```

#### Standalone
```shell
$ export UEFIPAYLOAD_PATH=<path_to_uefipayload>
$ git clone https://github.com/u-root/u-root
$ go build cmds/exp/uefiboot/main.go
$ ./main ${UEFIPAYLOAD_PATH}
```

### Know Issues:
* Not all real machine works.
* QEMU need to run with -machine q35.
