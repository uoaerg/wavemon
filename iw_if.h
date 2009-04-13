/*
 * wavemon - a wireless network monitoring aplication
 *
 * Copyright (c) 2001-2002 Jan Morgenstern <jan@jm-music.de>
 *
 * wavemon is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2, or (at your option) any later
 * version.
 *
 * wavemon is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along
 * with wavemon; see the file COPYING.  If not, write to the Free Software
 * Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include "wavemon.h"
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ether.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/wireless.h>

/* Definitions that appeared in more recent versions of wireless.h */
#ifndef IW_POWER_SAVING
#define IW_POWER_SAVING	0x4000		/* version 20 -> 21 */
#endif
#ifndef IW_MODE_MESH
#define IW_MODE_MESH	7		/* introduced in 2.6.26-rc1 */
#endif

/* Maximum length of a MAC address: 2 * 6 hex digits, 6 - 1 colons, plus '\0' */
#define MAC_ADDR_MAX	18

/*
 * Threshold for 'sane' noise levels.
 *
 * Some drivers simply set an arbitrary minimum noise level to mean 'invalid',
 * but do not set IW_QUAL_NOISE_INVALID so that the display gets stuck at a
 * "house number". The value below is suggested by and taken from the iwl3945
 * driver (constant IWL_NOISE_MEAS_NOT_AVAILABLE in iwl-3945.h).
 */
#define NOISE_DBM_SANE_MIN	-127

/* Static network interface information - see netdevice(7) */
struct if_info {		/* modified ifreq */
	struct ether_addr	hwaddr;
	struct in_addr		addr,
				netmask,
				bcast;
};
extern void if_getinf(char *ifname, struct if_info *info);

/**
 * struct iw_dyn_info  -  modified iw_req
 * @name:	interface name
 * @mode:	current operation mode (IW_MODE_xxx)
 *
 * @cap_*:	indicating capability/presence
 *
 * @essid:	Extended Service Set ID (network name)
 * @essid_ct:	index number of the @essid (starts at 1, 0 = off)
 * @nickname:	optional station nickname
 * @nwid:	Network ID (pre-802.11 hardware only)
 * @ap_addr:	BSSID or IBSSID
 *
 * @retry:	MAC-retransmission retry behaviour
 * @rts:	minimum packet size for which to perform RTS/CTS handshake
 * @frag:	802.11 frame fragmentation threshold size
 * @txpower:	TX power information
 * @power	power management information
 *
 * @freq:	frequency in Hz
 * @sens:	sensitivity threshold of the card
 * @bitrate:	bitrate (client mode)
 *
 * @key:	encryption key
 * @key_size:	length of @key in bytes
 * @key_flags:	bitmask with information about @key
 *
 */
struct iw_dyn_info {
	char		name[IFNAMSIZ];
	uint8_t		mode;

	bool		cap_essid:1,
			cap_nwid:1,
			cap_nickname:1,
			cap_freq:1,
			cap_sens:1,
			cap_bitrate:1,
			cap_txpower:1,
			cap_retry:1,
			cap_rts:1,
			cap_frag:1,
			cap_mode:1,
			cap_ap:1,
			cap_key:1,
			cap_power:1,
			cap_aplist:1;

	char		essid[IW_ESSID_MAX_SIZE+2];
	uint8_t		essid_ct;
	char		nickname[IW_ESSID_MAX_SIZE+2];
	struct iw_param nwid;
	struct sockaddr ap_addr;

	struct iw_param retry;
	struct iw_param rts;
	struct iw_param frag;
	struct iw_param txpower;
	struct iw_param power;

	float		freq;
	int32_t		sens;
	unsigned long	bitrate;

	char		key[IW_ENCODING_TOKEN_MAX];
	uint16_t	key_size;
	uint16_t	key_flags;
};

struct if_stat {
	unsigned long long	rx_packets,
				tx_packets;
	unsigned long long	rx_bytes,
				tx_bytes;
};

extern void if_getstat(char *ifname, struct if_stat *stat);

/*
 *	 Structs to communicate WiFi statistics
 */
struct iw_levelstat {
	float signal;		/* signal level in dBm */
	float noise;		/* noise  level in dBm */
};
extern void iw_sanitize(struct iw_range *range,
			struct iw_quality *qual,
			struct iw_levelstat *dbm);

/**
 * struct iw_stat - record current WiFi state
 * @range:	current range information
 * @stats:	current signal level statistics
 * @dbm:	the noise/signal of @stats in dBm
 */
