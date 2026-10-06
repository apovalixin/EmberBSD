# $NetBSD: Makefile,v 1.5 2011/01/19 11:55:50 plunky Exp $
# Origin: EmberBSD - initialize pthreads before Lua loads threaded native modules.

PROG=		lua
SRCS=		lua.c
BINDIR=		/usr/bin

CPPFLAGS+=	-DLUA_USE_READLINE

LDADD+=		-llua -lm -ledit -lterminfo -lpthread
DPADD+=		${LIBLUA} ${LIBM} ${LIBEDIT} ${LIBTERMINFO} ${LIBPTHREAD}

.include <bsd.own.mk>

.PATH:		${NETBSDSRCDIR}/external/mit/lua/dist/src \
		${NETBSDSRCDIR}/external/mit/lua/dist/doc

.include <bsd.prog.mk>
