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

#include "iw_nl80211.h"

// FIXME: declarations
extern void handle_cmd(struct cmd *cmd);

#define MAX_SCAN_WAIT	10000	/* maximum milliseconds spent waiting */

// FIXME: old WEXT stuff
/*
 * Meta-data about all the additional standard Wireless Extension events
 * we know about.
 */
/* Type of headers we know about (basically union iwreq_data) */
#define IW_HEADER_TYPE_NULL	0	/* Not available */
#define IW_HEADER_TYPE_CHAR	2	/* char [IFNAMSIZ] */
#define IW_HEADER_TYPE_UINT	4	/* __u32 */
#define IW_HEADER_TYPE_FREQ	5	/* struct iw_freq */
#define IW_HEADER_TYPE_ADDR	6	/* struct sockaddr */
#define IW_HEADER_TYPE_POINT	8	/* struct iw_point */
#define IW_HEADER_TYPE_PARAM	9	/* struct iw_param */
#define IW_HEADER_TYPE_QUAL	10	/* struct iw_quality */

/* Size (in bytes) of various events */
static const int event_type_size[] = {
	[IW_HEADER_TYPE_NULL]  = IW_EV_LCP_PK_LEN,
	[IW_HEADER_TYPE_CHAR]  = IW_EV_CHAR_PK_LEN,
	[IW_HEADER_TYPE_UINT]  = IW_EV_UINT_PK_LEN,
	[IW_HEADER_TYPE_FREQ]  = IW_EV_FREQ_PK_LEN,
	[IW_HEADER_TYPE_ADDR]  = IW_EV_ADDR_PK_LEN,
	/*
	 * Fix IW_EV_POINT_PK_LEN: some wireless.h versions define this
	 * erroneously as IW_EV_LCP_LEN + 4 (e.g. ESSID will disappear).
	 * The value below is from wireless tools 30.
	 */
	[IW_HEADER_TYPE_POINT] = IW_EV_LCP_PK_LEN + 4,
	[IW_HEADER_TYPE_PARAM] = IW_EV_PARAM_PK_LEN,
	[IW_HEADER_TYPE_QUAL]  = IW_EV_QUAL_PK_LEN
};

/* Handling flags */
#define IW_DESCR_FLAG_NONE	0x0000	/* Obvious */
/* Wrapper level flags */
#define IW_DESCR_FLAG_DUMP	0x0001	/* Not part of the dump command */
#define IW_DESCR_FLAG_EVENT	0x0002	/* Generate an event on SET */
#define IW_DESCR_FLAG_RESTRICT	0x0004	/* GET : request is ROOT only */
				/* SET : Omit payload from generated iwevent */
#define IW_DESCR_FLAG_NOMAX	0x0008	/* GET : no limit on request size */
/* Driver level flags */
#define IW_DESCR_FLAG_WAIT	0x0100	/* Wait for driver event */

struct iw_ioctl_description {
	__u8 header_type;	/* NULL, iw_point or other */
	__u8 token_type;	/* Future */
	__u16 token_size;	/* Granularity of payload */
	__u16 min_tokens;	/* Min acceptable token number */
	__u16 max_tokens;	/* Max acceptable token number */
	__u32 flags;		/* Special handling of the request */
};

/*----------------- End of code copied from iwlib -----------------------*/

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
	[SO_ESSID]	= cmp_essid,
	[SO_OPEN]	= cmp_open,
	[SO_CHAN_SIG]	= cmp_chan_sig,
	[SO_OPEN_SIG]	= cmp_open_sig
};

// FIXME:
void iw_nl80211_scan_trigger();
void iw_nl80211_get_scan_data(struct scan_result *sr);

