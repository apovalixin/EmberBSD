/* Origin: EmberBSD real-ucom native software contracts, 2026-10-10. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <rump/rump.h>
#include <rump/rump_syscalls.h>
#include "umock_test.h"

#define CHECK(c) do { if (!(c)) { \
	fprintf(stderr, "%s:%d: %s (errno=%d)\n", __func__, __LINE__, #c, errno); \
	exit(1); } } while (0)
#define KERNEL(call) do { rump_schedule(); call; rump_unschedule(); } while (0)

static void
delay(void)
{
	const struct timespec t = { 0, 10000000 };

	(void)nanosleep(&t, NULL);
}

static struct umock_record
snapshot(int p)
{
	struct umock_record r;

	KERNEL(rump_umock_snapshot(p, &r));
	return r;
}

static struct umock_record
pending(int p, unsigned after)
{
	struct umock_record r;

	for (int i = 0; i < 300; i++) {
		r = snapshot(p);
		if (r.pending && r.submits > after)
			return r;
		delay();
	}
	CHECK(0 && "TX not dispatched within 3 seconds");
	return snapshot(p);
}

static int
raw_open(int p)
{
	char path[32];
	int fd = -1;

	snprintf(path, sizeof(path), "/dev/dtyU%d", p);
	/* Rump's direct syscall API exposes kernel restart to its caller. */
	for (int i = 0; i < 300; i++) {
		fd = rump_sys_open(path, O_RDWR | O_NONBLOCK);
		if (fd >= 0 || errno != RUMP_ERESTART)
			return fd;
		delay();
	}
	CHECK(errno != RUMP_ERESTART);
	return fd;
}

static int
open_port(int p)
{
	struct termios t;
	int fd;

	fd = raw_open(p);
	CHECK(fd >= 0);
	CHECK(rump_sys_ioctl(fd, TIOCGETA, &t) == 0);
	cfmakeraw(&t);
	t.c_cflag |= CLOCAL;
	t.c_cflag &= ~HUPCL;
	CHECK(rump_sys_ioctl(fd, TIOCSETA, &t) == 0);
	return fd;
}

static void
outq(int fd, int wanted)
{
	int count = -1;

	for (int i = 0; i < 300; i++) {
		CHECK(rump_sys_ioctl(fd, TIOCOUTQ, &count) == 0);
		if (count == wanted)
			return;
		delay();
	}
	CHECK(count == wanted);
}

static void
read_bytes(int fd, const char *wanted, size_t len)
{
	char data[32];
	ssize_t n = -1;

	CHECK(len <= sizeof(data));
	for (int i = 0; i < 300; i++) {
		n = rump_sys_read(fd, data, len);
		if (n >= 0)
			break;
		CHECK(errno == EAGAIN || errno == EWOULDBLOCK);
		delay();
	}
	CHECK(n == (ssize_t)len && memcmp(data, wanted, len) == 0);
}

static void
complete(int p, struct umock_record r, size_t len, int error)
{

	KERNEL(rump_umock_done(p, r.epoch, r.cookie, len, error));
}

static void
test_open_io(void)
{
	int fd = open_port(0), error;
	unsigned submits = snapshot(0).submits;
	struct umock_record r;

	CHECK(rump_sys_write(fd, "ABC", 3) == 3);
	r = pending(0, submits);
	CHECK(r.length == 3 && memcmp(r.bytes, "ABC", 3) == 0);
	outq(fd, 3);
	complete(0, r, 3, 0);
	outq(fd, 0);
	KERNEL(error = rump_umock_input(0, r.epoch, "XYZ", 3));
	CHECK(error == 0);
	read_bytes(fd, "XYZ", 3);
	CHECK(rump_sys_close(fd) == 0);
}

