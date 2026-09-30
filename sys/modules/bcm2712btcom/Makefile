.include "../Makefile.inc"
.PATH: ${S}/dev/ic
KMOD=bcm2712btcom
OXTORG_KERNEL_CONFIG?=OXTORG64
CPPFLAGS+=-I${S}/arch/evbarm/compile/${OXTORG_KERNEL_CONFIG}
SRCS=bcm2712btcom.c
.include <bsd.kmodule.mk>
