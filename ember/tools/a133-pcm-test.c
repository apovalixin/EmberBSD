/* Origin: EmberBSD - exercise A133 PCM formats and buffer boundaries. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sun50i_a133_pcm.h"

static void
formats(void)
{
	static const struct {
		unsigned mode, rate, channels, frame;
		uint32_t tx, rx;
	} good[] = {
		{ A133_PCM_PLAY, 16000, 1, 2, 0x61004040, 0 },
		{ A133_PCM_PLAY, 16000, 2, 4, 0x61004000, 0 },
		{ A133_PCM_PLAY, 48000, 1, 2, 0x01004040, 0 },
		{ A133_PCM_PLAY, 48000, 2, 4, 0x01004000, 0 },
		{ A133_PCM_RECORD, 16000, 1, 2, 0, 0x61001200 },
		{ A133_PCM_RECORD, 48000, 1, 2, 0, 0x01001200 },
	};
	static const struct {
		unsigned mode, rate, channels;
	} bad[] = {
		{ 0, 16000, 1 }, { 3, 16000, 1 }, { 4, 16000, 1 },
		{ A133_PCM_PLAY, 0, 1 }, { A133_PCM_PLAY, 44100, 1 },
		{ A133_PCM_PLAY, 96000, 2 }, { A133_PCM_PLAY, 48000, 0 },
		{ A133_PCM_PLAY, 48000, 3 },
		{ A133_PCM_RECORD, 16000, 2 },
	};
	struct a133_pcm_format f, saved;
	size_t i;

	for (i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
		memset(&f, 0xa5, sizeof(f));
		assert(a133_pcm_format(good[i].mode, good[i].rate,
		    good[i].channels, &f) == 0);
		assert(f.frame_bytes == good[i].frame);
		assert(f.dac_fifoc == good[i].tx);
		assert(f.adc_fifoc == good[i].rx);
		/* Neither direction starts streams, DMA or IRQs. */
		assert((f.dac_fifoc & 0x1f) == 0);
		assert((f.adc_fifoc & 0x1000000f) == 0);
	}
	memset(&f, 0xa5, sizeof(f));
	saved = f;
	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		assert(a133_pcm_format(bad[i].mode, bad[i].rate,
		    bad[i].channels, &f) == EINVAL);
		assert(memcmp(&f, &saved, sizeof(f)) == 0);
	}
	assert(a133_pcm_format(A133_PCM_PLAY, 16000, 1, NULL) == EINVAL);
}

int
main(void)
{
	formats();
	puts("A133 PCM formats passed");
	return 0;
}
