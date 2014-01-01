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
#define NUMTOP		3	/* maximum number of 'top' statistics entries */

/* GLOBALS */
static struct iw_range range;
static struct scan_result sr;
static WINDOW *w_aplst;
static pid_t pid;
static void (*sig_tstp)(int);

/**
 * Sanitize and format single scan entry as a string.
 * @cur: entry to format
 * @buf: buffer to put results into
 * @buflen: length of @buf
 */
static void fmt_scan_entry(struct scan_entry *cur, char buf[], size_t buflen)
{
	struct iw_levelstat dbm;
	size_t len = 0;
	int channel = freq_to_channel(cur->freq, &range);

	iw_sanitize(&range, &cur->qual, &dbm);

	if (!(cur->qual.updated & (IW_QUAL_QUAL_INVALID|IW_QUAL_LEVEL_INVALID)))
		len += snprintf(buf + len, buflen - len, "%3.0f%%, %.0f dBm",
				1E2 * cur->qual.qual / range.max_qual.qual,
				dbm.signal);
	else if (!(cur->qual.updated & IW_QUAL_QUAL_INVALID))
		len += snprintf(buf + len, buflen - len, "%2d/%d",
				cur->qual.qual, range.max_qual.qual);
	else if (!(cur->qual.updated & IW_QUAL_LEVEL_INVALID))
		len += snprintf(buf + len, buflen - len, "%.0f dBm",
				dbm.signal);
	else
		len += snprintf(buf + len, buflen - len, "? dBm");

	if (cur->freq < 1e3)
		len += snprintf(buf + len, buflen - len, ", Chan %2.0f",
				cur->freq);
	else if (channel >= 0 && cur->freq < 5e9)
		len += snprintf(buf + len, buflen - len, ", ch %2d, %g MHz",
				channel, cur->freq / 1e6);
	else if (channel >= 0)
		len += snprintf(buf + len, buflen - len, ", CH %2d, %g MHz",
				channel, cur->freq / 1e6);
	else
		len += snprintf(buf + len, buflen - len, ", %g GHz",
				cur->freq / 1e9);

	/* Access Points are marked by CP_SCAN_CRYPT/CP_SCAN_UNENC already */
	if (cur->mode != IW_MODE_MASTER)
		len += snprintf(buf + len, buflen - len, " %s",
				iw_opmode(cur->mode));
	if (cur->flags)
		len += snprintf(buf + len, buflen - len, ", %s",
				 format_enc_capab(cur->flags, "/"));
}

static void get_scan_result(void)
{
	struct scan_entry *cur;

	memset(&sr, 0, sizeof(sr));

	sr.head = get_scan_list(conf_ifname(), range.we_version_compiled);
	if (sr.head) {
		sr.num_ch_stats  = NUMTOP;
		sr.channel_stats = channel_stats(sr.head, &range, &(sr.num_ch_stats));
	} else {
		switch(errno) {
		case EPERM:
			/* Don't try to read leftover results, it does not work reliably. */
			if (!has_net_admin_capability())
				snprintf(sr.msg, sizeof(sr.msg),
					 "This screen requires CAP_NET_ADMIN permissions");
			break;
		case EINTR:
		case EBUSY:
		case EAGAIN:
			/* Ignore temporary errors. */
			break;
		case EFAULT:
			/*
			 * EFAULT can occur after a window resizing event and is temporary.
			 * It may also occur when the interface is down, hence we need to
			 * test the interface status first.
			 */
			break;
		case ENETDOWN:
			snprintf(sr.msg, sizeof(sr.msg), "Interface %s is down - setting it up ...", conf_ifname());
			if (if_set_up(conf_ifname()) < 0)
				err_sys("Can not bring up interface '%s'", conf_ifname());
			break;
		case E2BIG:
			/*
			 * This is a driver issue, since already using the largest possible
			 * scan buffer. See comments in iwlist.c of wireless tools.
			 */
			snprintf(sr.msg, sizeof(sr.msg),
				 "No scan on %s: Driver returned too much data", conf_ifname());
			break;
		case 0:
			snprintf(sr.msg, sizeof(sr.msg), "Empty scan results on %s", conf_ifname());
			break;
		default:
			snprintf(sr.msg, sizeof(sr.msg),
				 "Scan failed on %s: %s", conf_ifname(), strerror(errno));
		}
	}

	for (cur = sr.head; cur; cur = cur->next, sr.num_entries++) {
		if (str_is_ascii(cur->essid))
			sr.max_essid_len = clamp(strlen(cur->essid),
						 sr.max_essid_len,
						 IW_ESSID_MAX_SIZE);
		sr.num_open += !cur->has_key;
		if (cur->freq < 1e3)
			;	/* cur->freq is channel number */
		else if (cur->freq < 5e9)
			sr.num_two_gig++;
		else
			sr.num_five_gig++;
	}
}

