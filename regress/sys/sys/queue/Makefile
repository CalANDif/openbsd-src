# kcompile uses the stock bsd.prog.mk machinery; safeunlink is built twice
# from one source via explicit rules below, so it is not listed in PROGS.
PROGS=		kcompile
SRCS_kcompile=	kcompile.c

# Pick up the in-tree <sys/queue.h>, not the installed one.
CFLAGS+=	-I${.CURDIR}/../../../../sys

REGRESS_TARGETS=	run-safeunlink run-safeunlink-off run-kcompile

run-safeunlink: safeunlink
	./safeunlink

run-safeunlink-off: safeunlink-off
	./safeunlink-off

run-kcompile: kcompile
	./kcompile

# safeunlink is built twice from the same source: once with the safe-unlink
# assertions enabled (the real test), and once without (demonstrates the
# raw write-what-where primitive the hardening blocks).
safeunlink: safeunlink.c
	${CC} ${CFLAGS} -DQUEUE_MACRO_ASSERT -DQUEUE_MACRO_DEBUG \
	    -o $@ ${.CURDIR}/safeunlink.c

safeunlink-off: safeunlink.c
	${CC} ${CFLAGS} -o $@ ${.CURDIR}/safeunlink.c

CLEANFILES+=	safeunlink safeunlink-off

.include <bsd.regress.mk>