static void
test_stale_duplicate(void)
{
	int fd = open_port(0), error;
	struct umock_record old, fresh;
	unsigned n = snapshot(0).submits;

	CHECK(rump_sys_write(fd, "old", 3) == 3);
	old = pending(0, n);
	CHECK(rump_sys_close(fd) == 0);
	fd = open_port(0);
	n = snapshot(0).submits;
	CHECK(rump_sys_write(fd, "new", 3) == 3);
	fresh = pending(0, n);
	CHECK(fresh.epoch != old.epoch);
	KERNEL(error = rump_umock_input(0, old.epoch, "bad", 3));
	CHECK(error == ENXIO);
	complete(0, old, 3, 0);
	outq(fd, 3);
	complete(0, fresh, 3, 0);
	outq(fd, 0);
	n = snapshot(0).submits;
	CHECK(rump_sys_write(fd, "NEXT", 4) == 4);
	struct umock_record next = pending(0, n);
	complete(0, fresh, 3, 0);
	outq(fd, 4);
	complete(0, next, 4, 0);
	outq(fd, 0);
	CHECK(rump_sys_close(fd) == 0);
}

static void
test_partial_fault(void)
{
	int fd = open_port(0), other = open_port(1), error;
	char data;
	unsigned n = snapshot(0).submits;

	CHECK(rump_sys_write(fd, "ABC", 3) == 3);
	struct umock_record r = pending(0, n);
	complete(0, r, 2, 0);
	delay();
	CHECK(rump_sys_write(fd, "D", 1) == -1 && errno == EIO);
	CHECK(rump_sys_read(fd, &data, 1) == -1 && errno == EIO);
	CHECK(snapshot(0).submits == n + 1);
	struct umock_record r1 = snapshot(1);
	KERNEL(error = rump_umock_input(1, r1.epoch, "OK", 2));
	CHECK(error == 0);
	read_bytes(other, "OK", 2);
	CHECK(rump_sys_close(fd) == 0 && rump_sys_close(other) == 0);
}

static void
test_pressure_overflow(void)
{
	int fd = open_port(0), other = open_port(1), error;
	uint8_t data[8192];
	char byte;
	struct umock_record r = snapshot(0);

	memset(data, 'A', sizeof(data));
	KERNEL(rump_umock_pause(0, 1));
	for (int i = 0; i < 300 && !snapshot(0).paused; i++)
		delay();
	CHECK(snapshot(0).paused);
	KERNEL(error = rump_umock_input(0, r.epoch, data, 6144));
	CHECK(error == 0);
	KERNEL(error = rump_umock_input(0, r.epoch, data, 2048));
	CHECK(error == 0);
	KERNEL(error = rump_umock_input(0, r.epoch, data, 1));
	CHECK(error == ENOBUFS);
	CHECK(rump_sys_read(fd, &byte, 1) == -1 && errno == ENOBUFS);
	r = snapshot(1);
	KERNEL(error = rump_umock_input(1, r.epoch, "B", 1));
	CHECK(error == 0);
	read_bytes(other, "B", 1);
	CHECK(rump_sys_close(fd) == 0 && rump_sys_close(other) == 0);
	fd = open_port(0);
	r = snapshot(0);
	KERNEL(rump_umock_pause(0, 1));
	KERNEL(error = rump_umock_input(0, r.epoch, "C", 1));
	CHECK(error == 0);
	KERNEL(rump_umock_pause(0, 0));
	read_bytes(fd, "C", 1);
	CHECK(rump_sys_close(fd) == 0);
}

static void
test_flush_pending(void)
{
	int fd = open_port(0), flags = FWRITE;
	unsigned n = snapshot(0).submits;

	CHECK(rump_sys_write(fd, "OLD", 3) == 3);
	struct umock_record old = pending(0, n);
	CHECK(rump_sys_ioctl(fd, TIOCFLUSH, &flags) == 0);
	CHECK(rump_sys_write(fd, "NEW", 3) == 3);
	outq(fd, 3);
	complete(0, old, 3, 0);
	struct umock_record fresh = pending(0, n + 1);
	CHECK(fresh.length == 3 && memcmp(fresh.bytes, "NEW", 3) == 0);
	outq(fd, 3);
	complete(0, fresh, 3, 0);
	outq(fd, 0);
	CHECK(rump_sys_close(fd) == 0);
}

