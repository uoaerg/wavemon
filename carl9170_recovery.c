/*
 * carl9170_recovery.c - automatic USB recovery for carl9170 adapter crashes
 *
 * Detects when the carl9170 USB WiFi adapter becomes unresponsive (firmware
 * upload failure, register write errors, probe failure) and recovers by
 * performing a USB unbind/rebind cycle via sysfs.
 *
 * Two recovery paths:
 *
 *   Startup recovery (carl9170_startup_recovery):
 *     Called before interface enumeration. Uses two strategies:
 *     a) Read saved USB path from ~/.wavemon/carl9170_usb_path (fast path)
 *     b) Scan /sys/bus/usb/devices/ for carl9170-compatible devices whose
 *        interface has no driver bound (probe failed after crash)
 *
 *   Runtime recovery (carl9170_recovery_attempt):
 *     Called from sampling loop when if_nametoindex() returns 0.
 *     Uses in-memory USB path saved during init.
 *
 * Requires root or write access to /sys/bus/usb/drivers/usb/{unbind,bind}.
 */
#include "iw_if.h"
#include <dirent.h>
#include <net/if.h>
#include <limits.h>
#include <time.h>
#include <sys/stat.h>

#define RECOVERY_WAIT_SEC	10
#define STATE_FILE		"carl9170_usb_path"
#define WAVEMON_DIR		".wavemon"

static char saved_ifname[IF_NAMESIZE];
static char saved_usb_path[64];
static bool is_carl9170;
static bool has_sysfs_access;

/**
 * Read a sysfs file into buf, stripping the trailing newline.
 * Returns number of bytes read, or 0 on failure.
 */
static int read_sysfs(const char *path, char *buf, size_t len)
{
	FILE *fp = fopen(path, "r");
	int n;

	if (!fp)
		return 0;
	if (!fgets(buf, len, fp)) {
		fclose(fp);
		return 0;
	}
	fclose(fp);
	n = strcspn(buf, "\n");
	buf[n] = '\0';
	return n;
}

/**
 * Extract USB bus path from sysfs for a network interface.
 *
 * /sys/class/net/wlan1/device -> .../usb3/3-7/3-7:1.0
 * We need "3-7" (the USB device, not the interface).
 */
static bool get_usb_bus_path(const char *ifname, char *buf, size_t len)
{
	char link[256], resolved[PATH_MAX];

	snprintf(link, sizeof(link), "/sys/class/net/%s/device", ifname);

	if (!realpath(link, resolved))
		return false;

	if (!strstr(resolved, "/usb"))
		return false;

	char *last_slash = strrchr(resolved, '/');
	if (!last_slash)
		return false;

	/* If basename contains ':', it's a USB interface — go up one level */
	if (strchr(last_slash + 1, ':')) {
		*last_slash = '\0';
		last_slash = strrchr(resolved, '/');
		if (!last_slash)
			return false;
	}

	snprintf(buf, len, "%s", last_slash + 1);
	return true;
}

static bool sysfs_write(const char *path, const char *value)
{
	FILE *fp = fopen(path, "w");

	if (fp) {
		fputs(value, fp);
		if (fclose(fp) == 0)
			return true;
	}

	/* No direct access — try via sudo (non-interactive) */
	char cmd[512];

	snprintf(cmd, sizeof(cmd),
		 "sudo -n sh -c 'echo \"%s\" > \"%s\"' 2>/dev/null",
		 value, path);
	return system(cmd) == 0;
}

static void state_file_path(char *buf, size_t len)
{
	const char *home = get_real_home();

	if (!home)
		home = "/tmp";
	snprintf(buf, len, "%s/%s/%s", home, WAVEMON_DIR, STATE_FILE);
}

static void save_state(const char *usb_path, const char *ifname)
{
	char path[512], dir[512];
	const char *home = get_real_home();
	FILE *fp;

	if (!home)
		return;

	snprintf(dir, sizeof(dir), "%s/%s", home, WAVEMON_DIR);
	mkdir(dir, 0700);
	fix_file_owner(dir);

	state_file_path(path, sizeof(path));
	fp = fopen(path, "w");
	if (fp) {
		fprintf(fp, "%s %s\n", usb_path, ifname);
		fclose(fp);
		fix_file_owner(path);
	}
}

static bool load_state(char *usb_path, char *ifname)
{
	char path[512], line[128];
	FILE *fp;

	state_file_path(path, sizeof(path));
	fp = fopen(path, "r");
	if (!fp)
		return false;

	if (!fgets(line, sizeof(line), fp)) {
		fclose(fp);
		return false;
	}
	fclose(fp);

	line[strcspn(line, "\n")] = '\0';

	if (sscanf(line, "%63s %15s", usb_path, ifname) != 2)
		return false;

	if (!strchr(usb_path, '-'))
		return false;

	return true;
}

