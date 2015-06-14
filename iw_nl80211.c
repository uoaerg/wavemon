/*
 * PROTOTYPE: add nl80211 calls to iw_if. Mostly copied/stolen from iw
 */
#include "wavemon.h"
#include <net/if.h>
#include <errno.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>

#include "iw_nl80211.h"

/* stolen/modified from iw:iw.c */
int handle_cmd(struct cmd *cmd)
{
	struct nl_cb *cb;
	struct nl_msg *msg;
	static int nl80211_id = -1;
	int ret;
	uint32_t ifindex;

	/*
	 * Initialization of static components:
	 * - per-cmd socket
	 * - global nl80211 ID
	 * - per-cmd interface index (in case conf_ifname() changes)
	 */
	if (!cmd->sk) {
		cmd->sk = nl_socket_alloc();
		if (!cmd->sk)
			err(1, "Failed to allocate netlink socket");

		/* NB: not setting sk buffer size, using default 32Kb */
		if (genl_connect(cmd->sk))
			err(1, "failed to connect to GeNetlink");
	}

	if (nl80211_id < 0) {
		nl80211_id = genl_ctrl_resolve(cmd->sk, "nl80211");
		if (nl80211_id < 0)
			err(1, "nl80211 not found");
	}

	ifindex = if_nametoindex(conf_ifname());
	if (ifindex == 0 && errno)
		err(1, "failed to look up interface %s", conf_ifname());

	/*
	 * Message Preparation
	 */
	msg = nlmsg_alloc();
	if (!msg)
		err(2, "failed to allocate netlink message");

	cb = nl_cb_alloc(0 ? NL_CB_DEBUG : NL_CB_DEFAULT);
	if (!cb)
		err(2, "failed to allocate netlink callback");

	genlmsg_put(msg, 0, 0, nl80211_id, 0, cmd->flags, cmd->cmd, 0);

	/* netdev identifier: interface index */
	if (nla_put(msg, NL80211_ATTR_IFINDEX, sizeof(ifindex), &ifindex) < 0)
		err(2, "failed to add ifindex attribute to netlink message");

	// wdev identifier: wdev index
	// NLA_PUT_U64(msg, NL80211_ATTR_WDEV, devidx);

	/* Set callback for this message */
	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, cmd->handler, cmd->handler_arg);

	ret = nl_send_auto_complete(cmd->sk, msg);
	if (ret < 0)
		err(2, "failed to send station-dump message");

	/*-------------------------------------------------------------------------
	 * Receive loop
	 *-------------------------------------------------------------------------*/
	nl_cb_err(cb, NL_CB_CUSTOM, error_handler, &ret);
	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &ret);
	nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &ret);

	while (ret > 0)
		nl_recvmsgs(cmd->sk, cb);

	nl_cb_put(cb);
	nlmsg_free(msg);

	return 0;
}
/*
 * STATION COMMANDS
 */
