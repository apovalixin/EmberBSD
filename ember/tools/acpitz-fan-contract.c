/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

struct zone {
	bool force_fan;
	bool powered;
	unsigned int refreshes;
	unsigned int samples;
};

static void
acpitz_get_zone(void *opaque, int verbose)
{
	struct zone *zone = opaque;
	assert(verbose == 0);
	zone->powered = false;
	zone->refreshes++;
}

static void
acpitz_get_status(void *opaque)
{
	struct zone *zone = opaque;
	assert(zone->refreshes > zone->samples);
	zone->powered = zone->force_fan;
	zone->samples++;
}

#include "acpitz-zone-callback.h"

int
main(void)
{
	struct zone zone = { .force_fan = true, .powered = true };

	(void)acpitz_get_status;

	/* Trip-point/device-list notifications must reapply the cooling policy. */
	acpitz_get_zone_quiet(&zone);
	assert(zone.powered);
	assert(zone.refreshes == 1 && zone.samples == 1);
	acpitz_get_zone_quiet(&zone);
	assert(zone.powered);
	assert(zone.refreshes == 2 && zone.samples == 2);
	zone.force_fan = false;
	acpitz_get_zone_quiet(&zone);
	assert(!zone.powered);
	assert(zone.refreshes == 3 && zone.samples == 3);
	puts("PASS: thermal-zone notifications reapply the cooling policy");
	return 0;
}
