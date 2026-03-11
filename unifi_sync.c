/*
 * unifi_sync.c - fetch BSSID→AP-name mappings from UniFi Network API
 *
 * Connects to a UniFi Network Application controller, reads the device
 * list, extracts BSSID and AP-name from each access point's VAP table,
 * and writes the result to ~/.wavemon/ap_names.map.
 *
 * Configuration (URL, site, API key) is stored AES-256-CBC encrypted in
 * ~/.wavemon/unifi.conf.enc (mode 0600). The encryption key is derived
 * from /etc/machine-id + UID, making the file unreadable on other
 * machines or by other users. The API key is prompted once via secure
 * terminal input (no echo).
 *
 * Requires: curl, jq, openssl (invoked via popen)
 *
 * API key generation:
 *   Only via https://unifi.ui.com/
 *   Settings > Control Plane > Integrations
 *   (NOT available in the local controller web UI)
 */
#include "wavemon.h"
#include <sys/stat.h>
#include <termios.h>

#define WAVEMON_DIR	".wavemon"
#define UNIFI_CONF_ENC	"unifi.conf.enc"
#define UNIFI_CONF_OLD	"unifi.conf"
#define AP_MAP_FILE	"ap_names.map"

static char conf_dir[512];
static char conf_file[512];
static char conf_file_old[512];
static char map_file[512];

static char unifi_url[256];
static char unifi_site[256];
static char unifi_api_key[256];

static char enc_passphrase[128];

static void init_paths(void)
{
	const char *home = get_real_home();

	if (!home || !*home)
		err_quit("can not determine home directory");
	snprintf(conf_dir, sizeof(conf_dir), "%s/%s", home, WAVEMON_DIR);
	snprintf(conf_file, sizeof(conf_file), "%s/%s/%s",
		 home, WAVEMON_DIR, UNIFI_CONF_ENC);
	snprintf(conf_file_old, sizeof(conf_file_old), "%s/%s/%s",
		 home, WAVEMON_DIR, UNIFI_CONF_OLD);
	snprintf(map_file, sizeof(map_file), "%s/%s/%s",
		 home, WAVEMON_DIR, AP_MAP_FILE);
}

/**
 * Derive encryption passphrase from machine-id + UID.
 * This binds the encrypted config to this machine and user.
 */
static void derive_passphrase(void)
{
	FILE *fp;
	char machine_id[64] = "";

	fp = fopen("/etc/machine-id", "r");
	if (fp) {
		if (fgets(machine_id, sizeof(machine_id), fp))
			machine_id[strcspn(machine_id, "\n")] = '\0';
		fclose(fp);
	}

	if (!machine_id[0]) {
		/* fallback: use hostname */
		gethostname(machine_id, sizeof(machine_id));
	}

	snprintf(enc_passphrase, sizeof(enc_passphrase),
		 "%s:%u", machine_id, getuid());
}

/** Read a line from stdin without echo (for API key input). */
static void read_secret(const char *prompt, char *buf, size_t len)
{
	struct termios old, new;

	fprintf(stderr, "%s", prompt);
	fflush(stderr);

	if (tcgetattr(STDIN_FILENO, &old) < 0)
		err_sys("tcgetattr");
	new = old;
	new.c_lflag &= ~(tcflag_t)ECHO;
	if (tcsetattr(STDIN_FILENO, TCSANOW, &new) < 0)
		err_sys("tcsetattr");

	if (fgets(buf, len, stdin))
		buf[strcspn(buf, "\n")] = '\0';

	tcsetattr(STDIN_FILENO, TCSANOW, &old);
	fprintf(stderr, "\n");
}

/** Read a line from stdin with echo. */
static void read_line(const char *prompt, char *buf, size_t len)
{
	fprintf(stderr, "%s", prompt);
	fflush(stderr);

	if (fgets(buf, len, stdin))
		buf[strcspn(buf, "\n")] = '\0';
}