static bool usb_rebind(const char *usb_path, const char *ifname)
{
	fprintf(stderr, "carl9170 recovery: rebinding usb device %s ...\n",
		usb_path);

	if (!sysfs_write("/sys/bus/usb/drivers/usb/unbind", usb_path)) {
		fprintf(stderr, "carl9170 recovery: unbind failed "
			"(need root?)\n");
		return false;
	}

	usleep(500000);

	if (!sysfs_write("/sys/bus/usb/drivers/usb/bind", usb_path)) {
		fprintf(stderr, "carl9170 recovery: bind failed\n");
		return false;
	}

	/* Wait for interface to reappear */
	for (int i = 0; i < RECOVERY_WAIT_SEC * 4; i++) {
		usleep(250000);
		if (if_nametoindex(ifname) != 0) {
			/* Give driver time to fully initialize */
			usleep(500000);
			fprintf(stderr, "carl9170 recovery: %s is back\n",
				ifname);
			return true;
		}
	}

	fprintf(stderr, "carl9170 recovery: %s did not reappear "
		"after %d seconds\n", ifname, RECOVERY_WAIT_SEC);
	return false;
}

/**
 * Scan /sys/bus/usb/devices/ for crashed carl9170 devices.
 *
 * A crashed carl9170 looks like:
 *   - USB device X-Y exists
 *   - USB interface X-Y:1.0 exists
 *   - X-Y:1.0 has no 'driver' symlink (probe failed)
 *   - X-Y:1.0/modalias contains "carl9170" compatible IDs
 *
 * We check the modalias for known carl9170 vendor:product prefixes.
 * The carl9170 driver supports multiple Atheros AR9170 USB IDs.
 */
static bool scan_crashed_carl9170(char *usb_path, size_t ulen)
{
	/*
	 * Known carl9170 USB vendor:product IDs (from kernel source
	 * drivers/net/wireless/ath/carl9170/usb.c).
	 * Modalias format: usb:vXXXXpYYYY...
	 */
	static const char *carl9170_modalias_prefixes[] = {
		"usb:v0CF3p9170",	/* Atheros AR9170 */
		"usb:v0CF3p1001",	/* TP-Link TL-WN821N v2 */
		"usb:v0CF3p1002",	/* Atheros AR9170 + 3G */
		"usb:v0CF3p1010",	/* Atheros */
		"usb:v0CF3pB002",	/* Ubiquiti WifiStation */
		"usb:v057Cp8401",	/* AVM Fritz!WLAN N */
		"usb:v057Cp8402",	/* AVM Fritz!WLAN N 2.4 */
		"usb:v0846p9010",	/* Netgear WNDA3100 */
		"usb:v0846p9040",	/* Netgear WNA1000 */
		"usb:v07D1p3C10",	/* D-Link DWA-160 A2 */
		"usb:v0CDEp0023",	/* Z-Com ZG-212 */
		"usb:v0CDEp0026",	/* Z-Com UB81 BG */
		"usb:v0CDEp0027",	/* Z-Com UB82 ABG */
		"usb:v083Ap7522",	/* Arcadyan WN7522 */
		"usb:v2019p5304",	/* Planex GW-US300MiniS */
		"usb:v04BBp093F",	/* IO-Data WN-GDN/US2 */
		"usb:v1435p0804",	/* Wistron NeWeb */
		"usb:v1435p0326",	/* Wistron NeWeb */
		"usb:v0586p3417",	/* ZyXEL NWD271N */
		"usb:v129Bp1667",	/* Siemens Gigaset USB 108 */
		NULL
	};
	DIR *dir;
	struct dirent *ent;

	dir = opendir("/sys/bus/usb/devices");
	if (!dir)
		return false;

	while ((ent = readdir(dir)) != NULL) {
		char iface[128], drv_path[256], modalias_path[256];
		char modalias[256];

		/* Only look at USB device directories (N-N or N-N.N) */
		if (ent->d_name[0] == '.' || !strchr(ent->d_name, '-'))
			continue;
		/* Skip interface entries (contain ':') */
		if (strchr(ent->d_name, ':'))
			continue;
		/* USB device names are short (e.g. "3-7", "3-6.1.3") */
		if (strlen(ent->d_name) > 20)
			continue;

		/* Build interface path: device:1.0 */
		snprintf(iface, sizeof(iface), "%s:1.0", ent->d_name);

		/* Check if interface has a driver bound */
		snprintf(drv_path, sizeof(drv_path),
			 "/sys/bus/usb/devices/%s/driver", iface);
		if (access(drv_path, F_OK) == 0)
			continue;	/* Driver bound — not crashed */

		/* Read interface modalias */
		snprintf(modalias_path, sizeof(modalias_path),
			 "/sys/bus/usb/devices/%s/modalias", iface);
		if (read_sysfs(modalias_path, modalias, sizeof(modalias)) <= 0)
			continue;

		/* Match against known carl9170 IDs */
		for (int i = 0; carl9170_modalias_prefixes[i]; i++) {
			if (strncmp(modalias, carl9170_modalias_prefixes[i],
				    strlen(carl9170_modalias_prefixes[i])) == 0) {
				snprintf(usb_path, ulen, "%s", ent->d_name);
				closedir(dir);
				fprintf(stderr, "carl9170 recovery: found "
					"crashed device at usb %s "
					"(modalias: %.30s...)\n",
					usb_path, modalias);
				return true;
			}
		}
	}

	closedir(dir);
	return false;
}

