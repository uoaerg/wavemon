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

/* GLOBALS */
static struct scan_result sr;
static pthread_t scan_thread;
static WINDOW *w_aplst;

#define SIGNAL_LEN_MAX 11
#define CHANNEL_LEN_MAX 8
#define CU_LEN_MAX 10
#define DASH_STR "-"
#define PLACE_HOLDER_STR " "
#define UNSUPPORTED_STR "?"

/**
 * Check entry against current active filter.
 * @cur: entry to filter
 * @ret: return true if filter hit
 */
static int check_filter_hit(struct scan_entry *cur)
{
	// Hide entry with hidden essid
	if (conf.scan_show_hidden == false && !*cur->essid)
		return true;

	// Hide unwanted band
	if (conf.scan_filter_band == SCAN_FILTER_BAND_2G && cur->freq > 2500)
		return true;
	else if (conf.scan_filter_band == SCAN_FILTER_BAND_5G && cur->freq < 2500)
		return true;

	return false;
}

/**
 * Sanitize and format single scan entry as a string.
 * @cur: entry to format
 * @buf: buffer to put results into
 * @buflen: length of @buf
 */
static void fmt_scan_entry(struct scan_entry *cur, char buf[], size_t buflen)
{
	size_t len = 0;
	size_t pr_len = 0;

	if (cur->bss_signal) {
		float sig_qual, sig_qual_max;

		if (cur->bss_signal_qual) {
			/* BSS_SIGNAL_UNSPEC is scaled 0..100 */
			sig_qual     = cur->bss_signal_qual;
			sig_qual_max = 100;
		} else {
			if (cur->bss_signal < -110)
				sig_qual = 0;
			else if (cur->bss_signal > -40)
				sig_qual = 70;
			else
				sig_qual = cur->bss_signal + 110;
			sig_qual_max = 70;
		}
		pr_len = snprintf(buf + len, buflen - len, "%3.0f",
				(1E2 * sig_qual)/sig_qual_max);
		len += pr_len;
		len += snprintf(buf + len, buflen - len, "%-*s",
				(int)(SIGNAL_LEN_MAX+1-pr_len), PLACE_HOLDER_STR);

		len += snprintf(buf + len, buflen - len, "%3d%-*s ",
				cur->bss_signal, SIGNAL_LEN_MAX-3, PLACE_HOLDER_STR); // Expect 3 chars for signal
	} else if (cur->bss_signal_qual) {
		len += snprintf(buf + len, buflen - len, "%2d/%d%-*s ",
				cur->bss_signal_qual, 100, SIGNAL_LEN_MAX*2, PLACE_HOLDER_STR);
	} else {
		len += snprintf(buf + len, buflen - len, "%-*s %-*s ",
				SIGNAL_LEN_MAX, "?", SIGNAL_LEN_MAX, "?");
	}

	if (cur->chan >= 0) {
		len += snprintf(buf + len, buflen - len, "%3d%-*s ",
				cur->chan, CHANNEL_LEN_MAX-3, PLACE_HOLDER_STR);
		len += snprintf(buf + len, buflen - len, "%d %-*s ",
				cur->freq, CHANNEL_LEN_MAX-5, "Mhz");
	} else {
		len += snprintf(buf + len, buflen - len, "%-*s ",
				CHANNEL_LEN_MAX, PLACE_HOLDER_STR);
		len += snprintf(buf + len, buflen - len, "%g %-*s ",
				cur->freq / 1e3, CHANNEL_LEN_MAX-4, "GHz");
	}

	// Have bss load stats
	if (cur->bss_capa & WLAN_CAPABILITY_ESS) {
		if (cur->bss_chan_usage > 2) {/* 1% is 2.55 */
			pr_len = snprintf(buf + len, buflen - len, "%3.0f",
					(1e2 * cur->bss_chan_usage)/2.55e2);
			len += pr_len;
			len += snprintf(buf + len, buflen - len, "%-*s",
					(int)(CU_LEN_MAX+1-pr_len), PLACE_HOLDER_STR);
		} else {
			len += snprintf(buf + len, buflen - len, "  %-*s ",
					CU_LEN_MAX-2, DASH_STR);
		}

		if (cur->bss_sta_count > 0) {
			pr_len = snprintf(buf + len, buflen - len, "%u",
					cur->bss_sta_count);
			len += pr_len;
			len += snprintf(buf + len, buflen - len, "%-*s",
					(int)(CHANNEL_LEN_MAX+1-pr_len), PLACE_HOLDER_STR);
		} else {
			len += snprintf(buf + len, buflen - len, "%-*s",
					CHANNEL_LEN_MAX+1, DASH_STR);
		}
	} else {
		len += snprintf(buf + len, buflen - len, "  %-*s   %-*s ",
				CU_LEN_MAX-2, UNSUPPORTED_STR, CHANNEL_LEN_MAX-2, UNSUPPORTED_STR);
	}

	// Capabilities
	if (cur->bss_capa & WLAN_CAPABILITY_ESS) {
		len += snprintf(buf + len, buflen - len, "ESS ");
		if (cur->bss_capa & WLAN_CAPABILITY_RADIO_MEASURE)
			len += snprintf(buf + len, buflen - len, "+Radio Meas ");
		if (cur->bss_capa & WLAN_CAPABILITY_SPECTRUM_MGMT)
			len += snprintf(buf + len, buflen - len, "+Spectrum Mgmt ");
	} else if (cur->bss_capa & WLAN_CAPABILITY_IBSS) {
		len += snprintf(buf + len, buflen - len, "IBSS ");
	}

	if (cur->ext_capa[2] & 0x08) // bss transtion
		len += snprintf(buf + len, buflen - len, "+BSS Trans ");
}