/** Decrypt and parse config file. Returns true if config is complete. */
static bool load_config(void)
{
	char cmd[1024];
	FILE *fp;
	char line[512];

	unifi_url[0] = unifi_site[0] = unifi_api_key[0] = '\0';

	/* Try encrypted config first */
	if (access(conf_file, R_OK) == 0) {
		snprintf(cmd, sizeof(cmd),
			 "openssl enc -d -aes-256-cbc -pbkdf2 -pass 'pass:%s' "
			 "-in '%s' 2>/dev/null",
			 enc_passphrase, conf_file);

		fp = popen(cmd, "r");
		if (!fp)
			return false;

		while (fgets(line, sizeof(line), fp)) {
			char key[64], val[256];

			if (sscanf(line, "%63[^=]=\"%255[^\"]\"", key, val) == 2) {
				if (strcmp(key, "UNIFI_URL") == 0)
					snprintf(unifi_url, sizeof(unifi_url), "%s", val);
				else if (strcmp(key, "UNIFI_SITE") == 0)
					snprintf(unifi_site, sizeof(unifi_site), "%s", val);
				else if (strcmp(key, "UNIFI_API_KEY") == 0)
					snprintf(unifi_api_key, sizeof(unifi_api_key), "%s", val);
			}
		}
		pclose(fp);

		if (unifi_url[0] && unifi_api_key[0])
			return true;
	}

	/* Migrate plaintext config if it exists */
	if (access(conf_file_old, R_OK) == 0) {
		fp = fopen(conf_file_old, "r");
		if (fp) {
			while (fgets(line, sizeof(line), fp)) {
				char key[64], val[256];

				if (sscanf(line, "%63[^=]=\"%255[^\"]\"", key, val) == 2) {
					if (strcmp(key, "UNIFI_URL") == 0)
						snprintf(unifi_url, sizeof(unifi_url), "%s", val);
					else if (strcmp(key, "UNIFI_SITE") == 0)
						snprintf(unifi_site, sizeof(unifi_site), "%s", val);
					else if (strcmp(key, "UNIFI_API_KEY") == 0)
						snprintf(unifi_api_key, sizeof(unifi_api_key), "%s", val);
				}
			}
			fclose(fp);

			if (unifi_url[0] && unifi_api_key[0]) {
				fprintf(stderr, "migrating plaintext config to encrypted...\n");
				/* Will be saved encrypted by caller */
				unlink(conf_file_old);
				return true;
			}
		}
	}

	return false;
}

/** Save config encrypted with AES-256-CBC. */
static void save_config(void)
{
	char cmd[1024];
	FILE *fp;

	mkdir(conf_dir, 0700);
	fix_file_owner(conf_dir);

	snprintf(cmd, sizeof(cmd),
		 "openssl enc -aes-256-cbc -pbkdf2 -pass 'pass:%s' "
		 "-out '%s' 2>/dev/null",
		 enc_passphrase, conf_file);

	fp = popen(cmd, "w");
	if (!fp)
		err_sys("cannot encrypt config");

	fprintf(fp, "UNIFI_URL=\"%s\"\n", unifi_url);
	fprintf(fp, "UNIFI_SITE=\"%s\"\n", unifi_site);
	fprintf(fp, "UNIFI_API_KEY=\"%s\"\n", unifi_api_key);

	if (pclose(fp) != 0)
		err_quit("failed to encrypt config (is openssl installed?)");

	chmod(conf_file, 0600);
	fix_file_owner(conf_file);

	/* Remove old plaintext config if it exists */
	unlink(conf_file_old);
}

static void prompt_config(void)
{
	char buf[256];

	read_line("unifi controller url (e.g. https://192.168.1.2): ",
		  buf, sizeof(buf));
	if (!buf[0])
		err_quit("controller url cannot be empty");
	/* strip trailing slash */
	size_t len = strlen(buf);
	if (len > 0 && buf[len - 1] == '/')
		buf[len - 1] = '\0';
	snprintf(unifi_url, sizeof(unifi_url), "%s", buf);

	read_line("site name [default]: ", buf, sizeof(buf));
	snprintf(unifi_site, sizeof(unifi_site), "%s",
		 buf[0] ? buf : "default");

	fprintf(stderr, "\napi key (generate at https://unifi.ui.com/"
		" > Settings > Control Plane > Integrations)\n");
	read_secret("api key: ", unifi_api_key, sizeof(unifi_api_key));
	if (!unifi_api_key[0])
		err_quit("api key cannot be empty");
}

