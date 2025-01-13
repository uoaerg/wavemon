/*
 * nl80211 calls and utilies. These were mostly taken from the iw code.
 */
#include "wavemon.h"
#include <net/if.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "iw_nl80211.h"

/** Append msg_attribute{type, len, data} to @cmd. */
static void add_msg_arg(struct cmd *cmd, int type, size_t len, const void * const data)
{
	cmd->msg_args = realloc(cmd->msg_args, sizeof(*cmd->msg_args) * (cmd->msg_args_len + 1));
	if (!cmd->msg_args)
		err_sys("failed to allocate space for message attributes");

	cmd->msg_args[cmd->msg_args_len].type = type;
	cmd->msg_args[cmd->msg_args_len].len  = len;
	cmd->msg_args[cmd->msg_args_len].data = data;
	cmd->msg_args_len += 1;
}

static void free_msg_args(struct cmd *cmd)
{
	free(cmd->msg_args);
	cmd->msg_args = NULL;
	cmd->msg_args_len = 0;
}

/**
 * handle_cmd: process @cmd (generic variant)
 * Returns 0 if ok, -errno < 0 on failure
 */
int handle_cmd(struct cmd *cmd)
{
	struct nl_cb *cb;
	struct nl_msg *msg;
	static int nl80211_id = -1;
	int ret;

	/*
	 * Initialization of static components:
	 * - per-cmd socket
	 * - global nl80211 ID
	 */
	if (!cmd->sk) {
		cmd->sk = nl_socket_alloc();
		if (!cmd->sk)
			err_sys("failed to allocate netlink socket");

		/* NB: not setting sk buffer size, using default 32Kb */
		if (genl_connect(cmd->sk))
			err_sys("failed to connect to GeNetlink");
	}

	if (nl80211_id < 0) {
		nl80211_id = genl_ctrl_resolve(cmd->sk, "nl80211");
		if (nl80211_id < 0)
			err_sys("nl80211 not found");
	}

	/*
	 * Message Preparation
	 */
	msg = nlmsg_alloc();
	if (!msg)
		err_sys("failed to allocate netlink message");

	cb = nl_cb_alloc(IW_NL_CB_DEBUG ? NL_CB_DEBUG : NL_CB_DEFAULT);
	if (!cb)
		err_sys("failed to allocate netlink callback");

	genlmsg_put(msg, 0, 0, nl80211_id, 0, cmd->flags, cmd->cmd, 0);

	/* Message attributes */
	if (cmd->msg_args) {
		for (size_t idx = 0; idx < cmd->msg_args_len; idx++)
			NLA_PUT(msg, cmd->msg_args[idx].type,
				     cmd->msg_args[idx].len,
				     cmd->msg_args[idx].data);
		free_msg_args(cmd);
	}

	ret = nl_send_auto_complete(cmd->sk, msg);
	if (ret < 0)
		err_sys("failed to send netlink message");

	/*-------------------------------------------------------------------------
	 * Receive loop
	 *-------------------------------------------------------------------------*/
	nl_cb_err(cb, NL_CB_CUSTOM, error_handler, &ret);
	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &ret);
	nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &ret);
	if (cmd->handler)
		nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, cmd->handler, cmd->handler_arg);

	/* Do not block, otherwise UI might get stalled waiting for updates */
	nl_socket_set_nonblocking(cmd->sk);
	while (ret > 0)
		if (nl_recvmsgs(cmd->sk, cb) == -NLE_AGAIN) {
			ret = -NLE_AGAIN;
			break;
		}

	nl_cb_put(cb);
	nlmsg_free(msg);
	goto out;

nla_put_failure:
	err_quit("failed to add attribute to netlink message");
out:
	return ret;
}

/**
 * handle_interface_cmd: handle @cmd for the configured default interface.
 */
int handle_interface_cmd(struct cmd *cmd)
{
	uint32_t ifindex = if_nametoindex(conf_ifname());

	if (ifindex == 0 && errno)
		err_sys("failed to look up interface index of '%s'", conf_ifname());

	/* netdev identifier: interface index */
	add_msg_arg(cmd, NL80211_ATTR_IFINDEX, sizeof(ifindex), &ifindex);

	return handle_cmd(cmd);
}

/*
 * STATION COMMANDS
 */
