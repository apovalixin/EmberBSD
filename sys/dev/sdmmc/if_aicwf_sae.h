/* SPDX-License-Identifier: BSD-2-Clause */
/* Bounded decoding of the AIC8800D80 external-authentication wire format. */
#ifndef _DEV_SDMMC_IF_AICWF_SAE_H_
#define _DEV_SDMMC_IF_AICWF_SAE_H_

static inline bool
aicwf_sae_request_valid(const uint8_t *p, size_t len, uint8_t vif,
    const uint8_t *peer, const uint8_t *ssid, size_t ssid_len)
{
	/* vif, mac_ssid, mac_addr, SAE selector (legacy firmware swaps it). */
	return len >= 44 && ssid_len <= 32 && p[0] == vif &&
	    p[1] == ssid_len && memcmp(p + 2, ssid, ssid_len) == 0 &&
	    memcmp(p + 34, peer, 6) == 0 &&
	    (memcmp(p + 40, "\x08\xac\x0f\x00", 4) == 0 ||
	    memcmp(p + 40, "\x00\x0f\xac\x08", 4) == 0);
}

static inline bool
aicwf_sae_frame_valid(const uint8_t *p, size_t len, const uint8_t *dest,
    const uint8_t *source, const uint8_t *peer)
{
	/* Only unfragmented, unprotected SAE authentication (retry is allowed). */
	return len >= 30 && len <= 1536 && p[0] == 0xb0 &&
	    (p[1] & ~0x08) == 0 && (p[22] & 0x0f) == 0 &&
	    p[24] == 3 && p[25] == 0 &&
	    memcmp(p + 4, dest, 6) == 0 &&
	    memcmp(p + 10, source, 6) == 0 &&
	    memcmp(p + 16, peer, 6) == 0;
}

static inline bool
aicwf_sae_igtk_valid(const uint8_t *p, size_t len)
{
	/* The firmware key command has no initial IPN field. Fail closed. */
	return len == 24 && (p[0] == 4 || p[0] == 5) && p[1] == 0 &&
	    memcmp(p + 2, "\0\0\0\0\0\0", 6) == 0;
}
#endif
