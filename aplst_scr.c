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
#include "iw_if.h"

#define START_LINE	2	/* where to begin the screen */
#define MAX_SCAN_WAIT	10000	/* maximum milliseconds spent waiting */

/*
 * Auxiliary declarations and functions imported from iwlib in order to
 * process and parse scan events. This code is copied with little change
 * from wireless tools 30. It remains here until the wext code will be
 * replaced by corresponding netlink calls.
 */

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

/*
 * Meta-data about all the standard Wireless Extension request we
 * know about.
 */
static const struct iw_ioctl_description standard_ioctl_descr[] = {
	[SIOCSIWCOMMIT	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_NULL,
	},
	[SIOCGIWNAME	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_CHAR,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[SIOCSIWNWID	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
		.flags		= IW_DESCR_FLAG_EVENT,
	},
	[SIOCGIWNWID	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[SIOCSIWFREQ	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_FREQ,
		.flags		= IW_DESCR_FLAG_EVENT,
	},
	[SIOCGIWFREQ	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_FREQ,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[SIOCSIWMODE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_UINT,
		.flags		= IW_DESCR_FLAG_EVENT,
	},
	[SIOCGIWMODE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_UINT,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[SIOCSIWSENS	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCGIWSENS	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCSIWRANGE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_NULL,
	},
	[SIOCGIWRANGE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= sizeof(struct iw_range),
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[SIOCSIWPRIV	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_NULL,
	},
	[SIOCGIWPRIV	- SIOCIWFIRST] = { /* (handled directly by us) */
		.header_type	= IW_HEADER_TYPE_NULL,
	},
	[SIOCSIWSTATS	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_NULL,
	},
	[SIOCGIWSTATS	- SIOCIWFIRST] = { /* (handled directly by us) */
		.header_type	= IW_HEADER_TYPE_NULL,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[SIOCSIWSPY	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= sizeof(struct sockaddr),
		.max_tokens	= IW_MAX_SPY,
	},
	[SIOCGIWSPY	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= sizeof(struct sockaddr) +
				  sizeof(struct iw_quality),
		.max_tokens	= IW_MAX_SPY,
	},
	[SIOCSIWTHRSPY	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= sizeof(struct iw_thrspy),
		.min_tokens	= 1,
		.max_tokens	= 1,
	},
	[SIOCGIWTHRSPY	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= sizeof(struct iw_thrspy),
		.min_tokens	= 1,
		.max_tokens	= 1,
	},
	[SIOCSIWAP	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_ADDR,
	},
	[SIOCGIWAP	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_ADDR,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[SIOCSIWMLME	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.min_tokens	= sizeof(struct iw_mlme),
		.max_tokens	= sizeof(struct iw_mlme),
	},
	[SIOCGIWAPLIST	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= sizeof(struct sockaddr) +
				  sizeof(struct iw_quality),
		.max_tokens	= IW_MAX_AP,
		.flags		= IW_DESCR_FLAG_NOMAX,
	},
	[SIOCSIWSCAN	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.min_tokens	= 0,
		.max_tokens	= sizeof(struct iw_scan_req),
	},
	[SIOCGIWSCAN	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_SCAN_MAX_DATA,
		.flags		= IW_DESCR_FLAG_NOMAX,
	},
	[SIOCSIWESSID	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_ESSID_MAX_SIZE + 1,
		.flags		= IW_DESCR_FLAG_EVENT,
	},
	[SIOCGIWESSID	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_ESSID_MAX_SIZE + 1,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[SIOCSIWNICKN	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_ESSID_MAX_SIZE + 1,
	},
	[SIOCGIWNICKN	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_ESSID_MAX_SIZE + 1,
	},
	[SIOCSIWRATE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCGIWRATE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCSIWRTS	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCGIWRTS	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCSIWFRAG	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCGIWFRAG	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCSIWTXPOW	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCGIWTXPOW	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCSIWRETRY	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCGIWRETRY	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCSIWENCODE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_ENCODING_TOKEN_MAX,
		.flags		= IW_DESCR_FLAG_EVENT | IW_DESCR_FLAG_RESTRICT,
	},
	[SIOCGIWENCODE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_ENCODING_TOKEN_MAX,
		.flags		= IW_DESCR_FLAG_DUMP | IW_DESCR_FLAG_RESTRICT,
	},
	[SIOCSIWPOWER	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCGIWPOWER	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
#ifdef SIOCSIWMODUL
	[SIOCSIWMODUL	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
#endif
#ifdef SIOCGIWMODUL
	[SIOCGIWMODUL	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
#endif
	[SIOCSIWGENIE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_GENERIC_IE_MAX,
	},
	[SIOCGIWGENIE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_GENERIC_IE_MAX,
	},
	[SIOCSIWAUTH	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCGIWAUTH	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCSIWENCODEEXT - SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.min_tokens	= sizeof(struct iw_encode_ext),
		.max_tokens	= sizeof(struct iw_encode_ext) +
				  IW_ENCODING_TOKEN_MAX,
	},
	[SIOCGIWENCODEEXT - SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.min_tokens	= sizeof(struct iw_encode_ext),
		.max_tokens	= sizeof(struct iw_encode_ext) +
				  IW_ENCODING_TOKEN_MAX,
	},
	[SIOCSIWPMKSA - SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.min_tokens	= sizeof(struct iw_pmksa),
		.max_tokens	= sizeof(struct iw_pmksa),
	},
};

static const struct iw_ioctl_description standard_event_descr[] = {
	[IWEVTXDROP - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_ADDR,
	},
	[IWEVQUAL - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_QUAL,
	},
	[IWEVCUSTOM - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_CUSTOM_MAX,
	},
	[IWEVREGISTERED - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_ADDR,
	},
	[IWEVEXPIRED - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_ADDR,
	},
	[IWEVGENIE - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_GENERIC_IE_MAX,
	},
	[IWEVMICHAELMICFAILURE - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= sizeof(struct iw_michaelmicfailure),
	},
	[IWEVASSOCREQIE - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_GENERIC_IE_MAX,
	},
	[IWEVASSOCRESPIE - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_GENERIC_IE_MAX,
	},
	[IWEVPMKIDCAND - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= sizeof(struct iw_pmkid_cand),
	},
};

struct stream_descr {
	char *current;		/* Current event in stream of events */
	char *value;		/* Current value in event */
	char *end;		/* End of the stream */
};

/*
 * Extract the next event from the event stream.
 */
static int iw_extract_event_stream(struct stream_descr *stream,
				   struct iw_event *iwe, int we_version)
{
	const struct iw_ioctl_description *descr = NULL;
	int event_type;
	unsigned int event_len = 1;	/* Invalid */
	unsigned cmd_index;	/* *MUST* be unsigned */
	char *pointer;

	if (stream->current + IW_EV_LCP_PK_LEN > stream->end)
		return 0;

	/* Extract the event header to get the event id.
	 * Note : the event may be unaligned, therefore copy... */
	memcpy((char *)iwe, stream->current, IW_EV_LCP_PK_LEN);

	if (iwe->len <= IW_EV_LCP_PK_LEN)
		return -1;

	/* Get the type and length of that event */
	if (iwe->cmd <= SIOCIWLAST) {
		cmd_index = iwe->cmd - SIOCIWFIRST;
		if (cmd_index < ARRAY_SIZE(standard_ioctl_descr))
			descr = standard_ioctl_descr + cmd_index;
	} else {
		cmd_index = iwe->cmd - IWEVFIRST;
		if (cmd_index < ARRAY_SIZE(standard_event_descr))
			descr = standard_event_descr + cmd_index;
	}

	/* Unknown events -> event_type = 0  =>  IW_EV_LCP_PK_LEN */
	event_type = descr ? descr->header_type : 0;
	event_len  = event_type_size[event_type];

	/* Check if we know about this event */
	if (event_len <= IW_EV_LCP_PK_LEN) {
		stream->current += iwe->len;			/* Skip to next event */
		return 2;
	}
	event_len -= IW_EV_LCP_PK_LEN;

	/* Fixup for earlier version of WE */
	if (we_version <= 18 && event_type == IW_HEADER_TYPE_POINT)
		event_len += IW_EV_POINT_OFF;

	if (stream->value != NULL)
		pointer = stream->value;			/* Next value in event */
	else
		pointer = stream->current + IW_EV_LCP_PK_LEN;	/* First value in event */

	/* Copy the rest of the event (at least, fixed part) */
	if (pointer + event_len > stream->end) {
		stream->current += iwe->len;			/* Skip to next event */
		return -2;
	}

	/* Fixup for WE-19 and later: pointer no longer in the stream */
	/* Beware of alignment. Dest has local alignment, not packed */
	if (we_version > 18 && event_type == IW_HEADER_TYPE_POINT)
		memcpy((char *)iwe + IW_EV_LCP_LEN + IW_EV_POINT_OFF, pointer, event_len);
	else
		memcpy((char *)iwe + IW_EV_LCP_LEN, pointer, event_len);

	/* Skip event in the stream */
	pointer += event_len;

	/* Special processing for iw_point events */
	if (event_type == IW_HEADER_TYPE_POINT) {
		unsigned int extra_len = iwe->len - (event_len + IW_EV_LCP_PK_LEN);

		if (extra_len > 0) {
			/* Set pointer on variable part (warning : non aligned) */
			iwe->u.data.pointer = pointer;

			/* Check that we have a descriptor for the command */
			if (descr == NULL) {
				/* Can't check payload -> unsafe... */
				iwe->u.data.pointer = NULL;	/* Discard paylod */
			} else {
				unsigned int token_len = iwe->u.data.length * descr->token_size;
				/*
				 * Ugly fixup for alignment issues.
				 * If the kernel is 64 bits and userspace 32 bits, we have an extra 4 + 4
				 * bytes. Fixing that in the kernel would break 64 bits userspace.
				 */
				if (token_len != extra_len && extra_len >= 4) {
					union iw_align_u16 {
						__u16 value;
						unsigned char byte[2];
					} alt_dlen;
					unsigned int alt_token_len;

					/* Userspace seems to not always like unaligned access,
					 * so be careful and make sure to align value.
					 * I hope gcc won't play any of its aliasing tricks... */
					alt_dlen.byte[0] = *(pointer);
					alt_dlen.byte[1] = *(pointer + 1);
					alt_token_len = alt_dlen.value * descr->token_size;

					/* Verify that data is consistent if assuming 64 bit alignment... */
					if (alt_token_len + 8 == extra_len) {

						/* Ok, let's redo everything */
						pointer -= event_len;
						pointer += 4;

						/* Dest has local alignment, not packed */
						memcpy((char *)iwe + IW_EV_LCP_LEN + IW_EV_POINT_OFF, pointer, event_len);
						pointer += event_len + 4;
						token_len = alt_token_len;

						/* We may have no payload */
						if (alt_token_len)
							iwe->u.data.pointer = pointer;
						else
							iwe->u.data.pointer = NULL;
					}
				}

				/* Discard bogus events which advertise more tokens than they carry ... */
				if (token_len > extra_len)
					iwe->u.data.pointer = NULL;	/* Discard paylod */

				/* Check that the advertised token size is not going to
				 * produce buffer overflow to our caller... */
				if (iwe->u.data.length > descr->max_tokens
				    && !(descr->flags & IW_DESCR_FLAG_NOMAX))
					iwe->u.data.pointer = NULL;	/* Discard payload */

				/* Same for underflows... */
				if (iwe->u.data.length < descr->min_tokens)
					iwe->u.data.pointer = NULL;	/* Discard paylod */
			}
		} else {
			/* No data */
			iwe->u.data.pointer = NULL;
		}

		stream->current += iwe->len;			/* Go to next event */
	} else {
		/*
		 * Ugly fixup for alignment issues.
		 * If the kernel is 64 bits and userspace 32 bits, we have an extra 4 bytes.
		 * Fixing that in the kernel would break 64 bits userspace.
		 */
		if (stream->value == NULL &&
		    ((iwe->len - IW_EV_LCP_PK_LEN) % event_len == 4 ||
		     (iwe->len == 12 && (event_type == IW_HEADER_TYPE_UINT ||
					 event_type == IW_HEADER_TYPE_QUAL)))) {

			pointer -= event_len;
			pointer += 4;

			/* Beware of alignment. Dest has local alignment, not packed */
			memcpy((char *)iwe + IW_EV_LCP_LEN, pointer, event_len);
			pointer += event_len;
		}

		if (pointer + event_len <= stream->current + iwe->len) {
			stream->value = pointer;		/* Go to next value */
		} else {
			stream->value = NULL;
			stream->current += iwe->len;		/* Go to next event */
		}
	}
	return 1;
}
/*----------------- End of code copied from iwlib -----------------------*/

/*
 *	Scan results are represented as a ranked list
 */
struct scan_result {
	struct ether_addr	ap_addr;
	char			essid[IW_ESSID_MAX_SIZE + 2];
	double			freq;		/* Frequency/channel */
	struct	iw_quality	qual;

	int			mode;		/* Operation mode */
	int 			has_key:1;

	struct scan_result *next;
};

/* Return >= 0 if a's signal >= b's signal, < 0 otherwise */
static int cmp_scan_sig(struct scan_result *a, struct scan_result *b)
{
	return a->qual.level - b->qual.level;
}

static void free_scan_result(struct scan_result *head)
{
	if (head) {
		free_scan_result(head->next);
		free(head);
	}
}

static struct scan_result *get_scan_list(int skfd, char *ifname, int we_version)
{
	struct scan_result *head = NULL;
	struct iwreq wrq;
	int wait, waited = 0;
	/*
	 * Some drivers may return very large scan results, either because there
	 * are many cells, or there are many large elements. Do not bother to
	 * guess buffer size, use maximum u16 wrq.u.data.length size.
	 */
	char scan_buf[0xffff];

	memset(&wrq, 0, sizeof(wrq));
	strncpy(wrq.ifr_ifrn.ifrn_name, ifname, IFNAMSIZ);
	if (ioctl(skfd, SIOCSIWSCAN, &wrq) < 0)
		return NULL;

	/* Larger initial timeout of 250ms between set and first get */
	for (wait = 250; (waited += wait) < MAX_SCAN_WAIT; wait = 100) {
		struct timeval tv = { 0, wait * 1000 };

		while (select(0, NULL, NULL, NULL, &tv) < 0)
			if (errno != EINTR && errno != EAGAIN)
				return NULL;

		wrq.u.data.pointer = scan_buf;
		wrq.u.data.length  = sizeof(scan_buf);
		wrq.u.data.flags   = 0;

		if (ioctl(skfd, SIOCGIWSCAN, &wrq) == 0)
			break;
	}

	if (wrq.u.data.length) {
		struct iw_event iwe;
		struct stream_descr stream;
		struct scan_result *new = NULL;
		int f = 0;		/* Idea taken from waproamd */

		memset(&stream, 0, sizeof(stream));
		stream.current = scan_buf;
		stream.end     = scan_buf + wrq.u.data.length;

		while (iw_extract_event_stream(&stream, &iwe, we_version) > 0) {
        		if (!new)
				new = calloc(1, sizeof(*new));

			switch (iwe.cmd) {
			case SIOCGIWAP:
                		f = 1;
				memcpy(&new->ap_addr, &iwe.u.ap_addr.sa_data, sizeof(new->ap_addr));
				break;
			case SIOCGIWESSID:
                		f |= 2;
				memset(new->essid, 0, sizeof(new->essid));

				if (iwe.u.essid.flags && iwe.u.essid.pointer && iwe.u.essid.length)
					memcpy(new->essid, iwe.u.essid.pointer, iwe.u.essid.length);
				break;
			case SIOCGIWMODE:
				new->mode = iwe.u.mode;
                    		f |= 4;
				break;
			case SIOCGIWFREQ:
                		f |= 8;
				new->freq = freq_to_hz(&iwe.u.freq);
				break;
			case SIOCGIWENCODE:
                		f |= 16;
				new->has_key = !(iwe.u.data.flags & IW_ENCODE_DISABLED);
				break;
			case IWEVQUAL:
				f |= 32;
				memcpy(&new->qual, &iwe.u.qual, sizeof(struct iw_quality));
				break;
			}
        		if (f == 63) {
				struct scan_result *cur = head, **prev = &head;

				f = 0;

				/* Sort in descending order of signal strength */
				while (cur && cmp_scan_sig(cur, new) > 0)
					prev = &cur->next, cur = cur->next;

				*prev     = new;
				new->next = cur;
				new       = NULL;
			}
		}
		free(new);	/* may have been allocated but not filled in */
	}
	return head;
}

static char *fmt_scan_result(struct scan_result *cur, struct iw_range *iw_range,
			     char buf[], size_t buflen)
{
	struct iw_levelstat dbm;
	size_t len = 0;
	int channel = freq_to_channel(cur->freq, iw_range);

	iw_sanitize(iw_range, &cur->qual, &dbm);

	if (!(cur->qual.updated & (IW_QUAL_QUAL_INVALID|IW_QUAL_LEVEL_INVALID)))
		len += snprintf(buf + len, buflen - len, "%2d/%d, %.0f dBm",
				cur->qual.qual, iw_range->max_qual.qual,
				dbm.signal);
	else if (!(cur->qual.updated & IW_QUAL_QUAL_INVALID))
		len += snprintf(buf + len, buflen - len, "%2d/%d",
				cur->qual.qual, iw_range->max_qual.qual);
	else if (!(cur->qual.updated & IW_QUAL_LEVEL_INVALID))
		len += snprintf(buf + len, buflen - len, "%.0f dBm",
				dbm.signal);
	else
		len += snprintf(buf + len, buflen - len, "? dBm");


	if (cur->freq < 1e3)
		len += snprintf(buf + len, buflen - len, ", Chan %2.0f",
				cur->freq);
	else if (channel >= 0)
		len += snprintf(buf + len, buflen - len, ", Ch %2d, %g MHz",
				channel, cur->freq / 1e6);
	else
		len += snprintf(buf + len, buflen - len, ", %g GHz",
				cur->freq / 1e9);

	/* Access Points are encoded in red/green already */
	if (cur->mode != IW_MODE_MASTER)
		len += snprintf(buf + len, buflen - len, " %s",
				iw_opmode(cur->mode));
	return buf;
}

static void display_aplist(WINDOW *w_aplst)
{
	char s[0x100];
	int i, line = START_LINE;
	struct iw_range range;
	struct scan_result *head, *cur;
	int skfd = socket(AF_INET, SOCK_DGRAM, 0);

	if (skfd < 0)
		err_sys("%s: can not open socket", __func__);

	iw_getinf_range(conf.ifname, &range);
	if (range.we_version_compiled < 14) {
		mvwclrtoborder(w_aplst, START_LINE, 1);
		sprintf(s, "%s does not support scanning", conf.ifname);
		waddstr_center(w_aplst, WAV_HEIGHT/2 - 1, s);
		goto done;
	}

	head = get_scan_list(skfd, conf.ifname, range.we_version_compiled);
	if (head) {
		;
	} else if (errno == EPERM) {
		/*
		 * Don't try to read leftover results, it does not work reliably
		 */
		sprintf(s, "This screen requires CAP_NET_ADMIN permissions");
	} else if (errno == EINTR || errno == EAGAIN || errno == EBUSY) {
		/* Ignore temporary errors */
		goto done;
	} else if (!if_is_up(conf.ifname)) {
		sprintf(s, "Interface '%s' is down - can not scan", conf.ifname);
	} else if (errno) {
		sprintf(s, "No scan on %s: %s", conf.ifname, strerror(errno));
	} else {
		sprintf(s, "No scan results on %s", conf.ifname);
	}

	for (i = 1; i <= MAXYLEN; i++)
		mvwclrtoborder(w_aplst, i, 1);

	if (!head)
		waddstr_center(w_aplst, WAV_HEIGHT/2 - 1, s);

	/* Truncate overly long access point lists to match screen height */
	for (cur = head; cur && line < MAXYLEN; line++, cur = cur->next) {
		int col = CP_SCAN_NON_AP;

		if (cur->mode == IW_MODE_MASTER)
			col = cur->has_key ? CP_SCAN_CRYPT : CP_SCAN_UNENC;

		wattron(w_aplst, COLOR_PAIR(col));
		mvwaddstr(w_aplst, line++, 1, " ");
		if (!*cur->essid || str_is_ascii(cur->essid))
			snprintf(s, sizeof(s), " \"%s\" ", cur->essid);
		else
			snprintf(s, sizeof(s), " <cryptic ESSID> ");
		waddstr_b(w_aplst, s);
		waddstr(w_aplst, ether_addr(&cur->ap_addr));
		wattroff(w_aplst, COLOR_PAIR(col));

		fmt_scan_result(cur, &range, s, sizeof(s));
		mvwaddstr(w_aplst, line, 1, "    ");
		waddstr_b(w_aplst, s);
	}
	free_scan_result(head);
done:
	close(skfd);
	wrefresh(w_aplst);
}

enum wavemon_screen scr_aplst(WINDOW *w_menu)
{
	WINDOW *w_aplst;
	struct timer t1;
	int key = 0;

	w_aplst = newwin_title(0, WAV_HEIGHT, "Scan window", false);

	/* Refreshing scan data can take seconds. Inform user. */
	mvwaddstr(w_aplst, START_LINE, 1, "Waiting for scan data ...");
	wrefresh(w_aplst);

	while (key < KEY_F(1) || key > KEY_F(10)) {
		display_aplist(w_aplst);

		start_timer(&t1, conf.info_iv * 1000000);
		while (!end_timer(&t1) && (key = wgetch(w_menu)) <= 0)
			usleep(5000);

		/* Keyboard shortcuts */
		if (key == 'q')
			key = KEY_F(10);
		else if (key == 'i')
			key = KEY_F(1);
	}

	delwin(w_aplst);

	return key - KEY_F(1);
}
