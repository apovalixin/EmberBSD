# U-Boot script for the Orange Pi Zero 4 (Allwinner A733).
#
# The board vendor's U-Boot looks for boot.scr on the first partition of
# the card. An image builder wraps this file into that script and puts the
# native kernel and the device tree beside it. The tree gets spare room
# before booti, because the boot loader adds its own properties to it.
setenv fdt_addr_r 0x4fa00000
setenv bootargs "root=NAME=netbsd-root"
load ${devtype} ${devnum}:${distro_bootpart} ${fdt_addr_r} ${prefix}dtb/allwinner/sun60i-a733-orangepi-zero4.dtb
fdt addr ${fdt_addr_r}
fdt resize 8192
load ${devtype} ${devnum}:${distro_bootpart} 0x44000000 ${prefix}netbsd.img
booti 0x44000000 - ${fdt_addr_r}