static void display_aplist(WINDOW *w_aplst)
{
	char s[IW_ESSID_MAX_SIZE << 3];
	int i, col, line = START_LINE;
	struct scan_entry *cur;

	for (i = 1; i <= MAXYLEN; i++)
		mvwclrtoborder(w_aplst, i, 1);

	get_scan_result();
	if (!sr.head)
		waddstr_center(w_aplst, WAV_HEIGHT/2 - 1, sr.msg);

	/* Truncate overly long access point lists to match screen height */
	for (cur = sr.head; cur && line < MAXYLEN; line++, cur = cur->next) {
		col = CP_SCAN_NON_AP;

		if (cur->mode == IW_MODE_MASTER)
			col = cur->has_key ? CP_SCAN_CRYPT : CP_SCAN_UNENC;

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
	}

	if (sr.num_entries < NUMTOP)
		goto done;

	wmove(w_aplst, MAXYLEN, 1);
	wadd_attr_str(w_aplst, A_REVERSE, "total:");
	sprintf(s, " %d", sr.num_entries);
	waddstr(w_aplst, s);

	if (sr.num_entries + START_LINE > line) {
		sprintf(s, " (%d not shown)", sr.num_entries + START_LINE - line);
		waddstr(w_aplst, s);
	}
	if (sr.num_open) {
		sprintf(s, ", %d open", sr.num_open);
		waddstr(w_aplst, s);
	}
	if (sr.num_two_gig && sr.num_five_gig) {
		waddch(w_aplst, ' ');
		wadd_attr_str(w_aplst, A_REVERSE, "5/2GHz:");
		sprintf(s, " %d/%d", sr.num_five_gig, sr.num_two_gig);
		waddstr(w_aplst, s);
	}

	if (sr.channel_stats) {
		waddch(w_aplst, ' ');
		if (conf.scan_sort_order == SO_CHAN_REV)
			sprintf(s, "bottom-%d:", (int)sr.num_ch_stats);
		else
			sprintf(s, "top-%d:", (int)sr.num_ch_stats);
		wadd_attr_str(w_aplst, A_REVERSE, s);

		for (i = 0; i < sr.num_ch_stats; i++) {
			sprintf(s, "%s ch#%d (%d)", i ? "," : "",
				sr.channel_stats[i].val, sr.channel_stats[i].count);
			waddstr(w_aplst, s);
		}
	}
done:
	free_scan_list(sr.head);
	free(sr.channel_stats);
	wrefresh(w_aplst);
}

void scr_aplst_init(void)
{
	w_aplst = newwin_title(0, WAV_HEIGHT, "Scan window", false);
	/*
	 * Both parent and child process write to the terminal, updating
	 * different areas of the screen. Suspending wavemon brings the
	 * terminal state out of order, messing up the screen. The choice
	 * is between a more  complicated (sophisticated) handling of
	 * signals, and to keep it simple by not allowing to suspend.
	 */
	sig_tstp = xsignal(SIGTSTP, SIG_IGN);

	/* Gathering scan data can take seconds. Inform user. */
	mvwaddstr(w_aplst, START_LINE, 1, "Waiting for scan data ...");
	wrefresh(w_aplst);

	iw_getinf_range(conf_ifname(), &range);

	pid = fork();
	if (pid < 0) {
		err_sys("could not fork scan process");
	} else if (pid == 0) {
		do display_aplist(w_aplst);
		while (usleep(conf.stat_iv * 1000) == 0);
		exit(EXIT_SUCCESS);
	}
}

int scr_aplst_loop(WINDOW *w_menu)
{
	return wgetch(w_menu);
}

void scr_aplst_fini(void)
{
	kill(pid, SIGTERM);
	delwin(w_aplst);
	xsignal(SIGTSTP, sig_tstp);
}