// stolen from iw:station.c
void parse_bitrate(struct nlattr *bitrate_attr, char *buf, int buflen)
{
	int rate = 0;
	char *pos = buf;
	struct nlattr *rinfo[NL80211_RATE_INFO_MAX + 1];
	static struct nla_policy rate_policy[NL80211_RATE_INFO_MAX + 1] = {
		[NL80211_RATE_INFO_BITRATE] = { .type = NLA_U16 },
		[NL80211_RATE_INFO_BITRATE32] = { .type = NLA_U32 },
		[NL80211_RATE_INFO_MCS] = { .type = NLA_U8 },
		[NL80211_RATE_INFO_40_MHZ_WIDTH] = { .type = NLA_FLAG },
		[NL80211_RATE_INFO_SHORT_GI] = { .type = NLA_FLAG },
	};

	if (nla_parse_nested(rinfo, NL80211_RATE_INFO_MAX,
			     bitrate_attr, rate_policy)) {
		snprintf(buf, buflen, "failed to parse nested rate attributes!");
		return;
	}

	if (rinfo[NL80211_RATE_INFO_BITRATE32])
		rate = nla_get_u32(rinfo[NL80211_RATE_INFO_BITRATE32]);
	else if (rinfo[NL80211_RATE_INFO_BITRATE])
		rate = nla_get_u16(rinfo[NL80211_RATE_INFO_BITRATE]);
	if (rate > 0)
		pos += snprintf(pos, buflen - (pos - buf),
				"%d.%d MBit/s", rate / 10, rate % 10);

	if (rinfo[NL80211_RATE_INFO_MCS])
		pos += snprintf(pos, buflen - (pos - buf),
				" MCS %d", nla_get_u8(rinfo[NL80211_RATE_INFO_MCS]));
	if (rinfo[NL80211_RATE_INFO_VHT_MCS])
		pos += snprintf(pos, buflen - (pos - buf),
				" VHT-MCS %d", nla_get_u8(rinfo[NL80211_RATE_INFO_VHT_MCS]));
	if (rinfo[NL80211_RATE_INFO_40_MHZ_WIDTH])
		pos += snprintf(pos, buflen - (pos - buf), " 40MHz");
	if (rinfo[NL80211_RATE_INFO_80_MHZ_WIDTH])
		pos += snprintf(pos, buflen - (pos - buf), " 80MHz");
	if (rinfo[NL80211_RATE_INFO_80P80_MHZ_WIDTH])
		pos += snprintf(pos, buflen - (pos - buf), " 80P80MHz");
	if (rinfo[NL80211_RATE_INFO_160_MHZ_WIDTH])
		pos += snprintf(pos, buflen - (pos - buf), " 160MHz");
	if (rinfo[NL80211_RATE_INFO_SHORT_GI])
		pos += snprintf(pos, buflen - (pos - buf), " short GI");
	if (rinfo[NL80211_RATE_INFO_VHT_NSS])
		pos += snprintf(pos, buflen - (pos - buf),
				" VHT-NSS %d", nla_get_u8(rinfo[NL80211_RATE_INFO_VHT_NSS]));
}

