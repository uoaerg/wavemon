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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>
#include <errno.h>
#include <err.h>
#include <time.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef HAVE_LIBNCURSESW
#define _XOPEN_SOURCE_EXTENDED
#endif
#if defined(HAVE_LIBNCURSESW) && defined(HAVE_NCURSESW_CURSES_H)
#include <ncursesw/curses.h>
#else
#include <ncurses.h>
#endif

#include "llist.h"

#define CFNAME	".wavemonrc"

/**
 * Minimum screen dimensions.
 * The number of lines depends on the size requirements of scr_info(). The
 * number of columns comes from the menubar length (10 items of length 6
 * plus the 'Fxx'). This value was also chosen since 24x80 is a very common
 * screen size, in particular allowing the use on the console.
 */
enum info_screen_geometry {
	WH_IFACE    = 2,	/* 'Interface' area at the top */
	WH_LEVEL    = 9,	/* Level meters */
	WH_STATS    = 3,	/* WiFi statistics area */
	WH_INFO     = 8,	/* WiFi information area */
	WH_NET_MIN  = 3,	/* Network interface information area */
	WH_NET_MAX  = 5,	/* Network interface information area */
	WH_MENU	    = 1		/* Menu bar at the bottom */
};
#define WH_INFO_SCR_BASE	(WH_IFACE + WH_LEVEL + WH_STATS + WH_MENU)
#define WH_INFO_SCR_MIN		(WH_INFO_SCR_BASE + WH_INFO + WH_NET_MIN)

#define MIN_SCREEN_LINES	WH_INFO_SCR_MIN
#define MIN_SCREEN_COLS		80
/*
 * Screen layout constants.
 *
 * All windows extend over the whole screen width; the vertical number of
 * rows is reduced by one due to the menubar at the bottom of the screen.
 */
#define WAV_WIDTH	(COLS)
#define WAV_HEIGHT	(LINES-1)
/*
 * Maximum length of a menubar key entry
 */
#define MAX_MENU_KEY	(MIN_SCREEN_COLS/10)
/*
 * Maximum lengths/coordinates inside the bordered screen.
 *
 * The printable window area is constrained by the frame lines connecting
 * the corner points (0, 0), (0, COLS-1), (LINES-1, 0), (LINES-1, COLS-1).
 */
#define MAXXLEN		(WAV_WIDTH  - 2)
#define MAXYLEN		(WAV_HEIGHT - 2)

/* Number of seconds to display a warning message outside ncurses mode */
#define WARN_DISPLAY_DELAY	1

/* Minimum SSID length */
#define MAX_ESSID_LEN		16

/*
 * Symbolic names of actions to take when crossing thresholds.
 * These actions invoke the corresponding ncurses functions.
 */
enum threshold_action {
	TA_DISABLED,
	TA_BEEP,
	TA_FLASH,
	TA_BEEP_FLASH
};

static inline void threshold_action(enum threshold_action action)
{
	if (action & TA_FLASH)
		flash();
	if (action & TA_BEEP)
		beep();
}

/*
 * Symbolic names for scan sort order comparison.
 */
enum scan_sort_order {
	SO_CHAN,
	SO_SIGNAL,
	SO_MAC,
	SO_ESSID,
	SO_OPEN,
	SO_CHAN_SIG,
	SO_OPEN_SIG
};

/** Band filtering (2.4 GHz / 5 GHz / Both) */
enum scan_filter_band {
	SCAN_FILTER_BAND_BOTH,
	SCAN_FILTER_BAND_2G,
	SCAN_FILTER_BAND_5G
};

/*
 * Global in-memory representation of current wavemon configuration state
 */
extern struct wavemon_conf {
	int	if_idx;			/* Index into interface list */

	int	stat_iv,
		info_iv;

	int	sig_min, sig_max,
		noise_min, noise_max;

	int	lthreshold,
		hthreshold;

	int	slotsize,
		meter_decay;

	/* Boolean values */
	int	check_geometry,		/* Ensure window is large enough */
		cisco_mac,		/* Cisco-style MAC addresses */
		transparent_bg,		/* Use terminal background instead of black */
		override_bounds,	/* Override autodetection */
		scan_sort_asc,		/* Direction of @scan_sort_order */
		scan_hidden_essids;	/* Whether to include hidden SSIDs */

	/* Enumerated values */
	int	scan_sort_order,	/* channel|signal|open|chan/sig ... */
		scan_filter_band,	/* 2.4ghz|5ghz|both */
		lthreshold_action,	/* disabled|beep|flash|beep+flash */
		hthreshold_action,	/* disabled|beep|flash|beep+flash */
		startup_scr;		/* info|histogram|aplist */
} conf;

