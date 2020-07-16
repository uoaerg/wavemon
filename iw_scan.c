/*
 * Auxiliary declarations and functions imported from iwlib in order to
 * process and parse scan events. This code is copied with little change
 * from wireless tools 30. It remains here until the wext code will be
 * replaced by corresponding netlink calls.
 */
#include "iw_if.h"
#include <search.h>		/* lsearch(3) */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "ie_id.h"
#include "iw_nl80211.h"

/* GLOBAL VARIABLES */
static struct nl_sock *scan_wait_sk;


/*
 * Ordering functions for scan results: all return true for a < b.
 */

/* Order by frequency. */
static bool cmp_freq(const struct scan_entry *a, const struct scan_entry *b)
{
	return a->freq < b->freq;
}

/* Order by signal strength. */
static bool cmp_sig(const struct scan_entry *a, const struct scan_entry *b)
{
	if (!a->bss_signal && !b->bss_signal)
		return a->bss_signal_qual < b->bss_signal_qual;
	return a->bss_signal < b->bss_signal;
}

/* Order by ESSID, organize entries with same ESSID by frequency and signal. */
static bool cmp_essid(const struct scan_entry *a, const struct scan_entry *b)
{
	int res = strncmp(a->essid, b->essid, IW_ESSID_MAX_SIZE);

	return res == 0 ? (a->freq == b->freq ? cmp_sig(a, b) : cmp_freq(a, b))
			: res < 0;
}

/* Order by MAC address */
static bool cmp_mac(const struct scan_entry *a, const struct scan_entry *b)
{
	return memcmp(&a->ap_addr, &b->ap_addr, sizeof(a->ap_addr)) < 0;
}

/* Order by frequency, grouping channels by ESSID. */
static bool cmp_chan(const struct scan_entry *a, const struct scan_entry *b)
{
	return a->freq == b->freq ? cmp_essid(a, b) : cmp_freq(a, b);
}

/* Order by frequency first, then by signal strength. */
static bool cmp_chan_sig(const struct scan_entry *a, const struct scan_entry *b)
{
	return a->freq == b->freq ? cmp_sig(a, b) : cmp_chan(a, b);
}

/* Order by openness (open access points frist). */
static bool cmp_open(const struct scan_entry *a, const struct scan_entry *b)
{
	return a->has_key < b->has_key;
}

/* Sort (open) access points by signal strength. */
static bool cmp_open_sig(const struct scan_entry *a, const struct scan_entry *b)
{
	return a->has_key == b->has_key ? cmp_sig(a, b) : cmp_open(a, b);
}

static bool (*scan_cmp[])(const struct scan_entry *, const struct scan_entry *) = {
	[SO_CHAN]	= cmp_chan,
	[SO_SIGNAL]	= cmp_sig,
	[SO_MAC]        = cmp_mac,
	[SO_ESSID]	= cmp_essid,
	[SO_OPEN]	= cmp_open,
	[SO_CHAN_SIG]	= cmp_chan_sig,
	[SO_OPEN_SIG]	= cmp_open_sig
};

/*
 * Scan event handling
 */

/* Callback event handler */
static int wait_event(struct nl_msg *msg, void *arg)
{
	struct wait_event *wait = arg;
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	int i;

	for (i = 0; i < wait->n_cmds; i++) {
		if (gnlh->cmd == wait->cmds[i])
			wait->cmd = gnlh->cmd;
	}
	return NL_SKIP;
}

/**
 * Wait for scan result notification sent by the kernel
 * Returns true if scan results are available, false if scan was aborted.
 * Taken from iw:event.c:__do_listen_events
 */
