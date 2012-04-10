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

/**
 * struct if_info  -  wireless interface network information
 * @hwaddr:		MAC address
 * @addr:		IPv4 interface address
 * @netmask:		IPv4 interface netmask
 * @bcast:		IPv4 interface broadcast address
 * @mtu:		interface MTU
 * @txqlen:		tx queue length
 * @flags:		interface flags
 * See also netdevice(7)
 */
struct if_info {
	struct ether_addr	hwaddr;
	struct in_addr		addr,
				netmask,
				bcast;
	uint16_t		mtu;
	short			txqlen;
	short			flags;
};
extern bool if_is_up(int skfd, const char *ifname);
extern int  if_set_up(int skfd, const char *ifname);
extern void if_getinf(const char *ifname, struct if_info *info);

/**
 * struct iw_key  -  Encoding information
 * @key:	encryption key
 * @size:	length of @key in bytes
 * @flags:	flags reported by SIOCGIWENCODE
 */
struct iw_key {
	uint8_t		key[IW_ENCODING_TOKEN_MAX];
	uint16_t	size;
	uint16_t	flags;
};

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
 * @keys:	array of encryption keys
 * @nkeys:	length of @keys
 * @active_key:	index of current key into @keys (counting from 1)
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
			cap_txpower:1,
			cap_retry:1,
			cap_rts:1,
			cap_frag:1,
			cap_mode:1,
			cap_ap:1,
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

	struct iw_key	*keys;
	uint8_t		nkeys;
	uint8_t		active_key;
};

/* Return the number of encryption keys marked 'active' in @info */
static inline uint8_t dyn_info_active_keys(struct iw_dyn_info *info)
{
	int i, num_active = 0;

	for (i = 0; i < info->nkeys; i++)
		num_active += info->keys[i].size &&
			      !(info->keys[i].flags & IW_ENCODE_DISABLED);
	return num_active;
}

/* Return the number of 40-bit/104-bit keys in @info */
static inline uint8_t dyn_info_wep_keys(struct iw_dyn_info *info)
{
	int i, num_wep = 0;

	for (i = 0; i < info->nkeys; i++)
		if (!(info->keys[i].flags & IW_ENCODE_DISABLED))
			num_wep += info->keys[i].size == 5 ||
				   info->keys[i].size == 13;
	return num_wep;
}
extern void dyn_info_get(struct iw_dyn_info *info,
			 const char *ifname, struct iw_range *ir);
extern void dyn_info_cleanup(struct iw_dyn_info *info);


/**
 * struct if_stat  -  Packet/byte counts for interfaces
 */
struct if_stat {
	unsigned long long	rx_packets,
				tx_packets;
	unsigned long long	rx_bytes,
				tx_bytes;
};

extern void if_getstat(const char *ifname, struct if_stat *stat);

/*
 *	 Structs to communicate WiFi statistics
 */
struct iw_levelstat {
	float	signal;		/* signal level in dBm */
	float	noise;		/* noise  level in dBm */
	uint8_t	flags;		/* level validity      */
};
#define IW_LSTAT_INIT { 0, 0, IW_QUAL_LEVEL_INVALID | IW_QUAL_NOISE_INVALID }

extern void iw_getinf_range(const char *ifname, struct iw_range *range);
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

/*
 * 	Periodic sampling of wireless statistics via timer alarm
 */
extern void iw_getstat(struct iw_stat *stat);
extern void iw_cache_update(struct iw_stat *stat);

extern void sampling_init(void (*sampling_handler)(int));
extern void sampling_do_poll(void);
static inline void sampling_stop(void)	{ alarm(0); }

/*
 *	Organization of scan results
 */
/**
 * struct scan_result  -  Ranked list of scan results
 * @ap_addr:	MAC address
 * @essid:	station SSID (may be empty)
 * @mode:	operation mode (type of station)
 * @freq:	frequency/channel information
 * @qual:	signal quality information
 * @has_key:	whether using encryption or not
 * @flags:	properties gathered from Information Elements
 * @next:	next, lower-ranking entry
 */
struct scan_result {
	struct ether_addr	ap_addr;
	char			essid[IW_ESSID_MAX_SIZE + 2];
	int			mode;
	double			freq;
	struct	iw_quality	qual;

	int 			has_key:1;
	uint32_t		flags;

	struct scan_result *next;
};
extern struct scan_result *get_scan_list(int skfd, const char *ifname, int we_version);
extern void free_scan_result(struct scan_result *head);

/**
 * struct cnt - count frequency of integer numbers
 * @val:	value to count
 * @count:	how often @val occurs
 */
