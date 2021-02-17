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
#include "iw_scan.h"

/* GLOBALS */
static struct scan_result sr = {
	.head          = NULL,
	.channel_stats = NULL,
	.msg[0]        = '\0',
	.mutex         = PTHREAD_MUTEX_INITIALIZER,
};
static pthread_t scan_thread;
static WINDOW *w_aplst;


/**
 * Sanitize and format single scan entry as a string.
 * @cur: entry to format
 * @buf: buffer to put results into
 * @buflen: length of @buf
 */
static void fmt_scan_entry(struct scan_entry *cur, char buf[], size_t buflen)
{
	size_t len = 0;

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
		len += snprintf(buf + len, buflen - len, "%3.0f%%, %d dBm",
				(1E2 * sig_qual)/ sig_qual_max, cur->bss_signal);
	} else if (cur->bss_signal_qual) {
		len += snprintf(buf + len, buflen - len, "%2d/%d",
				cur->bss_signal_qual, 100);
	} else {
		len += snprintf(buf + len, buflen - len, "? dBm");
	}

	if (cur->chan >= 0)
		len += snprintf(buf + len, buflen - len, ", %s %3d, %d MHz",
				cur->freq < 5e6 ? "ch" : "CH",
				cur->chan, cur->freq);
	else
		len += snprintf(buf + len, buflen - len, ", %g GHz",
				cur->freq / 1e3);

	if (cur->bss_capa & WLAN_CAPABILITY_ESS) {
		if (cur->bss_sta_count || cur->bss_chan_usage > 2) {
			if (cur->bss_sta_count)
				len += snprintf(buf + len, buflen - len, " %u sta", cur->bss_sta_count);
			if (cur->bss_chan_usage > 2) /* 1% is 2.55 */
				len += snprintf(buf + len, buflen - len, "%s %.0f%% chan",
						cur->bss_sta_count? "," : "", (1e2 * cur->bss_chan_usage)/2.55e2);
		} else {
			len += snprintf(buf + len, buflen - len, " ESS");
		}
		if (cur->bss_capa & WLAN_CAPABILITY_RADIO_MEASURE)
			len += snprintf(buf + len, buflen - len, ", Radio Measure");
		if (cur->bss_capa & WLAN_CAPABILITY_SPECTRUM_MGMT)
			len += snprintf(buf + len, buflen - len, ", Spectrum Mgmt");
	} else if (cur->bss_capa & WLAN_CAPABILITY_IBSS) {
		len += snprintf(buf + len, buflen - len, " IBSS");
	}
	if (cur->mesh_enabled) {
		len += snprintf(buf + len, buflen - len, ", Mesh");
	}
}

