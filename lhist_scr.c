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
#include "iw_if.h"
#include "iw_nl80211.h"

/* Number of lines in the key window at the bottom */
#define KEY_WIN_HEIGHT	3

/* Total number of lines in the history window */
#define HIST_WIN_HEIGHT	(WAV_HEIGHT - KEY_WIN_HEIGHT)

/*
 * Analogous to MAXYLEN, the following sets both the
 * - highest y/line index and the
 * - total count of lines inside the history window.
 */
#define HIST_MAXYLEN	(HIST_WIN_HEIGHT - 1)

/* Position (relative to right border) and maximum length of dBm level tags. */
#define LEVEL_TAG_POS	5

/* GLOBALS */
static WINDOW *w_lhist, *w_key;

/* Keep track of interface changes. */
static int last_if_idx = -1;

/*
 *	Keeping track of global minima/maxima
 */
static struct iw_extrema {
	bool	initialised;
	float	min;
	float	max;
} e_signal;

static void track_extrema(const float new_sample, struct iw_extrema *ie)
{
	if (!ie->initialised) {
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

	if (!ie->initialised)
		snprintf(range, sizeof(range), "unknown");
	else if (ie->min == ie->max)
		snprintf(range, sizeof(range), "%+.0f %s", ie->min, unit);
	else
		snprintf(range, sizeof(range), "%+.0f %s ... %+.0f %s",
				ie->min, unit, ie->max, unit);
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
static const uint32_t		COUNTMAX = ~0;

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
	struct iw_levelstat zero = {.signal = 0, .valid = false};

	if (index > IW_STACKSIZE || index > count)
		return zero;
	return iw_stats_cache[(count - index) % IW_STACKSIZE];
}

void iw_cache_update(struct iw_nl80211_linkstat *ls)
{
	static struct iw_levelstat avg = {.signal = 0, .valid = false};
	static int slot;
	int sig_level = ls->signal;

	/*
	 * Prefer signal level over average signal level.
	 * One card in particular (Intel 9260NGW) reported inconsistent
	 * station and beacon average signals.
	 * See https://github.com/uoaerg/wavemon/issues/47
	 */
	if (sig_level == 0)
		sig_level = ls->signal_avg;

	/*
	 * If hardware does not support dBm signal level, it will not
	 * be filled in, and show up as 0. Try to fall back to the BSS
	 * probe where again a 0 dBm value reflects 'not initialized'.
	 */
	if (sig_level == 0)
		sig_level = ls->bss_signal;

	/* If the signal level is positive, assume it is an absolute value (#100). */
	if (sig_level > 0)
		sig_level *= -1;

	if (sig_level == 0) {
		avg.valid = false;
	} else {
		avg.valid = true;
		avg.signal += (float)sig_level / conf.slotsize;
		track_extrema(sig_level, &e_signal);
	}

	if (++slot >= conf.slotsize) {
		iw_cache_insert(avg);

		avg.signal = slot = 0;
		avg.valid  = false;
	}
}

/*
 * Level-history display functions
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
	int level = round(yval);

	if (in_range(level, 1, HIST_MAXYLEN)) {
		wattrset(w_lhist, COLOR_PAIR(plot_colour) | A_BOLD);
#ifdef HAVE_LIBNCURSESW
		mvwadd_wch(w_lhist, hist_y(level), hist_x(xval), WACS_HLINE);
#else
		mvwaddch(w_lhist, hist_y(level), hist_x(xval), ACS_HLINE);
#endif
	}
}

static void display_lhist(void)
{
	struct iw_levelstat iwl;
	double sig_level;
	int x, y;

	for (x = 1; x <= MAXXLEN; x++) {

		iwl = iw_cache_get(x);

		/* Clear screen and set up horizontal grid lines */
		wattrset(w_lhist, COLOR_PAIR(CP_BLUE));
		for (y = 1; y <= HIST_MAXYLEN; y++)
			mvwaddch(w_lhist, hist_y(y), hist_x(x), (y % 5) ? ' ' : '-');

		if (x == LEVEL_TAG_POS && iwl.valid) {
			char	tmp[LEVEL_TAG_POS + 1];
			int	len;
			/*
			 * Tag the horizontal grid lines with dBm levels.
			 */
			wattrset(w_lhist, COLOR_PAIR(CP_GREEN));
			for (y = 1; y <= HIST_MAXYLEN; y++) {
				if (y != 1 && (y % 5) && y != HIST_MAXYLEN)
					continue;
				len = snprintf(tmp, sizeof(tmp), "%.0f",
					       hist_level_inverse(y, conf.sig_min,
								     conf.sig_max));
				mvwaddstr(w_lhist, hist_y(y), hist_x(len), tmp);
			}
		}

		if (iwl.valid) {
			sig_level = hist_level(iwl.signal, conf.sig_min, conf.sig_max);
			hist_plot(sig_level, x, CP_GREEN);
		}
	}

	wrefresh(w_lhist);
}

static void display_key(WINDOW *w_key)
{
	char buf[280];
	/* Clear the (one-line) screen) */
	wmove(w_key, 1, 1);
	wclrtoborder(w_key);

	wattrset(w_key, COLOR_PAIR(CP_STANDARD));
	waddch(w_key, '[');
	wattrset(w_key, COLOR_PAIR(CP_GREEN));
	waddch(w_key, ACS_HLINE);
	wattrset(w_key, COLOR_PAIR(CP_STANDARD));

	snprintf(buf, sizeof(buf), "] signal level (%s)", fmt_extrema(&e_signal, "dBm"));
	waddstr(w_key, buf);

	wrefresh(w_key);
}

void scr_lhist_init(void)
{
	w_lhist = newwin_title(0, HIST_WIN_HEIGHT, "Level history", true);
	w_key   = newwin_title(HIST_MAXYLEN + 1, KEY_WIN_HEIGHT, "Key", false);

	if (last_if_idx != conf.if_idx) {
		count = 0;
		e_signal.initialised = false;
		last_if_idx = conf.if_idx;
	}
	sampling_init(true);

	display_key(w_key);
}

int scr_lhist_loop(WINDOW *w_menu)
{
	static int vcount = 1;

	if (!--vcount) {
		vcount = conf.slotsize;
		display_lhist();
		display_key(w_key);
	}
	return wgetch(w_menu);
}

void scr_lhist_fini(void)
{
	sampling_stop();
	delwin(w_lhist);
	delwin(w_key);
}