/* stolen from iw:station.c */
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
	else
		pos += snprintf(pos, buflen - (pos - buf), "(unknown)");

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
	if (rinfo[NL80211_RATE_INFO_HE_MCS])
		pos += snprintf(pos, buflen - (pos - buf),
				" HE-MCS %d", nla_get_u8(rinfo[NL80211_RATE_INFO_HE_MCS]));
	if (rinfo[NL80211_RATE_INFO_HE_NSS])
		pos += snprintf(pos, buflen - (pos - buf),
				" HE-NSS %d", nla_get_u8(rinfo[NL80211_RATE_INFO_HE_NSS]));
	if (rinfo[NL80211_RATE_INFO_HE_GI])
		pos += snprintf(pos, buflen - (pos - buf),
				" HE-GI %d", nla_get_u8(rinfo[NL80211_RATE_INFO_HE_GI]));
	if (rinfo[NL80211_RATE_INFO_HE_DCM])
		pos += snprintf(pos, buflen - (pos - buf),
				" HE-DCM %d", nla_get_u8(rinfo[NL80211_RATE_INFO_HE_DCM]));
	if (rinfo[NL80211_RATE_INFO_HE_RU_ALLOC])
		pos += snprintf(pos, buflen - (pos - buf),
				" HE-RU-ALLOC %d", nla_get_u8(rinfo[NL80211_RATE_INFO_HE_RU_ALLOC]));
}

/*
 * INTERFACE COMMANDS
 */
void print_ssid_escaped(char *buf, const size_t buflen,
			const uint8_t *data, const size_t datalen)
{
	size_t i, l;

	memset(buf, '\0', buflen);
	/* Treat zeroed-out SSIDs separately */
	for (i = 0; i < datalen && data[i] == '\0'; i++)
		;
	if (i == datalen)
		return;

	for (i = l= 0; i < datalen; i++) {
		if (l + 4 >= buflen)
			return;
		else if (isprint(data[i]) && data[i] != ' ' && data[i] != '\\')
			l += sprintf(buf + l, "%c", data[i]);
		else if (data[i] == ' ' && i != 0 && i != datalen -1)
			l += sprintf(buf + l, " ");
		else
			l += sprintf(buf + l, "\\x%.2x", data[i]);
	}
}

/* stolen from iw:interface.c */
static int iface_handler(struct nl_msg *msg, void *arg)
{
	struct iw_nl80211_ifstat *ifs = (struct iw_nl80211_ifstat *)arg;
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];

	assert(ifs != NULL);

	nla_parse(tb_msg, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	if (tb_msg[NL80211_ATTR_WDEV])
		ifs->wdev = nla_get_u64(tb_msg[NL80211_ATTR_WDEV]);

	if (tb_msg[NL80211_ATTR_WIPHY])
		ifs->phy_id = nla_get_u32(tb_msg[NL80211_ATTR_WIPHY]);

	if (tb_msg[NL80211_ATTR_IFINDEX])
		ifs->ifindex = nla_get_u32(tb_msg[NL80211_ATTR_IFINDEX]);

	if (tb_msg[NL80211_ATTR_IFTYPE])
		ifs->iftype = nla_get_u32(tb_msg[NL80211_ATTR_IFTYPE]);

	if (tb_msg[NL80211_ATTR_SSID])
		print_ssid_escaped(ifs->ssid, sizeof(ifs->ssid),
				   nla_data(tb_msg[NL80211_ATTR_SSID]),
				   nla_len(tb_msg[NL80211_ATTR_SSID]));

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

	if (tb_msg[NL80211_ATTR_WIPHY_TX_POWER_LEVEL])
		ifs->tx_power = nla_get_u32(tb_msg[NL80211_ATTR_WIPHY_TX_POWER_LEVEL]) / 100.0;

	return NL_SKIP;
}

/* Power-saving-mode handler. Adapted from iw:ps.c. */
static int power_save_handler(struct nl_msg *msg, void *arg)
{
	struct iw_nl80211_ifstat *ifs = (struct iw_nl80211_ifstat *)arg;
	struct nlattr *attrs[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));

	nla_parse(attrs, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	if (attrs[NL80211_ATTR_PS_STATE]) {
		uint32_t state = nla_get_u32(attrs[NL80211_ATTR_PS_STATE]);

		ifs->power_save = state == NL80211_PS_ENABLED;
	}
	return NL_SKIP;
}

