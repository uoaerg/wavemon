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

/* GLOBALS */
static WINDOW *w_lhist, *w_menu;

/*
 * Simple array-based circular FIFO buffer
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Insertion works from lower to higher indices.
 * Access works from higher down to lower indices.
 *
 * Cases & assumptions:
 * ~~~~~~~~~~~~~~~~~~~~
 * - principle: unsigned counter + hash function to handle array wrap-around;
 * - buffer is empty if count == 0;
 * - else count indicates the next place to insert(modulo %IW_STACKSIZE).
 */
#define IW_STACKSIZE		1024
static struct iw_levelstat	iw_stats_cache[IW_STACKSIZE];
static uint32_t			count;
#define COUNTMAX		(typeof(count))-1

static void iw_cache_insert(const struct iw_levelstat new)
{
	iw_stats_cache[count % IW_STACKSIZE] = new;
	/*
	 * Handle counter overflow by mapping into a smaller index which is
	 * identical (modulo %IW_STACKSIZE) to the old value. (The datatype
	 * of 'count' must be able to express at least 2 * IW_STACKSIZE.)
	 */
	if (++count == COUNTMAX)
		count = IW_STACKSIZE + (COUNTMAX % IW_STACKSIZE);
}

static struct iw_levelstat iw_cache_get(const uint32_t index)
{
	struct iw_levelstat zero = {0, 0};

	if (index > IW_STACKSIZE || index > count)
		return zero;
	return iw_stats_cache[(count - index) % IW_STACKSIZE];
}

void iw_cache_update(struct iw_stat *iw)
{
	static struct iw_levelstat avg, prev;
	static int slot;

	avg.signal += iw->dbm.signal / conf.slotsize;
	avg.noise  += iw->dbm.noise  / conf.slotsize;

	if (++slot >= conf.slotsize) {
		iw_cache_insert(avg);

		if (conf.lthreshold_action &&
		    prev.signal < conf.lthreshold &&
		    avg.signal >= conf.lthreshold)
			threshold_action(conf.lthreshold);
		else if (conf.hthreshold_action &&
			 prev.signal > conf.hthreshold &&
			 avg.signal <= conf.hthreshold)
			threshold_action(conf.hthreshold);

		prev = avg;
		avg.signal = avg.noise = slot = 0;
	}
}


static void display_lhist(void)
{
	struct iw_levelstat iwl;
	chtype	ch;
	double	ratio, p, p_fract, snr;
	int	y, ysize,
		x, xsize;

	getmaxyx(w_lhist, ysize, xsize);
	--xsize;
	--ysize;

	ratio = (double)(ysize - 1) / (conf.sig_max - conf.sig_min);

	for (x = 1; x < xsize; x++) {

		for (y = 1; y <= ysize; y++)
			mvwaddch(w_lhist, y, xsize - x, ' ');

		iwl = iw_cache_get(x);
		snr = iwl.signal - max(iwl.noise, conf.noise_min);

		if (snr > 0) {
			if (snr < (conf.sig_max - conf.sig_min)) {

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

		if (iwl.noise >= conf.sig_min && iwl.noise <= conf.sig_max) {

			p_fract = modf((iwl.noise - conf.noise_min) * ratio, &p);
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

		if (iwl.signal >= conf.sig_min && iwl.signal <= conf.sig_max) {

			p_fract = modf((iwl.signal - conf.sig_min) * ratio, &p);
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

static void display_key(WINDOW *w_key)
{
	char s[0x100];

	wmove(w_key, 1, 1);
	wattrset(w_key, COLOR_PAIR(CP_STANDARD));
	waddch(w_key, '[');
	wattrset(w_key, COLOR_PAIR(CP_STATSIG));
	waddch(w_key, ACS_HLINE);
	wattrset(w_key, COLOR_PAIR(CP_STANDARD));
	sprintf(s, "] sig lvl (%d..%d dBm)  [", conf.sig_min, conf.sig_max);
	waddstr(w_key, s);
	wattrset(w_key, COLOR_PAIR(CP_STATNOISE));
	waddch(w_key, ACS_HLINE);
	wattrset(w_key, COLOR_PAIR(CP_STANDARD));
	sprintf(s, "] ns lvl (%d..%d dBm)  [", conf.noise_min, conf.noise_max);
	waddstr(w_key, s);
	wattrset(w_key, COLOR_PAIR(CP_STATSNR));
	waddch(w_key, ' ');
	wattrset(w_key, COLOR_PAIR(CP_STANDARD));
	waddstr(w_key, "] S-N ratio (dB)");
}

static void redraw_lhist(void)
{
	static int vcount = 1;

	if (!--vcount) {
		vcount = conf.slotsize;
		display_lhist();
		wrefresh(w_lhist);
		wmove(w_menu, 1, 0);
		wrefresh(w_menu);
	}
}

int scr_lhist(void)
{
	WINDOW *w_key;
	int key = 0;

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
