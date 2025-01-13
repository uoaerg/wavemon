/*
 * Definitions and functions for nl80211 based routines.
 */
#include <stdbool.h>
#include <netinet/ether.h>

#include <netlink/netlink.h>
#include <netlink/cli/addr.h>
#include <netlink/cli/link.h>

/*
 * Use local copy of nl80211.h rather than the one shipped with the distro in
 * /usr/include/linux. There are different versions, local one may be out of date.
 */
#include "nl80211.h"

#define BIT(x) (1ULL<<(x))		/* from iw:iw.h */

/* Set to 1 to enable callback debugging */
#define IW_NL_CB_DEBUG 0


/**
 * struct msg_attribute - attributes to nla_put into the message
 * @type:	type of the attribute
 * @len:	attribute length
 * @data:	pointer to data area of length @len
 */
struct msg_attribute {
	int		type;
	size_t		len;
	const void	*data;
};

/**
 * struct cmd - represent a single nl80211 command
 * @cmd:	  nl80211 command to send via GeNetlink
 * @sk:		  netlink socket to be used for this command
 * @flags:	  flags to set in the GeNetlink message
 * @hdr_flags:	  flags to set in the header of the message
 * @handler:	  netlink callback handler
 * @handler_arg:  argument for @handler
 * @msg_args:	  additional attributes to pass into message
 * @msg_args_len: number of elements in @msg_args
 */
struct cmd {
	enum nl80211_commands	cmd;
	struct nl_sock		*sk;

	int			flags,
				hdr_flags;

	int (*handler)(struct nl_msg *msg, void *arg);
	void			*handler_arg;

	struct msg_attribute	*msg_args;
	size_t			msg_args_len;
};
extern int handle_cmd(struct cmd *cmd);
extern int handle_interface_cmd(struct cmd *cmd);


/**
 * iw_nl80211_phy - PHY information
 * @retry_short:	short retry limit
 * @retry_long:		long retry limit
 * @bands:		number of bands
 * @rts_threshold:	RTS/CTS handshake frame length threshold (0..65536)
 * @frag_threshold:	fragmentation threshold (max frame length, 256..8000)
 */
struct iw_nl80211_phy {
	uint8_t		retry_short,
			retry_long,
			bands;

	uint32_t	rts_threshold,
			frag_threshold;
};

/**
 * iw_nl80211_ifstat - interface statistics
 * @phy_id:	PHY index
 * @ifindex:	ifindex of receiving interface
 * @wdev:	wireless device index
 * @iftype:	interface mode (access point ...)
 *
 * @freq:	frequency in MHz
 * @chan_width:	channel width
 * @chan_type:	channel type
 * @freq_ctr1:	center frequency #1
 * @freq_ctr2:	center frequency #2
 * @power_save:	whether power-saving mode is enabled
 * @tx_power:   TX power in dBm
 * @phy:	PHY information
 */
struct iw_nl80211_ifstat {
	uint32_t	phy_id,
			ifindex,
			wdev,
			iftype;

	char		ssid[64];

	uint32_t	freq,
			chan_width,
			chan_type,
			freq_ctr1,
			freq_ctr2;

	double		tx_power;
	bool		power_save;

	struct iw_nl80211_phy phy;
};
extern void iw_nl80211_getifstat(struct iw_nl80211_ifstat *is);
extern void iw_nl80211_get_phy(struct iw_nl80211_ifstat *ifs);
extern void iw_nl80211_get_power_save(struct iw_nl80211_ifstat *ifs);

/**
 * struct iw_nl80211_survey_data - channel survey data
 * @freq:  channel frequency (only filled in if it is in use)
 * @noise: channel noise in dBm (0 means invalid data)
 *
 * @active:   amount of time that the radio was on
 * @busy:     amount of the time the primary channel was sensed busy
 * @ext_busy: amount of time the extension channel was sensed busy
 * @rx:       amount of time the radio spent receiving data
 * @tx:       amount of time the radio spent transmitting data
 * @scan:     time the radio spent for scan
 */
struct iw_nl80211_survey {
	uint32_t	freq;
	int8_t		noise;

	struct time_data_in_milliseconds {
		uint64_t	active,
				busy,
				ext_busy,
				rx,
				tx,
				scan;
	} time;
};
extern void iw_nl80211_get_survey(struct iw_nl80211_survey *sd);

