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
#include "nl80211.h"
#include <netdb.h>
#include <stdbool.h>
#include <pthread.h>
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

/* Definitions from linux/ieee80211.h (not necessarily part of distro headers) */
#define WLAN_CAPABILITY_ESS		(1<<0)
#define WLAN_CAPABILITY_IBSS		(1<<1)
#define WLAN_CAPABILITY_IS_STA_BSS(cap) \
		(!((cap) & (WLAN_CAPABILITY_ESS | WLAN_CAPABILITY_IBSS)))
#define WLAN_CAPABILITY_PRIVACY		(1<<4)

/* 802.11h */
#define WLAN_CAPABILITY_SPECTRUM_MGMT   (1<<8)
#define WLAN_CAPABILITY_QOS             (1<<9)
#define WLAN_CAPABILITY_SHORT_SLOT_TIME (1<<10)
#define WLAN_CAPABILITY_APSD            (1<<11)
#define WLAN_CAPABILITY_RADIO_MEASURE   (1<<12)
#define WLAN_CAPABILITY_DSSS_OFDM       (1<<13)
#define WLAN_CAPABILITY_DEL_BACK        (1<<14)
#define WLAN_CAPABILITY_IMM_BACK        (1<<15)


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
	uint16_t		txqlen;
	uint16_t		flags;
};
extern bool if_is_up(const char *ifname);
extern int  if_set_up(const char *ifname);
extern void if_set_down_on_exit(int rc, void *arg);
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
	struct sockaddr ap_addr;

	struct iw_param retry;
	struct iw_param rts;
	struct iw_param frag;
	struct iw_param txpower;
	struct iw_param power;

	float		freq;
	int32_t		sens;
	unsigned long	bitrate;
};

extern void dyn_info_get(struct iw_dyn_info *info, const char *ifname);


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

/*
 * 	Periodic sampling of wireless statistics
 */
extern void sampling_init(void);
extern void sampling_stop(void);

/*
 *	Organization of scan results
 */
/**
 * struct scan_entry  -  Representation of a single scan result.
 * @ap_addr:	     MAC address
 * @essid:	     station SSID (may be empty)
 * @freq:	     frequency in MHz
 * @chan:	     channel corresponding to @freq (where applicable)
 * @has_key:	     whether using encryption or not
 * @last_seen:       time station was last seen in seconds
 * @tsf:             value of the Timing Synchronisation Function counter
 * @bss_signal:	     signal strength of BSS probe in dBm (or 0)
 * @bss_signal_qual: unitless signal strength of BSS probe, 0..100
 * @bss_capa:	     BSS capability flags
 * @bss_sta_count:   BSS station count
 * @bss_chan_usage:  BSS channel utilisation
 * @next:	     next entry in list
 */
struct scan_entry {
	struct ether_addr	ap_addr;
	char			essid[IW_ESSID_MAX_SIZE + 2];
	uint32_t		freq;
	int			chan;
	uint8_t			has_key:1;

	uint32_t		last_seen;
	uint64_t		tsf;

	int8_t			bss_signal;
	uint8_t			bss_signal_qual;
	uint16_t		bss_capa;
	uint8_t			bss_sta_count,
				bss_chan_usage;

	struct scan_entry	*next;
};
extern void sort_scan_list(struct scan_entry **headp);

/**
 * struct cnt - count frequency of integer numbers
 * @val:	value to count
 * @count:	how often @val occurs
 */
struct cnt {
	int	val;
	int	count;
};

/**
 * struct scan_result - Structure to aggregate all collected scan data.
 * @head:	   begin of scan_entry list (may be NULL)
 * @msg:	   error message, if any
 * @max_essid_len: maximum ESSID-string length (for formatting)
 * @channel_stats: array of channel statistics entries
 * @num.total:     number of entries in list starting at @head
 * @num.open:      number of open entries among @num.total
 * @num.two_gig:   number of 2.4GHz stations among @num.total
 * @num.five_gig:  number of 5 GHz stations among @num.total
 * @num.ch_stats:  length of @channel_stats array
 * @mutex:         protects against concurrent consumer/producer access
 */
struct scan_result {
	struct scan_entry *head;
	char		  msg[128];
	uint16_t	  max_essid_len;
	struct cnt	  *channel_stats;
	struct assorted_numbers {
		uint16_t	entries,
				open,
				two_gig,
				five_gig;
/* Maximum number of 'top' statistics entries. */
#define MAX_CH_STATS		3
		size_t		ch_stats;
	}		  num;
	pthread_mutex_t   mutex;
};

extern void init_scan_list(struct scan_result *sr);
extern void free_scan_list(struct scan_entry *head);
extern void *do_scan(void *sr_ptr);

/*
 * utils.c
 */
extern char *ether_addr(const struct ether_addr *ea);
extern char *ether_lookup(const struct ether_addr *ea);
extern char *mac_addr(const struct sockaddr *sa);
extern char *format_bssid(const struct sockaddr *ap);
extern uint8_t bit_count(uint32_t mask);
extern uint8_t prefix_len(const struct in_addr *netmask);
extern const char *pretty_time(const unsigned sec);
extern const char *pretty_time_ms(const unsigned msec);
extern int u8_to_dbm(const int power);
extern uint8_t dbm_to_u8(const int dbm);
extern double dbm2mw(const double in);
extern char *dbm2units(const double in);
extern double mw2dbm(const double in);

extern const char *dfs_domain_name(enum nl80211_dfs_regions region);
extern int ieee80211_frequency_to_channel(int freq);
extern const char *channel_width_name(enum nl80211_chan_width width);
extern const char *channel_type_name(enum nl80211_channel_type channel_type);
extern const char *iftype_name(enum nl80211_iftype iftype);

/*
 *	WEXT helper routines
 */
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

	if (freq < 1e3)		/* Convention: freq is channel number if < 1e3 */
		return freq;

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
