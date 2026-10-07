/* Origin: EmberBSD; AI-assisted bounded classic EXEC framing cases. */
/* SPDX-License-Identifier: BSD-2-Clause */

/* Mesa 26.2.4 virgl_protocol.h: classic NOP=0 and END_TRANSFERS=44. */
#define FRAMING_NOP 0U
#define FRAMING_END_TRANSFERS 44U
#define FRAMING_HEADER(command, object, payload) \
	((uint32_t)(command) | (uint32_t)(object) << 8 | (uint32_t)(payload) << 16)

static unsigned framing_attachment_locks;

static void
framing_lock_hook(struct mutex *m)
{
	if (m == &exec_priv.attachment_lock)
		framing_attachment_locks++;
}

static const char *const framing_names[] = {
	"one trailing byte", "two trailing bytes", "three trailing bytes",
	"aligned prefix plus one byte", "aligned prefix plus two bytes",
	"aligned prefix plus three bytes", "header without claimed payload",
	"valid prefix and truncated final packet", "payload at remaining boundary",
	"payload one word over remaining", "maximum unsigned payload exact",
	"maximum unsigned payload truncated", "zero-payload sequence advances",
	"opaque END_TRANSFERS padding", "nonzero padding then valid real packet",
	"nonzero padding then truncated real packet", "multiple packets unchanged",
	"malformed commands with valid hints and output fd zero", "copy fault preserves EFAULT fd zero",
	"valid output fd zero lifetime", "valid queue error ownership",
	"valid completion error lifetime", "input error precedes framing",
	"input deadline precedes framing", "alignment precedes input wait",
	"zero bytes remain forbidden", "unaligned user address copied once",
	"no opcode or object allowlist"
};