static void display_aplist(WINDOW *w_aplst)
{
	char s[IW_ESSID_MAX_SIZE << 3];
	const char *sort_type[] = {
		[SO_CHAN]	= "Chan",
		[SO_SIGNAL]	= "Sig",
		[SO_MAC]        = "Mac",
		[SO_ESSID]	= "Essid",
		[SO_OPEN]	= "Open",
		[SO_CHAN_SIG]	= "Ch/Sg",
		[SO_OPEN_SIG]	= "Op/Sg"
	};
	int i, col, line = 1;
	struct scan_entry *cur;

	/* Scanning can take several seconds - do not refresh while locked. */
	if (pthread_mutex_trylock(&sr.mutex))
		return;

	if (sr.head || *sr.msg)
		for (i = 1; i <= MAXYLEN; i++)
			mvwclrtoborder(w_aplst, i, 1);

	if (!sr.head)
		waddstr_center(w_aplst, WAV_HEIGHT/2 - 1, sr.msg);

	/* Truncate overly long access point lists to match screen height. */
	for (cur = sr.head; cur && line < MAXYLEN; cur = cur->next) {
		if (!conf.scan_hidden_essids && !*cur->essid)
			continue;

		if (!WLAN_CAPABILITY_IS_STA_BSS(cur->bss_capa) && (cur->bss_capa & WLAN_CAPABILITY_ESS)) {
			col = cur->has_key ? CP_RED : CP_GREEN;
		} else {
			col = CP_YELLOW;
		}

		wmove(w_aplst, line, 1);
		if (!*cur->essid) {
			sprintf(s, "%-*s ", sr.max_essid_len, "<hidden ESSID>");
			wattron(w_aplst, COLOR_PAIR(col));
			waddstr(w_aplst, s);
		} else if (str_is_ascii(cur->essid)) {
			sprintf(s, "%-*s ", sr.max_essid_len, cur->essid);
			waddstr_b(w_aplst, s);
			wattron(w_aplst, COLOR_PAIR(col));
		} else {
			sprintf(s, "%-*s ", sr.max_essid_len, "<cryptic ESSID>");
			wattron(w_aplst, COLOR_PAIR(col));
			waddstr(w_aplst, s);
		}
		waddstr(w_aplst, ether_addr(&cur->ap_addr));

		wattroff(w_aplst, COLOR_PAIR(col));

		fmt_scan_entry(cur, s, sizeof(s));
		waddstr(w_aplst, " ");
		waddstr(w_aplst, s);
		line++;
	}

	if (sr.num.entries < MAX_CH_STATS)
		goto done;

	wmove(w_aplst, MAXYLEN, 1);
	if (conf.scan_filter_band == SCAN_FILTER_BAND_2G) {
		wadd_attr_str(w_aplst, A_REVERSE, "total 2.4G:");
	} else if (conf.scan_filter_band == SCAN_FILTER_BAND_5G) {
		wadd_attr_str(w_aplst, A_REVERSE, "total 5G:");
	} else {
		wadd_attr_str(w_aplst, A_REVERSE, "total:");
	}
	sprintf(s, " %d ", sr.num.entries);
	waddstr(w_aplst, s);

	sprintf(s, "%s %ssc", sort_type[conf.scan_sort_order], conf.scan_sort_asc ? "a" : "de");
	wadd_attr_str(w_aplst, A_REVERSE, s);

	if (line == MAXYLEN && sr.num.entries > line - 1) {
		/* Truncated display truncated. Need to subtract 1 for the status line at the bottom. */
		sprintf(s, ", %d not shown", sr.num.entries - (line - 1));
		waddstr(w_aplst, s);
	}
	if (sr.num.open) {
		sprintf(s, ", %d open", sr.num.open);
		waddstr(w_aplst, s);
	}
	if (sr.num.hidden) {
		sprintf(s, ", %d hidden", sr.num.hidden);
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

		for (size_t i = 0; i < sr.num.ch_stats; i++) {
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
	mvwaddstr(w_aplst, 2, 1, "Waiting for scan data ...");
	wrefresh(w_aplst);

	pthread_create(&scan_thread, NULL, do_scan, &sr);
}

int scr_aplst_loop(WINDOW *w_menu)
{
	int key;

	display_aplist(w_aplst);

	key = wgetch(w_menu);
	switch (key) {
	/*
	 * Filtering
	 */
	case 'b':	/* Both 2.4 and 5 Ghz */
		conf.scan_filter_band = SCAN_FILTER_BAND_BOTH;
		return -1;
	case '2':	/* 2.4 GHz band only */
		conf.scan_filter_band = SCAN_FILTER_BAND_2G;
		return -1;
	case '5':	/* 5 GHz band only */
		conf.scan_filter_band = SCAN_FILTER_BAND_5G;
		return -1;
	case 'h':	/* Toggle inclusion of hidden ESSIDs */
		conf.scan_hidden_essids = !conf.scan_hidden_essids;
		return -1;
	/*
	 * Sort Order
	 */
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
	// Unblock mutex in case it was acquired within scr_aplst_loop. If mutex was not acquired
	// by main thread, it returns EPERM, and we can ignore the error.
	(void)pthread_mutex_unlock(&sr.mutex);
	pthread_cancel(scan_thread);
	pthread_join(scan_thread, NULL);
	delwin(w_aplst);
}