static bool has_command(const char *cmd)
{
	const char *dirs[] = {
		"/usr/bin", "/usr/local/bin", "/bin", NULL
	};

	for (int i = 0; dirs[i]; i++) {
		char path[256];

		snprintf(path, sizeof(path), "%s/%s", dirs[i], cmd);
		if (access(path, X_OK) == 0)
			return true;
	}
	return false;
}

/**
 * Try fetching from a given API endpoint.
 * Returns the number of BSSID mappings written, or 0 on failure.
 */
static int try_fetch(const char *endpoint)
{
	char cmd[1024];
	FILE *pipe, *mapfp;
	char line[256];
	int count = 0;
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	char timestamp[32];

	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm);

	snprintf(cmd, sizeof(cmd),
		 "curl -sk --connect-timeout 10 --max-time 30 "
		 "-H 'X-API-KEY: %s' '%s' 2>/dev/null | "
		 "jq -r '"
		 ".data[] "
		 "| select(.vap_table != null) "
		 "| .name as $n "
		 "| .vap_table[] "
		 "| select(.bssid != null and .bssid != \"\") "
		 "| \"\\(.bssid),\\($n),\\(.essid // \"\")\"' "
		 "2>/dev/null",
		 unifi_api_key, endpoint);

	pipe = popen(cmd, "r");
	if (!pipe)
		return 0;

	mapfp = fopen(map_file, "w");
	if (!mapfp) {
		pclose(pipe);
		return 0;
	}

	fprintf(mapfp, "bssid,ap_name,ssid\n");

	while (fgets(line, sizeof(line), pipe)) {
		if (line[0] == '\n' || line[0] == '\0')
			continue;
		fputs(line, mapfp);
		count++;
		if (count <= 10)
			fprintf(stderr, "  %s", line);
	}

	pclose(pipe);
	fclose(mapfp);

	if (count == 0) {
		unlink(map_file);
	} else {
		fix_file_owner(map_file);
	}

	return count;
}

void unifi_sync(bool reset)
{
	int count;
	char endpoint[512];
	const char *home = get_real_home();

	/* Ensure valid CWD for shell subprocesses (popen). */
	if (home)
		if (chdir(home)) { /* ignore */ }

	init_paths();
	derive_passphrase();

	if (!has_command("curl") || !has_command("jq") || !has_command("openssl"))
		err_quit("required commands not found: curl, jq, openssl\n"
			 "  install with: sudo apt install curl jq openssl");

	if (reset || !load_config()) {
		prompt_config();
	}
	/* Always save (re-encrypts, ensures encrypted format) */
	save_config();

	fprintf(stderr, "fetching devices from %s (site: %s)...\n\n",
		unifi_url, unifi_site);

	/* Try new-style endpoint first (UniFi Network 8+) */
	snprintf(endpoint, sizeof(endpoint),
		 "%s/proxy/network/api/s/%s/stat/device",
		 unifi_url, unifi_site);
	count = try_fetch(endpoint);

	if (count <= 0) {
		/* Fallback: older controller endpoint */
		snprintf(endpoint, sizeof(endpoint),
			 "%s/api/s/%s/stat/device",
			 unifi_url, unifi_site);
		count = try_fetch(endpoint);
	}

	if (count <= 0)
		err_quit("no bssid mappings found.\n"
			 "  check: controller url, api key, site name\n"
			 "  reset with: wavemon --unifi-reset");

	if (count > 10)
		fprintf(stderr, "  ... (%d more)\n", count - 10);

	fprintf(stderr, "\nwritten %d bssid mappings to %s\n", count, map_file);
	fprintf(stderr, "wavemon will auto-load this map on startup.\n");
}