static void
framing_case(unsigned which)
{
	uint32_t words[65536] = {FRAMING_NOP};
	unsigned char unaligned[sizeof(uint32_t) + 1];
	spinlock_t input_lock = {0};
	struct dma_fence explicit = {
		.refs=1, .f_magic=FENCE_MAGIC_GOOD, .signaled=true,
		.lock=&input_lock, .error=-EIO
	};
	int expected = 0, ret;
	bool copied = true, late_reject = false;

	exec_setup();
	exec_args.command = (uintptr_t)words;
	lock_hook = framing_lock_hook;
	if (which < 6) {
		exec_args.size = which < 3 ? which + 1 : which + 2;
		expected = -EINVAL;
		copied = false;
	}
	if (which == 6 || which == 17 || which == 18 ||
	    which == 22 || which == 23) {
		words[0] = FRAMING_HEADER(FRAMING_NOP, 0, 1);
		if (which != 18)
			expected = -EINVAL;
	}
	if (which == 7) {
		exec_args.size = 8;
		words[1] = FRAMING_HEADER(FRAMING_NOP, 0, 1);
		expected = -EINVAL;
	}
	if (which == 8 || which == 9) {
		exec_args.size = 12;
		words[0] = FRAMING_HEADER(FRAMING_NOP, 0, which == 8 ? 2 : 3);
		words[1] = UINT32_MAX;
		words[2] = 0x80000000U;
		if (which == 9)
			expected = -EINVAL;
	}
	if (which == 10 || which == 11) {
		exec_native.max_request = sizeof(words) + 256;
		exec_args.size = sizeof(words) - (which == 11 ? 4 : 0);
		words[0] = FRAMING_HEADER(FRAMING_NOP, 0, UINT16_MAX);
		words[65535] = UINT32_MAX;
		if (which == 11)
			expected = -EINVAL;
	}
	if (which == 12)
		exec_args.size = 64;
	if (which >= 13 && which <= 15) {
		words[0] = FRAMING_HEADER(FRAMING_END_TRANSFERS, 0, 2);
		words[1] = UINT32_MAX;
		words[2] = 0x80000001U;
		exec_args.size = which == 13 ? 12 : 16;
		words[3] = FRAMING_HEADER(FRAMING_NOP, 0, which == 15 ? 1 : 0);
		if (which == 15)
			expected = -EINVAL;
	}
	if (which == 16 || which == 26) {
		words[0] = FRAMING_HEADER(FRAMING_NOP, 0, 1);
		words[1] = UINT32_MAX;
		words[2] = FRAMING_NOP;
		words[3] = FRAMING_HEADER(FRAMING_END_TRANSFERS, 0, 0);
		exec_args.size = 16;
		if (which == 26) {
			memcpy(unaligned + 1, words + 3, sizeof(uint32_t));
			exec_args.command = (uintptr_t)(unaligned + 1);
			exec_args.size = sizeof(uint32_t);
		}
	}
	if (which >= 17 && which <= 21) {
		exec_args.num_bo_handles = 2;
		exec_args.flags = VIRTGPU_EXECBUF_FENCE_FD_OUT;
		output_fd = 0;
	}
	if (which == 18) {
		fail_copy = 2;
		expected = -EFAULT;
	}
	if (which == 20) {
		queue_error = -ENOMEM;
		expected = -ENOMEM;
	}
	if (which == 22 || which == 23 || which == 24) {
		exec_args.flags = VIRTGPU_EXECBUF_FENCE_FD_IN;
		exec_args.fence_fd = 0;
		input = &explicit;
		copied = false;
		expected = -EIO;
		if (which == 23) {
			explicit.signaled = false;
			explicit.error = 0;
			input_wait = 0;
			expected = -ETIMEDOUT;
		}
		if (which == 24) {
			exec_args.size = 5;
			expected = -EINVAL;
		}
	}
	if (which == 25) {
		exec_args.size = 0;
		expected = -EINVAL;
		copied = false;
	}
	if (which == 27)
		words[0] = FRAMING_HEADER(255, 255, 0);
	late_reject = expected && copied && which != 20;
	ret = exec_submit();
	if (ret != expected)
		fprintf(stderr, "%s: got %d expected %d, PRE=%d queue=%d installs=%d\n",
		    framing_names[which], ret, expected, backing[0].pre, queue_calls, installs);
	assert(ret == expected);
	assert(copy_calls == (copied ? 1 + !!exec_args.num_bo_handles : 0));
	if (expected) {
		assert(!accepted && !installs && exec_args.fence_fd == -1);
		if (which != 20) {
			assert(!queue_calls && !backing[0].pre && !backing[1].pre);
			assert(!framing_attachment_locks && !sync_created);
		}
		if (late_reject)
			assert(alloc_calls == (exec_args.num_bo_handles ? 3 : 1));
		if (which == 17 || which == 18) {
			assert(files_allocated == 1 && aborts == 1 && !closes);
			assert(lookup_calls == 2 && !reserved_file);
		}
	} else {
		assert(accepted == 1 && exec_pending[0]);
		assert(exec_pending[0]->data_size == exec_args.size);
		assert(!memcmp(exec_pending[0]->data_buf,
		    u64_to_user_ptr(exec_args.command), exec_args.size));
		if (which == 21) {
			retire(exec_pending[0], -EIO);
			exec_pending[0] = NULL;
			assert(dma_fence_get_status(
			    ((struct sync_file *)reserved_file->f_data)->sf_fence) == -EIO);
		}
	}
	if (which == 22 || which == 23)
		assert(wait_calls == 1 && input_fd == 0 && explicit.refs == 1);
	if (which == 24)
		assert(!wait_calls && explicit.refs == 1);
	if (installs) {
		assert(exec_args.fence_fd == 0 && !aborts && reserved_file);
		(void)reserved_file->f_ops->fo_close(reserved_file);
		test_free(reserved_file);
		reserved_file = NULL;
	}
	exec_cleanup();
	assert(!reservations && closes == sync_created && aborts + installs == files_allocated);
	assert(!device.fence_drv.pending && !stops);
}

static int
framing_contract_main(void)
{
	unsigned failed = 0, count = sizeof(framing_names) / sizeof(framing_names[0]);

	for (unsigned i = 0; i < count; i++) {
		pid_t pid = fork();
		int status;
		bool pass;

		assert(pid >= 0);
		if (pid == 0) {
			framing_case(i);
			exit(0);
		}
		assert(waitpid(pid, &status, 0) == pid);
		pass = WIFEXITED(status) && WEXITSTATUS(status) == 0;
		if (!pass)
			failed++;
		printf("%s %s\n", pass ? "PASS" : "FAIL", framing_names[i]);
		fflush(stdout);
	}
	printf("%u framing groups, %u failed\n", count, failed);
	return failed ? 1 : 0;
}
