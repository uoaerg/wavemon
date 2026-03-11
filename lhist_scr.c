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

/* Height of the link info bar at the top */
#define INFO_WIN_HEIGHT	2

/* Total number of lines in the history window */
#define HIST_WIN_HEIGHT	(WAV_HEIGHT - KEY_WIN_HEIGHT - INFO_WIN_HEIGHT)

/*
 * Analogous to MAXYLEN, the following sets both the
 * - highest y/line index and the
 * - total count of lines inside the history window.
 */
#define HIST_MAXYLEN	(HIST_WIN_HEIGHT - 1)

/* Position (relative to right border) and maximum length of dBm level tags. */
#define LEVEL_TAG_POS	5

/* Fixed scale ranges for the three graph areas */
#define SIG_SCALE_MIN	-100	/* rssi/noise: -100 .. 0 dBm */
#define SIG_SCALE_MAX	0
#define SNR_SCALE_MIN	0	/* snr: 0 .. 100 dB */
#define SNR_SCALE_MAX	100
#define RTT_SCALE_MIN	0	/* latency: 0 .. 200 ms */
#define RTT_SCALE_MAX	200

/* GLOBALS */
static WINDOW *w_link, *w_lhist, *w_key;

/* Cached link info — updated from iw_cache_update via sampling thread */
static struct {
	char		ssid[64];
	struct ether_addr bssid;
	uint32_t	freq;
	uint32_t	chan_width;
	double		tx_power;
	char		rx_bitrate[100];
	bool		valid;
} link_cache;

/* Keep track of interface changes. */
static int last_if_idx = -1;

/*
 *	Keeping track of global minima/maxima
 */
static struct iw_extrema {
	bool	initialised;
	float	min;
	float	max;
} e_signal, e_noise, e_snr, e_rtt;

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
	struct iw_levelstat zero = {.signal = 0, .noise = 0, .rtt_ms = 0,
				   .noise_valid = false, .rtt_valid = false, .valid = false};

	if (index > IW_STACKSIZE || index > count)
		return zero;
	return iw_stats_cache[(count - index) % IW_STACKSIZE];
}

void iw_cache_update(struct iw_nl80211_linkstat *ls)
{
	static struct iw_levelstat avg = {.signal = 0, .noise = 0, .rtt_ms = 0,
					  .noise_valid = false, .rtt_valid = false, .valid = false};
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

	if (iw_nl80211_have_survey_data(ls)) {
		avg.noise_valid = true;
		avg.noise += (float)ls->survey.noise / conf.slotsize;
		track_extrema(ls->survey.noise, &e_noise);
		if (avg.valid)
			track_extrema(sig_level - ls->survey.noise, &e_snr);
	}

	/* Sample latest RTT from ping thread */
	{
		float rtt;

		if (ping_get_rtt(&rtt)) {
			avg.rtt_valid = true;
			avg.rtt_ms += rtt / conf.slotsize;
			track_extrema(rtt, &e_rtt);
		}
	}

	/* Cache link info for display (written by sampling thread only) */
	memcpy(&link_cache.bssid, &ls->bssid, sizeof(ls->bssid));
	memcpy(link_cache.rx_bitrate, ls->rx_bitrate, sizeof(ls->rx_bitrate));
	link_cache.valid = true;

	if (++slot >= conf.slotsize) {
		iw_cache_insert(avg);

		avg.signal = avg.noise = avg.rtt_ms = slot = 0;
		avg.valid = false;
		avg.noise_valid = false;
		avg.rtt_valid = false;
	}
}

/*
 * Level-history display functions
 */

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

/*
 * Split graph into three stacked areas sharing the X-axis (time):
 *   Top area  — Latency / RTT (yellow)  0..rtt_max ms
 *   Separator — horizontal line
 *   Mid area  — SNR  (0–60 dB, cyan)
 *   Separator — horizontal line
 *   Bot area  — RSSI + Noise (dBm)
 *
 * yval coordinate space: 1 = window bottom, HIST_MAXYLEN = window top.
 */