static bool wait_for_scan_events(void)
{
	static const uint32_t cmds[] = {
		NL80211_CMD_NEW_SCAN_RESULTS,
		NL80211_CMD_SCAN_ABORTED,
	};
	struct wait_event wait_ev = {
		.cmds   = cmds,
		.n_cmds = ARRAY_SIZE(cmds),
		.cmd    = 0
	};
	struct nl_cb *cb;

	if (!scan_wait_sk)
		scan_wait_sk = alloc_nl_mcast_sk("scan");

	cb = nl_cb_alloc(IW_NL_CB_DEBUG ? NL_CB_DEBUG : NL_CB_DEFAULT);
	if (!cb)
		err_sys("failed to allocate netlink callbacks");

	/* no sequence checking for multicast messages */
	nl_cb_set(cb, NL_CB_SEQ_CHECK, NL_CB_CUSTOM, no_seq_check, NULL);
	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, wait_event, &wait_ev);

	while (!wait_ev.cmd)
		nl_recvmsgs(scan_wait_sk, cb);
	nl_cb_put(cb);

	return wait_ev.cmd == NL80211_CMD_NEW_SCAN_RESULTS;
}

/**
 * Scan result handler. Stolen from iw:scan.c
 * This also updates the scan-result statistics.
 */
int scan_dump_handler(struct nl_msg *msg, void *arg)
{
	struct scan_result *sr = (struct scan_result *)arg;
	struct scan_entry *new;
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *tb[NL80211_ATTR_MAX + 1];
	struct nlattr *bss[NL80211_BSS_MAX + 1];
	static struct nla_policy bss_policy[NL80211_BSS_MAX + 1] = {
		[NL80211_BSS_TSF]                  = { .type = NLA_U64 },
		[NL80211_BSS_FREQUENCY]            = { .type = NLA_U32 },
		[NL80211_BSS_BSSID]                = { },
		[NL80211_BSS_BEACON_INTERVAL]      = { .type = NLA_U16 },
		[NL80211_BSS_CAPABILITY]           = { .type = NLA_U16 },
		[NL80211_BSS_INFORMATION_ELEMENTS] = { },
		[NL80211_BSS_SIGNAL_MBM]           = { .type = NLA_U32 },
		[NL80211_BSS_SIGNAL_UNSPEC]        = { .type = NLA_U8  },
		[NL80211_BSS_STATUS]               = { .type = NLA_U32 },
		[NL80211_BSS_SEEN_MS_AGO]          = { .type = NLA_U32 },
		[NL80211_BSS_BEACON_IES]           = { },
	};

	nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	if (!tb[NL80211_ATTR_BSS])
		return NL_SKIP;

	if (nla_parse_nested(bss, NL80211_BSS_MAX,
			     tb[NL80211_ATTR_BSS],
			     bss_policy))
		return NL_SKIP;

	if (!bss[NL80211_BSS_BSSID])
		return NL_SKIP;

	new = calloc(1, sizeof(*new));
	if (!new)
		err_sys("failed to allocate scan entry");

	memcpy(&new->ap_addr, nla_data(bss[NL80211_BSS_BSSID]), sizeof(new->ap_addr));

	if (bss[NL80211_BSS_FREQUENCY]) {
		new->freq = nla_get_u32(bss[NL80211_BSS_FREQUENCY]);
		new->chan = ieee80211_frequency_to_channel(new->freq);
	}

	if (bss[NL80211_BSS_SIGNAL_UNSPEC])
		new->bss_signal_qual = nla_get_u8(bss[NL80211_BSS_SIGNAL_UNSPEC]);


	if (bss[NL80211_BSS_SIGNAL_MBM]) {
		int s = nla_get_u32(bss[NL80211_BSS_SIGNAL_MBM]);
		new->bss_signal = s / 100;
	}

	if (bss[NL80211_BSS_CAPABILITY]) {
		new->bss_capa = nla_get_u16(bss[NL80211_BSS_CAPABILITY]);
		new->has_key  = (new->bss_capa & WLAN_CAPABILITY_PRIVACY) != 0;
	}

	if (bss[NL80211_BSS_SEEN_MS_AGO])
		new->last_seen = nla_get_u32(bss[NL80211_BSS_SEEN_MS_AGO]);

	if (bss[NL80211_BSS_TSF])
		new->tsf = nla_get_u64(bss[NL80211_BSS_TSF]);

	if (bss[NL80211_BSS_INFORMATION_ELEMENTS]) {
		uint8_t *ie = nla_data(bss[NL80211_BSS_INFORMATION_ELEMENTS]);
		int ielen   = nla_len(bss[NL80211_BSS_INFORMATION_ELEMENTS]);

		while (ielen >= 2 && ielen >= ie[1]) {
			uint8_t len = ie[1];

			switch (ie[0]) {
			case IE_SSID:
				if (len > 0 && len <= 32)
					print_ssid_escaped(new->essid, sizeof(new->essid),
							   ie+2, len);
				break;
			case IE_BSS_LOAD:
				if (len >= 5) {
					new->bss_sta_count  = ie[3] << 8 | ie[2];
					new->bss_chan_usage = ie[4];
				}
				break;
			case IE_EXT_CAPABILITIES:
				if (len >= sizeof(new->ext_capa)) {
					memcpy(new->ext_capa, &ie[2], sizeof(new->ext_capa));
				}
				break;
			}
			// Point to next ie
			ielen -= ie[1] + 2;
			ie    += ie[1] + 2;
		}
	}

	/* Update stats */
	new->next = sr->head;
	sr->head  = new;
	if (str_is_ascii(new->essid))
		sr->max_essid_len = clamp(strlen(new->essid),
					  sr->max_essid_len,
					  IW_ESSID_MAX_SIZE);

	if (new->freq > 45000)	/* 802.11ad 60GHz spectrum */
		err_quit("FIXME: can not handle %d MHz spectrum yet", new->freq);
	else if (new->freq >= 5000)
		sr->num.five_gig++;
	else if (new->freq >= 2000)
		sr->num.two_gig++;
	sr->num.entries += 1;
	sr->num.open    += !new->has_key;

	return NL_SKIP;
}

