/*
 * Regression test for safe-unlinking consistency checks in <sys/queue.h>.
 *
 * Each sub-test runs in a forked child that shares a sentinel word with the
 * parent via MAP_SHARED.  The child corrupts a link field so that the queue
 * macro -- if unchecked -- would write through the sentinel.  With
 * QUEUE_MACRO_ASSERT the macro must abort() before any store happens, so
 * the parent verifies both that the child was killed by SIGABRT and that
 * the sentinel is unchanged.
 *
 * Built without QUEUE_MACRO_ASSERT, the same binary demonstrates the raw
 * write-what-where primitive instead.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/queue.h>

#include <err.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SENTINEL_MAGIC	((void *)(uintptr_t)0x5afe5afe5afe5afeULL)

struct telm {
	int			 id;
	TAILQ_ENTRY(telm)	 link;
};
TAILQ_HEAD(thead, telm);

struct lelm {
	int			 id;
	LIST_ENTRY(lelm)	 link;
};
LIST_HEAD(lhead, lelm);

static int failures;

/*
 * Shared sentinel.  Points to a MAP_SHARED page so the parent can observe
 * whether the child's queue macro wrote through it before dying.
 */
static void **sentinel;

static void
sentinel_init(void)
{
	sentinel = mmap(NULL, sizeof(*sentinel), PROT_READ | PROT_WRITE,
	    MAP_ANON | MAP_SHARED, -1, 0);
	if (sentinel == MAP_FAILED)
		err(1, "mmap");
	*sentinel = SENTINEL_MAGIC;
}

static void
expect_abort(const char *name, void (*fn)(void))
{
	pid_t pid;
	int status;

	*sentinel = SENTINEL_MAGIC;

	fflush(stdout);
	pid = fork();
	if (pid == -1)
		err(1, "fork");
	if (pid == 0) {
		/* Child: don't dump core; run the test; shouldn't return. */
		signal(SIGABRT, SIG_DFL);
		fn();
		/* If we get here the macro did not abort. */
		_exit(0);
	}

	if (waitpid(pid, &status, 0) == -1)
		err(1, "waitpid");

#ifdef QUEUE_MACRO_ASSERT
	if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGABRT) {
		printf("FAIL %-32s child did not abort (status 0x%x)\n",
		    name, status);
		failures++;
		return;
	}
	if (*sentinel != SENTINEL_MAGIC) {
		printf("FAIL %-32s sentinel clobbered (%p)\n",
		    name, *sentinel);
		failures++;
		return;
	}
	printf("PASS %-32s aborted, sentinel intact\n", name);
#else
	/*
	 * Negative control: without QUEUE_MACRO_ASSERT the corrupted
	 * operation runs to completion and the sentinel is overwritten.
	 * This demonstrates the write-what-where primitive the hardening
	 * blocks.  Not every sub-test writes through sentinel in the
	 * unprotected case; only report when it does.
	 */
	if (*sentinel != SENTINEL_MAGIC)
		printf("DEMO %-32s sentinel overwritten -> %p (unprotected)\n",
		    name, *sentinel);
	else if (WIFSIGNALED(status))
		printf("DEMO %-32s crashed with signal %d (unprotected)\n",
		    name, WTERMSIG(status));
	else
		printf("DEMO %-32s ran to completion (unprotected)\n", name);
#endif
}

static void
expect_ok(const char *name, void (*fn)(void))
{
	pid_t pid;
	int status;

	fflush(stdout);
	pid = fork();
	if (pid == -1)
		err(1, "fork");
	if (pid == 0) {
		fn();
		_exit(0);
	}
	if (waitpid(pid, &status, 0) == -1)
		err(1, "waitpid");
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		printf("FAIL %-32s correct usage failed (status 0x%x)\n",
		    name, status);
		failures++;
		return;
	}
	printf("PASS %-32s correct usage ok\n", name);
}

/* ---------------------------------------------------------------- TAILQ */

static void
tailq_setup(struct thead *h, struct telm *a, struct telm *b, struct telm *c)
{
	TAILQ_INIT(h);
	a->id = 1; b->id = 2; c->id = 3;
	TAILQ_INSERT_TAIL(h, a, link);
	TAILQ_INSERT_TAIL(h, b, link);
	TAILQ_INSERT_TAIL(h, c, link);
}

