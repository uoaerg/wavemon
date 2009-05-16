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
	char s[0x100];
	int i, j, line = 2;
	struct iw_quality *qual, qual_pivot;
	struct sockaddr *hwa, hwa_pivot;
	struct iw_range range;
	struct iw_levelstat dbm;
	struct iwreq iwr;
	int skfd = socket(AF_INET, SOCK_DGRAM, 0);

	if (skfd < 0)
		fatal_error("could not open socket");

	iw_getinf_range(conf.ifname, &range);
	for (i = 1; i <= MAXYLEN; i++)
		mvwclrtoborder(w_aplst, i, 1);

	strncpy(iwr.ifr_name, conf.ifname, IFNAMSIZ);
	iwr.u.data.pointer = (caddr_t) buf;
	iwr.u.data.length  = IW_MAX_AP;
	iwr.u.data.flags   = 0;

	if (ioctl(skfd, SIOCGIWAPLIST, &iwr) < 0) {
		sprintf(s, "%s does not have a list of peers/access points",
			   conf.ifname);
		waddstr_center(w_aplst, WAV_HEIGHT/2 - 1, s);
		goto done;
	}

	if (iwr.u.data.length == 0) {
		sprintf(s, "%s: no peer/access point in range", conf.ifname);
		waddstr_center(w_aplst, WAV_HEIGHT/2 - 1, s);
	} else if (iwr.u.data.length == 1) {
		mvwaddstr(w_aplst, line, 1, "Peer/access point:");
	} else {
		sprintf(s, "%d peers/access points in range:", iwr.u.data.length);
		mvwaddstr(w_aplst, line, 1, s);
	}

	hwa  = (struct sockaddr *)   buf;
	qual = (struct iw_quality *) (hwa + iwr.u.data.length);

	/*
	 * Show access points in descending order of signal quality by
	 * sorting both lists in parallel, using simple insertion sort.
	 */
	for (i = 0; i < iwr.u.data.length; i++) {

		qual_pivot = qual[i];
		hwa_pivot  = hwa[i];

		for (j = i; j > 0 && qual[j-1].qual < qual_pivot.qual; j--) {
			qual[j] = qual[j-1];
			hwa[j]  = hwa[j-1];
		}

		qual[j] = qual_pivot;
		hwa[j]  = hwa_pivot;
	}

	for (i = 0, line += 2; i < iwr.u.data.length; i++, line++) {

		mvwaddstr(w_aplst, line++, 1, "  ");
		waddstr_b(w_aplst, mac_addr(&hwa[i]));

		if (iwr.u.data.flags) {
			iw_sanitize(&range, &qual[i], &dbm);
			sprintf(s, "Quality%c %2d/%d, Signal%c %.0f dBm (%s), Noise%c %.0f dBm",
				qual[i].updated & IW_QUAL_QUAL_UPDATED ? ':' : '=',
				qual[i].qual, range.max_qual.qual,
				qual[i].updated & IW_QUAL_LEVEL_UPDATED ? ':' : '=',
				dbm.signal, dbm2units(dbm.signal),
				qual[i].updated & IW_QUAL_NOISE_UPDATED ? ':' : '=',
				dbm.noise);
			mvwaddstr(w_aplst, line++, 5, s);
		}
	}
done:
	close(skfd);
	wrefresh(w_aplst);
}

enum wavemon_screen scr_aplst(WINDOW *w_menu)
{
	WINDOW *w_aplst;
	struct timer t1;
	int key = 0;

	w_aplst = newwin_title(0, WAV_HEIGHT, "Access point list", false);

	while (key < KEY_F(1) || key > KEY_F(10)) {
		display_aplist(w_aplst);
		start_timer(&t1, 50000);
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
