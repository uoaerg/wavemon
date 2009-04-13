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

static void sig_winch(int signo)
{
	endwin();
	dealloc_on_exit();
	errx(1, "under the pain of death, thou shaltst not resize thyne window");
}

int main(int argc, char *argv[])
{
	int (*current_scr) (void) = NULL;
	int nextscr;

	getconf(argc, argv);

	if (signal(SIGWINCH, sig_winch) < 0)
		err(1, "cannot install handler for window changes");

	/* initialize the ncurses interface */
	initscr();
	cbreak();
	noecho();
	nonl();
	clear();
	curs_set(0);

	start_color();
	init_pair(CP_STANDARD, COLOR_WHITE, COLOR_BLACK);
	init_pair(CP_SCALEHI, COLOR_RED, COLOR_BLACK);
	init_pair(CP_SCALEMID, COLOR_YELLOW, COLOR_BLACK);
	init_pair(CP_SCALELOW, COLOR_GREEN, COLOR_BLACK);
	init_pair(CP_WTITLE, COLOR_CYAN, COLOR_BLACK);
	init_pair(CP_INACTIVE, COLOR_CYAN, COLOR_BLACK);
	init_pair(CP_ACTIVE, COLOR_CYAN, COLOR_BLUE);
	init_pair(CP_STATSIG, COLOR_GREEN, COLOR_BLACK);
	init_pair(CP_STATNOISE, COLOR_RED, COLOR_BLACK);
	init_pair(CP_STATSNR, COLOR_BLUE, COLOR_BLUE);
	init_pair(CP_STATBKG, COLOR_BLUE, COLOR_BLACK);
	init_pair(CP_STATSIG_S, COLOR_GREEN, COLOR_BLUE);
	init_pair(CP_STATNOISE_S, COLOR_RED, COLOR_BLUE);
	init_pair(CP_PREF_NORMAL, COLOR_WHITE, COLOR_BLACK);
	init_pair(CP_PREF_SELECT, COLOR_WHITE, COLOR_BLUE);
	init_pair(CP_PREF_ARROW, COLOR_RED, COLOR_BLACK);

	switch (conf.startup_scr) {
	case 0:
		current_scr = scr_info;
		break;
	case 1:
		current_scr = scr_lhist;
		break;
	case 2:
		current_scr = scr_aplst;
		break;
	}

	do {
		reinit_on_changes();
		switch (nextscr = current_scr()) {
		case 0:
			current_scr = scr_info;
			break;
		case 1:
			current_scr = scr_lhist;
			break;
		case 2:
			current_scr = scr_aplst;
			break;
		case 6:
			current_scr = scr_conf;
			break;
		case 7:
			current_scr = scr_help;
			break;
		case 8:
			current_scr = scr_about;
			break;
		}
	} while (nextscr != 9);

	endwin();
	dealloc_on_exit();

	return 0;
}
