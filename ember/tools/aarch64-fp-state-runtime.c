/* Origin: EmberBSD; AI-assisted native AArch64 FP state regression. */
/* SPDX-License-Identifier: BSD-2-Clause */

#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <sys/wait.h>

#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef __aarch64__
#error This regression requires native AArch64 FP support.
#endif

struct fp_state {
	uint64_t control, status;
};
static const struct fp_state selected = { UINT64_C(0x00800000), 0x10 };
static volatile sig_atomic_t handled;
static atomic_int start_threads;

static struct fp_state
read_state(void)
{
	struct fp_state state;

	__asm__ volatile("mrs %0, fpcr\n\tmrs %1, fpsr"
	    : "=r" (state.control), "=r" (state.status) : : "memory");
	return state;
}

static void
write_state(struct fp_state state)
{

	__asm__ volatile("msr fpcr, %0\n\tmsr fpsr, %1\n\tisb"
	    : : "r" (state.control), "r" (state.status) : "memory");
}

static int
same_state(struct fp_state a, struct fp_state b)
{

	return a.control == b.control && a.status == b.status;
}

static uint32_t
single_op(uint32_t a_bits, uint32_t b_bits, int multiply)
{
	float a, b, result;
	uint32_t bits;

	memcpy(&a, &a_bits, sizeof(a));
	memcpy(&b, &b_bits, sizeof(b));
	if (multiply)
		__asm__ volatile("fmul %s0, %s1, %s2"
		    : "=w" (result) : "w" (a), "w" (b));
	else
		__asm__ volatile("fadd %s0, %s1, %s2"
		    : "=w" (result) : "w" (a), "w" (b));
	memcpy(&bits, &result, sizeof(bits));
	return bits;
}

static uint64_t
double_op(uint64_t a_bits, uint64_t b_bits, int multiply)
{
	double a, b, result;
	uint64_t bits;

	memcpy(&a, &a_bits, sizeof(a));
	memcpy(&b, &b_bits, sizeof(b));
	if (multiply)
		__asm__ volatile("fmul %d0, %d1, %d2"
		    : "=w" (result) : "w" (a), "w" (b));
	else
		__asm__ volatile("fadd %d0, %d1, %d2"
		    : "=w" (result) : "w" (a), "w" (b));
	memcpy(&bits, &result, sizeof(bits));
	return bits;
}

static int
defaults(struct fp_state initial)
{
	uint32_t f_out, f_in, f_nan, f_round;
	uint64_t d_out, d_in, d_nan, d_round;
	int failed;

	/* No FPCR write precedes these operations or the initial snapshot. */
	f_out = single_op(0x00800000, 0x3f000000, 1);
	f_in = single_op(0x00400000, 0x40000000, 1);
	f_nan = single_op(0x7fc12345, 0x3f800000, 0);
	f_round = single_op(0x3f800000, 0x33800000, 0);
	d_out = double_op(UINT64_C(0x0010000000000000),
	    UINT64_C(0x3fe0000000000000), 1);
	d_in = double_op(UINT64_C(0x0008000000000000),
	    UINT64_C(0x4000000000000000), 1);
	d_nan = double_op(UINT64_C(0x7ff8123456789abc),
	    UINT64_C(0x3ff0000000000000), 0);
	d_round = double_op(UINT64_C(0x3ff0000000000000),
	    UINT64_C(0x3ca0000000000000), 0);
	failed = initial.control != 0 || initial.status != 0 ||
	    f_out != 0x00400000 || f_in != 0x00800000 ||
	    f_nan != 0x7fc12345 || f_round != 0x3f800000 ||
	    d_out != UINT64_C(0x0008000000000000) ||
	    d_in != UINT64_C(0x0010000000000000) ||
	    d_nan != UINT64_C(0x7ff8123456789abc) ||
	    d_round != UINT64_C(0x3ff0000000000000);
	write_state(initial);
	printf("fresh exec: FPCR=%08" PRIx64 " FPSR=%08" PRIx64 "\n",
	    initial.control, initial.status);
	printf("float output/input/NaN/round: %08" PRIx32 " %08" PRIx32
	    " %08" PRIx32 " %08" PRIx32 "\n", f_out, f_in, f_nan, f_round);
	printf("double output/input/NaN/round: %016" PRIx64 " %016" PRIx64
	    " %016" PRIx64 " %016" PRIx64 "\n", d_out, d_in, d_nan, d_round);
	printf("fresh exec defaults: %s\n", failed ? "FAIL" : "PASS");
	return failed;
}