// stolen and modified from iw:station.c
static int station_handler(struct nl_msg *msg, void *arg)
{
	struct iw_nl80211_stat *is = (struct iw_nl80211_stat *)arg;

	struct nlattr *tb[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *sinfo[NL80211_STA_INFO_MAX + 1];
	char state_name[10];
	struct nl80211_sta_flag_update *sta_flags;
	static struct nla_policy stats_policy[NL80211_STA_INFO_MAX + 1] = {
		[NL80211_STA_INFO_INACTIVE_TIME] = { .type = NLA_U32 },
		[NL80211_STA_INFO_RX_BYTES] = { .type = NLA_U32 },
		[NL80211_STA_INFO_TX_BYTES] = { .type = NLA_U32 },
		[NL80211_STA_INFO_RX_PACKETS] = { .type = NLA_U32 },
		[NL80211_STA_INFO_TX_PACKETS] = { .type = NLA_U32 },
		[NL80211_STA_INFO_SIGNAL] = { .type = NLA_U8 },
		[NL80211_STA_INFO_T_OFFSET] = { .type = NLA_U64 },
		[NL80211_STA_INFO_TX_BITRATE] = { .type = NLA_NESTED },
		[NL80211_STA_INFO_RX_BITRATE] = { .type = NLA_NESTED },
		[NL80211_STA_INFO_LLID] = { .type = NLA_U16 },
		[NL80211_STA_INFO_PLID] = { .type = NLA_U16 },
		[NL80211_STA_INFO_PLINK_STATE] = { .type = NLA_U8 },
		[NL80211_STA_INFO_TX_RETRIES] = { .type = NLA_U32 },
		[NL80211_STA_INFO_TX_FAILED] = { .type = NLA_U32 },
		[NL80211_STA_INFO_STA_FLAGS] =
			{ .minlen = sizeof(struct nl80211_sta_flag_update) },
		[NL80211_STA_INFO_LOCAL_PM] = { .type = NLA_U32},
		[NL80211_STA_INFO_PEER_PM] = { .type = NLA_U32},
		[NL80211_STA_INFO_NONPEER_PM] = { .type = NLA_U32},
		[NL80211_STA_INFO_CHAIN_SIGNAL] = { .type = NLA_NESTED },
		[NL80211_STA_INFO_CHAIN_SIGNAL_AVG] = { .type = NLA_NESTED },
	};

	assert(is != NULL);

	nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	/*
	 * TODO: validate the interface and mac address!
	 * Otherwise, there's a race condition as soon as
	 * the kernel starts sending station notifications.
	 */

	if (!tb[NL80211_ATTR_STA_INFO]) {
		fprintf(stderr, "sta stats missing!\n");
		return NL_SKIP;
	}
	if (nla_parse_nested(sinfo, NL80211_STA_INFO_MAX,
			     tb[NL80211_ATTR_STA_INFO],
			     stats_policy)) {
		fprintf(stderr, "failed to parse nested attributes!\n");
		return NL_SKIP;
	}

	memcpy(&is->mac_addr, nla_data(tb[NL80211_ATTR_MAC]), ETH_ALEN);

	if (sinfo[NL80211_STA_INFO_INACTIVE_TIME])
		is->inactive_time = nla_get_u32(sinfo[NL80211_STA_INFO_INACTIVE_TIME]);
	if (sinfo[NL80211_STA_INFO_RX_BYTES])
		is->rx_bytes = nla_get_u32(sinfo[NL80211_STA_INFO_RX_BYTES]);
	if (sinfo[NL80211_STA_INFO_RX_PACKETS])
		is->rx_packets = nla_get_u32(sinfo[NL80211_STA_INFO_RX_PACKETS]);
	if (sinfo[NL80211_STA_INFO_TX_BYTES])
		is->tx_packets = nla_get_u32(sinfo[NL80211_STA_INFO_TX_BYTES]);
	if (sinfo[NL80211_STA_INFO_TX_PACKETS])
		is->tx_bytes = nla_get_u32(sinfo[NL80211_STA_INFO_TX_PACKETS]);
	if (sinfo[NL80211_STA_INFO_TX_RETRIES])
		is->tx_retries = nla_get_u32(sinfo[NL80211_STA_INFO_TX_RETRIES]);
	if (sinfo[NL80211_STA_INFO_TX_FAILED])
		is->tx_failed = nla_get_u32(sinfo[NL80211_STA_INFO_TX_FAILED]);

	/* XXX
	char *chain;
	chain = get_chain_signal(sinfo[NL80211_STA_INFO_CHAIN_SIGNAL]);
	if (sinfo[NL80211_STA_INFO_SIGNAL])
		printf("\n\tsignal:  \t%d %sdBm",
			(int8_t)nla_get_u8(sinfo[NL80211_STA_INFO_SIGNAL]),
			chain);

	chain = get_chain_signal(sinfo[NL80211_STA_INFO_CHAIN_SIGNAL_AVG]);
	if (sinfo[NL80211_STA_INFO_SIGNAL_AVG])
		printf("\n\tsignal avg:\t%d %sdBm",
			(int8_t)nla_get_u8(sinfo[NL80211_STA_INFO_SIGNAL_AVG]),
			chain);
			*/

	if (sinfo[NL80211_STA_INFO_T_OFFSET])
		is->tx_offset = nla_get_u64(sinfo[NL80211_STA_INFO_T_OFFSET]);

	if (sinfo[NL80211_STA_INFO_TX_BITRATE])
		parse_bitrate(sinfo[NL80211_STA_INFO_TX_BITRATE], is->tx_bitrate, sizeof(is->tx_bitrate));

	if (sinfo[NL80211_STA_INFO_RX_BITRATE])
		parse_bitrate(sinfo[NL80211_STA_INFO_RX_BITRATE], is->rx_bitrate, sizeof(is->rx_bitrate));

	if (sinfo[NL80211_STA_INFO_EXPECTED_THROUGHPUT]) {
		is->expected_thru = nla_get_u32(sinfo[NL80211_STA_INFO_EXPECTED_THROUGHPUT]);
		/* convert in Mbps but scale by 1000 to save kbps units */
		is->expected_thru = is->expected_thru * 1000 / 1024;
	}

	if (sinfo[NL80211_STA_INFO_LLID])
		printf("\n\tmesh llid:\t%d",
			nla_get_u16(sinfo[NL80211_STA_INFO_LLID]));
	if (sinfo[NL80211_STA_INFO_PLID])
		printf("\n\tmesh plid:\t%d",
			nla_get_u16(sinfo[NL80211_STA_INFO_PLID]));
	if (sinfo[NL80211_STA_INFO_PLINK_STATE]) {
		switch (nla_get_u8(sinfo[NL80211_STA_INFO_PLINK_STATE])) {
		case LISTEN:
			strcpy(state_name, "LISTEN");
			break;
		case OPN_SNT:
			strcpy(state_name, "OPN_SNT");
			break;
		case OPN_RCVD:
			strcpy(state_name, "OPN_RCVD");
			break;
		case CNF_RCVD:
			strcpy(state_name, "CNF_RCVD");
			break;
		case ESTAB:
			strcpy(state_name, "ESTAB");
			break;
		case HOLDING:
			strcpy(state_name, "HOLDING");
			break;
		case BLOCKED:
			strcpy(state_name, "BLOCKED");
			break;
		default:
			strcpy(state_name, "UNKNOWN");
			break;
		}
		printf("\n\tmesh plink:\t%s", state_name);
	}

	/* XXX
	if (sinfo[NL80211_STA_INFO_LOCAL_PM]) {
		printf("\n\tmesh local PS mode:\t");
		print_power_mode(sinfo[NL80211_STA_INFO_LOCAL_PM]);
	}
	if (sinfo[NL80211_STA_INFO_PEER_PM]) {
		printf("\n\tmesh peer PS mode:\t");
		print_power_mode(sinfo[NL80211_STA_INFO_PEER_PM]);
	}
	if (sinfo[NL80211_STA_INFO_NONPEER_PM]) {
		printf("\n\tmesh non-peer PS mode:\t");
		print_power_mode(sinfo[NL80211_STA_INFO_NONPEER_PM]);
	}
	*/

	if (sinfo[NL80211_STA_INFO_STA_FLAGS]) {
		sta_flags = (struct nl80211_sta_flag_update *)
			    nla_data(sinfo[NL80211_STA_INFO_STA_FLAGS]);

		if (sta_flags->mask & BIT(NL80211_STA_FLAG_AUTHORIZED) &&
		    sta_flags->set & BIT(NL80211_STA_FLAG_AUTHORIZED))
			is->authenticated = true;

		if (sta_flags->mask & BIT(NL80211_STA_FLAG_AUTHENTICATED) &&
		    sta_flags->set & BIT(NL80211_STA_FLAG_AUTHENTICATED))
			is->authenticated = true;

		if (sta_flags->mask & BIT(NL80211_STA_FLAG_SHORT_PREAMBLE) &&
		    sta_flags->set & BIT(NL80211_STA_FLAG_SHORT_PREAMBLE))
			is->long_preamble = true;

		if (sta_flags->mask & BIT(NL80211_STA_FLAG_WME) &&
		    sta_flags->set & BIT(NL80211_STA_FLAG_WME))
			is->wme = true;

		if (sta_flags->mask & BIT(NL80211_STA_FLAG_MFP) &&
		    sta_flags->set & BIT(NL80211_STA_FLAG_MFP))
			is->mfp = true;

		if (sta_flags->mask & BIT(NL80211_STA_FLAG_TDLS_PEER) &&
		    sta_flags->set & BIT(NL80211_STA_FLAG_TDLS_PEER))
			is->tdls = true;
	}

	return NL_SKIP;
}


/*
 * INTERFACE COMMANDS
 */
void print_ssid_escaped(char *buf, const size_t buflen,
			const uint8_t *data, const size_t datalen)
{
	int i, l;

	for (i = l= 0; i < datalen; i++) {
		if (l + 4 >= buflen) {
			buf[buflen-1] = '\0';
			return;
		}
		if (isprint(data[i]) && data[i] != ' ' && data[i] != '\\')
			l += sprintf(buf + l, "%c", data[i]);
		else if (data[i] == ' ' && i != 0 && i != datalen -1)
			l += sprintf(buf + l, " ");
		else
			l += sprintf(buf + l, "\\x%.2x", data[i]);
	}
}

// stolen from iw:interface.c
static int iface_handler(struct nl_msg *msg, void *arg)
{
	struct iw_nl80211_ifstat *ifs = (struct iw_nl80211_ifstat *)arg;
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];

	assert(ifs != NULL);

	nla_parse(tb_msg, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	if (tb_msg[NL80211_ATTR_WIPHY])
		ifs->phy = nla_get_u32(tb_msg[NL80211_ATTR_WIPHY]);

	if (tb_msg[NL80211_ATTR_IFINDEX])
		ifs->ifindex = nla_get_u32(tb_msg[NL80211_ATTR_IFINDEX]);

	if (tb_msg[NL80211_ATTR_WDEV])
		ifs->wdev = nla_get_u64(tb_msg[NL80211_ATTR_WDEV]);
	if (tb_msg[NL80211_ATTR_SSID])
		print_ssid_escaped(ifs->ssid, sizeof(ifs->ssid),
				   nla_data(tb_msg[NL80211_ATTR_SSID]),
				   nla_len(tb_msg[NL80211_ATTR_SSID]));

	if (tb_msg[NL80211_ATTR_IFTYPE])
		ifs->iftype = nla_get_u32(tb_msg[NL80211_ATTR_IFTYPE]);

	ifs->chan_width = -1;
	ifs->chan_type  = -1;
	if (tb_msg[NL80211_ATTR_WIPHY_FREQ]) {
		ifs->freq = nla_get_u32(tb_msg[NL80211_ATTR_WIPHY_FREQ]);

		if (tb_msg[NL80211_ATTR_CHANNEL_WIDTH]) {
			ifs->chan_width = nla_get_u32(tb_msg[NL80211_ATTR_CHANNEL_WIDTH]);

			if (tb_msg[NL80211_ATTR_CENTER_FREQ1])
				ifs->freq_ctr1 = nla_get_u32(tb_msg[NL80211_ATTR_CENTER_FREQ1]);
			if (tb_msg[NL80211_ATTR_CENTER_FREQ2])
				ifs->freq_ctr2 = nla_get_u32(tb_msg[NL80211_ATTR_CENTER_FREQ2]);

		}
		if (tb_msg[NL80211_ATTR_WIPHY_CHANNEL_TYPE])
			ifs->chan_type = nla_get_u32(tb_msg[NL80211_ATTR_WIPHY_CHANNEL_TYPE]);
	}

	return NL_SKIP;
}

/* Regulatory domain, stolen from iw:reg.c */
static int reg_handler(struct nl_msg *msg, void *arg)
{
	struct iw_nl80211_reg *ir = (struct iw_nl80211_reg *)arg;
	struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	char *alpha2;

	ir->region = -1;

	nla_parse(tb_msg, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		genlmsg_attrlen(gnlh, 0), NULL);

	if (!tb_msg[NL80211_ATTR_REG_ALPHA2])
		return NL_SKIP;

	if (!tb_msg[NL80211_ATTR_REG_RULES])
		return NL_SKIP;

	if (tb_msg[NL80211_ATTR_WIPHY])
		printf("phy#%d%s\n", nla_get_u32(tb_msg[NL80211_ATTR_WIPHY]),
		       tb_msg[NL80211_ATTR_WIPHY_SELF_MANAGED_REG] ?
		       " (self-managed)" : "");
	else
		printf("global\n");

	if (tb_msg[NL80211_ATTR_DFS_REGION])
		ir->region = nla_get_u8(tb_msg[NL80211_ATTR_DFS_REGION]);
	else
		ir->region = NL80211_DFS_UNSET;

	alpha2 = nla_data(tb_msg[NL80211_ATTR_REG_ALPHA2]);
	ir->country[0] = alpha2[0];
	ir->country[1] = alpha2[1];

	return NL_SKIP;
}

/*
 * COMMAND HANDLERS
 */
static struct cmd cmd_reg = {
	.cmd	 = NL80211_CMD_GET_REG,
	.flags	 = 0,
	.handler = reg_handler
};

static struct cmd cmd_ifstat = {
	.cmd	 = NL80211_CMD_GET_INTERFACE,
	.flags	 = 0,
	.handler = iface_handler
};

static struct cmd cmd_station = {
	.cmd	 = NL80211_CMD_GET_STATION,
	.flags	 = NLM_F_DUMP,
	.handler = station_handler,
};

void iw_nl80211_getreg(struct iw_nl80211_reg *ir)
{
	cmd_reg.handler_arg = ir;
	memset(ir, 0, sizeof(*ir));
	handle_cmd(&cmd_reg);
}

void iw_nl80211_getstat(struct iw_nl80211_stat *is)
{
	cmd_station.handler_arg = is;
	memset(is, 0, sizeof(*is));
	handle_cmd(&cmd_station);
}

void iw_nl80211_getifstat(struct iw_nl80211_ifstat *ifs)
{
	cmd_ifstat.handler_arg = ifs;
	memset(ifs, 0, sizeof(*ifs));
	handle_cmd(&cmd_ifstat);
}

/* Teardown: terminate all sockets */
void iw_nl80211_fini(void)
{
	if (cmd_station.sk)
		nl_socket_free(cmd_station.sk);
	if (cmd_ifstat.sk)
		nl_socket_free(cmd_ifstat.sk);
	cmd_station.sk = NULL;
	cmd_ifstat.sk  = NULL;
}

