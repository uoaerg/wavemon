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
#include <ctype.h>
#include <stdbool.h>

#define CFNAME ".wavemonrc"

struct wavemon_conf {
	char	ifname[32];
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
};

struct conf_item {
	char	*name,		/* name for preferences screen */
		*cfname;	/* name for ~/.wavemonrc */

	enum	{		/* type of parameter */
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

extern int conf_items;

void getconf(struct wavemon_conf *conf, int argc, char *argv[]);
void write_cf();
void dealloc_on_exit();

static inline void str_tolower(char *s)
{
	while (s && *s != '\0')
		*s++ = tolower(*s);
}