static int
child_status(pid_t pid)
{
	int status;

	if (pid < 0 || waitpid(pid, &status, 0) != pid)
		return 1;
	return !WIFEXITED(status) || WEXITSTATUS(status) != 0;
}

static void
handler(int signo)
{
	const struct fp_state temporary = { UINT64_C(0x03400000), 0x8 };

	(void)signo;
	write_state(temporary);
	handled = 1;
}

struct thread_test {
	struct fp_state state;
	int failed;
};

static void *
thread_test(void *arg)
{
	struct thread_test *test = arg;
	struct fp_state original = read_state();
	unsigned int i;

	write_state(test->state);
	while (!atomic_load_explicit(&start_threads, memory_order_acquire))
		sched_yield();
	for (i = 0; i < 128; i++) {
		if (sched_yield() != 0 || !same_state(read_state(), test->state)) {
			test->failed = 1;
			break;
		}
	}
	write_state(original);
	return NULL;
}

static int
preservation(void)
{
	struct fp_state original = read_state();
	struct sigaction action, saved_action;
	struct thread_test tests[2] = {
		{ { UINT64_C(0x00400000), 0x10 }, 0 },
		{ { UINT64_C(0x03800000), 0x2 }, 0 }
	};
	pthread_t threads[2];
	pid_t pid;
	int failed = 0, fork_failed, signal_failed, thread_failed;

	write_state(selected);
	pid = fork();
	if (pid == 0)
		_exit(!same_state(read_state(), selected));
	fork_failed = child_status(pid) || !same_state(read_state(), selected);
	failed |= fork_failed;
	write_state(selected);
	memset(&action, 0, sizeof(action));
	action.sa_handler = handler;
	sigemptyset(&action.sa_mask);
	signal_failed = sigaction(SIGUSR1, &action, &saved_action) != 0;
	if (!signal_failed) {
		signal_failed = raise(SIGUSR1) != 0 || !handled ||
		    !same_state(read_state(), selected);
		if (sigaction(SIGUSR1, &saved_action, NULL) != 0)
			signal_failed = 1;
	}
	failed |= signal_failed;
	write_state(selected);
	thread_failed = pthread_create(&threads[0], NULL, thread_test,
	    &tests[0]) != 0;
	if (!thread_failed) {
		if (pthread_create(&threads[1], NULL, thread_test, &tests[1]) != 0) {
			/* Release the first thread on a resource failure, then join it. */
			thread_failed = 1;
		}
		atomic_store_explicit(&start_threads, 1, memory_order_release);
		if (!thread_failed && pthread_join(threads[1], NULL) != 0)
			thread_failed = 1;
		if (pthread_join(threads[0], NULL) != 0)
			thread_failed = 1;
	}
	thread_failed |= tests[0].failed || tests[1].failed ||
	    !same_state(read_state(), selected);
	failed |= thread_failed;
	write_state(original);
	printf("fork state: %s; signal state: %s; two thread states: %s\n",
	    fork_failed ? "FAIL" : "PASS", signal_failed ? "FAIL" : "PASS",
	    thread_failed ? "FAIL" : "PASS");
	return failed;
}

static int
exec_defaults(const char *self)
{
	struct fp_state original = read_state();
	pid_t pid;
	int failed;

	/* Fork must inherit this; exec must replace it with the initial state. */
	write_state(selected);
	pid = fork();
	if (pid == 0) {
		if (!same_state(read_state(), selected))
			_exit(2);
		execl(self, self, "--defaults", (char *)NULL);
		_exit(2);
	}
	failed = child_status(pid) || !same_state(read_state(), selected);
	write_state(original);
	printf("exec reset from selected parent state: %s\n",
	    failed ? "FAIL" : "PASS");
	return failed;
}

int
main(int argc, char **argv)
{
	/* Read before any test, library output or explicit mode change. */
	struct fp_state initial = read_state();
	int failed;

	if (argc != 2) {
		fprintf(stderr, "usage: %s --defaults|--preserve|--exec-defaults\n",
		    argv[0]);
		return 2;
	}
	if (strcmp(argv[1], "--defaults") == 0)
		failed = defaults(initial);
	else if (strcmp(argv[1], "--preserve") == 0)
		failed = preservation();
	else if (strcmp(argv[1], "--exec-defaults") == 0)
		failed = exec_defaults(argv[0]);
	else
		return 2;
	write_state(initial);
	return failed ? 1 : 0;
}
