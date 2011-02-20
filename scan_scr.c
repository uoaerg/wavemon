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
		len += snprintf(buf + len, buflen - len, "%.0f%%, %.0f dBm",
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
	else if (channel >= 0)
		len += snprintf(buf + len, buflen - len, ", Ch %2d, %g MHz",
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
	char s[0x100];
	int i, line = START_LINE;
	struct iw_range range;
	struct scan_result *head, *cur;
	int skfd = socket(AF_INET, SOCK_DGRAM, 0);

	if (skfd < 0)
		err_sys("%s: can not open socket", __func__);

	iw_getinf_range(if_list[conf.if_idx], &range);

	head = get_scan_list(skfd, if_list[conf.if_idx], range.we_version_compiled);
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
	} else if (!if_is_up(skfd, if_list[conf.if_idx])) {
		sprintf(s, "Interface '%s' is down ", if_list[conf.if_idx]);
		if (!has_net_admin_capability())
			strcat(s, "- can not scan");
		else if (if_set_up(skfd, if_list[conf.if_idx]) < 0)
			sprintf(s, "Can not bring up '%s' for scanning: %s",
				if_list[conf.if_idx], strerror(errno));
		else
			strcat(s, "- setting it up ...");
	} else if (errno) {
		sprintf(s, "No scan on %s: %s", if_list[conf.if_idx], strerror(errno));
	} else {
		sprintf(s, "No scan results on %s", if_list[conf.if_idx]);
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

		waddstr(w_aplst, ether_addr(&cur->ap_addr));
		waddstr_b(w_aplst, "  ");

		if (!*cur->essid) {
			waddstr(w_aplst, "<hidden ESSID>");
		} else if (str_is_ascii(cur->essid)) {
			wattroff(w_aplst, COLOR_PAIR(col));
			waddstr_b(w_aplst, cur->essid);
			wattron(w_aplst, COLOR_PAIR(col));
		} else {
			waddstr(w_aplst, "<cryptic ESSID>");
		}
		wattroff(w_aplst, COLOR_PAIR(col));

		fmt_scan_result(cur, &range, s, sizeof(s));
		mvwaddstr(w_aplst, line, 1, "   ");
		waddstr(w_aplst, s);
	}
	free_scan_result(head);
done:
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
		while (usleep(200000) == 0);
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