struct iw_stat {
	struct iw_range		range;
	struct iw_statistics	stat;
	struct iw_levelstat	dbm;
};

extern void iw_getstat(struct iw_stat *stat);
extern void iw_cache_update(struct iw_stat *stat);

extern void iw_getinf_dyn(char *ifname, struct iw_dyn_info *info);
extern void iw_getinf_range(char *ifname, struct iw_range *range);

extern void (*iw_stat_redraw) (void);

/*
 *	Helper routines
 */
static inline const char *iw_opmode(const uint8_t mode)
{
	static char *modes[] = { "Auto",
				 "Ad-Hoc",
				 "Managed",
				 "Master",
				 "Repeater",
				 "Secondary",
				 "Monitor",
				 "Mesh"
	};

	return mode < ARRAY_SIZE(modes) ? modes[mode] : "Unknown/bug";
}

static inline bool is_zero_ether_addr(const uint8_t *mac)
{
	return ! (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]);
}

static inline bool is_broadcast_ether_addr(const uint8_t *mac)
{
	return (mac[0] & mac[1] & mac[2] & mac[3] & mac[4] & mac[5]) == 0xff;
}

/* Print a mac-address, include leading zeroes (unlike ether_ntoa(3)) */
static inline char *ether_addr(const struct ether_addr *ea)
{
	static char str[MAC_ADDR_MAX];

	sprintf(str, "%02X:%02X:%02X:%02X:%02X:%02X",
		ea->ether_addr_octet[0], ea->ether_addr_octet[1],
		ea->ether_addr_octet[2], ea->ether_addr_octet[3],
		ea->ether_addr_octet[4], ea->ether_addr_octet[5]);
	return str;
}

/* Print mac-address translation from /etc/ethers if available */
static inline char *ether_lookup(const struct ether_addr *ea)
{
	static char hostname[BUFSIZ];

	if (ether_ntohost(hostname, ea) == 0)
		return hostname;
	return ether_addr(ea);
}

/* Format an Ethernet mac address */
static inline char *mac_addr(const struct sockaddr *sa)
{
	struct ether_addr zero = { {0} };

	if (sa->sa_family != ARPHRD_ETHER)
		return ether_addr(&zero);
	return ether_lookup((struct ether_addr *)sa->sa_data);
}

/* Format a (I)BSSID */
static inline char *format_bssid(const struct sockaddr *ap)
{
	const struct ether_addr *bssid = (struct ether_addr *)ap->sa_data;

	if (is_zero_ether_addr(bssid->ether_addr_octet))
		return "Not-Associated";
	if (is_broadcast_ether_addr(bssid->ether_addr_octet))
		return "Invalid";
	return mac_addr(ap);
}

/* count bits set in @mask the Brian Kernighan way */
static inline uint8_t bit_count(uint32_t mask)
{
	uint8_t bits_set;

	for (bits_set = 0; mask; bits_set++)
		mask &= mask - 1;

	return bits_set;
}

/* netmask = contiguous 1's followed by contiguous 0's */
static inline uint8_t prefix_len(const struct in_addr *netmask)
{
	return bit_count(netmask->s_addr);
}

/* Convert log dBm values to linear mW */
static inline double dbm2mw(const double in)
{
	return pow(10.0, in / 10.0);
}

static inline char *dbm2units(const double in)
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
static inline double mw2dbm(const double in)
{
	return 10.0 * log10(in);
}

/* Format driver TX power information */
static inline char *format_txpower(const struct iw_param *txpwr)
{
	static char txline[0x40];

	if (txpwr->flags & IW_TXPOW_RELATIVE)
		snprintf(txline, sizeof(txline), "%d (no units)", txpwr->value);
	else if (txpwr->flags & IW_TXPOW_MWATT)
		snprintf(txline, sizeof(txline), "%.0f dBm (%d mW)",
				mw2dbm(txpwr->value), txpwr->value);
	else
		snprintf(txline, sizeof(txline), "%d dBm (%.2f mW)",
				txpwr->value, dbm2mw(txpwr->value));
	return txline;
}