static void display_lhist(void)
{
	struct iw_levelstat iwl;
	int x, y;
	const bool have_noise = e_noise.initialised;

	/*
	 * Area boundaries in yval space (1=bottom, HIST_MAXYLEN=top).
	 * With noise data:  3 panels — rssi(33%) | snr(33%) | latency(33%)
	 * Without noise:    2 panels — rssi(50%) | latency(50%)
	 */
	const int sig_lo  = 1;
	int sig_hi, sep1, snr_lo, snr_hi, sep2, rtt_lo, rtt_hi;

	if (have_noise) {
		sig_hi  = HIST_MAXYLEN * 2 / 6;
		sep1    = sig_hi + 1;
		snr_lo  = sep1 + 1;
		snr_hi  = HIST_MAXYLEN * 4 / 6;
		sep2    = snr_hi + 1;
		rtt_lo  = sep2 + 1;
	} else {
		sig_hi  = HIST_MAXYLEN / 2;
		sep1    = sig_hi + 1;
		snr_lo  = snr_hi = sep2 = 0;	/* unused */
		rtt_lo  = sep1 + 1;
	}
	rtt_hi = HIST_MAXYLEN;

	for (x = 1; x <= MAXXLEN; x++) {

		iwl = iw_cache_get(x);

		/* ── Clear RSSI area (bottom) ── */
		for (y = sig_lo; y <= sig_hi; y++)
			mvwaddch(w_lhist, hist_y(y), hist_x(x), ' ');

		/* ── Separator 1 ── */
		wattrset(w_lhist, COLOR_PAIR(CP_STANDARD));
#ifdef HAVE_LIBNCURSESW
		mvwadd_wch(w_lhist, hist_y(sep1), hist_x(x), WACS_HLINE);
#else
		mvwaddch(w_lhist, hist_y(sep1), hist_x(x), ACS_HLINE);
#endif

		if (have_noise) {
			/* ── Clear SNR area (middle) ── */
			for (y = snr_lo; y <= snr_hi; y++)
				mvwaddch(w_lhist, hist_y(y), hist_x(x), ' ');

			/* ── Separator 2 ── */
			wattrset(w_lhist, COLOR_PAIR(CP_STANDARD));
#ifdef HAVE_LIBNCURSESW
			mvwadd_wch(w_lhist, hist_y(sep2), hist_x(x), WACS_HLINE);
#else
			mvwaddch(w_lhist, hist_y(sep2), hist_x(x), ACS_HLINE);
#endif
		}

		/* ── Clear latency area (top) ── */
		for (y = rtt_lo; y <= rtt_hi; y++)
			mvwaddch(w_lhist, hist_y(y), hist_x(x), ' ');

		/* ── Plot RSSI (green) ── */
		if (iwl.valid) {
			double sl = map_range(iwl.signal, SIG_SCALE_MIN, SIG_SCALE_MAX,
					      sig_lo, sig_hi);
			int level = round(sl);
			if (in_range(level, sig_lo, sig_hi)) {
				wattrset(w_lhist, COLOR_PAIR(CP_GREEN) | A_BOLD);
				mvwaddch(w_lhist, hist_y(level), hist_x(x), ACS_BULLET);
			}
		}

		/* ── Plot noise (red) ── */
		if (iwl.noise_valid) {
			double nl = map_range(iwl.noise, SIG_SCALE_MIN, SIG_SCALE_MAX,
					      sig_lo, sig_hi);
			int level = round(nl);
			if (in_range(level, sig_lo, sig_hi)) {
				wattrset(w_lhist, COLOR_PAIR(CP_RED) | A_BOLD);
				mvwaddch(w_lhist, hist_y(level), hist_x(x), ACS_BULLET);
			}
		}

		/* ── Plot SNR (cyan) ── */
		if (have_noise && iwl.valid && iwl.noise_valid) {
			double snr_val = iwl.signal - iwl.noise;
			double sl = map_range(snr_val, SNR_SCALE_MIN, SNR_SCALE_MAX,
					      snr_lo, snr_hi);
			int level = round(sl);
			if (in_range(level, snr_lo, snr_hi)) {
				wattrset(w_lhist, COLOR_PAIR(CP_CYAN) | A_BOLD);
				mvwaddch(w_lhist, hist_y(level), hist_x(x), ACS_BULLET);
			}
		}

		/* ── Plot RTT (yellow) ── */
		if (iwl.rtt_valid && iwl.rtt_ms >= 0) {
			double rl = map_range(iwl.rtt_ms, RTT_SCALE_MIN, RTT_SCALE_MAX,
					      rtt_lo, rtt_hi);
			int level = round(rl);
			if (in_range(level, rtt_lo, rtt_hi)) {
				wattrset(w_lhist, COLOR_PAIR(CP_YELLOW) | A_BOLD);
				mvwaddch(w_lhist, hist_y(level), hist_x(x), ACS_BULLET);
			}
		}
	}

	/* ── Area titles ── */
	wattrset(w_lhist, COLOR_PAIR(CP_STANDARD) | A_BOLD);
	mvwaddstr(w_lhist, hist_y(rtt_hi), 1, "latency (ms)");

	if (have_noise) {
		wattrset(w_lhist, COLOR_PAIR(CP_STANDARD) | A_BOLD);
		mvwaddstr(w_lhist, hist_y(snr_hi), 1, "snr (dB)");

		wattrset(w_lhist, COLOR_PAIR(CP_STANDARD) | A_BOLD);
		mvwaddstr(w_lhist, hist_y(sig_hi), 1, "rssi/noise (dBm)");
	} else {
		wattrset(w_lhist, COLOR_PAIR(CP_STANDARD) | A_BOLD);
		mvwaddstr(w_lhist, hist_y(sig_hi), 1, "rssi (dBm)");
	}

	/* ── Scale labels (right side, dim) ── */
	{
		char tmp[8];
		int len, val;
		double ypos;

		wattrset(w_lhist, COLOR_PAIR(CP_STANDARD) | A_DIM);

		/* rssi area */
		for (val = SIG_SCALE_MIN; val <= SIG_SCALE_MAX; val += 10) {
			ypos = map_range(val, SIG_SCALE_MIN, SIG_SCALE_MAX,
					 sig_lo, sig_hi);
			int yp = round(ypos);
			if (in_range(yp, sig_lo, sig_hi)) {
				len = snprintf(tmp, sizeof(tmp), "%d", val);
				mvwaddstr(w_lhist, hist_y(yp), hist_x(len), tmp);
			}
		}

		/* snr area (only if noise available) */
		if (have_noise) {
			for (val = SNR_SCALE_MIN; val <= SNR_SCALE_MAX; val += 10) {
				ypos = map_range(val, SNR_SCALE_MIN, SNR_SCALE_MAX,
						 snr_lo, snr_hi);
				int yp = round(ypos);
				if (in_range(yp, snr_lo, snr_hi)) {
					len = snprintf(tmp, sizeof(tmp), "%d", val);
					mvwaddstr(w_lhist, hist_y(yp), hist_x(len), tmp);
				}
			}
		}

		/* latency area */
		for (val = RTT_SCALE_MIN; val <= RTT_SCALE_MAX; val += 20) {
			ypos = map_range(val, RTT_SCALE_MIN, RTT_SCALE_MAX,
					 rtt_lo, rtt_hi);
			int yp = round(ypos);
			if (in_range(yp, rtt_lo, rtt_hi)) {
				len = snprintf(tmp, sizeof(tmp), "%d", val);
				mvwaddstr(w_lhist, hist_y(yp), hist_x(len), tmp);
			}
		}
	}

	/* ── Live values at left edge ── */
	{
		struct iw_levelstat newest = iw_cache_get(1);
		char tmp[8];

		if (newest.valid) {
			double ypos = map_range(newest.signal, SIG_SCALE_MIN,
						SIG_SCALE_MAX, sig_lo, sig_hi);
			int yp = round(ypos);
			if (in_range(yp, sig_lo, sig_hi)) {
				snprintf(tmp, sizeof(tmp), "%.0f", newest.signal);
				wattrset(w_lhist, COLOR_PAIR(CP_GREEN) | A_BOLD);
				mvwaddstr(w_lhist, hist_y(yp), 1, tmp);
			}
		}

		if (newest.noise_valid) {
			double ypos = map_range(newest.noise, SIG_SCALE_MIN,
						SIG_SCALE_MAX, sig_lo, sig_hi);
			int yp = round(ypos);
			if (in_range(yp, sig_lo, sig_hi)) {
				snprintf(tmp, sizeof(tmp), "%.0f", newest.noise);
				wattrset(w_lhist, COLOR_PAIR(CP_RED) | A_BOLD);
				mvwaddstr(w_lhist, hist_y(yp), 1, tmp);
			}
		}

		if (have_noise && newest.valid && newest.noise_valid) {
			double snr_val = newest.signal - newest.noise;
			double ypos = map_range(snr_val, SNR_SCALE_MIN, SNR_SCALE_MAX,
						snr_lo, snr_hi);
			int yp = round(ypos);
			if (in_range(yp, snr_lo, snr_hi)) {
				snprintf(tmp, sizeof(tmp), "%.0f", snr_val);
				wattrset(w_lhist, COLOR_PAIR(CP_CYAN) | A_BOLD);
				mvwaddstr(w_lhist, hist_y(yp), 1, tmp);
			}
		}

		if (newest.rtt_valid && newest.rtt_ms >= 0) {
			double ypos = map_range(newest.rtt_ms, RTT_SCALE_MIN, RTT_SCALE_MAX,
						rtt_lo, rtt_hi);
			int yp = round(ypos);
			if (in_range(yp, rtt_lo, rtt_hi)) {
				snprintf(tmp, sizeof(tmp), "%.0f", newest.rtt_ms);
				wattrset(w_lhist, COLOR_PAIR(CP_YELLOW) | A_BOLD);
				mvwaddstr(w_lhist, hist_y(yp), 1, tmp);
			}
		}
	}

	wrefresh(w_lhist);
}

