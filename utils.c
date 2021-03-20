/*
 * General-purpose utilities used by multiple files.
 */
#include "wavemon.h"
#include "nl80211.h"
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ether.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <sys/socket.h>
#include <linux/if.h>

/* Maximum length of a MAC address: 2 * 6 hex digits, 6 - 1 colons, plus '\0' */
#define MAC_ADDR_MAX	18

/** Read up to @buflen contents of file @path into @buf. Returns -1 on error. */
ssize_t read_file(const char *path, char *buf, size_t buflen) {
	int fd = open(path, O_RDONLY), n;

	if (fd < 0)
		return -1;

	memset(buf, 0, buflen);

	n = read(fd, buf, buflen);
	if (close(fd) < 0)
		return -1;
	return n;
}

/** Read contents of @path into @num. Return 1 if ok, 0 on format error, -1 on error */
int read_number_file(const char *path, uint32_t *num) {
	char buf[64];
	int rc = read_file(path, buf, sizeof(buf));

	if (rc < 0)
		return rc;
	return sscanf(buf, "%u", num);
}

/* Return true if all ethernet octets are zero. */
bool ether_addr_is_zero(const struct ether_addr *ea)
{
	static const struct ether_addr zero = {{0}};

	return memcmp(ea, &zero, sizeof(zero)) == 0;
}

/* Print a mac-address, include leading zeroes (unlike ether_ntoa(3)) */
char *ether_addr(const struct ether_addr *ea)
{
	static char mac[MAC_ADDR_MAX];
	char *d = mac, *a = ether_ntoa(ea);
next_chunk:
	if (a[0] == '\0' || a[1] == '\0' || a[1] == ':')
		*d++ = '0';
	while ((*d++ = conf.cisco_mac ? (*a == ':' ? '.' : *a) : toupper(*a)))
		if (*a++ == ':')
			goto next_chunk;
	return mac;
}

/* Print mac-address translation from /etc/ethers if available */
char *ether_lookup(const struct ether_addr *ea)
{
	static char hostname[BUFSIZ];

	if (ether_ntohost(hostname, ea) == 0)
		return hostname;
	return ether_addr(ea);
}

/* Format an Ethernet mac address */
char *mac_addr(const struct sockaddr *sa)
{
	if (sa->sa_family != ARPHRD_ETHER)
		return "00:00:00:00:00:00";
	return ether_lookup((const struct ether_addr *)sa->sa_data);
}

/* count bits set in @mask the Brian Kernighan way */
uint8_t bit_count(uint32_t mask)
{
	uint8_t bits_set;

	for (bits_set = 0; mask; bits_set++)
		mask &= mask - 1;

	return bits_set;
}

/* netmask = contiguous 1's followed by contiguous 0's */
uint8_t prefix_len(const struct sockaddr *netmask)
{
	uint8_t bs = 0;

	if (netmask->sa_family == AF_INET)
		return bit_count(((const struct sockaddr_in *)netmask)->sin_addr.s_addr);
	for (int i = 0; i < 16; i++)
		bs += bit_count(((const struct sockaddr_in6 *)netmask)->sin6_addr.s6_addr[i]);
	return bs;
}

/* Pretty-print @sec into a string of up to 6 characters. */
const char *pretty_time(const unsigned sec)
{
	static char buf[12];
	unsigned d = sec / 86400,
		 h = sec % 86400 / 3600,
		 m = sec %  3600 /   60;

	if (d > 9) {
		sprintf(buf, "%u days", d);
	} else if (d) {
		if (h) {
			sprintf(buf, "%ud %uh", d, h);
		} else if (m) {
			sprintf(buf, "%ud %dm", d, m);
		} else {
			sprintf(buf, "%u day%s", d, d == 1 ? "" : "s");
		}
	} else if (h) {
		sprintf(buf, "%u:%02uh", h, m);
	} else if (m) {
		sprintf(buf, "%u:%02um", m, sec % 60);
	} else {
		sprintf(buf, "%u sec",  sec);
	}
	return buf;
}