/* stolen from iw:scan.c */
int scan_dump_handler(struct nl_msg *msg, void *arg)
{
	struct scan_result *sr = (struct scan_result *)arg;
	struct scan_entry *new = calloc(1, sizeof(*new));
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

	// FIXME:
#if 0
			switch (iwe.cmd) {
			case SIOCGIWMODE:
				new->mode = iwe.u.mode;
                    		f |= 4;
				break;
			case SIOCGIWENCODE:
                		f |= 16;
				new->has_key = !(iwe.u.data.flags & IW_ENCODE_DISABLED);
				break;
			case IWEVQUAL:
				f |= 32;
				memcpy(&new->qual, &iwe.u.qual, sizeof(struct iw_quality));
				break;
			case IWEVGENIE:
				f |= 64;
				iw_extract_ie(&iwe, new);
				break;
			}
		}
#endif
	memcpy(&new->ap_addr, nla_data(bss[NL80211_BSS_BSSID]), sizeof(new->ap_addr));

	/*
	if (bss[NL80211_BSS_STATUS]) {
		switch (nla_get_u32(bss[NL80211_BSS_STATUS])) {
		case NL80211_BSS_STATUS_AUTHENTICATED:
			printf(" -- authenticated");
			break;
		case NL80211_BSS_STATUS_ASSOCIATED:
			printf(" -- associated");
			break;
		case NL80211_BSS_STATUS_IBSS_JOINED:
			printf(" -- joined");
			break;
		default:
			printf(" -- unknown status: %d",
				nla_get_u32(bss[NL80211_BSS_STATUS]));
			break;
		}
	}
	printf("\n");
*/
#ifdef __LATER
	if (bss[NL80211_BSS_TSF]) {
		unsigned long long tsf;
		tsf = (unsigned long long)nla_get_u64(bss[NL80211_BSS_TSF]);
		printf("\tTSF: %llu usec (%llud, %.2lld:%.2llu:%.2llu)\n",
			tsf, tsf/1000/1000/60/60/24, (tsf/1000/1000/60/60) % 24,
			(tsf/1000/1000/60) % 60, (tsf/1000/1000) % 60);
	}
	if (bss[NL80211_BSS_BEACON_INTERVAL])
		printf("\tbeacon interval: %d TUs\n",
			nla_get_u16(bss[NL80211_BSS_BEACON_INTERVAL]));
	if (bss[NL80211_BSS_CAPABILITY]) {
		__u16 capa = nla_get_u16(bss[NL80211_BSS_CAPABILITY]);
		/*
		printf("\tcapability:");

		if (new->freq > 45000)
			print_capa_dmg(capa);
		else
			print_capa_non_dmg(capa);
		printf(" (0x%.4x)\n", capa);
		*/
	}

	if (bss[NL80211_BSS_SEEN_MS_AGO]) {
		int age = nla_get_u32(bss[NL80211_BSS_SEEN_MS_AGO]);
		printf("\tlast seen: %d ms ago\n", age);
	}

#endif /* LATER */

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

	if (bss[NL80211_BSS_INFORMATION_ELEMENTS]) {
		uint8_t *ie = nla_data(bss[NL80211_BSS_INFORMATION_ELEMENTS]);
		uint8_t len = ie[1];
		int ielen   = nla_len(bss[NL80211_BSS_INFORMATION_ELEMENTS]);

 		while (ielen >= 2 && ielen >= ie[1]) {
			// FIXME: check min/max length
			switch (ie[0]) {
			case 0:
				print_ssid_escaped(new->essid, sizeof(new->essid),
						 ie+2,
						 len);
				break;
			}
			ielen -= ie[1] + 2;
			ie    += ie[1] + 2;
        	}
	}

	pthread_mutex_lock(&sr->mutex);
	new->next = sr->head;
	sr->head  = new;
	pthread_mutex_unlock(&sr->mutex);

	return NL_SKIP;
}