/* Format driver Power Management information */
static inline char *format_power(const struct iw_param *pwr,
				 const struct iw_range *range)
{
	static char buf[0x80];
	double val = pwr->value;
	int len = 0;

	if (pwr->disabled)
		return "off";
	else if (pwr->flags == IW_POWER_ON)
		return "on";

	if (pwr->flags & IW_POWER_MIN)
		len += snprintf(buf + len, sizeof(buf) - len, "min ");
	if (pwr->flags & IW_POWER_MAX)
		len += snprintf(buf + len, sizeof(buf) - len, "max ");

	if (pwr->flags & IW_POWER_TIMEOUT)
		len += snprintf(buf + len, sizeof(buf) - len, "timeout ");
	else if (pwr->flags & IW_POWER_SAVING)
		len += snprintf(buf + len, sizeof(buf) - len, "saving ");
	else
		len += snprintf(buf + len, sizeof(buf) - len, "period ");

	if (pwr->flags & IW_POWER_RELATIVE && range->we_version_compiled < 21)
		len += snprintf(buf + len, sizeof(buf) - len, "%+g", val/1e6);
	else if (pwr->flags & IW_POWER_RELATIVE)
		len += snprintf(buf + len, sizeof(buf) - len, "%+g", val);
	else if (val > 1e6)
		len += snprintf(buf + len, sizeof(buf) - len, "%g s", val/1e6);
	else if (val > 1e3)
		len += snprintf(buf + len, sizeof(buf) - len, "%g ms", val/1e3);
	else
		len += snprintf(buf + len, sizeof(buf) - len, "%g us", val);

	switch (pwr->flags & IW_POWER_MODE) {
	case IW_POWER_UNICAST_R:
		len += snprintf(buf + len, sizeof(buf) - len, ", rcv unicast");
		break;
	case IW_POWER_MULTICAST_R:
		len += snprintf(buf + len, sizeof(buf) - len, ", rcv mcast");
		break;
	case IW_POWER_ALL_R:
		len += snprintf(buf + len, sizeof(buf) - len, ", rcv all");
		break;
	case IW_POWER_FORCE_S:
		len += snprintf(buf + len, sizeof(buf) - len, ", force send");
		break;
	case IW_POWER_REPEATER:
		len += snprintf(buf + len, sizeof(buf) - len, ", repeat mcast");
	}

	return buf;
}

/* See comments on 'struct iw_freq' in wireless.h */
static inline float freq_to_hz(const struct iw_freq *freq)
{
	return freq->m * pow(10, freq->e);
}

/* Return channel number or -1 on error. Based on iw_freq_to_channel() */
static inline int freq_to_channel(double freq, const struct iw_range *range)
{
	int i;

	if (freq < 1.0e3)
		return -1;

	for (i = 0; i < range->num_frequency; i++)
		if (freq_to_hz(&range->freq[i]) == freq)
			return range->freq[i].i;
	return -1;
}

static inline char *format_retry(const struct iw_param *retry,
				 const struct iw_range *range)
{
	static char buf[0x80];
	double val = retry->value;
	int len = 0;

	if (retry->disabled)
		return "off";
	else if (retry->flags == IW_RETRY_ON)
		return "on";

	if (retry->flags & IW_RETRY_MIN)
		len += snprintf(buf + len, sizeof(buf) - len, "min ");
	if (retry->flags & IW_RETRY_MAX)
		len += snprintf(buf + len, sizeof(buf) - len, "max ");
	if (retry->flags & IW_RETRY_SHORT)
		len += snprintf(buf + len, sizeof(buf) - len, "short ");
	if (retry->flags & IW_RETRY_LONG)
		len += snprintf(buf + len, sizeof(buf) - len, "long ");

	if (retry->flags & IW_RETRY_LIFETIME)
		len += snprintf(buf + len, sizeof(buf) - len, "lifetime ");
	else {
		snprintf(buf + len, sizeof(buf) - len, "limit %d", retry->value);
		return buf;
	}

	if (retry->flags & IW_RETRY_RELATIVE && range->we_version_compiled < 21)
		len += snprintf(buf + len, sizeof(buf) - len, "%+g", val/1e6);
	else if (retry->flags & IW_RETRY_RELATIVE)
		len += snprintf(buf + len, sizeof(buf) - len, "%+g", val);
	else if (val > 1e6)
		len += snprintf(buf + len, sizeof(buf) - len, "%g s", val/1e6);
	else if (val > 1e3)
		len += snprintf(buf + len, sizeof(buf) - len, "%g ms", val/1e3);
	else
		len += snprintf(buf + len, sizeof(buf) - len, "%g us", val);

	return buf;
}