/* Like pretty_time, but allow milliseconds */
const char *pretty_time_ms(const unsigned msec)
{
	static char buf[12];

	if (msec < 1000) {
		sprintf(buf, "%u ms",  msec);
		return buf;
	}
	return pretty_time(msec/1000);
}

/* Convert log dBm values to linear mW */
double dbm2mw(const double in)
{
	return pow(10.0, in / 10.0);
}

char *dbm2units(const double in)
{
	static char with_units[0x100];
	double val = dbm2mw(in);

	if (val < 0.00000001) {
		sprintf(with_units, "%.2f pW", val * 1e9);
	} else if (val < 0.00001) {
		sprintf(with_units, "%.2f nW", val * 1e6);
	} else if (val < 0.01) {
		sprintf(with_units, "%.2f uW", val * 1e3);
	} else {
		sprintf(with_units, "%.2f mW", val);
	}
	return with_units;
}

/* Convert linear mW values to log dBm */
double mw2dbm(const double in)
{
	return 10.0 * log10(in);
}

/* Stolen from iw:util.c */
int ieee80211_frequency_to_channel(int freq)
{
	/* see 802.11-2007 17.3.8.3.2 and Annex J */
	if (freq == 2484)
		return 14;
	else if (freq < 2484)
		return (freq - 2407) / 5;
	else if (freq >= 4910 && freq <= 4980)
		return (freq - 4000) / 5;
	else if (freq <= 45000) /* DMG band lower limit */
		return (freq - 5000) / 5;
	else if (freq >= 58320 && freq <= 64800)
		return (freq - 56160) / 2160;
	else
		return 0;
}

const char *channel_width_name(enum nl80211_chan_width width)
{
	switch (width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
		return "20 MHz (no HT)";
	case NL80211_CHAN_WIDTH_20:
		return "20 MHz";
	case NL80211_CHAN_WIDTH_40:
		return "40 MHz";
	case NL80211_CHAN_WIDTH_80:
		return "80 MHz";
	case NL80211_CHAN_WIDTH_80P80:
		return "80+80 MHz";
	case NL80211_CHAN_WIDTH_160:
		return "160 MHz";
	case NL80211_CHAN_WIDTH_5:
		return "5 MHz";
	case NL80211_CHAN_WIDTH_10:
		return "10 MHz";
	default:
		return "unknown";
	}
}

/* stolen from iw:interface.c */
const char *channel_type_name(enum nl80211_channel_type channel_type)
{
	switch (channel_type) {
	case NL80211_CHAN_NO_HT:
		return "NO HT";
	case NL80211_CHAN_HT20:
		return "HT20";
	case NL80211_CHAN_HT40MINUS:
		return "HT40-";
	case NL80211_CHAN_HT40PLUS:
		return "HT40+";
	default:
		return "unknown";
	}
}

/* stolen from iw:util.c */
const char *iftype_name(enum nl80211_iftype iftype)
{
	static char modebuf[100];
	static const char *ifmodes[NL80211_IFTYPE_MAX + 1] = {
		"Unspecified",
		"IBSS",
		"Managed",
		"AP",
		"AP/VLAN",
		"WDS",
		"Monitor",
		"Mesh Point",
		"P2P-Client",
		"P2P-GO",
		"P2P-Device",
		"Outside of a BSS",
		"NAN",
	};

	if (iftype <= NL80211_IFTYPE_MAX && ifmodes[iftype])
		return ifmodes[iftype];
	sprintf(modebuf, "Unknown mode (%d)", iftype);
	return modebuf;
}

/* stolen from iw:reg.c */
const char *dfs_domain_name(enum nl80211_dfs_regions region)
{
	switch (region) {
	case NL80211_DFS_UNSET:
		return "DFS-UNSET";
	case NL80211_DFS_FCC:
		return "DFS-FCC";
	case NL80211_DFS_ETSI:
		return "DFS-ETSI";
	case NL80211_DFS_JP:
		return "DFS-JP";
	default:
		return "DFS-invalid";
	}
}
