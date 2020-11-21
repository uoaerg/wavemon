/*
 * wavemon - a wireless network monitoring application
 *
 * Copyright (c) 2001-2002 Jan Morgenstern <jan@jm-music.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
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
extern void if_set_down_on_exit(void);
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

/*
 *	 Structs to communicate WiFi statistics
 */
struct iw_levelstat {
	float	signal;		/* signal level in dBm */
	float	noise;		/* noise  level in dBm */
	uint8_t	flags;		/* level validity      */
};
#define IW_LSTAT_INIT { 0, 0, IW_QUAL_LEVEL_INVALID | IW_QUAL_NOISE_INVALID }

/*
 * 	Periodic sampling of wireless statistics
 */
extern void sampling_init(bool do_not_swap_pointers);
extern void sampling_stop(void);

/*
 * utils.c
 */
extern char *ether_addr(const struct ether_addr *ea);
extern char *ether_lookup(const struct ether_addr *ea);
extern char *mac_addr(const struct sockaddr *sa);
extern uint8_t bit_count(uint32_t mask);
extern uint8_t prefix_len(const struct in_addr *netmask);
extern const char *pretty_time(const unsigned sec);
extern const char *pretty_time_ms(const unsigned msec);
extern double dbm2mw(const double in);
extern char *dbm2units(const double in);

extern const char *dfs_domain_name(enum nl80211_dfs_regions region);
extern int ieee80211_frequency_to_channel(int freq);
extern const char *channel_width_name(enum nl80211_chan_width width);
extern const char *channel_type_name(enum nl80211_channel_type channel_type);
extern const char *iftype_name(enum nl80211_iftype iftype);