/*
 * Initialisation & Configuration
 */
extern void getconf(int argc, char *argv[]);

/* Configuration items to manipulate the current configuration */
struct conf_item {
	char	*name,		/* name for preferences screen */
		*cfname;	/* name for ~/.wavemonrc */

	enum {			/* type of parameter */
		t_int,		/* @v.i is interpreted as raw value */
		t_list,		/* @v.i is an index into @list */
		t_sep,		/* dummy, separator entry */
		t_func		/* void (*fp) (void) */
	} type;

	union {			/* type-dependent container for value */
		int	*i;	/* t_int and t_list index into @list  */
		void (*fp)();	/* t_func */
	} v;

	char	**list;		/* t_list: NULL-terminated array of strings */
	int	*dep;		/* dependency */
	bool	hidden;		/* only allow to set via wavemonrc */

	int	min,		/* value boundaries */
		max,
		inc;		/* increment for value changes */

	char	*unit;		/* name of units to display */
};

/*
 *	Screen functions
 */
enum wavemon_screen {
	SCR_INFO,	/* F1 */
	SCR_LHIST,	/* F2 */
	SCR_SCAN,	/* F3 */
	SCR_EMPTY_F4,	/* placeholder */
	SCR_EMPTY_F5,	/* placeholder */
	SCR_EMPTY_F6,	/* placeholder */
	SCR_PREFS,	/* F7 */
	SCR_HELP,	/* F8 */
	SCR_ABOUT,	/* F9 */
	SCR_QUIT	/* F10 */
};

extern void scr_info_init(void);
extern int  scr_info_loop(WINDOW *w_menu);
extern void scr_info_fini(void);

extern void scr_lhist_init(void);
extern int  scr_lhist_loop(WINDOW *w_menu);
extern void scr_lhist_fini(void);

extern void scr_aplst_init(void);
extern int  scr_aplst_loop(WINDOW *w_menu);
extern void scr_aplst_fini(void);

extern void scr_conf_init(void);
extern int  scr_conf_loop(WINDOW *w_menu);
extern void scr_conf_fini(void);

extern void scr_help_init(void);
extern int  scr_help_loop(WINDOW *w_menu);
extern void scr_help_fini(void);

extern void scr_about_init(void);
extern int  scr_about_loop(WINDOW *w_menu);
extern void scr_about_fini(void);

/*
 *	Ncurses definitions and functions
 */
extern WINDOW *newwin_title(int y, int h, const char *title, bool nobottom);
extern WINDOW *wmenubar(const enum wavemon_screen active);

extern void wclrtoborder(WINDOW *win);
extern void mvwclrtoborder(WINDOW *win, int y, int x);

extern void wadd_attr_str(WINDOW *win, const int attrs, const char *s);
static inline void waddstr_b(WINDOW * win, const char *s)
{
	wadd_attr_str(win, A_BOLD, s);
}

extern void waddstr_center(WINDOW * win, int y, const char *s);
extern const char *curtail(const char *str, const char *sep, int len);

extern void waddbar(WINDOW *win, int y, float v, float min, float max,
		    int8_t *cscale, bool rev);
extern void waddthreshold(WINDOW *win, int y, float v, float tv,
			  float minv, float maxv, int8_t *cscale, chtype tch);
enum colour_pair {
	CP_STANDARD = 1,
	CP_RED,
	CP_YELLOW,
	CP_GREEN,
	CP_CYAN,
	CP_BLUE,
	CP_RED_ON_BLUE,
	CP_GREEN_ON_BLUE,
	CP_CYAN_ON_BLUE,
	CP_BLUE_ON_BLUE,
};