/* Query PHY information. */
static int phy_handler(struct nl_msg *msg, void *arg)
{
	struct iw_nl80211_ifstat *ifs = (struct iw_nl80211_ifstat *)arg;
	struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));

	nla_parse(tb_msg, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	if (tb_msg[NL80211_ATTR_WIPHY] &&
	    nla_get_u32(tb_msg[NL80211_ATTR_WIPHY]) != ifs->phy_id)
		return NL_SKIP;

	if (tb_msg[NL80211_ATTR_WIPHY_RETRY_SHORT])
		ifs->phy.retry_short = nla_get_u8(tb_msg[NL80211_ATTR_WIPHY_RETRY_SHORT]);

	if (tb_msg[NL80211_ATTR_WIPHY_RETRY_LONG])
		ifs->phy.retry_long = nla_get_u8(tb_msg[NL80211_ATTR_WIPHY_RETRY_LONG]);

	if (tb_msg[NL80211_ATTR_WIPHY_BANDS]) {
		struct nlattr *nl_band;
		int rem_band;

		nla_for_each_nested(nl_band, tb_msg[NL80211_ATTR_WIPHY_BANDS], rem_band) {
			ifs->phy.bands++;
		}
	}

	if (tb_msg[NL80211_ATTR_WIPHY_RTS_THRESHOLD])
		ifs->phy.rts_threshold = nla_get_u32(tb_msg[NL80211_ATTR_WIPHY_RTS_THRESHOLD]);

	if (tb_msg[NL80211_ATTR_WIPHY_FRAG_THRESHOLD])
		ifs->phy.frag_threshold = nla_get_u32(tb_msg[NL80211_ATTR_WIPHY_FRAG_THRESHOLD]);

	return NL_SKIP;
}

/**
 * survey_handler - channel survey data
 * This handler will be called multiple times, for each channel.
 * stolen from iw:survey.c
 */
