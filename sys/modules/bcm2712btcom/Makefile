.include "../Makefile.inc"
.PATH: ${S}/dev/ic
KMOD=bcm2712btcom
EMBER_KERNEL_CONFIG?=EMBER64
CPPFLAGS+=-I${S}/arch/evbarm/compile/${EMBER_KERNEL_CONFIG}
SRCS=bcm2712btcom.c
.include <bsd.kmodule.mk>
