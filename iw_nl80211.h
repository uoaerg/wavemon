/*
 * Definitions and functions for nl80211 based routines.
 */
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>

#include <netinet/ether.h>
#include <stdbool.h>

/*
 * Use local copy of nl80211.h rather than the one shipped with the distro in
 * /usr/include/linux. There are different versions, local one may be out of date.
 */
#include "nl80211.h"

#define BIT(x) (1ULL<<(x))		/* from iw:iw.h */

/** struct msg_attribute - attributes to nla_put into the message
 * @type:	type of the attribute
 * @len:	attribute length
 * @data:	pointer to data area of length @len
 */
struct msg_attribute {
	int		type,
			len;
	const void	*data;
};

/**
 * struct cmd - stolen and modified from iw:iw.h
 * @cmd:	  nl80211 command to send via GeNetlink
 * @sk:		  netlink socket to be used for this command
 * @flags:	  flags to set in the GeNetlink message
 * @handler:	  netlink callback handler
 * @handler_arg:  argument for @handler
 * @msg_args:	  additional attributes to pass into message
 * @msg_args_len: number of elements in @msg_args
 */
struct cmd {
	enum nl80211_commands	cmd;
	struct nl_sock		*sk;
	int			flags;
	int (*handler)(struct nl_msg *msg, void *arg);
	void 			*handler_arg;

	struct msg_attribute	*msg_args;
	uint8_t			msg_args_len;
};

/**
 * iw_nl80211_ifstat - interface statistics
 * @phy:	PHY index
 * @ifindex:	ifindex of receiving interface
 * @wdev:	wireless device index
 * @iftype:	interface mode (access point ...)
 *
 * @freq:	frequency in MHz
 * @chan_width:	channel width
 * @chan_type:	channel type
 * @freq_ctr1:	center frequency #1
 * @freq_ctr2:	center frequency #2
 */
struct iw_nl80211_ifstat {
	uint32_t	phy,
			ifindex,
			wdev,
			iftype;

	char		ssid[64];

	uint32_t	freq;
	int		chan_width,
			chan_type,
			freq_ctr1,
			freq_ctr2;
};
extern void iw_nl80211_getifstat(struct iw_nl80211_ifstat *is);

/* struct iw_nl80211_linkstat - link statistics
 * @status:           association status (%nl80211_bss_status)
 * @bssid:            station MAC address
 * @inactive_time:    inactivity in msec
 * @connected_time:   time since last connecting in sec
 * @beacon_loss:      count of time beacon loss was detected
 * @rx_bytes/packets: byte/packet counter for RX direction
 * @rx_drop_misc:     packets dropped for unspecified reasons
 * @tx_bytes/packets: byte/packet counter for TX direction
 * @tx_retries:       TX retry counter
 * @tx_failed:        TX failure counter
 * @expected_thru:    expected throughput in kpbs
 * @signal:           signal strength in dBm
 * @tx_bitrate:	      string describing current TX bitrate
 * @rx_bitrate:	      string describing current RX bitrate
 * @authorized:       FIXME
 * @authenticated:    FIXME
 * @long_preamble:    whether using long or short preamble
 * @wme:              Wireless Multimedia Extensions / Wi-Fi Multimedia
 * @mfp:              Management Frame Protection
 * @tdls:             Tunneled Direct Link Setup
 */
struct iw_nl80211_linkstat {
	uint32_t	  	status;
	struct ether_addr	bssid;
	/*
	 * Station details (not always filled in):
	 */
	uint32_t		inactive_time,
				connected_time,
				beacon_loss,
				rx_bytes,
				rx_packets;
	uint64_t		rx_drop_misc;

	uint32_t		tx_bytes,
				tx_packets,
				tx_retries,
				tx_failed;

	uint32_t		expected_thru;
	int8_t			signal;

	char			tx_bitrate[100],
				rx_bitrate[100];

	bool			authorized:1,
				authenticated:1,
				long_preamble:1,
				wme:1,
				mfp:1,
				tdls:1;
};
extern void iw_nl80211_get_linkstat(struct iw_nl80211_linkstat *ls);

/**
 * struct iw_nl80211_reg - regulatory domain information
 * @region: 	regulatory DFS region (%nl80211_dfs_regions or -1)
 * @country:	two-character country code
 */
struct iw_nl80211_reg {
	int	region;
	char	country[3];
};
extern void iw_nl80211_getreg(struct iw_nl80211_reg *ir);

/*
 * utils.c
 */
extern bool ether_addr_is_zero(const struct ether_addr *ea);

/*
 * (Ge)Netlink and nl80211 Internals
 */
// stolen from iw:station.c
enum plink_state {
	LISTEN,
	OPN_SNT,
	OPN_RCVD,
	CNF_RCVD,
	ESTAB,
	HOLDING,
	BLOCKED
};

/* Predefined handlers, stolen from iw:iw.c */
static inline int error_handler(struct sockaddr_nl *nla, struct nlmsgerr *err,
			 void *arg)
{
	int *ret = arg;
	*ret = err->error;
	return NL_STOP;
}

static inline int finish_handler(struct nl_msg *msg, void *arg)
{
	int *ret = arg;
	*ret = 0;
	return NL_SKIP;
}

static inline int ack_handler(struct nl_msg *msg, void *arg)
{
	int *ret = arg;
	*ret = 0;
	return NL_STOP;
}
