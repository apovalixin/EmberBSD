
.include "../Makefile.inc"

.PATH:	${S}/dev/acpi ${S}/dev/cadence

KMOD=	if_cemac_acpi
IOCONF=	if_cemac_acpi.ioconf
SRCS=	cemac_acpi.c if_cemac.c

.include <bsd.kmodule.mk>
