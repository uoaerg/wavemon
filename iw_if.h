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
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ether.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/if.h>

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
 * struct interface_info - record wireless interface information
 *
 * @ifname:	interface name (e.g. 'wlp3s0')
 * @mac_addr:	Mac address of @ifname
 * @phy_id:	PHY index
 * @ifindex:	general network interface index
 * @wdev:	wireless-device index
 */
struct interface_info {
	char			*ifname;
	struct ether_addr	mac_addr;
	uint32_t		phy_id,
				ifindex,
				wdev;

	struct interface_info	*next;
};
extern int iw_nl80211_get_interface_list(struct interface_info **head);
extern int iw_nl80211_get_interface_data(struct interface_info **data);
extern size_t count_interface_list(struct interface_info *head);
extern void free_interface_list(struct interface_info *head);


/**
 * struct addr_info  -  interface address information
 * @addr:	   IP address in CIDR format
 * @count:	   How many addresses of this type are present
 * @preferred_lft: Preferred lifetime of @addr
 * @valid_lft:     Valid lifetime of @addr
 */
struct addr_info {
	char		addr[64];
	uint8_t		count;
	uint32_t	preferred_lft,
			valid_lft;
};

/**
 * struct if_info  -  wireless interface network information
 * @ifindex:	Interface index
 * @ifname:	Interface name
 * @hwaddr:	MAC address
 * @v4,v6:	IPv4/6 address
 * @flags:	Interface flags
 * @type:	Interface type (relevant for master interface)
 * @mtu:	Interface MTU
 * @carrier:    Carrier mode of the interface
 * @mode:       Link mode of the interface
 * @qdisc:	Queuing discipline
 * @numtxq:	Number of TX queues
 * @txqlen:	TX queue length
 * @master:	Information about master interface (if present)
 */
struct if_info {
	int			ifindex;
	char			ifname[64];
	struct ether_addr	hwaddr;
	struct addr_info	v4,
				v6;
	char			type[16];
	uint16_t		flags;
	uint16_t		mtu;
	char			carrier[16],
				mode[16],
				qdisc[16];
	uint16_t		numtxq,
				txqlen;

	struct if_info		*master;
};

extern bool if_is_up(const char *ifname);
extern int  if_set_up(const char *ifname);
extern void if_set_down_on_exit(void);
extern void if_getinf(const char *ifname, struct if_info *info);

/** Interface bonding. */
extern const char *get_bonding_mode(const char *bonding_iface);
extern bool is_primary_slave(const char *bonding_iface, const char *slave);

/*
 *	 Structs to communicate WiFi statistics
 */
struct iw_levelstat {
	float	signal;		/* Signal level in dBm. */
	bool	valid;		/* Whether a valid @signal was registered. */
};

/*
 * 	Periodic sampling of wireless statistics
 */
extern void sampling_init(bool do_not_swap_pointers);
extern void sampling_stop(void);

/*
 * rfkill.c
 */
typedef enum {
	RFKILL_STATE_SOFT_BLOCKED = 0, // Transmitter is turned off by software.
	RFKILL_STATE_UNBLOCKED    = 1, // Transmitter is (potentially) active.
	RFKILL_STATE_HARD_BLOCKED = 2, // Transmitter is blocked by hardware (e.g. switch).
	RFKILL_STATE_FULL_BLOCKED = 3, // Transmitter is both soft and hard blocked.
	RFKILL_STATE_UNDEFINED    = 4, // Unable to determine rfkill state.
} rfkill_state_t;

extern rfkill_state_t get_rfkill_state(const uint32_t wdev_index);
extern bool is_rfkill_blocked_state(const rfkill_state_t state);
extern const char *rfkill_state_name(const rfkill_state_t state);
extern bool default_interface_is_rfkill_blocked(void);

/*
 * utils.c
 */
extern ssize_t read_file(const char *path, char *buf, size_t buflen);
extern int read_number_file(const char *path, uint32_t *num);

extern char *ether_addr(const struct ether_addr *ea);
extern char *ether_lookup(const struct ether_addr *ea);
extern char *mac_addr(const struct sockaddr *sa);
extern uint8_t bit_count(uint32_t mask);
extern uint8_t prefix_len(const struct sockaddr *netmask);
extern const char *lft2str(const uint32_t lifetime);
extern const char *pretty_time(const unsigned sec);
extern const char *pretty_time_ms(const unsigned msec);
extern double dbm2mw(const double in);
extern char *dbm2units(const double in);

extern const char *dfs_domain_name(enum nl80211_dfs_regions region);
extern int ieee80211_frequency_to_channel(int freq);
extern const char *channel_width_name(enum nl80211_chan_width width);
extern const char *channel_type_name(enum nl80211_channel_type channel_type);
extern const char *iftype_name(enum nl80211_iftype iftype);
