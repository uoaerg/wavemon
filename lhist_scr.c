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

/* Number of lines in the key window at the bottom */
#define KEY_WIN_HEIGHT	3

/* Total number of lines in the histogram window */
#define HIST_WIN_HEIGHT	(WAV_HEIGHT - KEY_WIN_HEIGHT)

/*
 * Analogous to MAXYLEN, the following sets both the
 * - highest y/line index and the
 * - total count of lines inside the histogram window.
 */
#define HIST_MAXYLEN	(HIST_WIN_HEIGHT - 1)

/* Position (relative to right border) and maximum length of dBm level tags. */
#define LEVEL_TAG_POS	5

/* GLOBALS */
static WINDOW *w_lhist, *w_key;

/*
 *	Keeping track of global minima/maxima
 */
static struct iw_extrema {
	bool	initialised;
	float	min;
	float	max;
} e_signal, e_noise, e_snr;

static void init_extrema(struct iw_extrema *ie)
{
	memset(ie, 0, sizeof(*ie));
}

static void track_extrema(const float new_sample, struct iw_extrema *ie)
{
	if (! ie->initialised) {
		ie->initialised = true;
		ie->min = ie->max = new_sample;
	} else if (new_sample < ie->min) {
		ie->min = new_sample;
	} else if (new_sample > ie->max) {
		ie->max = new_sample;
	}
}

static char *fmt_extrema(const struct iw_extrema *ie, const char *unit)
{
	static char range[256];

	if (! ie->initialised)
		snprintf(range, sizeof(range), "unknown");
	else if (ie->min == ie->max)
		snprintf(range, sizeof(range), "%+.0f %s", ie->min, unit);
	else
		snprintf(range, sizeof(range), "%+.0f..%+.0f %s", ie->min,
								ie->max, unit);
	return range;
}

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
	struct iw_levelstat zero = IW_LSTAT_INIT;

	if (index > IW_STACKSIZE || index > count)
		return zero;
	return iw_stats_cache[(count - index) % IW_STACKSIZE];
}

void iw_cache_update(struct iw_stat *iw)
{
	static struct iw_levelstat prev, avg = IW_LSTAT_INIT;
	static int slot;

	if (! (iw->stat.qual.updated & IW_QUAL_LEVEL_INVALID)) {
		avg.flags  &= ~IW_QUAL_LEVEL_INVALID;
		avg.signal += iw->dbm.signal / conf.slotsize;
		track_extrema(iw->dbm.signal, &e_signal);
	}

	if (! (iw->stat.qual.updated & IW_QUAL_NOISE_INVALID)) {
		avg.flags &= ~IW_QUAL_NOISE_INVALID;
		avg.noise += iw->dbm.noise / conf.slotsize;
		track_extrema(iw->dbm.noise, &e_noise);
		track_extrema(iw->dbm.signal - iw->dbm.noise, &e_snr);
	}

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
		avg.flags  = IW_QUAL_LEVEL_INVALID | IW_QUAL_NOISE_INVALID;
	}
}

/*
 * Histogram-specific display functions
 */
static double hist_level(double val, int min, int max)
{
	return map_range(val, min, max, 1, HIST_MAXYLEN);
}

static double hist_level_inverse(int y_level, int min, int max)
{
	return map_range(y_level, 1, HIST_MAXYLEN, min, max);
}

/* Order needs to be reversed as y-coordinates grow downwards */
static int hist_y(int yval)
{
	return reverse_range(yval, 1, HIST_MAXYLEN);
}

/* Values come in from the right, so 'x' also needs to be reversed */
static int hist_x(int xval)
{
	return reverse_range(xval, 1, MAXXLEN);
}

/* plot single values, without clamping to min/max */
static void hist_plot(double yval, int xval, enum colour_pair plot_colour)
{
	double level, fraction;
	chtype ch;

	fraction = modf(yval, &level);

	if (in_range(level, 1, HIST_MAXYLEN)) {
		/*
		 * The 5 different scanline chars provide a pretty good accuracy.
		 * ncurses will fall back to standard ASCII chars anyway if they
		 * are not available.
		 */
		if (fraction < 0.2)
			ch = ACS_S9;
		else if (fraction < 0.4)
			ch = ACS_S7;
		else if (fraction < 0.6)
			ch = ACS_HLINE;
		else if (fraction < 0.8)
			ch = ACS_S3;
		else
			ch = ACS_S1;

		wattrset(w_lhist, COLOR_PAIR(plot_colour) | A_BOLD);
		mvwaddch(w_lhist, hist_y(level), hist_x(xval), ch);
	}
}