static void display_link_info(void)
{
	display_link_header(w_link, link_cache.valid ? &link_cache.bssid : NULL);
}

static void display_key(WINDOW *w_key)
{
	char buf[256];

	/* ── Line 1: RSSI/Noise/SNR legend ── */
	wmove(w_key, 1, 1);
	wclrtoeol(w_key);

	wattrset(w_key, COLOR_PAIR(CP_STANDARD));
	waddch(w_key, '[');
	wattrset(w_key, COLOR_PAIR(CP_GREEN) | A_BOLD);
	waddch(w_key, ACS_BULLET);
	wattrset(w_key, COLOR_PAIR(CP_STANDARD));

	if (e_noise.initialised && e_snr.initialised) {
		snprintf(buf, sizeof(buf), "] rssi (%s)  [", fmt_extrema(&e_signal, "dBm"));
		waddstr(w_key, buf);

		wattrset(w_key, COLOR_PAIR(CP_RED) | A_BOLD);
		waddch(w_key, ACS_BULLET);
		wattrset(w_key, COLOR_PAIR(CP_STANDARD));

		snprintf(buf, sizeof(buf), "] noise (%s)  [", fmt_extrema(&e_noise, "dBm"));
		waddstr(w_key, buf);

		wattrset(w_key, COLOR_PAIR(CP_CYAN) | A_BOLD);
		waddch(w_key, ACS_BULLET);
		wattrset(w_key, COLOR_PAIR(CP_STANDARD));

		snprintf(buf, sizeof(buf), "] snr (%s)  [", fmt_extrema(&e_snr, "dB"));
		waddstr(w_key, buf);
	} else {
		snprintf(buf, sizeof(buf), "] rssi (%s)  [", fmt_extrema(&e_signal, "dBm"));
		waddstr(w_key, buf);
	}

	wattrset(w_key, COLOR_PAIR(CP_YELLOW) | A_BOLD);
	waddch(w_key, ACS_BULLET);
	wattrset(w_key, COLOR_PAIR(CP_STANDARD));

	if (e_rtt.initialised) {
		snprintf(buf, sizeof(buf), "] rtt → %s (%s)",
			 conf.ping_target, fmt_extrema(&e_rtt, "ms"));
	} else {
		snprintf(buf, sizeof(buf), "] rtt → %s", conf.ping_target);
	}
	waddstr(w_key, buf);

	wrefresh(w_key);
}

