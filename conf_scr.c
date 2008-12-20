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
#include <ncurses.h>

#include "conf.h"
#include "ui.h"
#include "conf_scr.h"
#include "llist.h"
#include "defs.h"

struct wavemon_conf *conf;

static int	first_item;
static int	list_ofs = 0;
static int	wconfx, wconfy;

void waddstr_item(WINDOW *w, int y, struct conf_item *item, char hilight)
{
	char	s[0x40];
	
	int	x1 = 0,
		x2 = 40;

	wattrset(w, COLOR_PAIR(CP_PREF_NORMAL));

	mvwhline(w, y, x1, ' ', x2 - x1);
	
	if (item->type != t_sep && item->type != t_func) {
		mvwaddstr(w, y, (item->dep ? x1 + 2 : x1), item->name);

		switch (item->type) {
			case t_int:
				sprintf(s, "%ld", *(long int *)item->v);
				break;
			case t_float:
				sprintf(s, "%f", *(double *)item->v);
				break;
			case t_string:
				strncpy(s, item->v, item->max);
				break;
			case t_switch:
				strcpy(s, (*(char *)item->v ? "Enabled" : "Disabled"));
				break;
			case t_list:
				strncpy(s, ll_get(item->list, *(int *)item->v), 32);
				break;
			case t_listval:
				strncpy(s, (char *)item->v, 32);
			/* Fall through, dummy statements to pacify gcc -Wall */
			case t_sep:
			case t_func:
				break;
		}

		if (!item->unit) {
			wmove(w, y, x2 - strlen(s));
			if (hilight) {
				wattron(w, A_REVERSE);
				waddstr_b(w, s);
				wattroff(w, A_REVERSE);
			} else
				waddstr_b(w, s);
		} else {
			wmove(w, y, x2 - strlen(s) - strlen(item->unit) - 1);
			if (hilight) {
				wattron(w, A_REVERSE);
				waddstr_b(w, s);
				wattroff(w, A_REVERSE);
			} else
				waddstr_b(w, s);
			mvwaddstr(w, y, x2 - strlen(item->unit), item->unit);
		}
	} else if (item->type == t_sep && item->name) {
		sprintf(s, "- %s -", item->name);
		mvwaddstr(w, y, 20 - (strlen(s) >> 1), s);
	} else if (item->type == t_func) {
		wmove(w, y, 20 - (strlen(item->name) >> 1));
		if (hilight) {
			wattron(w, A_REVERSE);
			waddstr(w, item->name);
			wattroff(w, A_REVERSE);
		} else waddstr(w, item->name);
	}
}

void change_item(int inum, char sign, char accel)
{
	struct conf_item *item = ll_get(conf_items, inum);
	int tmp;
		
	switch (item->type) {
		case t_int:
			if (!accel) {
				if (*(long int *)item->v + item->inc * sign <= item->max &&
				    *(long int *)item->v + item->inc * sign >= item->min)
					*(long int *)item->v += item->inc * sign;
			} else if (*(long int *)item->v + 10 * item->inc * sign <= item->max &&
				   *(long int *)item->v + 10 * item->inc * sign >= item->min) {
					*(long int *)item->v += 10 * item->inc * sign;
			} else if (sign > 0) {
				if (*(long int *)item->v < item->max)
					*(long int *)item->v = item->max;
			} else if (*(long int *)item->v > item->min) {
					*(long int *)item->v = item->min;
			}
			break;
		case t_switch:
			*(char *)item->v = !*(char *)item->v;
			break;
		case t_list:
			*(long int *)item->v += sign;
			if (*(long int *)item->v >= ll_size(item->list))
				*(long int *)item->v = 0;
			else if (*(long int *)item->v < 0)
				*(long int *)item->v = ll_size(item->list) - 1;
			break;
		case t_listval:
			tmp = ll_scan(item->list, "s", (char *)item->v);
			if (tmp == -1) {
				tmp = 0;
			} else {
				tmp += sign;
				if (tmp >= ll_size(item->list)) {
					tmp = 0;
				} else if (tmp < 0) {
					tmp = ll_size(item->list) - 1;
				}
			}
			strncpy(item->v, ll_get(item->list, tmp), 32);
			break;
		/* Dummy statements to pacify gcc -Wall */
		case t_float:
		case t_string:
		case t_sep:
		case t_func:
			break;
	}
}	

