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

/**
 * struct cmd - stolen and modified from iw:iw.h
 * @cmd:	 nl80211 command to send via GeNetlink
 * @sk:		 netlink socket to be used for this command
 * @flags:	 flags to set in the GeNetlink message
 * @handler:	 netlink callback handler
 * @handler_arg: argument for @handler
 */
struct cmd {
	enum nl80211_commands	cmd;
	struct nl_sock		*sk;
	int			flags;
	int (*handler)(struct nl_msg *msg, void *arg);
	void 			*handler_arg;
};
extern void iw_nl80211_fini(void);

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

/* struct iw_nl80211_stat - nl80211 statistics
 * @nl80211_id:	GeNetlink identifier for nl80211
 * @nl_sock:	netlink socket for @nl80211_id
 * @ifindex:	interface index for conf_ifname()
 */
struct iw_nl80211_stat {
	/*
	 * Station Statistics
	 */
	uint32_t	inactive_time,	/* in ms */
			rx_bytes,
			rx_packets,
			tx_bytes,
			tx_packets,
			tx_retries,
			tx_failed;
	uint64_t	tx_offset;

	char		tx_bitrate[100],
			rx_bitrate[100];

	uint32_t	expected_thru;	/* expected throughput in kbps */

	bool		authorized:1,
			authenticated:1,
			long_preamble:1,
			wme:1,		/* Wireless Multimedia Extensions / Wi-Fi Multimedia */
			mfp:1, 		/* Management Frame Protection */
			tdls:1;		/* Tunneled Direct Link Setup */

	struct ether_addr mac_addr;
};
extern void iw_nl80211_getstat(struct iw_nl80211_stat *is);

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