static void display_aplist(WINDOW *w_aplst)
{
	char s[IW_ESSID_MAX_SIZE << 3];
	const char *sort_type[] = {
		[SO_CHAN]	= "Chan",
		[SO_SIGNAL]	= "Sig",
		[SO_MAC]	= "Mac",
		[SO_ESSID]	= "Essid",
		[SO_OPEN]	= "Open",
		[SO_CHAN_SIG]	= "Ch/Sg",
		[SO_OPEN_SIG]	= "Op/Sg"
	};
	int i, col, line = START_LINE;
	struct scan_entry *cur;

	/* Scanning can take several seconds - do not refresh if locked. */
	if (pthread_mutex_trylock(&sr.mutex))
		return;

	if (sr.head || *sr.msg)
		for (i = 1; i <= MAXYLEN; i++)
			mvwclrtoborder(w_aplst, i, 1);

	if (!sr.head)
		waddstr_center(w_aplst, WAV_HEIGHT/2 - 1, sr.msg);

	sort_scan_list(&sr.head);

	/* Print header (when scan result ready) */
	if (sr.num.entries != 0) {
		wmove(w_aplst, line, 1); // move cursor to start line
		int len = 0;
		len += snprintf(s + len, sizeof(s) - len, "%-*s", sr.max_essid_len+1, "SSID");
		len += snprintf(s + len, sizeof(s) - len, "%-*s", MAC_ADDR_MAX+1, "BSSID");
		len += snprintf(s + len, sizeof(s) - len, "%-*s", SIGNAL_LEN_MAX+1, "Signal(%)");
		len += snprintf(s + len, sizeof(s) - len, "%-*s", SIGNAL_LEN_MAX+1, "Signal(dBm)");
		len += snprintf(s + len, sizeof(s) - len, "%-*s", CHANNEL_LEN_MAX+1, "Channel");
		len += snprintf(s + len, sizeof(s) - len, "%-*s", CHANNEL_LEN_MAX+1, "Freq");
		len += snprintf(s + len, sizeof(s) - len, "%-*s", CU_LEN_MAX+1, "ChUtil(%)");
		len += snprintf(s + len, sizeof(s) - len, "%-*s", CHANNEL_LEN_MAX+1, "Station");
		len += snprintf(s + len, sizeof(s) - len, "%-*s", MAC_ADDR_MAX+1, "Capability");

		wattron(w_aplst, COLOR_PAIR(CP_SCAN_NON_AP));
		wadd_attr_str(w_aplst, A_BOLD, s);
		wattroff(w_aplst, COLOR_PAIR(CP_SCAN_NON_AP));
		line++;
	}

	/* Truncate overly long access point lists to match screen height. */
	for (cur = sr.head; cur && line < MAXYLEN; cur = cur->next) {
		col = CP_SCAN_NON_AP;

		if (!WLAN_CAPABILITY_IS_STA_BSS(cur->bss_capa) && (cur->bss_capa & WLAN_CAPABILITY_ESS)) {
			col = cur->has_key ? CP_SCAN_CRYPT : CP_SCAN_UNENC;
		}

		if (check_filter_hit(cur)) continue;

		wmove(w_aplst, line, 1);
		if (!*cur->essid) {
			sprintf(s, "%-*s ", sr.max_essid_len, "<hidden>");
			wattron(w_aplst, COLOR_PAIR(col));
			waddstr(w_aplst, s);
		} else if (str_is_ascii(cur->essid)) {
			sprintf(s, "%-*s ", sr.max_essid_len, cur->essid);
			waddstr_b(w_aplst, s);
			wattron(w_aplst, COLOR_PAIR(col));
		} else {
			sprintf(s, "%-*s ", sr.max_essid_len, "<cryptic>");
			wattron(w_aplst, COLOR_PAIR(col));
			waddstr(w_aplst, s);
		}
		waddstr(w_aplst, ether_addr(&cur->ap_addr));
		waddstr(w_aplst, " ");

		wattroff(w_aplst, COLOR_PAIR(col));

		fmt_scan_entry(cur, s, sizeof(s));
		waddstr(w_aplst, " ");
		waddstr(w_aplst, s);
		line++;
	}

	if (sr.num.entries < MAX_CH_STATS)
		goto done;

	// Show summary at bottom
	wmove(w_aplst, MAXYLEN, 1);
	wadd_attr_str(w_aplst, A_REVERSE, "total:");
	sprintf(s, " %d ", sr.num.entries);
	waddstr(w_aplst, s);

	sprintf(s, "%s %ssc", sort_type[conf.scan_sort_order], conf.scan_sort_asc ? "a" : "de");
	wadd_attr_str(w_aplst, A_REVERSE, s);

	if (sr.num.entries + START_LINE > line) {
		sprintf(s, ", %d not shown", sr.num.entries + START_LINE - line);
		waddstr(w_aplst, s);
	}
	if (sr.num.open) {
		sprintf(s, ", %d open", sr.num.open);
		waddstr(w_aplst, s);
	}

	if (sr.num.two_gig && sr.num.five_gig) {
		waddch(w_aplst, ' ');
		wadd_attr_str(w_aplst, A_REVERSE, "5/2GHz:");
		sprintf(s, " %d/%d", sr.num.five_gig, sr.num.two_gig);
		waddstr(w_aplst, s);
	}

	if (sr.channel_stats) {
		waddch(w_aplst, ' ');
		if (conf.scan_sort_order == SO_CHAN && !conf.scan_sort_asc)
			sprintf(s, "bottom-%d:", (int)sr.num.ch_stats);
		else
			sprintf(s, "top-%d:", (int)sr.num.ch_stats);
		wadd_attr_str(w_aplst, A_REVERSE, s);

		for (i = 0; i < sr.num.ch_stats; i++) {
			waddstr(w_aplst, i ? ", " : " ");
			sprintf(s, "ch#%d", sr.channel_stats[i].val);
			wadd_attr_str(w_aplst, A_BOLD, s);
			sprintf(s, " (%d)", sr.channel_stats[i].count);
			waddstr(w_aplst, s);
		}
	}
done:
	pthread_mutex_unlock(&sr.mutex);
	wrefresh(w_aplst);
}

