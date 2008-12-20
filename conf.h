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

struct wavemon_conf {
	char	ifname[32];
	int		stat_iv;
	int		info_iv;
	int		sig_min, sig_max, noise_min, noise_max;
	char	dump,
			random,
			linear,
			override_bounds;
	int		lthreshold_action, lthreshold;
	int		hthreshold_action, hthreshold;
	int		slotsize;
	int		startup_scr;
};

struct conf_item {
	/* pointer to value */
	void	*v;
	/* name for preferences screen */
	char	*name;
	/* type of parameter */
	enum	{t_int, t_float, t_string, t_switch, t_list, t_listval, t_sep, t_func} type;
	/* list of available settings (for t_list) */
	int	list;
	/* value boundaries */
	double	min, max;
	/* increment for value changes */
	double	inc;
	/* name of units to display */
	char	*unit;
	/* dependency (must be t_switch) */
	char	*dep;
	/* name for ~/.wavemonrc */
	char	*cfname;
};

extern int conf_items;

void getconf(struct wavemon_conf *conf, int argc, char *argv[]);
void write_cf();
void dealloc_on_exit();
