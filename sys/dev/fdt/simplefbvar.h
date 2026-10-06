/* Origin: EmberBSD deferred firmware console selection, 2026-10-06. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _DEV_FDT_SIMPLEFBVAR_H_
#define _DEV_FDT_SIMPLEFBVAR_H_

/* Called during deferred attachment, before config_finalize fallback. */
bool simplefb_console_reserve(device_t);
void simplefb_console_commit(device_t);

#endif
