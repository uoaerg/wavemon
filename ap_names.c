/*
 * ap_names.c - BSSID to friendly AP-name mapping
 *
 * Reads a CSV mapping file:
 *
 *   bssid,ap_name,ssid
 *   aa:bb:cc:dd:ee:ff,my-access-point,MyNetwork
 *   11:22:33:44:55:66,office-ap,GuestWiFi
 *
 * The header row is required but skipped during parsing.
 * Only bssid and ap_name are used for lookup; ssid is informational.
 *
 * The file path is set via ap_names_set_file() before calling ap_names_load().
 * Lookups via ap_names_lookup() return the friendly name or NULL.
 */
#include "iw_if.h"

#define MAX_AP_NAMES 256

static struct {
	struct ether_addr	bssid;
	char			name[64];
} ap_map[MAX_AP_NAMES];

static int ap_map_count;
static char ap_names_path[256];

void ap_names_set_file(const char *path)
{
	snprintf(ap_names_path, sizeof(ap_names_path), "%s", path);
}

void ap_names_load(void)
{
	FILE *fp;
	char line[256];
	bool header_skipped = false;

	ap_map_count = 0;

	if (!ap_names_path[0])
		return;

	fp = fopen(ap_names_path, "r");
	if (!fp)
		return;

	while (fgets(line, sizeof(line), fp) && ap_map_count < MAX_AP_NAMES) {
		char *p = line;

		/* strip trailing newline */
		p[strcspn(p, "\r\n")] = '\0';

		/* skip empty lines and comments */
		if (*p == '\0' || *p == '#')
			continue;

		/* skip CSV header row */
		if (!header_skipped) {
			if (strncmp(p, "bssid", 5) == 0) {
				header_skipped = true;
				continue;
			}
			header_skipped = true;
		}

		/* parse: bssid,ap_name[,ssid] */
		char *bssid_str = p;
		char *comma = strchr(p, ',');

		if (!comma)
			continue;
		*comma = '\0';
		char *name_str = comma + 1;

		/* trim optional ssid field */
		char *comma2 = strchr(name_str, ',');
		if (comma2)
			*comma2 = '\0';

		/* strip whitespace around fields */
		while (*bssid_str == ' ') bssid_str++;
		while (*name_str == ' ') name_str++;

		struct ether_addr *ea = ether_aton(bssid_str);
		if (ea && *name_str) {
			ap_map[ap_map_count].bssid = *ea;
			snprintf(ap_map[ap_map_count].name,
				 sizeof(ap_map[ap_map_count].name),
				 "%s", name_str);
			ap_map_count++;
		}
	}
	fclose(fp);
}

const char *ap_names_lookup(const struct ether_addr *bssid)
{
	if (!bssid)
		return NULL;

	for (int i = 0; i < ap_map_count; i++) {
		if (memcmp(&ap_map[i].bssid, bssid, sizeof(*bssid)) == 0)
			return ap_map[i].name;
	}
	return NULL;
}