static int survey_handler(struct nl_msg *msg, void *arg)
{
	struct iw_nl80211_survey *sd = (struct iw_nl80211_survey *)arg;
	struct nlattr *tb[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *sinfo[NL80211_SURVEY_INFO_MAX + 1];

	static struct nla_policy survey_policy[NL80211_SURVEY_INFO_MAX + 1] = {
		[NL80211_SURVEY_INFO_FREQUENCY]     = { .type = NLA_U32 },
		[NL80211_SURVEY_INFO_NOISE]         = { .type = NLA_U8 },
		[NL80211_SURVEY_INFO_IN_USE]        = { .type = NLA_FLAG },
		[NL80211_SURVEY_INFO_TIME]          = { .type = NLA_U64 },
		[NL80211_SURVEY_INFO_TIME_BUSY]     = { .type = NLA_U64 },
		[NL80211_SURVEY_INFO_TIME_EXT_BUSY] = { .type = NLA_U64 },
		[NL80211_SURVEY_INFO_TIME_RX]       = { .type = NLA_U64 },
		[NL80211_SURVEY_INFO_TIME_TX]       = { .type = NLA_U64 },
		[NL80211_SURVEY_INFO_TIME_SCAN]     = { .type = NLA_U64 },
	};

	nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	if (!tb[NL80211_ATTR_SURVEY_INFO])
		return NL_SKIP;

	if (nla_parse_nested(sinfo, NL80211_SURVEY_INFO_MAX,
			     tb[NL80211_ATTR_SURVEY_INFO], survey_policy))
		return NL_SKIP;

	/* The frequency is needed to match up with the associated station */
	if (!sinfo[NL80211_SURVEY_INFO_FREQUENCY])
		return NL_SKIP;

	/* We are only interested in the data of the operating channel */
	if (!sinfo[NL80211_SURVEY_INFO_IN_USE])
		return NL_SKIP;

	sd->freq  = nla_get_u32(sinfo[NL80211_SURVEY_INFO_FREQUENCY]);

	if (sinfo[NL80211_SURVEY_INFO_NOISE])
		sd->noise = (int8_t)nla_get_u8(sinfo[NL80211_SURVEY_INFO_NOISE]);

	if (sinfo[NL80211_SURVEY_INFO_TIME])
		sd->time.active = nla_get_u64(sinfo[NL80211_SURVEY_INFO_TIME]);

	if (sinfo[NL80211_SURVEY_INFO_TIME_BUSY])
		sd->time.busy = nla_get_u64(sinfo[NL80211_SURVEY_INFO_TIME_BUSY]);

	if (sinfo[NL80211_SURVEY_INFO_TIME_EXT_BUSY])
		sd->time.ext_busy = nla_get_u64(sinfo[NL80211_SURVEY_INFO_TIME_EXT_BUSY]);

	if (sinfo[NL80211_SURVEY_INFO_TIME_RX])
		sd->time.rx = nla_get_u64(sinfo[NL80211_SURVEY_INFO_TIME_RX]);

	if (sinfo[NL80211_SURVEY_INFO_TIME_TX])
		sd->time.tx = nla_get_u64(sinfo[NL80211_SURVEY_INFO_TIME_TX]);

	if (sinfo[NL80211_SURVEY_INFO_TIME_SCAN])
		sd->time.scan = nla_get_u64(sinfo[NL80211_SURVEY_INFO_TIME_SCAN]);

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

	if (tb_msg[NL80211_ATTR_DFS_REGION])
		ir->region = nla_get_u8(tb_msg[NL80211_ATTR_DFS_REGION]);
	else
		ir->region = NL80211_DFS_UNSET;

	alpha2 = nla_data(tb_msg[NL80211_ATTR_REG_ALPHA2]);
	ir->country[0] = alpha2[0];
	ir->country[1] = alpha2[1];

	return NL_SKIP;
}

/* Check if nl80211 allows splitting of wiphy information. Adapted from iw:info.c.*/
static int have_split_wiphy_handler(struct nl_msg *msg, void *arg)
{
	struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	bool *has_split_wiphy = (bool *)arg;

	nla_parse(tb_msg, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	if (tb_msg[NL80211_ATTR_PROTOCOL_FEATURES]) {
		uint32_t feat = nla_get_u32(tb_msg[NL80211_ATTR_PROTOCOL_FEATURES]);

		if (feat & NL80211_PROTOCOL_FEATURE_SPLIT_WIPHY_DUMP) {
			*has_split_wiphy = true;
		}
	}
	return NL_SKIP;
}

static int link_handler(struct nl_msg *msg, void *arg)
{
	struct iw_nl80211_linkstat *ls = arg;
	struct nlattr *tb[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *bss[NL80211_BSS_MAX + 1];
	static struct nla_policy bss_policy[NL80211_BSS_MAX + 1] = {
		[NL80211_BSS_TSF] = { .type = NLA_U64 },
		[NL80211_BSS_FREQUENCY] = { .type = NLA_U32 },
		[NL80211_BSS_BSSID] = { 0 },
		[NL80211_BSS_BEACON_INTERVAL] = { .type = NLA_U16 },
		[NL80211_BSS_CAPABILITY] = { .type = NLA_U16 },
		[NL80211_BSS_INFORMATION_ELEMENTS] = { 0 },
		[NL80211_BSS_SIGNAL_MBM] = { .type = NLA_U32 },
		[NL80211_BSS_SIGNAL_UNSPEC] = { .type = NLA_U8 },
		[NL80211_BSS_STATUS] = { .type = NLA_U32 },
	};

	nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	if (!tb[NL80211_ATTR_BSS])
		return NL_SKIP;

	if (nla_parse_nested(bss, NL80211_BSS_MAX, tb[NL80211_ATTR_BSS], bss_policy))
		return NL_SKIP;

	if (!bss[NL80211_BSS_BSSID])
		return NL_SKIP;

	if (!bss[NL80211_BSS_STATUS])
		return NL_SKIP;

	if (bss[NL80211_BSS_SIGNAL_UNSPEC])
		ls->bss_signal_qual = nla_get_u8(bss[NL80211_BSS_SIGNAL_UNSPEC]);

	if (bss[NL80211_BSS_SIGNAL_MBM]) {
		int s = nla_get_u32(bss[NL80211_BSS_SIGNAL_MBM]);
		ls->bss_signal = s / 100;
	}

	ls->status = nla_get_u32(bss[NL80211_BSS_STATUS]);
	switch (ls->status) {
	case NL80211_BSS_STATUS_ASSOCIATED:	/* apparently no longer used */
	case NL80211_BSS_STATUS_AUTHENTICATED:
	case NL80211_BSS_STATUS_IBSS_JOINED:
		memcpy(&ls->bssid, nla_data(bss[NL80211_BSS_BSSID]), ETH_ALEN);
	}

	return NL_SKIP;
}

static int link_sta_handler(struct nl_msg *msg, void *arg)
{
	struct iw_nl80211_linkstat *ls = arg;
	struct nlattr *tb[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *sinfo[NL80211_STA_INFO_MAX + 1];
	struct nlattr *binfo[NL80211_STA_BSS_PARAM_MAX + 1];
	struct nl80211_sta_flag_update *sta_flags;
	static struct nla_policy stats_policy[NL80211_STA_INFO_MAX + 1] = {
		[NL80211_STA_INFO_CONNECTED_TIME] = { .type = NLA_U32 },
		[NL80211_STA_INFO_INACTIVE_TIME] = { .type = NLA_U32 },
		[NL80211_STA_INFO_RX_BYTES] = { .type = NLA_U32 },
		[NL80211_STA_INFO_RX_BYTES64] = { .type = NLA_U64 },
		[NL80211_STA_INFO_TX_BYTES] = { .type = NLA_U32 },
		[NL80211_STA_INFO_TX_BYTES64] = { .type = NLA_U64 },
		[NL80211_STA_INFO_RX_PACKETS] = { .type = NLA_U32 },
		[NL80211_STA_INFO_TX_PACKETS] = { .type = NLA_U32 },
		[NL80211_STA_INFO_SIGNAL] = { .type = NLA_U8 },
		[NL80211_STA_INFO_SIGNAL_AVG] = { .type = NLA_U8 },
		[NL80211_STA_INFO_T_OFFSET] = { .type = NLA_U64 },
		[NL80211_STA_INFO_TX_BITRATE] = { .type = NLA_NESTED },
		[NL80211_STA_INFO_RX_BITRATE] = { .type = NLA_NESTED },
		[NL80211_STA_INFO_RX_DROP_MISC] = { .type = NLA_U64 },
		[NL80211_STA_INFO_BEACON_RX] = { .type = NLA_U64 },
		[NL80211_STA_INFO_BEACON_LOSS] = { .type = NLA_U32 },
		[NL80211_STA_INFO_BEACON_SIGNAL_AVG] = { .type = NLA_U8 },
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
	static struct nla_policy bss_policy[NL80211_STA_BSS_PARAM_MAX + 1] = {
		[NL80211_STA_BSS_PARAM_CTS_PROT] = { .type = NLA_FLAG },
		[NL80211_STA_BSS_PARAM_SHORT_PREAMBLE] = { .type = NLA_FLAG },
		[NL80211_STA_BSS_PARAM_SHORT_SLOT_TIME] = { .type = NLA_FLAG },
		[NL80211_STA_BSS_PARAM_DTIM_PERIOD] = { .type = NLA_U8 },
		[NL80211_STA_BSS_PARAM_BEACON_INTERVAL] = { .type = NLA_U16 },
	};

	nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	if (!tb[NL80211_ATTR_STA_INFO])
		return NL_SKIP;

	if (nla_parse_nested(sinfo, NL80211_STA_INFO_MAX,
			     tb[NL80211_ATTR_STA_INFO],
			     stats_policy))
		return NL_SKIP;

	if (sinfo[NL80211_STA_INFO_TX_RETRIES])
		ls->tx_retries = nla_get_u32(sinfo[NL80211_STA_INFO_TX_RETRIES]);
	if (sinfo[NL80211_STA_INFO_TX_FAILED])
		ls->tx_failed = nla_get_u32(sinfo[NL80211_STA_INFO_TX_FAILED]);


	if (sinfo[NL80211_STA_INFO_EXPECTED_THROUGHPUT]) {
		ls->expected_thru = nla_get_u32(sinfo[NL80211_STA_INFO_EXPECTED_THROUGHPUT]);
		/* convert in Mbps but scale by 1000 to save kbps units */
		ls->expected_thru = ls->expected_thru * 1000 / 1024;
	}
	if (sinfo[NL80211_STA_INFO_INACTIVE_TIME])
		ls->inactive_time = nla_get_u32(sinfo[NL80211_STA_INFO_INACTIVE_TIME]);
	if (sinfo[NL80211_STA_INFO_CONNECTED_TIME])
		ls->connected_time = nla_get_u32(sinfo[NL80211_STA_INFO_CONNECTED_TIME]);

	if (sinfo[NL80211_STA_INFO_RX_BYTES64])
		ls->rx_bytes = nla_get_u64(sinfo[NL80211_STA_INFO_RX_BYTES64]);
	if (sinfo[NL80211_STA_INFO_RX_PACKETS])
		ls->rx_packets = nla_get_u32(sinfo[NL80211_STA_INFO_RX_PACKETS]);
	if (sinfo[NL80211_STA_INFO_RX_DROP_MISC])
		ls->rx_drop_misc = nla_get_u64(sinfo[NL80211_STA_INFO_RX_DROP_MISC]);

	if (sinfo[NL80211_STA_INFO_TX_BYTES64])
		ls->tx_bytes = nla_get_u64(sinfo[NL80211_STA_INFO_TX_BYTES64]);
	if (sinfo[NL80211_STA_INFO_TX_PACKETS])
		ls->tx_packets = nla_get_u32(sinfo[NL80211_STA_INFO_TX_PACKETS]);

	if (sinfo[NL80211_STA_INFO_SIGNAL])
		ls->signal = (int8_t)nla_get_u8(sinfo[NL80211_STA_INFO_SIGNAL]);
	if (sinfo[NL80211_STA_INFO_SIGNAL_AVG])
		ls->signal_avg = (int8_t)nla_get_u8(sinfo[NL80211_STA_INFO_SIGNAL_AVG]);


	if (sinfo[NL80211_STA_INFO_BEACON_SIGNAL_AVG])
		ls->beacon_avg_sig = nla_get_u8(sinfo[NL80211_STA_INFO_BEACON_SIGNAL_AVG]);
	if (sinfo[NL80211_STA_INFO_BEACON_RX])
		ls->beacons = nla_get_u64(sinfo[NL80211_STA_INFO_BEACON_RX]);
	if (sinfo[NL80211_STA_INFO_BEACON_LOSS])
		ls->beacon_loss = nla_get_u32(sinfo[NL80211_STA_INFO_BEACON_LOSS]);

	if (sinfo[NL80211_STA_INFO_TX_BITRATE])
		parse_bitrate(sinfo[NL80211_STA_INFO_TX_BITRATE], ls->tx_bitrate, sizeof(ls->tx_bitrate));

	if (sinfo[NL80211_STA_INFO_RX_BITRATE])
		parse_bitrate(sinfo[NL80211_STA_INFO_RX_BITRATE], ls->rx_bitrate, sizeof(ls->rx_bitrate));

	if (sinfo[NL80211_STA_INFO_STA_FLAGS]) {
		sta_flags = (struct nl80211_sta_flag_update *)
			    nla_data(sinfo[NL80211_STA_INFO_STA_FLAGS]);

		if (sta_flags->mask & BIT(NL80211_STA_FLAG_SHORT_PREAMBLE) &&
		    sta_flags->set & BIT(NL80211_STA_FLAG_SHORT_PREAMBLE))
			ls->long_preamble = true;

		if (sta_flags->mask & BIT(NL80211_STA_FLAG_WME) &&
		    sta_flags->set & BIT(NL80211_STA_FLAG_WME))
			ls->wme = true;

		if (sta_flags->mask & BIT(NL80211_STA_FLAG_MFP) &&
		    sta_flags->set & BIT(NL80211_STA_FLAG_MFP))
			ls->mfp = true;

		if (sta_flags->mask & BIT(NL80211_STA_FLAG_TDLS_PEER) &&
		    sta_flags->set & BIT(NL80211_STA_FLAG_TDLS_PEER))
			ls->tdls = true;
	}

	/* BSS Flags */
	if (sinfo[NL80211_STA_INFO_BSS_PARAM]) {
		if (nla_parse_nested(binfo, NL80211_STA_BSS_PARAM_MAX,
				     sinfo[NL80211_STA_INFO_BSS_PARAM],
				     bss_policy) == 0) {
			if (binfo[NL80211_STA_BSS_PARAM_CTS_PROT]) {
				ls->cts_protection = true;
			}
			if (binfo[NL80211_STA_BSS_PARAM_SHORT_PREAMBLE])
				ls->long_preamble = false;
			if (binfo[NL80211_STA_BSS_PARAM_SHORT_SLOT_TIME])
				ls->short_slot_time = true;

			ls->beacon_int  = nla_get_u16(binfo[NL80211_STA_BSS_PARAM_BEACON_INTERVAL]);
			ls->dtim_period = nla_get_u8(binfo[NL80211_STA_BSS_PARAM_DTIM_PERIOD]);
		}
	}

	return NL_SKIP;
}

/*
 * COMMAND HANDLERS
 */
void iw_nl80211_get_linkstat(struct iw_nl80211_linkstat *ls)
{
	static struct cmd cmd_linkstat = {
		.cmd	 = NL80211_CMD_GET_SCAN,
		.flags	 = NLM_F_DUMP,
		.handler = link_handler
	};
	static struct cmd cmd_getstation = {
		.cmd	 = NL80211_CMD_GET_STATION,
		.flags	 = 0,
		.handler = link_sta_handler
	};

	cmd_linkstat.handler_arg = ls;
	memset(ls, 0, sizeof(*ls));
	handle_interface_cmd(&cmd_linkstat);

	/* If not associated to another station, the bssid is zeroed out */
	if (ether_addr_is_zero(&ls->bssid))
		return;
	/*
	 * Details of the associated station
	 */
	cmd_getstation.handler_arg  = ls;
	add_msg_arg(&cmd_getstation, NL80211_ATTR_MAC, sizeof(ls->bssid), &ls->bssid);

	handle_interface_cmd(&cmd_getstation);

	/* Channel survey data */
	iw_nl80211_get_survey(&ls->survey);
}

void iw_nl80211_getreg(struct iw_nl80211_reg *ir)
{
	static struct cmd cmd_reg = {
		.cmd	 = NL80211_CMD_GET_REG,
		.flags	 = 0,
		.handler = reg_handler
	};

	cmd_reg.handler_arg = ir;
	memset(ir, 0, sizeof(*ir));
	handle_interface_cmd(&cmd_reg);
}

/** Check kernel for split-wiphy support. Single-thread use only. */
static bool iw_nl80211_have_split_wiphy_dump()
{
	static bool nl80211_has_split_wiphy, checked;
	static struct cmd cmd_features = {
		.cmd	     = NL80211_CMD_GET_PROTOCOL_FEATURES,
		.flags	     = 0,
		.handler     = have_split_wiphy_handler,
		.handler_arg = &nl80211_has_split_wiphy,
	};

	if (!checked)
		handle_interface_cmd(&cmd_features);
	checked = true;
	return nl80211_has_split_wiphy;
}

void iw_nl80211_getifstat(struct iw_nl80211_ifstat *ifs)
{
	static struct cmd cmd_ifstat = {
		.cmd	 = NL80211_CMD_GET_INTERFACE,
		.flags	 = 0,
		.handler = iface_handler
	};

	cmd_ifstat.handler_arg = ifs;
	memset(ifs, 0, sizeof(*ifs));
	handle_interface_cmd(&cmd_ifstat);
}

void iw_nl80211_get_phy(struct iw_nl80211_ifstat *ifs)
{
	static struct cmd cmd_phy_info = {
		.cmd	   = NL80211_CMD_GET_WIPHY,
		.flags	   = 0,
		.hdr_flags = 0,
		.handler   = phy_handler,
	};

	if (iw_nl80211_have_split_wiphy_dump()) {
		cmd_phy_info.hdr_flags |= NL80211_ATTR_SPLIT_WIPHY_DUMP;
		cmd_phy_info.flags |= NLM_F_DUMP;
	}
	cmd_phy_info.handler_arg = ifs;
	memset(&ifs->phy, 0, sizeof(ifs->phy));
	handle_interface_cmd(&cmd_phy_info);
}

void iw_nl80211_get_power_save(struct iw_nl80211_ifstat *ifs) {
	static struct cmd cmd_power_save_info = {
		.cmd	   = NL80211_CMD_GET_POWER_SAVE,
		.flags	   = 0,
		.handler   = power_save_handler,
	};

	cmd_power_save_info.handler_arg = ifs;
	ifs->power_save = false;
	handle_interface_cmd(&cmd_power_save_info);
}

void iw_nl80211_get_survey(struct iw_nl80211_survey *sd)
{
	static struct cmd cmd_survey = {
		.cmd	 = NL80211_CMD_GET_SURVEY,
		.flags	 = NLM_F_DUMP,
		.handler = survey_handler
	};

	cmd_survey.handler_arg = sd;
	memset(sd, 0, sizeof(*sd));
	handle_interface_cmd(&cmd_survey);
}

/*
 * Multicast Handling
 */
/**
 * struct handler_args - arguments to resolve multicast group
 * @group: group name to resolve
 * @id:    ID it resolves into
 */
struct handler_args {
	const char	*group;
	int		id;
};

/* stolen from iw:genl.c */
static int family_handler(struct nl_msg *msg, void *arg)
{
	struct handler_args *grp = arg;
	struct nlattr *tb[CTRL_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *mcgrp;
	int rem_mcgrp;

	nla_parse(tb, CTRL_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	if (!tb[CTRL_ATTR_MCAST_GROUPS])
		return NL_SKIP;

	nla_for_each_nested(mcgrp, tb[CTRL_ATTR_MCAST_GROUPS], rem_mcgrp) {
		struct nlattr *tb_mcgrp[CTRL_ATTR_MCAST_GRP_MAX + 1];

		nla_parse(tb_mcgrp, CTRL_ATTR_MCAST_GRP_MAX,
			  nla_data(mcgrp), nla_len(mcgrp), NULL);

		if (!tb_mcgrp[CTRL_ATTR_MCAST_GRP_NAME] ||
		    !tb_mcgrp[CTRL_ATTR_MCAST_GRP_ID])
			continue;
		if (strncmp(nla_data(tb_mcgrp[CTRL_ATTR_MCAST_GRP_NAME]),
			    grp->group, nla_len(tb_mcgrp[CTRL_ATTR_MCAST_GRP_NAME])))
			continue;
		grp->id = nla_get_u32(tb_mcgrp[CTRL_ATTR_MCAST_GRP_ID]);
		break;
	}

	return NL_SKIP;
}

/* stolen from iw:genl.c */
int nl_get_multicast_id(struct nl_sock *sock, const char *family, const char *group)
{
	struct nl_msg *msg;
	struct nl_cb *cb;
	int ret, ctrlid;
	struct handler_args grp = {
		.group = group,
		.id = -ENOENT,
	};

	msg = nlmsg_alloc();
	if (!msg)
		return -ENOMEM;

	cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!cb) {
		ret = -ENOMEM;
		goto out_fail_cb;
	}

	ctrlid = genl_ctrl_resolve(sock, "nlctrl");

	genlmsg_put(msg, 0, 0, ctrlid, 0,
		    0, CTRL_CMD_GETFAMILY, 0);

	ret = -ENOBUFS;
	NLA_PUT_STRING(msg, CTRL_ATTR_FAMILY_NAME, family);

	ret = nl_send_auto_complete(sock, msg);
	if (ret < 0)
		goto out;

	ret = 1;

	nl_cb_err(cb, NL_CB_CUSTOM, error_handler, &ret);
	nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &ret);
	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, family_handler, &grp);

	while (ret > 0)
		nl_recvmsgs(sock, cb);

	if (ret == 0)
		ret = grp.id;
nla_put_failure:
out:
	nl_cb_put(cb);
out_fail_cb:
	nlmsg_free(msg);
	return ret;
}

/**
 * Allocate a GeNetlink socket ready to listen for nl80211 multicast group @grp
 * @grp: identifier of an nl80211 multicast group (e.g. "scan")
 */
struct nl_sock *alloc_nl_mcast_sk(const char *grp)
{
	int mcid, ret;
	struct nl_sock *sk = nl_socket_alloc();

	if (!sk)
		err_sys("failed to allocate netlink multicast socket");

	if (genl_connect(sk))
		err_sys("failed to connect multicast socket to GeNetlink");

	mcid = nl_get_multicast_id(sk, "nl80211", grp);
	if (mcid < 0)
		err_quit("failed to resolve nl80211 '%s' multicast group", grp);

	ret = nl_socket_add_membership(sk, mcid);
	if (ret)
		err_sys("failed to join nl80211 multicast group %s", grp);

	return sk;
}
