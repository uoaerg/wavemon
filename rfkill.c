/*
 * Routines to check and describe the rfkill state of a wireless interface.
 *
 * Based on https://www.kernel.org/doc/Documentation/ABI/stable/sysfs-class-rfkill
 */
#include "iw_if.h"
#include <string.h>

/** Return human-friendly description of @state. */
const char *rfkill_state_name(const rfkill_state_t state) {
	static const char *states[] = {
		[RFKILL_STATE_UNDEFINED]    = "unknown",
		[RFKILL_STATE_UNBLOCKED]    = "unblocked",
		[RFKILL_STATE_SOFT_BLOCKED] = "software RF-kill",
		[RFKILL_STATE_HARD_BLOCKED] = "hardware RF-kill",
		[RFKILL_STATE_FULL_BLOCKED] = "hardware/software RF-kill",
	};
	return states[state];
}

/** Return true if @state indicates that RF-kill is active. */
bool is_rfkill_blocked_state(const rfkill_state_t state) {
	return state != RFKILL_STATE_UNBLOCKED && state != RFKILL_STATE_UNDEFINED;
}

/** Determine the rfkill state of wireless interface identified by @wdev_index. */
rfkill_state_t get_rfkill_state(const uint32_t wdev_index) {
	rfkill_state_t state = RFKILL_STATE_UNDEFINED;
	char wdev_state_path[256];
	uint32_t val;

	snprintf(wdev_state_path, sizeof(wdev_state_path) - 1,
		 "/sys/class/rfkill/rfkill%u/hard", wdev_index);
	if (read_number_file(wdev_state_path, &val) != 1)
		return RFKILL_STATE_UNDEFINED;

	state = val ? RFKILL_STATE_HARD_BLOCKED : RFKILL_STATE_UNBLOCKED;

	snprintf(wdev_state_path, sizeof(wdev_state_path) - 1,
		 "/sys/class/rfkill/rfkill%u/soft", wdev_index);
	if (read_number_file(wdev_state_path, &val) != 1)
		return RFKILL_STATE_UNDEFINED;

	if (!val)
		return state;
	if (state == RFKILL_STATE_HARD_BLOCKED)
		return RFKILL_STATE_FULL_BLOCKED;
	return RFKILL_STATE_SOFT_BLOCKED;
}

/** Return true if wireless interface @iface is known to be blocked by rfkill. */
bool is_rfkill_blocked(const uint32_t wdev_index) {
	return is_rfkill_blocked_state(get_rfkill_state(wdev_index));
}
