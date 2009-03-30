/*
 * wavemon - a wireless network monitoring aplication
 *
 * Copyright (c) 2001-2002 Jan Morgenstern <jan@jm-music.de>
 * Copyright (c) 2009      Gerrit Renker <gerrit@erg.abdn.ac.uk>
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <err.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdbool.h>

#include <ncurses.h>

#include "llist.h"

#define CFNAME	".wavemonrc"

/*
 * Global in-memory representation of current wavemon configuration state
 */
extern struct wavemon_conf {
	char	ifname[LISTVAL_MAX];
	int	stat_iv,
		info_iv,
		sig_min, sig_max,
		noise_min, noise_max;

	int	lthreshold,
		hthreshold;

	int	slotsize;

	/* Boolean values which are 'char' for consistency with item->dep */
	char	dump,
		random,
		linear,
		override_bounds;

	char	lthreshold_action,	/* disabled|beep|flash|beep+flash */
		hthreshold_action;

	char	startup_scr;		/* info|histogram|aplist */
} conf;

/*
 *	Initialisation & Configuration functions
 */
extern void getconf(int argc, char *argv[]);
extern void reinit_on_changes(void);


/*
 * Configuration Items to manipulate the current configuration
 */
struct conf_item {
	char	*name,		/* name for preferences screen */
		*cfname;	/* name for ~/.wavemonrc */

	enum {			/* type of parameter */
		t_int,
		t_string,
		t_listval,
		t_switch,
		t_list,
		t_sep,		/* dummy */
		t_func		/* void (*fp) (void) */
	} type;

	union {			/* type-dependent container for value */
		int	*i;	/* t_int */
		char	*s;	/* t_string, t_listval */
		char	*b;	/*
				 * t_switch: a boolean type
				 * t_list: an enum type where 0 means "off"
				 * A pointer is needed to propagate the changes. See
				 * the 'char' types in the above wavemon_conf struct.
				 */
		void (*fp)();	/* t_func */
	} v;

	int	list;		/* list of available settings (for t_list) */
	char	*dep;		/* dependency (must be t_switch) */

	double	min,		/* value boundaries */
		max,
		inc;		/* increment for value changes */

	char	*unit;		/* name of units to display */
};

/*
 * Handling of Configuration Items
 */
extern int  conf_items;
extern void dealloc_on_exit(void);

/*
 *	Screen functions
 */
extern int scr_about(void);
extern int scr_aplst(void);
extern int scr_conf(void);
extern int scr_help(void);
extern int scr_info(void);
extern int scr_lhist(void);

/*
 *	Ncurses definitions and functions
 */
extern WINDOW *newwin_title(int h, int w, int x, int y, char *title, char t, char b);

extern void waddstr_b(WINDOW *win, const char *s);
extern void waddstr_center(WINDOW *win, int y, const char *s);

extern void wmenubar(WINDOW *win, int active);
extern void waddbar(WINDOW *win, float v, float minv, float maxv, int y, int x,
		    int maxx, char *cscale, bool rev);
extern void waddthreshold(WINDOW *win, float v, float tv, float minv, float maxv,
			  int y, int x, int maxx, char *cscale, chtype tch);
enum colour_pair {
	CP_STANDARD = 1,
	CP_SCALEHI,
	CP_SCALEMID,
	CP_SCALELOW,
	CP_WTITLE,
	CP_INACTIVE,
	CP_ACTIVE,
	CP_STATSIG,
	CP_STATNOISE,
	CP_STATSNR,
	CP_STATBKG,
	CP_STATSIG_S,
	CP_STATNOISE_S,
	CP_PREF_NORMAL,
	CP_PREF_SELECT,
	CP_PREF_ARROW
};

static inline int cp_from_scale(float value, const char *cscale, bool reverse)
{
	enum colour_pair cp;

	if (value < (float)cscale[0])
		cp = reverse ? CP_SCALEHI : CP_SCALELOW;
	else if (value < (float)cscale[1])
		cp = CP_SCALEMID;
	else
		cp = reverse ? CP_SCALELOW : CP_SCALEHI;

	return COLOR_PAIR(cp);
}


/*
 *	Timers
 */
struct timer {
	unsigned long long	stime;
	unsigned long		duration;
};

extern void start_timer(struct timer *t, unsigned long d);
extern int  end_timer(struct timer *t);

/*
 *	Error handling
 */
extern void fatal_error(char *format, ...);

/*
 *	Helper functions
 */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static inline void str_tolower(char *s)
{
	while (s && *s != '\0')
		*s++ = tolower(*s);
}