int next_item(int inum)
{
	int	rv = inum;
	struct conf_item *item = ll_get(conf_items, inum);
	
	do {
		rv++;
		item = ll_get(conf_items, rv);
	} while (item->type == t_sep || (item->dep && !*item->dep));

	if (rv >= ll_size(conf_items)) {	
		rv = first_item;
		list_ofs = 0;
	}
	
	return rv;
}

int prev_item(int inum)
{
	int	rv = inum;
	struct conf_item *item;
	
	do {
		rv--;
		item = ll_get(conf_items, rv);
	} while (item->type == t_sep || (item->dep && !*item->dep));

	if (rv < first_item)
		rv = ll_size(conf_items) - 1;
	
	return rv;
}

int m_pref(WINDOW *w_conf, int active_item)
{
	struct conf_item *item;
	int active_line = 0;
	int items, i, j = 0;

	werase(w_conf);

	items = ll_size(conf_items);
	for (i = 0; i < items; i++) {
		item = ll_get(conf_items, i);
		if (!item->dep || *item->dep) {
			if (i != active_item)
				waddstr_item(w_conf, j++, item, 0);
			else {
				waddstr_item(w_conf, j, item, 1);
				active_line = j++;
			}
		}
	}
	
	return active_line;
}

int scr_conf(struct wavemon_conf *wmconf)
{
	WINDOW	*w_conf, *w_menu, *w_confpad;
	int	key = 0;
	int	num_items;
	int	active_line, active_item = 0;
	int	subw;
	void	(*func)();
	struct conf_item *item;

	conf = wmconf;
	
	num_items = ll_size(conf_items);

	w_conf = newwin_title(LINES - 1, COLS, 0, 0, "Preferences", 0, 0);
	w_confpad = newpad(ll_size(conf_items) + 1, 40);
	w_menu = newwin(1, COLS, LINES - 1, 0);
	
	getmaxyx(w_conf, wconfy, wconfx);
	subw = wconfy - 3;
	
	while ((item = ll_get(conf_items, active_item)) && item->type == t_sep)
		active_item++;
	first_item = active_item;
	
	wmenubar(w_menu, 6);
	wmove(w_menu, 1, 0);
	nodelay(w_menu, FALSE);
	keypad(w_menu, TRUE);

	wrefresh(w_conf);
	wrefresh(w_menu);
	
	active_line = m_pref(w_confpad, active_item);

	while (key < KEY_F(1) || key > KEY_F(10)) {
		active_line = m_pref(w_confpad, active_item);
		
		if (active_line - list_ofs > subw) {
			list_ofs = active_line - subw;
		} else if (active_line < list_ofs)
			list_ofs = active_line;

		prefresh(w_confpad, list_ofs, 0, 1, (COLS >> 1) - 20, wconfy - 2, wconfx - 1);
		wmove(w_menu, 1, 0);
		wrefresh(w_menu);
		key = wgetch(w_menu);
		switch (key) {
			case KEY_DOWN:
				active_item = next_item(active_item);
				break;
			case KEY_UP:
				active_item = prev_item(active_item);
				break;
			case KEY_LEFT:
				change_item(active_item, -1, 0);
				break;
			case KEY_RIGHT:
				change_item(active_item, 1, 0);
				break;
			case '\r':
				item = ll_get(conf_items, active_item);
				if (item->type == t_func) {
					flash();
					func = item->v;
					func();
				}
				break;
			/* Keyboard shortcuts */
			case 'q':
				key = KEY_F(10);
				break;
			case 'i':
				key = KEY_F(1);
		}
	}
	
	werase(w_conf); wrefresh(w_conf); delwin(w_conf);
	delwin(w_confpad);
	werase(w_menu); wrefresh(w_menu); delwin(w_menu);

	return key - KEY_F(1);
}