static void
tq_remove_bad_prev(void)
{
	struct thead h;
	struct telm a, b, c;

	tailq_setup(&h, &a, &b, &c);
	/* Point prev at the shared sentinel: unprotected REMOVE would do
	 * *sentinel = b.tqe_next (== &c). */
	b.link.tqe_prev = (struct telm **)sentinel;
	TAILQ_REMOVE(&h, &b, link);
}

static void
tq_remove_bad_next(void)
{
	struct thead h;
	struct telm a, b, c, fake;

	tailq_setup(&h, &a, &b, &c);
	memset(&fake, 0, sizeof(fake));
	/* Forge a "next" whose tqe_prev points at the sentinel: unprotected
	 * REMOVE would do fake.tqe_prev = b.tqe_prev, i.e. *sentinel = ... */
	fake.link.tqe_prev = (struct telm **)sentinel;
	b.link.tqe_next = &fake;
	TAILQ_REMOVE(&h, &b, link);
}

static void
tq_insert_tail_bad_last(void)
{
	struct thead h;
	struct telm a, b, c, n;

	tailq_setup(&h, &a, &b, &c);
	/* Corrupt tqh_last: unprotected INSERT_TAIL would do *sentinel = &n. */
	h.tqh_last = (struct telm **)sentinel;
	TAILQ_INSERT_TAIL(&h, &n, link);
}

static void
tq_insert_before_bad_prev(void)
{
	struct thead h;
	struct telm a, b, c, n;

	tailq_setup(&h, &a, &b, &c);
	b.link.tqe_prev = (struct telm **)sentinel;
	TAILQ_INSERT_BEFORE(&b, &n, link);
}

static void
tq_insert_after_bad_next(void)
{
	struct thead h;
	struct telm a, b, c, n, fake;

	tailq_setup(&h, &a, &b, &c);
	memset(&fake, 0, sizeof(fake));
	fake.link.tqe_prev = (struct telm **)sentinel;
	b.link.tqe_next = &fake;
	TAILQ_INSERT_AFTER(&h, &b, &n, link);
}

static void
tq_replace_bad_prev(void)
{
	struct thead h;
	struct telm a, b, c, n;

	tailq_setup(&h, &a, &b, &c);
	b.link.tqe_prev = (struct telm **)sentinel;
	TAILQ_REPLACE(&h, &b, &n, link);
}

static void
tq_double_remove(void)
{
	struct thead h;
	struct telm a, b, c;

	tailq_setup(&h, &a, &b, &c);
	TAILQ_REMOVE(&h, &b, link);
	/* b.link.tqe_prev is now _Q_INVALID; second remove must abort via
	 * _Q_CHECK_LIVE rather than fault on a non-canonical dereference. */
	TAILQ_REMOVE(&h, &b, link);
}

static void
tq_correct_usage(void)
{
	struct thead h, h2;
	struct telm a, b, c, d, e, *p;
	int i, want[] = { 4, 1, 5, 3 };

	tailq_setup(&h, &a, &b, &c);
	TAILQ_REMOVE(&h, &b, link);			/* 1 3 */
	d.id = 4;
	TAILQ_INSERT_HEAD(&h, &d, link);		/* 4 1 3 */
	e.id = 5;
	TAILQ_INSERT_BEFORE(&c, &e, link);		/* 4 1 5 3 */

	i = 0;
	TAILQ_FOREACH(p, &h, link) {
		if (p->id != want[i++])
			errx(1, "tailq order mismatch");
	}
	if (i != 4)
		errx(1, "tailq length mismatch");

	TAILQ_INIT(&h2);
	TAILQ_INSERT_TAIL(&h2, &b, link);
	TAILQ_CONCAT(&h, &h2, link);
	if (TAILQ_LAST(&h, thead) != &b)
		errx(1, "tailq concat tail wrong");
}

/* ----------------------------------------------------------------- LIST */

