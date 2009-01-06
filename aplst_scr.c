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
#include <ncurses.h>

#include "conf.h"
#include "ui.h"
#include "timer.h"
#include "aplst_scr.h"
#include "iw_if.h"
#include "defs.h"

struct wavemon_conf *conf;

void display_aplist(char *ifname, WINDOW *w_aplst)
{
	struct iw_aplist axp;
	char	s[0x100];
	int		ysize, xsize;
	int 	i;
	
	getmaxyx(w_aplst, ysize, xsize);
	for (i = 1; i < ysize - 1; i++)
		mvwhline(w_aplst, i, 1, ' ', xsize - 2);

	if (iw_get_aplist(ifname, &axp)) {
		if (axp.num) {
			sprintf(s, "%d access point(s) in range.", axp.num);
			mvwaddstr(w_aplst, 1, 1, s);

			if (axp.has_quality) {
				for (i = 0; i < axp.num; i++) {
					wmove(w_aplst, 3 + i * 2, 1);
					sprintf(s, "%2d ", i);
					waddstr(w_aplst, s);
					sprintf(s, "%2hhX:%2hhX:%2hhX:%2hhX:%2hhX:%2hhX",
							axp.aplist[i].addr.sa_data[0], axp.aplist[i].addr.sa_data[1], 
							axp.aplist[i].addr.sa_data[2], axp.aplist[i].addr.sa_data[3], 
							axp.aplist[i].addr.sa_data[4], axp.aplist[i].addr.sa_data[5]);
					waddstr_b(w_aplst, s);

					wmove(w_aplst, 4 + i * 2, 1);
					sprintf(s, "Link quality: %d, signal level: %d, noise level: %d",
							axp.aplist[i].quality.qual, axp.aplist[i].quality.level,
							axp.aplist[i].quality.noise);
					waddstr(w_aplst, s);
				}
			} else {
				for (i = 0; i < axp.num; i++) {
					wmove(w_aplst, 3 + i, 1);
					sprintf(s, "%2d ", i);
					waddstr(w_aplst, s);
					sprintf(s, "%2hhX:%2hhX:%2hhX:%2hhX:%2hhX:%2hhX",
							axp.aplist[i].addr.sa_data[0], axp.aplist[i].addr.sa_data[1], 
							axp.aplist[i].addr.sa_data[2], axp.aplist[i].addr.sa_data[3], 
							axp.aplist[i].addr.sa_data[4], axp.aplist[i].addr.sa_data[5]);
					waddstr_b(w_aplst, s);
				}
				waddstr_center(w_aplst, 4 + axp.num, "No link quality information available.");
			}

		} else waddstr_center(w_aplst, (LINES >> 1) - 1, "No access points in range.");
	} else waddstr_center(w_aplst, (LINES >> 1) - 1, "Access point list not available.");
}

int scr_aplst(struct wavemon_conf *wmconf) {
	WINDOW	*w_aplst, *w_menu;
	struct timer t1;
	int		key = 0;

	conf = wmconf;

	w_aplst = newwin_title(LINES - 1, COLS, 0, 0, "Access point list", 0, 0);
	w_menu = newwin(1, COLS, LINES - 1, 0);
	
	wmenubar(w_menu, 2);
	wmove(w_menu, 1, 0);
	nodelay(w_menu, TRUE); keypad(w_menu, TRUE);

	wrefresh(w_aplst);
	wrefresh(w_menu);
	
	do {
		do {
			display_aplist(conf->ifname, w_aplst);
			wrefresh(w_aplst);
			wmove(w_menu, 1, 0);
			wrefresh(w_menu);
			start_timer(&t1, 50000);
			while (!end_timer(&t1) && (key = wgetch(w_menu)) <= 0) usleep(5000);
		} while (key <= 0);
		while (!end_timer(&t1));
	} while (key < 265 || key > 275);
	
	werase(w_aplst); wrefresh(w_aplst); delwin(w_aplst);
	werase(w_menu); wrefresh(w_menu); delwin(w_menu);
	
	return key - 265;
}