void scr_lhist_init(void)
{
	w_link  = newwin_title(0, INFO_WIN_HEIGHT, "link", true);
	w_lhist = newwin_title(INFO_WIN_HEIGHT, HIST_WIN_HEIGHT, "level history", true);
	w_key   = newwin_title(INFO_WIN_HEIGHT + HIST_MAXYLEN + 1, KEY_WIN_HEIGHT, "key", false);

	if (last_if_idx != conf.if_idx) {
		count = 0;
		e_signal.initialised = false;
		e_noise.initialised  = false;
		e_snr.initialised    = false;
		e_rtt.initialised    = false;
		last_if_idx = conf.if_idx;
	}
	ping_start(conf.ping_target);
	sampling_init(true);

	display_key(w_key);
}

int scr_lhist_loop(WINDOW *w_menu)
{
	static int vcount = 1;

	if (interface_lost || interface_recovered || interface_crash)
		return KEY_F(10);

	if (!--vcount) {
		vcount = conf.slotsize;
		display_link_info();
		display_lhist();
		display_key(w_key);
	}
	display_root_warning();
	return wgetch(w_menu);
}

void scr_lhist_fini(void)
{
	sampling_stop();
	ping_stop();
	delwin(w_link);
	delwin(w_lhist);
	delwin(w_key);
	link_cache.valid = false;
}
