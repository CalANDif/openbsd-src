/*
 * Compile-only check that the safe-unlink macros expand correctly under
 * the kernel gate (_KERNEL + DIAGNOSTIC) without pulling in <sys/systm.h>
 * or <lib/libkern/libkern.h>.  The queue macros reference panic() only at
 * expansion sites, so a local prototype suffices.  This file is linked
 * into a do-nothing executable; its purpose is to fail the regress build
 * if the kernel-path macro expansion is ever broken.
 */

#define _KERNEL
#define DIAGNOSTIC

#include <sys/queue.h>

/* Minimal panic() so the kernel-path _Q_PANIC() expansion resolves. */
__dead void panic(const char *, ...);
__dead void
panic(const char *fmt, ...)
{
	(void)fmt;
	for (;;)
		;
}

struct kelm {
	int			 id;
	TAILQ_ENTRY(kelm)	 tlink;
	LIST_ENTRY(kelm)	 llink;
};
TAILQ_HEAD(kthead, kelm);
LIST_HEAD(klhead, kelm);

/*
 * Expand every mutating macro once so the compiler sees the full
 * _Q_CHECK_* / _Q_PANIC() expansion under the kernel gate.
 */
void kcompile(void);
void
kcompile(void)
{
	struct kthead th, th2;
	struct klhead lh;
	struct kelm a, b, c;

	TAILQ_INIT(&th);
	TAILQ_INIT(&th2);
	TAILQ_INSERT_HEAD(&th, &a, tlink);
	TAILQ_INSERT_TAIL(&th, &b, tlink);
	TAILQ_INSERT_AFTER(&th, &a, &c, tlink);
	TAILQ_INSERT_BEFORE(&b, &c, tlink);
	TAILQ_REPLACE(&th, &a, &c, tlink);
	TAILQ_REMOVE(&th, &b, tlink);
	TAILQ_CONCAT(&th, &th2, tlink);

	LIST_INIT(&lh);
	LIST_INSERT_HEAD(&lh, &a, llink);
	LIST_INSERT_AFTER(&a, &b, llink);
	LIST_INSERT_BEFORE(&b, &c, llink);
	LIST_REPLACE(&a, &c, llink);
	LIST_REMOVE(&b, llink);
}

int
main(void)
{
	/* Compile-only; do not actually run the (nonsensical) sequence. */
	return 0;
}