static void
test_rejected_controls(void)
{
	struct termios before, after, t;
	int fd;

	KERNEL(rump_umock_termios(2, &before));
	KERNEL(rump_umock_errors(2, EINVAL, 0, 0));
	CHECK(raw_open(2) == -1 && errno == EINVAL);
	KERNEL(rump_umock_termios(2, &after));
	CHECK(before.c_ospeed == after.c_ospeed && before.c_cflag == after.c_cflag);
	KERNEL(rump_umock_errors(2, 0, EOPNOTSUPP, 0));
	CHECK(raw_open(2) == -1 && errno == EOPNOTSUPP);
	KERNEL(rump_umock_errors(2, 0, 0, EIO));
	CHECK(raw_open(2) == -1 && errno == EIO);
	KERNEL(rump_umock_errors(2, 0, 0, 0));
	fd = open_port(2);
	CHECK(rump_sys_ioctl(fd, TIOCGETA, &before) == 0);
	t = before;
	t.c_ospeed = t.c_ispeed = B19200;
	KERNEL(rump_umock_errors(2, EINVAL, EOPNOTSUPP, 0));
	CHECK(rump_sys_ioctl(fd, TIOCSETA, &t) == -1 && errno == EINVAL);
	CHECK(rump_sys_ioctl(fd, TIOCGETA, &after) == 0);
	CHECK(before.c_ospeed == after.c_ospeed && before.c_cflag == after.c_cflag);
	CHECK(rump_sys_ioctl(fd, TIOCSBRK, NULL) == -1 && errno == EOPNOTSUPP);
	KERNEL(rump_umock_errors(2, 0, 0, 0));
	CHECK(rump_sys_close(fd) == 0);
}

struct close_job { int fd, error; };
struct param_job { int fd, error; struct termios t; };
static void *param_thread(void *arg)
{
	struct param_job *job = arg;

	job->error = rump_sys_ioctl(job->fd, TIOCSETA, &job->t);
	return NULL;
}

static void
test_control_serialization(void)
{
	int fd = open_port(2);
	pthread_t first, second;
	struct param_job a = { .fd = fd }, b = { .fd = fd };
	unsigned before = snapshot(2).param_overlap;

	CHECK(rump_sys_ioctl(fd, TIOCGETA, &a.t) == 0);
	b.t = a.t;
	a.t.c_ispeed = a.t.c_ospeed = B19200;
	b.t.c_ispeed = b.t.c_ospeed = B38400;
	KERNEL(rump_umock_param_gate(2, 1));
	CHECK(pthread_create(&first, NULL, param_thread, &a) == 0);
	for (int i = 0; i < 300 && snapshot(2).param_active == 0; i++)
		delay();
	CHECK(snapshot(2).param_active == 1);
	CHECK(pthread_create(&second, NULL, param_thread, &b) == 0);
	for (int i = 0; i < 20; i++)
		delay();
	CHECK(snapshot(2).param_overlap == (int)before);
	KERNEL(rump_umock_param_gate(2, 0));
	CHECK(pthread_join(first, NULL) == 0 && pthread_join(second, NULL) == 0);
	CHECK(a.error == 0 && b.error == 0);
	CHECK(rump_sys_close(fd) == 0);
}

static void *close_thread(void *arg)
{
	struct close_job *job = arg;

	job->error = rump_sys_close(job->fd);
	return NULL;
}

static void
test_close_dispatch_barrier(void)
{
	int fd = open_port(3);
	struct umock_record before = snapshot(3);
	struct close_job job = { fd, -2 };
	pthread_t thread;

	KERNEL(rump_umock_gate(3, 1));
	CHECK(rump_sys_write(fd, "gate", 4) == 4);
	struct umock_record active = pending(3, before.submits);
	CHECK(active.active);
	CHECK(pthread_create(&thread, NULL, close_thread, &job) == 0);
	for (int i = 0; i < 5; i++)
		delay();
	CHECK(snapshot(3).stops == before.stops);
	KERNEL(rump_umock_gate(3, 0));
	CHECK(pthread_join(thread, NULL) == 0 && job.error == 0);
	CHECK(snapshot(3).stops == before.stops + 1 && !snapshot(3).active);
}