// FIXME:
static void get_scan_list(struct scan_result *sr)
{
	int wait, waited = 0;

	/* We are checking errno when returning NULL, so reset it here */
	errno = 0;

	iw_nl80211_scan_trigger();

	/* Larger initial timeout of 250ms between set and first get */
	for (wait = 250; (waited += wait) < MAX_SCAN_WAIT; wait = 100) {
		struct timeval tv = { 0, wait * 1000 };

		while (select(0, NULL, NULL, NULL, &tv) < 0)
			if (errno != EINTR && errno != EAGAIN)
				return;

		break;
	}
	iw_nl80211_get_scan_data(sr);

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

static void free_scan_list(struct scan_entry *head)
{
	if (head) {
		free_scan_list(head->next);
		free(head);
	}
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

/*
 *	Scan results.
 */
void scan_result_init(struct scan_result *sr)
{
	memset(sr, 0, sizeof(*sr));
	iw_getinf_range(conf_ifname(), &sr->range);
	pthread_mutex_init(&sr->mutex, NULL);
}

void scan_result_fini(struct scan_result *sr)
{
	free_scan_list(sr->head);
	free(sr->channel_stats);
	pthread_mutex_destroy(&sr->mutex);
}

/** The actual scan thread. */
void *do_scan(void *sr_ptr)
{
	struct scan_result *sr = (struct scan_result *)sr_ptr;
	struct scan_entry *cur;

	pthread_detach(pthread_self());

	do {
		pthread_mutex_lock(&sr->mutex);

		free_scan_list(sr->head);
		free(sr->channel_stats);

		sr->head          = NULL;
		sr->channel_stats = NULL;
		sr->msg[0]        = '\0';
		sr->max_essid_len = MAX_ESSID_LEN;
		memset(&(sr->num), 0, sizeof(sr->num));
		pthread_mutex_unlock(&sr->mutex);

		get_scan_list(sr);
		if (!sr->head) {
			switch(errno) {
			case EPERM:
				/* Don't try to read leftover results, it does not work reliably. */
				if (!has_net_admin_capability())
					snprintf(sr->msg, sizeof(sr->msg),
						 "This screen requires CAP_NET_ADMIN permissions");
				break;
			case EFAULT:
				/*
				 * EFAULT can occur after a window resizing event and is temporary.
				 * It may also occur when the interface is down, hence defer handling.
				 */
				break;
			case EINTR:
			case EBUSY:
			case EAGAIN:
				/* Temporary errors. */
				snprintf(sr->msg, sizeof(sr->msg), "Waiting for scan data on %s ...", conf_ifname());
				break;
			case ENETDOWN:
				snprintf(sr->msg, sizeof(sr->msg), "Interface %s is down - setting it up ...", conf_ifname());
				if (if_set_up(conf_ifname()) < 0)
					err_sys("Can not bring up interface '%s'", conf_ifname());
				break;
			case E2BIG:
				/*
				 * This is a driver issue, since already using the largest possible
				 * scan buffer. See comments in iwlist.c of wireless tools.
				 */
				snprintf(sr->msg, sizeof(sr->msg),
					 "No scan on %s: Driver returned too much data", conf_ifname());
				break;
			case 0:
				snprintf(sr->msg, sizeof(sr->msg), "Empty scan results on %s", conf_ifname());
				break;
			default:
				snprintf(sr->msg, sizeof(sr->msg),
					 "Scan failed on %s: %s", conf_ifname(), strerror(errno));
			}
		}

		for (cur = sr->head; cur; cur = cur->next) {
			if (str_is_ascii(cur->essid))
				sr->max_essid_len = clamp(strlen(cur->essid),
							  sr->max_essid_len,
							  IW_ESSID_MAX_SIZE);
			if (cur->freq >= 5e9)
				sr->num.five_gig++;
			else if (cur->freq >= 2e9)
				sr->num.two_gig++;
			sr->num.entries += 1;
			sr->num.open    += !cur->has_key;
		}
		compute_channel_stats(sr);

		pthread_mutex_unlock(&sr->mutex);
	} while (usleep(conf.stat_iv * 1000) == 0);

	return NULL;
}