static void
list_setup(struct lhead *h, struct lelm *a, struct lelm *b, struct lelm *c)
{
	LIST_INIT(h);
	a->id = 1; b->id = 2; c->id = 3;
	LIST_INSERT_HEAD(h, c, link);
	LIST_INSERT_HEAD(h, b, link);
	LIST_INSERT_HEAD(h, a, link);
}

static void
li_remove_bad_prev(void)
{
	struct lhead h;
	struct lelm a, b, c;

	list_setup(&h, &a, &b, &c);
	b.link.le_prev = (struct lelm **)sentinel;
	LIST_REMOVE(&b, link);
}

static void
li_remove_bad_next(void)
{
	struct lhead h;
	struct lelm a, b, c, fake;

	list_setup(&h, &a, &b, &c);
	memset(&fake, 0, sizeof(fake));
	fake.link.le_prev = (struct lelm **)sentinel;
	b.link.le_next = &fake;
	LIST_REMOVE(&b, link);
}

static void
li_insert_before_bad_prev(void)
{
	struct lhead h;
	struct lelm a, b, c, n;

	list_setup(&h, &a, &b, &c);
	b.link.le_prev = (struct lelm **)sentinel;
	LIST_INSERT_BEFORE(&b, &n, link);
}

static void
li_replace_bad_prev(void)
{
	struct lhead h;
	struct lelm a, b, c, n;

	list_setup(&h, &a, &b, &c);
	b.link.le_prev = (struct lelm **)sentinel;
	LIST_REPLACE(&b, &n, link);
}

static void
li_double_remove(void)
{
	struct lhead h;
	struct lelm a, b, c;

	list_setup(&h, &a, &b, &c);
	LIST_REMOVE(&b, link);
	LIST_REMOVE(&b, link);
}

static void
li_correct_usage(void)
{
	struct lhead h;
	struct lelm a, b, c, d, *p;
	int i, want[] = { 1, 4, 3 };

	list_setup(&h, &a, &b, &c);
	d.id = 4;
	LIST_REPLACE(&b, &d, link);		/* 1 4 3 */
	i = 0;
	LIST_FOREACH(p, &h, link) {
		if (p->id != want[i++])
			errx(1, "list order mismatch");
	}
	if (i != 3)
		errx(1, "list length mismatch");
	LIST_REMOVE(&a, link);
	LIST_REMOVE(&c, link);
	LIST_REMOVE(&d, link);
	if (!LIST_EMPTY(&h))
		errx(1, "list not empty");
}

/* ---------------------------------------------------------------- main */

int
main(void)
{
	sentinel_init();

#ifdef QUEUE_MACRO_ASSERT
	printf("safe-unlink checks: ENABLED\n");
#else
	printf("safe-unlink checks: DISABLED (demonstrating raw primitive)\n");
#endif

	/* Correct usage must never trip the checks. */
	expect_ok("tailq-correct",            tq_correct_usage);
	expect_ok("list-correct",             li_correct_usage);

	/* Corrupted usage must abort before writing through sentinel. */
	expect_abort("tailq-remove-bad-prev",       tq_remove_bad_prev);
	expect_abort("tailq-remove-bad-next",       tq_remove_bad_next);
	expect_abort("tailq-insert-tail-bad-last",  tq_insert_tail_bad_last);
	expect_abort("tailq-insert-before-bad-prev",tq_insert_before_bad_prev);
	expect_abort("tailq-insert-after-bad-next", tq_insert_after_bad_next);
	expect_abort("tailq-replace-bad-prev",      tq_replace_bad_prev);
	expect_abort("tailq-double-remove",         tq_double_remove);
	expect_abort("list-remove-bad-prev",        li_remove_bad_prev);
	expect_abort("list-remove-bad-next",        li_remove_bad_next);
	expect_abort("list-insert-before-bad-prev", li_insert_before_bad_prev);
	expect_abort("list-replace-bad-prev",       li_replace_bad_prev);
	expect_abort("list-double-remove",          li_double_remove);

	if (failures) {
		printf("\n%d test(s) FAILED\n", failures);
		return 1;
	}
#ifdef QUEUE_MACRO_ASSERT
	printf("\nall tests passed\n");
#endif
	return 0;
}