/**
 * Find the wireless interface name created by a USB device.
 * Scans /sys/class/net/wlan* and checks if the device path
 * leads back to the given USB device.
 */
static bool find_wlan_for_usb(const char *usb_path, char *ifname, size_t len)
{
	DIR *dir;
	struct dirent *ent;

	dir = opendir("/sys/class/net");
	if (!dir)
		return false;

	while ((ent = readdir(dir)) != NULL) {
		char link[256], resolved[PATH_MAX];

		if (strncmp(ent->d_name, "wlan", 4) != 0)
			continue;
		if (strlen(ent->d_name) >= IF_NAMESIZE)
			continue;

		snprintf(link, sizeof(link),
			 "/sys/class/net/%.16s/device", ent->d_name);
		if (!realpath(link, resolved))
			continue;

		/* Check if this interface belongs to our USB device */
		if (strstr(resolved, usb_path)) {
			snprintf(ifname, len, "%s", ent->d_name);
			closedir(dir);
			return true;
		}
	}

	closedir(dir);
	return false;
}

/**
 * Startup recovery — called before interface enumeration.
 *
 * Strategy 1: Read saved state file (fast path for known devices).
 * Strategy 2: Scan sysfs for crashed carl9170 USB devices (no state needed).
 */
bool carl9170_startup_recovery(void)
{
	char usb_path[64], ifname[IF_NAMESIZE];
	bool recovered = false;

	/* Strategy 1: saved state file */
	if (load_state(usb_path, ifname)) {
		if (if_nametoindex(ifname) != 0)
			return false;	/* Already up */

		char check[256];
		snprintf(check, sizeof(check),
			 "/sys/bus/usb/devices/%s", usb_path);
		if (access(check, F_OK) == 0) {
			recovered = usb_rebind(usb_path, ifname);
			if (recovered)
				return true;
		}
	}

	/* Strategy 2: scan for crashed carl9170 devices */
	if (!scan_crashed_carl9170(usb_path, sizeof(usb_path)))
		return false;

	/*
	 * Rebind and then find which wlan interface appeared.
	 * We don't know the ifname yet — the old one may have been wlan1
	 * but after rebind it could be wlan1 or wlanN.
	 */
	fprintf(stderr, "carl9170 recovery: rebinding usb device %s ...\n",
		usb_path);

	if (!sysfs_write("/sys/bus/usb/drivers/usb/unbind", usb_path)) {
		fprintf(stderr, "carl9170 recovery: unbind failed "
			"(need root?)\n");
		return false;
	}

	usleep(500000);

	if (!sysfs_write("/sys/bus/usb/drivers/usb/bind", usb_path)) {
		fprintf(stderr, "carl9170 recovery: bind failed\n");
		return false;
	}

	/* Wait for a wlan interface to appear on this USB device */
	for (int i = 0; i < RECOVERY_WAIT_SEC * 4; i++) {
		usleep(250000);
		if (find_wlan_for_usb(usb_path, ifname, sizeof(ifname))) {
			usleep(500000);
			fprintf(stderr, "carl9170 recovery: %s is back\n",
				ifname);
			/* Save for future fast recovery */
			save_state(usb_path, ifname);
			return true;
		}
	}

	fprintf(stderr, "carl9170 recovery: no interface appeared "
		"after %d seconds\n", RECOVERY_WAIT_SEC);
	return false;
}

/**
 * Initialize runtime recovery state. Called after interface is selected.
 * Saves USB path to state file for future startup recovery.
 */
void carl9170_recovery_init(const char *ifname)
{
	char drv[32];

	snprintf(saved_ifname, sizeof(saved_ifname), "%s", ifname);
	saved_usb_path[0] = '\0';
	is_carl9170 = false;

	if_get_driver(ifname, drv, sizeof(drv));
	if (strcmp(drv, "carl9170") != 0)
		return;

	is_carl9170 = true;
	has_sysfs_access = (geteuid() == 0);

	if (!get_usb_bus_path(ifname, saved_usb_path, sizeof(saved_usb_path))) {
		saved_usb_path[0] = '\0';
		return;
	}

	save_state(saved_usb_path, ifname);
}

bool carl9170_is_recoverable(void)
{
	return is_carl9170 && saved_usb_path[0];
}

bool carl9170_usb_present(void)
{
	char path[256];

	if (!saved_usb_path[0])
		return false;
	snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s", saved_usb_path);
	return access(path, F_OK) == 0;
}

bool carl9170_needs_root(void)
{
	return is_carl9170 && !has_sysfs_access;
}

/**
 * Runtime recovery — called from sampling loop when interface disappears.
 * Returns true if the interface came back, false otherwise.
 */
bool carl9170_recovery_attempt(void)
{
	if (!carl9170_is_recoverable())
		return false;

	return usb_rebind(saved_usb_path, saved_ifname);
}