static int iw_nl80211_scan_trigger(void)
{
	static struct cmd cmd_trigger_scan = {
		.cmd = NL80211_CMD_TRIGGER_SCAN,
	};

	return handle_cmd(&cmd_trigger_scan);
}

static int iw_nl80211_get_scan_data(struct scan_result *sr)
{
	static struct cmd cmd_scan_dump = {
		.cmd	 = NL80211_CMD_GET_SCAN,
		.flags	 = NLM_F_DUMP,
		.handler = scan_dump_handler
	};

	memset(sr, 0, sizeof(*sr));
	cmd_scan_dump.handler_arg = sr;

	return handle_cmd(&cmd_scan_dump);
}

/*
 * Simple sort routine.
 * FIXME: use hash or tree to store entries, a list to display them.
 */
void sort_scan_list(struct scan_entry **headp)
{
	struct scan_entry *head = NULL, *cur, *new = *headp, **prev;

	while (new) {
		for (cur = head, prev = &head; cur &&
		     conf.scan_sort_asc == scan_cmp[conf.scan_sort_order](cur, new);
		     prev = &cur->next, cur = cur->next)
			;
		*prev = new;
		new = new->next;
		(*prev)->next = cur;
	}
	*headp = head;
}

/** De-allocate list. Use after all threads are terminated. */
void free_scan_list(struct scan_entry *head)
{
	if (head) {
		free_scan_list(head->next);
		free(head);
	}
}

/** Initialize scan results. Requires lock to be taken. */
void init_scan_list(struct scan_result *sr)
{
	free_scan_list(sr->head);
	free(sr->channel_stats);
	sr->head          = NULL;
	sr->channel_stats = NULL;
	sr->msg[0]        = '\0';
	sr->max_essid_len = MAX_ESSID_LEN;
	memset(&(sr->num), 0, sizeof(sr->num));
	sr->mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
}

/*
 * 	Channel statistics shown at the bottom of scan screen.
 */

/*
 * For lsearch, it compares key value with array member, needs to
 * return 0 if they are the same, non-0 otherwise.
 */
static int cmp_key(const void *a, const void *b)
{
	return ((struct cnt *)a)->val - ((struct cnt *)b)->val;
}

/* For quick-sorting the array in descending order of counts */
static int cmp_cnt(const void *a, const void *b)
{
	if (conf.scan_sort_order == SO_CHAN && !conf.scan_sort_asc)
		return ((struct cnt *)a)->count - ((struct cnt *)b)->count;
	return ((struct cnt *)b)->count - ((struct cnt *)a)->count;
}

