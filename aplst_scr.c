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

static void display_aplist(WINDOW *w_aplst)
{
	uint8_t buf[(sizeof(struct iw_quality) +
		     sizeof(struct sockaddr)) * IW_MAX_AP];
	char	s[0x100];
	int	ysize, xsize, i, line = 2;
	struct iw_quality *qual;
	struct iw_range range;
	struct iw_levelstat dbm;
	struct iwreq iwr;
	int skfd = socket(AF_INET, SOCK_DGRAM, 0);

	if (skfd < 0)
		fatal_error("could not open socket");

	iw_getinf_range(conf.ifname, &range);
	getmaxyx(w_aplst, ysize, xsize);
	for (i = 1; i < ysize - 1; i++)
		mvwhline(w_aplst, i, 1, ' ', xsize - 2);

	strncpy(iwr.ifr_name, conf.ifname, IFNAMSIZ);
	iwr.u.data.pointer = (caddr_t) buf;
	iwr.u.data.length  = IW_MAX_AP;
	iwr.u.data.flags   = 0;

	if (ioctl(skfd, SIOCGIWAPLIST, &iwr) < 0) {
		sprintf(s, "%s does not have a list of peers/access points.", conf.ifname);
		waddstr_center(w_aplst, LINES/2 - 1, s);
		goto done;
	}

	if (iwr.u.data.length == 0) {
		waddstr_center(w_aplst, LINES/2 - 1, "No peer/access point in range.");
	} else if (iwr.u.data.length == 1) {
		mvwaddstr(w_aplst, line, 1, "Peer/access point:");
	} else {
		sprintf(s, "%d peers/access points in range:", iwr.u.data.length);
		mvwaddstr(w_aplst, line, 1, s);
	}

	qual = (struct iw_quality *)(buf + sizeof(struct sockaddr) * iwr.u.data.length);

	for (i = 0, line += 2; i < iwr.u.data.length; i++, line++) {

		mvwaddstr(w_aplst, line++, 1, "  ");
		waddstr_b(w_aplst, mac_addr(buf + i * sizeof(struct sockaddr)));

		if (iwr.u.data.flags) {
			iw_sanitize(&range, &qual[i], &dbm);
			sprintf(s, "Quality%c %2d/%d, Signal%c %.0f dBm (%s), Noise%c %.0f dBm",
				qual[i].updated & IW_QUAL_QUAL_UPDATED  ? ':' : '=',
				qual[i].qual, range.max_qual.qual,
				qual[i].updated & IW_QUAL_LEVEL_UPDATED ? ':' : '=',
				dbm.signal,  dbm2units(dbm.signal),
				qual[i].updated & IW_QUAL_NOISE_UPDATED ? ':' : '=', dbm.noise);
			mvwaddstr(w_aplst, line++, 5, s);
		}
	}
done:
	close(skfd);
}

int scr_aplst(void)
{
	WINDOW		*w_aplst, *w_menu;
	struct timer	t1;
	int		key = 0;

	w_aplst = newwin_title(LINES - 1, COLS, 0, 0, "Access point list", 0, 0);
	w_menu = newwin(1, COLS, LINES - 1, 0);

	wmenubar(w_menu, 2);
	wmove(w_menu, 1, 0);
	nodelay(w_menu, TRUE); keypad(w_menu, TRUE);

	wrefresh(w_aplst);
	wrefresh(w_menu);

	while (key < KEY_F(1) || key > KEY_F(10)) {
		display_aplist(w_aplst);
		wrefresh(w_aplst);
		wmove(w_menu, 1, 0);
		wrefresh(w_menu);
		start_timer(&t1, 50000);
		while (!end_timer(&t1) && (key = wgetch(w_menu)) <= 0)
			usleep(5000);

		/* Keyboard shortcuts */
		if (key == 'q')
			key = KEY_F(10);
		else if (key == 'i')
			key = KEY_F(1);
	}

	werase(w_aplst); wrefresh(w_aplst); delwin(w_aplst);
	werase(w_menu); wrefresh(w_menu); delwin(w_menu);

	return key - KEY_F(1);
}