static inline int cp_from_scale(float value, int8_t const *cscale, bool reverse)
{
	enum colour_pair cp;

	if (value < (float)cscale[0])
		cp = reverse ? CP_RED : CP_GREEN;
	else if (value < (float)cscale[1])
		cp = CP_YELLOW;
	else
		cp = reverse ? CP_GREEN : CP_RED;

	return COLOR_PAIR(cp);
}

/*
 *	Wireless interfaces
 */
extern void conf_get_interface_list(void);
extern const char *conf_ifname(void);

/*
 *	Error handling
 */
extern bool has_net_admin_capability(void);
extern void err_msg(const char *format, ...);
extern void err_quit(const char *format, ...);
extern void err_sys(const char *format, ...);

/*
 *	Helper functions
 */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static inline void (*xsignal(int signo, void (*handler)(int)))(int)
{
	struct sigaction old_sa, sa = { .sa_handler = handler, .sa_flags = 0 };

	if (sigemptyset(&sa.sa_mask) < 0 || sigaction(signo, &sa, &old_sa) < 0)
		err_sys("xsignal(%d) failed", signo);
	return old_sa.sa_handler;
}

/** Return the length of the NULL-terminated array @argv. */
static inline size_t argv_count(char **argv)
{
	size_t cnt = 0;

	assert(argv != NULL);
	while (*argv++)
		cnt++;
	return cnt;
}

/** Find any entry in @argv whose prefix is @what. */
static inline int argv_find(char **argv, const char *what)
{
	const size_t len = strlen(what);

	for (size_t i = 0; argv[i]; i++)
		if (strncasecmp(argv[i], what, len) == 0)
			return i;
	return -1;
}

static inline void str_tolower(char *s)
{
	for (; s && *s; s++)
		*s = tolower(*s);
}

/* Check if @str is printable (compare iw_essid_escape()) */
static inline bool str_is_ascii(char *s)
{
	if (!s || !*s)
		return false;
	for (; *s; s++)
		if (!isascii(*s) || iscntrl(*s))
			return false;
	return true;
}

/* number of digits needed to display integer part of @val */
static inline int num_int_digits(const double val)
{
	return 1 + (val > 1.0 ? log10(val) : val < -1.0 ? log10(-val) : 0);
}

static inline int max(const int a, const int b)
{
	return a > b ? a : b;
}

static inline bool in_range(int val, int min, int max)
{
	return min <= val && val <= max;
}

static inline int clamp(int val, int min, int max)
{
	return val < min ? min : (val > max ? max : val);
}

/* SI units -- see units(7) */
static inline char *byte_units(const double bytes)
{
	static char result[0x100];

	if (bytes >= 1 << 30)
		sprintf(result, "%0.2lf GiB", bytes / (1 << 30));
	else if (bytes >= 1 << 20)
		sprintf(result, "%0.2lf MiB", bytes / (1 << 20));
	else if (bytes >= 1 << 10)
		sprintf(result, "%0.2lf KiB", bytes / (1 << 10));
	else
		sprintf(result, "%.0lf B", bytes);

	return result;
}

/* Integer units - similar to %g for float. */
static inline char *int_counts(uint32_t count)
{
	static char result[0x10];

	if (count < 1000)
		sprintf(result, "%u", count);
	else if (count < 1000000)
		sprintf(result, "%uk", count/1000);
	else
		sprintf(result, "%.lg", (double)count);

	return result;
}

/**
 * Compute exponentially weighted moving average
 * @mavg:	old value of the moving average
 * @sample:	new sample to update @mavg
 * @weight:	decay factor for new samples, 0 < weight <= 1
 */
static inline double ewma(double mavg, double sample, double weight)
{
	return mavg == 0 ? sample : weight * mavg + (1.0 - weight) * sample;
}

/* map 0.0 <= ratio <= 1.0 into min..max */
static inline double map_val(double ratio, double min, double max)
{
	return min + ratio * (max - min);
}

/* map minv <= val <= maxv into the range min..max (no clamping) */
static inline double map_range(double val, double minv, double maxv,
			       double min, double max)
{
	return map_val((val - minv) / (maxv - minv), min, max);
}

/* map val into the reverse range max..min */
static inline int reverse_range(int val, int min, int max)
{
	assert(min <= val && val <= max);
	return max - (val - min);
}
