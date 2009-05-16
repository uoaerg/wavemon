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
#include <locale.h>

/* GLOBALS */
static const struct {
	char key_name[6];
	enum wavemon_screen (*screen_func)(WINDOW *);
} screens[] = {
	[SCR_INFO]	= { "info",	scr_info  },
	[SCR_LHIST]	= { "lhist",	scr_lhist },
	[SCR_APLIST]	= { "aplst",	scr_aplst },
	[SCR_EMPTY_F4]	= { "",		NULL	  },
	[SCR_EMPTY_F5]	= { "",		NULL	  },
	[SCR_EMPTY_F6]	= { "",		NULL	  },
	[SCR_CONF]	= { "prefs",	scr_conf  },
	[SCR_HELP]	= { "help",	scr_help  },
	[SCR_ABOUT]	= { "about",	scr_about },
	[SCR_QUIT]	= { "quit",	NULL	  }
};

static void update_menubar(WINDOW *menu, const enum wavemon_screen active)
{
	enum wavemon_screen cur;

	for (cur = SCR_INFO, wmove(menu, 0, 0); cur <= SCR_QUIT; cur++) {
		wattrset(menu, A_REVERSE | A_BOLD);
		wprintw(menu, "F%d", cur + 1);

		wattrset(menu, cur != active ? COLOR_PAIR(CP_INACTIVE)
					     : COLOR_PAIR(CP_ACTIVE) | A_BOLD);
		wprintw(menu, "%-6s", screens[cur].key_name);
	}
	wrefresh(menu);
}

static void sig_winch(int signo)
{
	endwin();
	errx(1, "under the pain of death, thou shaltst not resize thyne window");
}

int main(int argc, char *argv[])
{
	WINDOW *w_menu;
	enum wavemon_screen cur, next;

	getconf(argc, argv);

	if (signal(SIGWINCH, sig_winch) == SIG_ERR)
		err(1, "cannot install handler for window changes");

	/* honour numeric separators if the environment defines them */
	setlocale(LC_NUMERIC, "");

	/* initialize the ncurses interface */
	initscr();
	if (LINES < MIN_SCREEN_LINES || COLS < MIN_SCREEN_COLS)
		fatal_error("need at least a screen of %ux%u, have only %ux%u",
			    MIN_SCREEN_LINES, MIN_SCREEN_COLS, LINES, COLS);
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

	w_menu = newwin(1, WAV_WIDTH, WAV_HEIGHT, 0);
	nodelay(w_menu, TRUE);
	keypad(w_menu, TRUE);

	for (cur = SCR_HELP, next = conf.startup_scr; next != SCR_QUIT; ) {

		if (screens[next].screen_func != NULL)
			cur = next;

		reinit_on_changes();
		update_menubar(w_menu, cur);
		next = (*screens[cur].screen_func)(w_menu);

		clear();
		refresh();
	}
	endwin();

	return EXIT_SUCCESS;
}