static void display_lhist(void)
{
	struct iw_levelstat iwl;
	double snr_level, noise_level, sig_level;
	enum colour_pair plot_colour;
	int x, y;

	for (x = 1; x <= MAXXLEN; x++) {

		iwl = iw_cache_get(x);

		/* Clear screen and set up horizontal grid lines */
		wattrset(w_lhist, COLOR_PAIR(CP_STATBKG));
		for (y = 1; y <= HIST_MAXYLEN; y++)
			mvwaddch(w_lhist, hist_y(y), hist_x(x), y % 5 ? ' ' : '-');

		/*
		 * SNR comes first, as it determines the background. If either
		 * noise or signal is invalid, set level below minimum value to
		 * indicate that no background is present.
		 */
		if (iwl.flags & (IW_QUAL_NOISE_INVALID | IW_QUAL_LEVEL_INVALID)) {
			snr_level = 0;
		} else {
			snr_level = hist_level(iwl.signal - iwl.noise,
					       conf.sig_min - conf.noise_max,
					       conf.sig_max - conf.noise_min);

			wattrset(w_lhist, COLOR_PAIR(CP_STATSNR));
			for (y = 1; y <= clamp(snr_level, 1, HIST_MAXYLEN); y++)
				mvwaddch(w_lhist, hist_y(y), hist_x(x), ' ');
		}

		if (! (iwl.flags & IW_QUAL_NOISE_INVALID)) {
			noise_level = hist_level(iwl.noise, conf.noise_min, conf.noise_max);
			plot_colour = noise_level > snr_level ? CP_STATNOISE : CP_STATNOISE_S;
			hist_plot(noise_level, x, plot_colour);

		} else if (x == LEVEL_TAG_POS && ! (iwl.flags & IW_QUAL_LEVEL_INVALID)) {
			char	tmp[LEVEL_TAG_POS + 1];
			int	len;
			/*
			 * Tag the horizontal grid lines with dBm levels.
			 * This is only supported for signal levels, when the screen is not
			 * shared by several graphs (each having a different scale).
			 */
			wattrset(w_lhist, COLOR_PAIR(CP_STATSIG));
			for (y = 1; y <= HIST_MAXYLEN; y++) {
				if (y != 1 && (y % 5) && y != HIST_MAXYLEN)
					continue;
				len = snprintf(tmp, sizeof(tmp), "%.0f",
					       hist_level_inverse(y, conf.sig_min,
								     conf.sig_max));
				mvwaddstr(w_lhist, hist_y(y), hist_x(len), tmp);
			}
		}

		if (! (iwl.flags & IW_QUAL_LEVEL_INVALID)) {
			sig_level   = hist_level(iwl.signal, conf.sig_min, conf.sig_max);
			plot_colour = sig_level > snr_level ? CP_STATSIG : CP_STATSIG_S;
			hist_plot(sig_level, x, plot_colour);
		}
	}

	wrefresh(w_lhist);
}

static void display_key(WINDOW *w_key)
{
	/* Clear the (one-line) screen) */
	wmove(w_key, 1, 1);
	wclrtoborder(w_key);

	wattrset(w_key, COLOR_PAIR(CP_STANDARD));
	waddch(w_key, '[');
	wattrset(w_key, COLOR_PAIR(CP_STATSIG));
	waddch(w_key, ACS_HLINE);
	wattrset(w_key, COLOR_PAIR(CP_STANDARD));

	wprintw(w_key, "] sig lvl (%s)  [", fmt_extrema(&e_signal, "dBm"));

	wattrset(w_key, COLOR_PAIR(CP_STATNOISE));
	waddch(w_key, ACS_HLINE);
	wattrset(w_key, COLOR_PAIR(CP_STANDARD));

	wprintw(w_key, "] ns lvl (%s)  [", fmt_extrema(&e_noise, "dBm"));

	wattrset(w_key, COLOR_PAIR(CP_STATSNR));
	waddch(w_key, ' ');

	wattrset(w_key, COLOR_PAIR(CP_STANDARD));
	wprintw(w_key, "] S-N ratio (%s)", fmt_extrema(&e_snr, "dB"));

	wrefresh(w_key);
}

static void redraw_lhist(int signum)
{
	static int vcount = 1;

	sampling_do_poll();
	if (!--vcount) {
		vcount = conf.slotsize;
		display_lhist();
		display_key(w_key);
	}
}

void scr_lhist_init(void)
{
	w_lhist = newwin_title(0, HIST_WIN_HEIGHT, "Level histogram", true);
	w_key   = newwin_title(HIST_MAXYLEN + 1, KEY_WIN_HEIGHT, "Key", false);

	init_extrema(&e_signal);
	init_extrema(&e_noise);
	init_extrema(&e_snr);
	sampling_init(redraw_lhist);

	display_key(w_key);
}

int scr_lhist_loop(WINDOW *w_menu)
{
	return wgetch(w_menu);
}

void scr_lhist_fini(void)
{
	sampling_stop();
	delwin(w_lhist);
	delwin(w_key);
}