/* struct iw_nl80211_linkstat - aggregate link statistics
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
 * @expected_thru:    expected throughput in kB/s
 * @beacon_int:	      beacon interval in Time Units of 1024usec
 * @dtim_period:      DTIM period for beaconing
 * @beacon_avg_sig:   average beacon signal (in dBm)
 * @beacons:          number of beacons received
 * @beacon_loss:      count of times beacon loss was detected
 * @signal:           signal strength in dBm (0 if not present)
 * @signal_avg:       average signal strength in dBm
 * @bss_signal:       signal strength of BSS probe in dBm (or 0)
 * @bss_signal_qual:  unitless signal strength of BSS probe, 0..100
 * @tx_bitrate:       string describing current TX bitrate
 * @rx_bitrate:       string describing current RX bitrate
 * @cts_protection:   whether CTS protection is set
 * @long_preamble:    whether using long or short preamble
 * @short_slot_time:  whether short slots are enabled
 * @wme:              Wireless Multimedia Extensions / Wi-Fi Multimedia
 * @mfp:              Management Frame Protection
 * @tdls:             Tunneled Direct Link Setup
 * @survey:           channel survey data (where present)
 */
struct iw_nl80211_linkstat {
	uint32_t	  	status;
	struct ether_addr	bssid;
	/*
	 * Station details (not always filled in):
	 */
	uint32_t		inactive_time,
				connected_time;
	uint64_t		rx_bytes;
	uint32_t		rx_packets;
	uint64_t		rx_drop_misc;

	uint16_t		beacon_int;
	uint8_t			dtim_period,
				beacon_avg_sig;
	uint64_t		beacons;
	uint32_t		beacon_loss;

	uint64_t		tx_bytes;
	uint32_t		tx_packets,
				tx_retries,
				tx_failed;

	uint32_t		expected_thru;
	int8_t			signal,
				signal_avg;

	int8_t			bss_signal;
	uint8_t			bss_signal_qual;

	char			tx_bitrate[100],
				rx_bitrate[100];

	bool			cts_protection:1,
				long_preamble:1,
				short_slot_time:1,
				wme:1,
				mfp:1,
				tdls:1;
	/*
	 * Channel survey data (requires suitable card, e.g. ath9k).
	 */
	struct iw_nl80211_survey	survey;
};
extern void iw_nl80211_get_linkstat(struct iw_nl80211_linkstat *ls);
extern void iw_cache_update(struct iw_nl80211_linkstat *ls);

/* Indicate whether @ls contains usable channel survey data */
static inline bool iw_nl80211_have_survey_data(struct iw_nl80211_linkstat *ls)
{
	return ls->survey.freq != 0 && ls->survey.noise != 0;
}

/**
 * struct iw_nl80211_reg - regulatory domain information
 * @region:  regulatory DFS region (%nl80211_dfs_regions or -1)
 * @country: two-character country code
 */
struct iw_nl80211_reg {
	int	region;
	char	country[3];
};
extern void iw_nl80211_getreg(struct iw_nl80211_reg *ir);
extern void print_ssid_escaped(char *buf, const size_t buflen,
			       const uint8_t *data, const size_t datalen);

/*
 * Multicast event handling (taken from iw:event.c and iw:scan.c)
 */
/**
 * struct wait_event - wait for arrival of a specified message
 * @cmds:   array of GeNetlink commands (>0) to match
 * @n_cmds: length of @cmds
 * @cmd:    matched element of @cmds (if message arrived), else 0
 */
struct wait_event {
	const uint32_t	*cmds;
	uint8_t		n_cmds;
	uint32_t	cmd;
};
extern struct nl_sock *alloc_nl_mcast_sk(const char *grp);

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
static inline int error_handler(struct sockaddr_nl __attribute__((unused))*nla,
				struct nlmsgerr *err, void *arg)
{
	int *ret = arg;
	*ret = err->error;
	return NL_STOP;
}

static inline int finish_handler(struct nl_msg __attribute__((unused))*msg, void *arg)
{
	int *ret = arg;
	*ret = 0;
	return NL_SKIP;
}

static inline int ack_handler(struct nl_msg __attribute__((unused))*msg, void *arg)
{
	int *ret = arg;
	*ret = 0;
	return NL_STOP;
}

static inline int no_seq_check(struct nl_msg __attribute__((unused))*msg,
			       void __attribute__((unused))*arg)
{
	return NL_OK;
}
