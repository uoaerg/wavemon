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

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <ncurses.h>

#include "conf.h"
#include "ui.h"
#include "timer.h"
#include "iw_if.h"
#include "lhist_scr.h"

struct wavemon_conf *conf;

WINDOW *w_lhist, *w_key, *w_menu;

void display_lhist(char *ifname, WINDOW *w_lhist)
{
	chtype	ch;
	double	ratio, p, p_fract;
	int	snr,
		y, ysize,
		x, xsize;

	getmaxyx(w_lhist, ysize, xsize);
	--xsize;
	--ysize;

	ratio = (ysize - 1) / (conf->sig_max - (double)conf->sig_min);

	for (x = 1; x < xsize; x++) {

		for (y = 1; y <= ysize; y++)
			mvwaddch(w_lhist, y, xsize - x, ' ');

		snr = iw_stats_cache[x].signal - iw_stats_cache[x].noise;

		if (snr > 0) {
			if (snr < (conf->sig_max - conf->sig_min)) {

				wattrset(w_lhist, COLOR_PAIR(CP_STATSNR));
				for (y = 0; y < snr * ratio; y++)
					mvwaddch(w_lhist, ysize - y, xsize - x, ' ');
				wattroff(w_lhist, COLOR_PAIR(CP_STATSNR));

				wattrset(w_lhist, COLOR_PAIR(CP_STATBKG));
				for (; y < ysize; y++)
					mvwaddch(w_lhist, ysize - y, xsize - x, y % 5 ? ' ' : '-');
				wattroff(w_lhist, COLOR_PAIR(CP_STATBKG));

			} else {
				wattrset(w_lhist, COLOR_PAIR(CP_STATSNR));
				for (y = 1; y <= ysize; y++)
					mvwaddch(w_lhist, y, xsize - x, ' ');
				wattroff(w_lhist, COLOR_PAIR(CP_STATSNR));
			}
		} else {
			wattrset(w_lhist, COLOR_PAIR(CP_STATBKG));
			for (y = 1; y < ysize; y++)
				mvwaddch(w_lhist, ysize - y, xsize - x, y % 5 ? ' ' : '-');
			wattroff(w_lhist, COLOR_PAIR(CP_STATBKG));
		}

		if (iw_stats_cache[x].noise >= conf->sig_min &&
		    iw_stats_cache[x].noise <= conf->sig_max) {

			p_fract = modf((iw_stats_cache[x].noise - conf->noise_min) * ratio, &p);
			/*
			 *      the 5 different scanline chars provide a pretty good accuracy.
			 *      ncurses will fall back to standard ASCII chars anyway if they're
			 *      not available.
			 */
			if (p_fract < 0.2)
				ch = ACS_S9;
			else if (p_fract < 0.4)
				ch = ACS_S7;
			else if (p_fract < 0.6)
				ch = ACS_HLINE;
			else if (p_fract < 0.8)
				ch = ACS_S3;
			else
				ch = ACS_S1;
			/* check whether the line goes through the SNR base graph */
			wattrset(w_lhist, p > snr * ratio ? COLOR_PAIR(CP_STATNOISE)
							  : COLOR_PAIR(CP_STATNOISE_S));
			mvwaddch(w_lhist, ysize - (int)p, xsize - x, ch);
		}

		if (iw_stats_cache[x].signal >= conf->sig_min &&
		    iw_stats_cache[x].signal <= conf->sig_max) {

			p_fract = modf((iw_stats_cache[x].signal - conf->sig_min) * ratio, &p);
			if (p_fract < 0.2)
				ch = ACS_S9;
			else if (p_fract < 0.4)
				ch = ACS_S7;
			else if (p_fract < 0.6)
				ch = ACS_HLINE;
			else if (p_fract < 0.8)
				ch = ACS_S3;
			else
				ch = ACS_S1;
			wattrset(w_lhist, p > snr * ratio ? COLOR_PAIR(CP_STATSIG)
							  : COLOR_PAIR(CP_STATSIG_S));
			mvwaddch(w_lhist, ysize - (int)p, xsize - x, ch);
			wattroff(w_lhist, COLOR_PAIR(CP_STATSIG));
		}
	}
}

void display_key(WINDOW *w_key)
{
	char s[0x100];

	wmove(w_key, 1, 1);
	wattrset(w_key, COLOR_PAIR(CP_STANDARD));
	waddch(w_key, '[');
	wattrset(w_key, COLOR_PAIR(CP_STATSIG));
	waddch(w_key, ACS_HLINE);
	wattrset(w_key, COLOR_PAIR(CP_STANDARD));
	sprintf(s, "] sig lvl (%d..%d dBm)  [", conf->sig_min, conf->sig_max);
	waddstr(w_key, s);
	wattrset(w_key, COLOR_PAIR(CP_STATNOISE));
	waddch(w_key, ACS_HLINE);
	wattrset(w_key, COLOR_PAIR(CP_STANDARD));
	sprintf(s, "] ns lvl (dBm)  [");
	waddstr(w_key, s);
	wattrset(w_key, COLOR_PAIR(CP_STATSNR));
	waddch(w_key, ' ');
	wattrset(w_key, COLOR_PAIR(CP_STANDARD));
	waddstr(w_key, "] S-N ratio (dB)");
}

void redraw_lhist()
{
	static int vcount = 1;

	if (!--vcount) {
		vcount = conf->slotsize;
		display_lhist(conf->ifname, w_lhist);
		wrefresh(w_lhist);
		wmove(w_menu, 1, 0);
		wrefresh(w_menu);
	}
}

int scr_lhist(struct wavemon_conf *wmconf)
{
	int key = 0;

	conf = wmconf;

	w_lhist = newwin_title(LINES - 4, COLS, 0, 0, "Level histogram", 0, 1);
	w_key = newwin_title(3, COLS, LINES - 4, 0, "Key", 1, 0);
	w_menu = newwin(1, COLS, LINES - 1, 0);

	display_key(w_key);
	wrefresh(w_key);

	wmenubar(w_menu, 1);
	wmove(w_menu, 1, 0);
	nodelay(w_menu, FALSE);
	keypad(w_menu, TRUE);

	iw_stat_redraw = redraw_lhist;
	while (key < KEY_F(1) || key > KEY_F(10)) {
		while ((key = wgetch(w_menu)) <= 0)
			usleep(5000);

		/* Keyboard shortcuts */
		if (key == 'q')
			key = KEY_F(10);
		else if (key == 'i')
			key = KEY_F(1);
	}
	iw_stat_redraw = NULL;

	werase(w_lhist);
	wrefresh(w_lhist);
	delwin(w_lhist);
	werase(w_key);
	wrefresh(w_key);
	delwin(w_key);
	werase(w_menu);
	wrefresh(w_menu);
	delwin(w_menu);

	return key - KEY_F(1);
}