/**
 * Fill in sr->channel_stats (must not have been allocated yet).
 */
static void compute_channel_stats(struct scan_result *sr)
{
	struct scan_entry *cur;
	struct cnt *bin, key = {0, 0};
	size_t n = 0;

	if (!sr->num.entries)
		return;

	sr->channel_stats = calloc(sr->num.entries, sizeof(key));
	for (cur = sr->head; cur; cur = cur->next) {
		if (cur->chan >= 0) {
			key.val = cur->chan;
			bin = lsearch(&key, sr->channel_stats, &n, sizeof(key), cmp_key);
			if (bin)
				bin->count++;
		}
	}

	if (n > 0) {
		qsort(sr->channel_stats, n, sizeof(key), cmp_cnt);
	} else {
		free(sr->channel_stats);
		sr->channel_stats = NULL;
	}
	sr->num.ch_stats = n < MAX_CH_STATS ? n : MAX_CH_STATS;
}

/** The actual scan thread. */
void *do_scan(void *sr_ptr)
{
	struct scan_result *sr = sr_ptr;
	sigset_t blockmask;
	int ret = 0;

	/* SIGWINCH is supposed to be handled in the main thread. */
	sigemptyset(&blockmask);
	sigaddset(&blockmask, SIGWINCH);
	pthread_sigmask(SIG_BLOCK, &blockmask, NULL);

	pthread_detach(pthread_self());
	do {
		ret = iw_nl80211_scan_trigger();

		pthread_mutex_lock(&sr->mutex);
		init_scan_list(sr);
		switch(-ret) {
		case 0:
		case EBUSY:
			/* Trigger returns -EBUSY if a scan request is pending or ready. */
			pthread_mutex_unlock(&sr->mutex);

			/* Do not hold the lock while awaiting results. */
			if (!wait_for_scan_events()) {
				pthread_mutex_lock(&sr->mutex);
				snprintf(sr->msg, sizeof(sr->msg), "Waiting for scan data...");
				pthread_mutex_unlock(&sr->mutex);
			} else {
				pthread_mutex_lock(&sr->mutex);
				ret = iw_nl80211_get_scan_data(sr);
				if (ret < 0) {
					snprintf(sr->msg, sizeof(sr->msg),
						 "Scan failed on %s: %s", conf_ifname(), strerror(-ret));
				} else if (!sr->head) {
					snprintf(sr->msg, sizeof(sr->msg), "Empty scan results on %s", conf_ifname());
				}
				compute_channel_stats(sr);
				pthread_mutex_unlock(&sr->mutex);
			}
			break;
		case EPERM:
			if (!has_net_admin_capability())
				snprintf(sr->msg, sizeof(sr->msg), "This screen requires CAP_NET_ADMIN permissions");
			pthread_mutex_unlock(&sr->mutex);
			break;
		case EFAULT:
			/* EFAULT can occur after a window resizing event: temporary, fall through. */
		case EINTR:
		case EAGAIN:
			/* Temporary errors. */
			snprintf(sr->msg, sizeof(sr->msg), "Waiting for device to become ready ...");
			pthread_mutex_unlock(&sr->mutex);
			break;
		case ENETDOWN:
			if (!if_is_up(conf_ifname())) {
				snprintf(sr->msg, sizeof(sr->msg), "Interface %s is down - setting it up ...", conf_ifname());
				pthread_mutex_unlock(&sr->mutex);

				if (if_set_up(conf_ifname()) < 0)
					err_sys("Can not bring up interface '%s'", conf_ifname());
				if (atexit(if_set_down_on_exit) < 0)
					snprintf(sr->msg, sizeof(sr->msg), "Warning: unable to restore %s down state on exit", conf_ifname());
				break;
			}
			/* fall through */
		default:
			snprintf(sr->msg, sizeof(sr->msg), "Scan trigger failed on %s: %s", conf_ifname(), strerror(-ret));
			pthread_mutex_unlock(&sr->mutex);
		}
	} while (usleep(conf.stat_iv * 1000) == 0);

	return NULL;
}