void scr_aplst_init(void)
{
	w_aplst = newwin_title(0, WAV_HEIGHT, "Scan window", false);

	/* Gathering scan data can take seconds. Inform user. */
	mvwaddstr(w_aplst, START_LINE, 1, "Waiting for scan data ...");
	wrefresh(w_aplst);

	init_scan_list(&sr);
	pthread_create(&scan_thread, NULL, do_scan, &sr);
}

int scr_aplst_loop(WINDOW *w_menu)
{
	int key;

	display_aplist(w_aplst);

	key = wgetch(w_menu);
	switch (key) {
	case '0':	/* All band */
		conf.scan_filter_band = SCAN_FILTER_BAND_BOTH;
		return -1;
	case '2':	/* 2.4G band */
		conf.scan_filter_band = SCAN_FILTER_BAND_2G;
		return -1;
	case '5':	/* 5G band */
		conf.scan_filter_band = SCAN_FILTER_BAND_5G;
		return -1;
	case 'a':	/* ascending */
		conf.scan_sort_asc = true;
		return -1;
	case 'c':	/* channel */
		conf.scan_sort_order = SO_CHAN;
		return -1;
	case 'C':	/* channel and signal */
		conf.scan_sort_order = SO_CHAN_SIG;
		return -1;
	case 'd':	/* descending */
		conf.scan_sort_asc = false;
		return -1;
	case 'e':	/* ESSID */
		conf.scan_sort_order = SO_ESSID;
		return -1;
	case 'h':	/* toggle hidden ssid */
		conf.scan_show_hidden = !conf.scan_show_hidden;
		return -1;
	case 'm':	/* MAC address */
		conf.scan_sort_order = SO_MAC;
		return -1;
	case 'o':	/* open (descending is default) */
		conf.scan_sort_order = SO_OPEN;
		conf.scan_sort_asc = false;
		return -1;
	case 'O':	/* open and signal (descending) */
		conf.scan_sort_order = SO_OPEN_SIG;
		conf.scan_sort_asc = false;
		return -1;
	case 's':	/* signal */
		conf.scan_sort_order = SO_SIGNAL;
		return -1;
	}
	return key;
}

void scr_aplst_fini(void)
{
	pthread_cancel(scan_thread);
	free_scan_list(sr.head);
	free(sr.channel_stats);

	delwin(w_aplst);
}