struct read_job { int fd, error; ssize_t result; char byte; };
static void *read_thread(void *arg)
{
	struct read_job *job = arg;

	/* Match libc/kernel restart handling for a canceled blocking read. */
	do {
		job->result = rump_sys_read(job->fd, &job->byte, 1);
	} while (job->result == -1 && errno == RUMP_ERESTART);
	job->error = errno;
	return NULL;
}

static void
test_fault_wakeup_poll(void)
{
	int fd = open_port(0), flags = rump_sys_fcntl(fd, F_GETFL);
	struct read_job job = { .fd = fd };
	struct umock_record r = snapshot(0);
	struct pollfd pollfd = { .fd = fd, .events = POLLIN | POLLOUT };
	pthread_t thread;

	CHECK(flags >= 0);
	CHECK(rump_sys_fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == 0);
	CHECK(pthread_create(&thread, NULL, read_thread, &job) == 0);
	for (int i = 0; i < 10; i++)
		delay();
	KERNEL(rump_umock_fault(0, r.epoch, ETIMEDOUT));
	CHECK(pthread_join(thread, NULL) == 0);
	CHECK(job.result == -1 && (job.error == EIO || job.error == ETIMEDOUT));
	CHECK(rump_sys_poll(&pollfd, 1, 0) == 1 && (pollfd.revents & POLLERR));
	CHECK(raw_open(0) == -1 && errno == ETIMEDOUT);
	CHECK(rump_sys_close(fd) == 0);
}

static void
test_detach_failed_attach_legacy(void)
{
	int fd = open_port(3), error;
	struct umock_record before = snapshot(3);
	char byte;
	unsigned usb;

	KERNEL(error = rump_umock_remove(3));
	CHECK(error == 0);
	CHECK(snapshot(3).detaches == before.detaches + 1);
	/* Revoked TTY vnode reads may report EOF; no data may be delivered. */
	CHECK(rump_sys_read(fd, &byte, 1) <= 0);
	CHECK(rump_sys_write(fd, "X", 1) == -1);
	CHECK(rump_sys_close(fd) == 0);
	CHECK(rump_sys_open("/dev/dtyU3", O_RDWR | O_NONBLOCK) == -1);
	CHECK(rump_sys_open("/dev/dtyU4", O_RDWR | O_NONBLOCK) == -1 && errno == ENXIO);
	CHECK(rump_sys_open("/dev/dtyU5", O_RDWR | O_NONBLOCK) == -1 && errno == ENXIO);
	CHECK(snapshot(5).detaches == 0);
	CHECK(rump_sys_open("/dev/dtyU6", O_RDWR | O_NONBLOCK) == -1 && errno == ENXIO);
	KERNEL(usb = rump_umock_usb_calls());
	CHECK(usb == 1);
}

int
main(void)
{
	struct { const char *name; void (*test)(void); } tests[] = {
		{ "open_io", test_open_io },
		{ "stale_duplicate", test_stale_duplicate },
		{ "partial_fault", test_partial_fault },
		{ "pressure_overflow", test_pressure_overflow },
		{ "flush_pending", test_flush_pending },
		{ "rejected_controls", test_rejected_controls },
		{ "control_serialization", test_control_serialization },
		{ "close_dispatch_barrier", test_close_dispatch_barrier },
		{ "fault_wakeup_poll", test_fault_wakeup_poll },
		{ "detach_failed_attach_legacy", test_detach_failed_attach_legacy }
	};

	alarm(45);
	CHECK(rump_init() == 0);
	for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		tests[i].test();
		printf("PASS %s\n", tests[i].name);
		fflush(stdout);
	}
	puts("ucom native: 10/10 groups passed, 0 failures (test-only USB)");
	return 0;
}
