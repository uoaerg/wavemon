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
#define NUMTOP		5	/* maximum number of 'top' statistics entries */

/* GLOBALS */
static WINDOW *w_aplst;
static pid_t pid;
static void (*sig_tstp)(int);

static char *fmt_scan_result(struct scan_result *cur, struct iw_range *iw_range,
			     char buf[], size_t buflen)
{
	struct iw_levelstat dbm;
	size_t len = 0;
	int channel = freq_to_channel(cur->freq, iw_range);

	iw_sanitize(iw_range, &cur->qual, &dbm);

	if (!(cur->qual.updated & (IW_QUAL_QUAL_INVALID|IW_QUAL_LEVEL_INVALID)))
		len += snprintf(buf + len, buflen - len, "%3.0f%%, %.0f dBm",
				1E2 * cur->qual.qual / iw_range->max_qual.qual,
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
	else if (channel >= 0 && cur->freq < 5e9)
		len += snprintf(buf + len, buflen - len, ", ch %2d, %g MHz",
				channel, cur->freq / 1e6);
	else if (channel >= 0)
		len += snprintf(buf + len, buflen - len, ", CH %3d, %g MHz",
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
	return buf;
}

static void display_aplist(WINDOW *w_aplst)
{
	char s[IW_ESSID_MAX_SIZE << 3];
	int max_essid_len = 0;
	int i, line = START_LINE;
	int total = 0, open = 0, tg = 0, fg = 0;
	struct iw_range range;
	struct scan_result *head, *cur;
	struct cnt *stats;
	int max_cnt = NUMTOP;
	int skfd = socket(AF_INET, SOCK_DGRAM, 0);

	if (skfd < 0)
		err_sys("%s: can not open socket", __func__);

	iw_getinf_range(conf_ifname(), &range);

	head = get_scan_list(skfd, conf_ifname(),
			     range.we_version_compiled);
	if (head) {
		;
	} else if (errno == EPERM || !has_net_admin_capability()) {
		/*
		 * Don't try to read leftover results, it does not work reliably
		 */
		sprintf(s, "This screen requires CAP_NET_ADMIN permissions");
	} else if (errno == EINTR || errno == EAGAIN || errno == EBUSY) {
		/* Ignore temporary errors */
		goto done;
	} else if (!if_is_up(skfd, conf_ifname())) {
		sprintf(s, "Interface '%s' is down ", conf_ifname());
		if (!has_net_admin_capability())
			strcat(s, "- can not scan");
		else if (if_set_up(skfd, conf_ifname()) < 0)
			sprintf(s, "Can not bring up '%s' for scanning: %s",
				conf_ifname(), strerror(errno));
		else
			strcat(s, "- setting it up ...");
	} else if (errno == EFAULT) {
		/*
		 * EFAULT can occur after a window resizing event and is temporary.
		 * It may also occur when the interface is down, hence we need to
		 * test the interface status first.
		 */
		goto done;
	} else if (errno == E2BIG) {
		/*
		 * This is a driver issue, since already using the largest possible
		 * scan buffer. See comments in iwlist.c of wireless tools.
		 */
		sprintf(s, "No scan on %s: Driver returned too much data", conf_ifname());
	} else if (errno) {
		sprintf(s, "No scan on %s: %s", conf_ifname(), strerror(errno));
	} else {
		sprintf(s, "No scan results on %s", conf_ifname());
	}

	for (i = 1; i <= MAXYLEN; i++)
		mvwclrtoborder(w_aplst, i, 1);

	if (!head)
		waddstr_center(w_aplst, WAV_HEIGHT/2 - 1, s);

	for (cur = head; cur; cur = cur->next, total++) {
		if (str_is_ascii(cur->essid))
			max_essid_len = clamp(strlen(cur->essid),
					      max_essid_len, IW_ESSID_MAX_SIZE);
		open += ! cur->has_key;
		if (cur->freq < 1e3)
			;	/* cur->freq is channel number */
		else if (cur->freq < 5e9)
			tg++;
		else
			fg++;
	}

	/* Truncate overly long access point lists to match screen height */
	for (cur = head; cur && line < MAXYLEN; line++, cur = cur->next) {
		int col = CP_SCAN_NON_AP;

		if (cur->mode == IW_MODE_MASTER)
			col = cur->has_key ? CP_SCAN_CRYPT : CP_SCAN_UNENC;

		wmove(w_aplst, line, 1);
		if (!*cur->essid) {
			sprintf(s, "%-*s ", max_essid_len, "<hidden ESSID>");
			wattron(w_aplst, COLOR_PAIR(col));
			waddstr(w_aplst, s);
		} else if (str_is_ascii(cur->essid)) {
			sprintf(s, "%-*s ", max_essid_len, cur->essid);
			waddstr_b(w_aplst, s);
			wattron(w_aplst, COLOR_PAIR(col));
		} else {
			sprintf(s, "%-*s ", max_essid_len, "<cryptic ESSID>");
			wattron(w_aplst, COLOR_PAIR(col));
			waddstr(w_aplst, s);
		}
		waddstr(w_aplst, ether_addr(&cur->ap_addr));

		wattroff(w_aplst, COLOR_PAIR(col));

		fmt_scan_result(cur, &range, s, sizeof(s));
		waddstr(w_aplst, " ");
		waddstr(w_aplst, s);
	}

	/* Summary statistics at the bottom. */
	if (total < NUMTOP)
		goto done;

	wmove(w_aplst, MAXYLEN, 1);
	wadd_attr_str(w_aplst, A_REVERSE, "total:");
	sprintf(s, " %d", total);
	waddstr(w_aplst, s);

	if (total + START_LINE > line) {
		sprintf(s, " (%d not shown)", total + START_LINE - line);
		waddstr(w_aplst, s);
	}
	if (open) {
		sprintf(s, ", %d open", open);
		waddstr(w_aplst, s);
	}
	if (tg && fg) {
		waddch(w_aplst, ' ');
		wadd_attr_str(w_aplst, A_REVERSE, "5/2GHz:");
		sprintf(s, " %d/%d", fg, tg);
		waddstr(w_aplst, s);
	}

	stats = channel_stats(head, &range, &max_cnt);
	if (stats) {
		waddch(w_aplst, ' ');
		if (conf.scan_sort_order == SO_CHAN_REV)
			sprintf(s, "bottom-%d:", max_cnt);
		else
			sprintf(s, "top-%d:", max_cnt);
		wadd_attr_str(w_aplst, A_REVERSE, s);

		for (i = 0; i < max_cnt; i++) {
			sprintf(s, "%s CH-%d(%d)", i ? "," : "",
				   stats[i].val, stats[i].count);
			waddstr(w_aplst, s);
		}
	}
	free(stats);
done:
	free_scan_result(head);
	close(skfd);
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