struct cnt {
	int	val;
	int	count;
};
extern struct cnt *channel_stats(struct scan_result *head,
				 struct iw_range *iw_range, int *max_cnt);

/*
 *	General helper routines
 */
static inline const char *iw_opmode(const uint8_t mode)
{
	static char *modes[] = {
		[IW_MODE_AUTO]	  = "Auto",
		[IW_MODE_ADHOC]	  = "Ad-Hoc",
		[IW_MODE_INFRA]	  = "Managed",
		[IW_MODE_MASTER]  = "Master",
		[IW_MODE_REPEAT]  = "Repeater",
		[IW_MODE_SECOND]  = "Secondary",
		[IW_MODE_MONITOR] = "Monitor",
		[IW_MODE_MESH]	  = "Mesh"
	};

	return mode < ARRAY_SIZE(modes) ? modes[mode] : "Unknown/bug";
}

/* Print a mac-address, include leading zeroes (unlike ether_ntoa(3)) */
static inline char *ether_addr(const struct ether_addr *ea)
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
	if (sa->sa_family != ARPHRD_ETHER)
		return "00:00:00:00:00:00";
	return ether_lookup((const struct ether_addr *)sa->sa_data);
}

/* Format a (I)BSSID */
static inline char *format_bssid(const struct sockaddr *ap)
{
	uint8_t bcast_addr[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	uint8_t  zero_addr[ETH_ALEN] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	if (memcmp(ap->sa_data, zero_addr, ETH_ALEN) == 0)
		return "Not-Associated";
	if (memcmp(ap->sa_data, bcast_addr, ETH_ALEN) == 0)
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

/* Absolute power measurement in dBm (IW_QUAL_DBM): map into -192 .. 63 range */
static inline int u8_to_dbm(const int power)
{
	return power > 63 ? power - 0x100 : power;
}
static inline uint8_t dbm_to_u8(const int dbm)
{
	return dbm < 0 ? dbm + 0x100 : dbm;
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

/* Return frequency or 0 on error. Based on iw_channel_to_freq() */
static inline double channel_to_freq(uint8_t chan, const struct iw_range *range)
{
	int c;

	for (c = 0; c < range->num_frequency; c++)
		/* Check if it actually has stored a frequency */
		if (range->freq[c].i == chan && range->freq[c].m > 1000)
			return freq_to_hz(&range->freq[c]);
	return 0.0;
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

/* print @key in cleartext if it is in ASCII format, use hex format otherwise */
static inline char *format_key(const struct iw_key *const iwk)
{
	static char buf[128];
	int i, is_printable = 0, len = 0;

	/* Over-estimate key size: 2 chars per hex digit plus '-' */
	assert(iwk != NULL && iwk->size * 3 < sizeof(buf));

	for (i = 0; i < iwk->size && (is_printable = isprint(iwk->key[i])); i++)
		;

	if (is_printable)
		len += sprintf(buf, "\"");

	for (i = 0; i < iwk->size; i++)
		if (is_printable) {
			len += sprintf(buf + len, "%c", iwk->key[i]);
		} else {
			if (i > 0 && (i & 1) == 0)
				len += sprintf(buf + len, "-");
			len += sprintf(buf + len, "%02X", iwk->key[i]);
		}

	if (is_printable)
		len += sprintf(buf + len, "\"");

	sprintf(buf + len, " (%u bits)", iwk->size * 8);

	return buf;
}

/* Human-readable representation of IW_ENC_CAPA_ types */
static inline const char *format_enc_capab(const uint32_t capa, const char *sep)
{
	static char buf[32];
	size_t len = 0, max = sizeof(buf);

	if (capa & IW_ENC_CAPA_WPA)
		len = snprintf(buf, max, "WPA");
	if (capa & IW_ENC_CAPA_WPA2)
		len += snprintf(buf + len, max - len, "%sWPA2", len ? sep : "");
	if (capa & IW_ENC_CAPA_CIPHER_TKIP)
		len += snprintf(buf + len, max - len, "%sTKIP", len ? sep : "");
	if (capa & IW_ENC_CAPA_CIPHER_CCMP)
		len += snprintf(buf + len, max - len, "%sCCMP", len ? sep : "");
	buf[len] = '\0';
	return buf;
}

/* Display only the supported WPA type */
#define IW_WPA_MASK	(IW_ENC_CAPA_WPA|IW_ENC_CAPA_WPA2)
static inline const char *format_wpa(struct iw_range *ir)
{
	return format_enc_capab(ir->enc_capa & IW_WPA_MASK, "/");
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
