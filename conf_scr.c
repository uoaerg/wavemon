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
#include "wavemon.h"

/* Make configuration screen fit into half of minimum screen width */
#define CONF_SCREEN_WIDTH	(MIN_SCREEN_COLS / 2)

static int first_item;
static int list_ofs = 0;
static int wconfx, wconfy;

static void waddstr_item(WINDOW *w, int y, struct conf_item *item, char hilight)
{
	char s[0x40];

	wattrset(w, COLOR_PAIR(CP_PREF_NORMAL));

	mvwhline(w, y, 0, ' ', CONF_SCREEN_WIDTH);

	if (item->type != t_sep && item->type != t_func) {
		mvwaddstr(w, y, item->dep ? 2 : 0, item->name);

		switch (item->type) {
		case t_int:
			sprintf(s, "%d", *item->v.i);
			break;
		case t_string:
			strncpy(s, item->v.s, item->max);
			break;
		case t_switch:
			strcpy(s, *item->v.b ? "Enabled" : "Disabled");
			break;
		case t_list:
			strncpy(s, ll_get(item->list, *item->v.b), LISTVAL_MAX);
			break;
		case t_listval:
			strncpy(s, item->v.s, LISTVAL_MAX);
			/* Fall through, dummy statements to pacify gcc -Wall */
		case t_sep:
		case t_func:
			break;
		}

		if (!item->unit) {
			wmove(w, y, CONF_SCREEN_WIDTH - strlen(s));
			if (hilight) {
				wattron(w, A_REVERSE);
				waddstr_b(w, s);
				wattroff(w, A_REVERSE);
			} else
				waddstr_b(w, s);
		} else {
			wmove(w, y, CONF_SCREEN_WIDTH - strlen(s) - strlen(item->unit) - 1);
			if (hilight) {
				wattron(w, A_REVERSE);
				waddstr_b(w, s);
				wattroff(w, A_REVERSE);
			} else
				waddstr_b(w, s);
			mvwaddstr(w, y, CONF_SCREEN_WIDTH - strlen(item->unit), item->unit);
		}
	} else if (item->type == t_sep && item->name) {
		sprintf(s, "- %s -", item->name);
		mvwaddstr(w, y, (CONF_SCREEN_WIDTH  - strlen(s)) / 2, s);
	} else if (item->type == t_func) {
		wmove(w, y, (CONF_SCREEN_WIDTH - strlen(item->name)) / 2);
		if (hilight) {
			wattron(w, A_REVERSE);
			waddstr(w, item->name);
			wattroff(w, A_REVERSE);
		} else
			waddstr(w, item->name);
	}
}

static void change_item(int inum, char sign, char accel)
{
	struct conf_item *item = ll_get(conf_items, inum);
	int tmp;

	switch (item->type) {
	case t_int:
		if (!accel) {
			if (*item->v.i + item->inc * sign <= item->max &&
			    *item->v.i + item->inc * sign >= item->min)
				*item->v.i += item->inc * sign;
		} else if (*item->v.i + 10 * item->inc * sign <= item->max &&
			   *item->v.i + 10 * item->inc * sign >= item->min) {
			*item->v.i += 10 * item->inc * sign;
		} else if (sign > 0) {
			if (*item->v.i < item->max)
				*item->v.i = item->max;
		} else if (*item->v.i > item->min) {
			*item->v.i = item->min;
		}
		break;
	case t_switch:
		*item->v.b = *item->v.b == 0 ? 1 : 0;
		break;
	case t_list:
		*item->v.b = *item->v.b + sign;
		if (*item->v.b >= ll_size(item->list))
			*item->v.b = 0;
		else if (*item->v.b < 0)
			*item->v.b = ll_size(item->list) - 1;
		break;
	case t_listval:
		tmp = ll_scan(item->list, "s", item->v.s);
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
		strncpy(item->v.s, ll_get(item->list, tmp), LISTVAL_MAX);
		break;
		/* Dummy statements to pacify gcc -Wall */
	case t_string:
	case t_sep:
	case t_func:
		break;
	}
}

static int next_item(int rv)
{
	struct conf_item *item = ll_get(conf_items, rv);

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

static int prev_item(int rv)
{
	struct conf_item *item;

	do {
		rv--;
		item = ll_get(conf_items, rv);
	} while (item->type == t_sep || (item->dep && !*item->dep));

	if (rv < first_item)
		rv = ll_size(conf_items) - 1;

	return rv;
}

static int m_pref(WINDOW *w_conf, int active_item)
{
	struct conf_item *item;
	int active_line, i, j, items = ll_size(conf_items);

	werase(w_conf);

	for (active_line = i = j = 0; i < items; i++) {
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

enum wavemon_screen scr_conf(WINDOW *w_menu)
{
	WINDOW *w_conf, *w_confpad;
	int key = 0;
	int active_line, active_item = 0;
	int subw;
	void (*func) ();
	struct conf_item *item;

	w_conf    = newwin_title(0, LINES - 1, "Preferences", false);
	w_confpad = newpad(ll_size(conf_items) + 1, CONF_SCREEN_WIDTH);

	getmaxyx(w_conf, wconfy, wconfx);
	subw = wconfy - 3;

	while ((item = ll_get(conf_items, active_item)) && item->type == t_sep)
		active_item++;
	first_item = active_item;

	while (key < KEY_F(1) || key > KEY_F(10)) {
		active_line = m_pref(w_confpad, active_item);

		if (active_line - list_ofs > subw) {
			list_ofs = active_line - subw;
		} else if (active_line < list_ofs)
			list_ofs = active_line;

		prefresh(w_confpad, list_ofs, 0, 1, (COLS - CONF_SCREEN_WIDTH)/2,
				    wconfy - 2, wconfx - 1);
		wrefresh(w_conf);

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
				func = item->v.fp;
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

	delwin(w_conf);
	delwin(w_confpad);

	return key - KEY_F(1);
}
