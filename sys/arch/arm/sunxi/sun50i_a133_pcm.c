/* Origin: EmberBSD - validate A133 PCM formats and transfer geometry. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/types.h>
#include <sys/errno.h>

#include "sun50i_a133_pcm.h"

int
a133_pcm_format(unsigned mode, unsigned rate, unsigned channels,
    struct a133_pcm_format *out)
{
	struct a133_pcm_format f = { 0 };
	uint32_t fs;

	if (out == NULL || (mode != A133_PCM_PLAY && mode != A133_PCM_RECORD) ||
	    (rate != 16000 && rate != 48000) || channels < 1 || channels > 2 ||
	    (mode == A133_PCM_RECORD && channels != 1))
		return EINVAL;

	/* Selector 3 is the observed 16 kHz setting in both native and Linux. */
	fs = rate == 16000 ? (3U << 29) : 0;
	f.frame_bytes = channels * 2;
	if (mode == A133_PCM_PLAY) {
		/* Low 16 bits, TX threshold 64; mono duplicates left into right. */
		f.dac_fifoc = fs | (1U << 24) | (64U << 8);
		if (channels == 1)
			f.dac_fifoc |= 1U << 6;
	} else {
		/* Low 16 bits, left ADC only, RX threshold 32. */
		f.adc_fifoc = fs | (1U << 24) | (1U << 12) | (32U << 4);
	}
	*out = f;
	return 0;
}
