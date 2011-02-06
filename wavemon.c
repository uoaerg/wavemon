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

/**
 * screen switching table
 * @key_name:	name under which the screen appears in the menu bar
 * @init:	screen initialisation function pointer
 * @loop:	screen update function pointer (connected to menu)
 * @fini:	screen cleanup function pointer
 */
static const struct {
	const char *const	key_name;
	void		 	(*init)(void);
	int			(*loop)(WINDOW *);
	void			(*fini)(void);
} screens[] = {
	[SCR_INFO]	= {
		.key_name = "info",
		.init	  = scr_info_init,
		.loop	  = scr_info_loop,
		.fini	  = scr_info_fini
	},
	[SCR_LHIST]	= {
		.key_name = "lhist",
		.init	  = scr_lhist_init,
		.loop	  = scr_lhist_loop,
		.fini	  = scr_lhist_fini
	},
	[SCR_APLIST]	= {
		.key_name = "scan",
		.init	  = scr_aplst_init,
		.loop	  = scr_aplst_loop,
		.fini	  = scr_aplst_fini
	},
	[SCR_EMPTY_F4]	= {
		.key_name = "",
	},
	[SCR_EMPTY_F5]	= {
		.key_name = "",
	},
	[SCR_EMPTY_F6]	= {
		.key_name = "",
	},
	[SCR_CONF]	= {
		.key_name = "prefs",
		.init	  = scr_conf_init,
		.loop	  = scr_conf_loop,
		.fini	  = scr_conf_fini
	},
	[SCR_HELP]	= {
		.key_name = "help",
		.init	  = scr_help_init,
		.loop	  = scr_help_loop,
		.fini	  = scr_help_fini
	},
	[SCR_ABOUT]	= {
		.key_name = "about",
		.init	  = scr_about_init,
		.loop	  = scr_about_loop,
		.fini	  = scr_about_fini
	},
	[SCR_QUIT]	= {
		.key_name = "quit",
	}
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

	if (!isatty(STDIN_FILENO))
		errx(1, "input is not from a terminal");

	xsignal(SIGWINCH, sig_winch);
	xsignal(SIGCHLD, SIG_IGN);

	/* honour numeric separators if the environment defines them */
	setlocale(LC_NUMERIC, "");

	/* initialize the ncurses interface */
	initscr();
	if (LINES < MIN_SCREEN_LINES || COLS < MIN_SCREEN_COLS)
		err_quit("need at least a screen of %ux%u, have only %ux%u",
			    MIN_SCREEN_LINES, MIN_SCREEN_COLS, LINES, COLS);
	cbreak();
	noecho();
	nonl();
	clear();
	curs_set(0);

	start_color();
	init_pair(CP_STANDARD,	COLOR_WHITE,	COLOR_BLACK);
	init_pair(CP_SCALEHI,	COLOR_RED,	COLOR_BLACK);
	init_pair(CP_SCALEMID,	COLOR_YELLOW,	COLOR_BLACK);
	init_pair(CP_SCALELOW,	COLOR_GREEN,	COLOR_BLACK);
	init_pair(CP_WTITLE,	COLOR_CYAN,	COLOR_BLACK);
	init_pair(CP_INACTIVE,	COLOR_CYAN,	COLOR_BLACK);
	init_pair(CP_ACTIVE,	COLOR_CYAN,	COLOR_BLUE);

	init_pair(CP_STATSIG,	  COLOR_GREEN,	COLOR_BLACK);
	init_pair(CP_STATNOISE,	  COLOR_RED,	COLOR_BLACK);
	init_pair(CP_STATSNR,	  COLOR_BLUE,	COLOR_BLUE);
	init_pair(CP_STATBKG,	  COLOR_BLUE,	COLOR_BLACK);
	init_pair(CP_STATSIG_S,	  COLOR_GREEN,	COLOR_BLUE);
	init_pair(CP_STATNOISE_S, COLOR_RED,	COLOR_BLUE);

	init_pair(CP_PREF_NORMAL, COLOR_WHITE,	COLOR_BLACK);
	init_pair(CP_PREF_SELECT, COLOR_WHITE,	COLOR_BLUE);
	init_pair(CP_PREF_ARROW,  COLOR_RED,	COLOR_BLACK);

	init_pair(CP_SCAN_CRYPT,  COLOR_RED,	COLOR_BLACK);
	init_pair(CP_SCAN_UNENC,  COLOR_GREEN,	COLOR_BLACK);
	init_pair(CP_SCAN_NON_AP, COLOR_YELLOW, COLOR_BLACK);

	for (cur = next = conf.startup_scr; next != SCR_QUIT; cur = next) {

		w_menu = newwin(1, WAV_WIDTH, WAV_HEIGHT, 0);
		nodelay(w_menu, TRUE);
		keypad(w_menu, TRUE);

		update_menubar(w_menu, cur);
		(*screens[cur].init)();
		do {
			int key = (*screens[cur].loop)(w_menu);

			if (key <= 0)
				usleep(5000);
			switch (key) {
			case KEY_F(1):
			case KEY_F(2):
			case KEY_F(3):
			case KEY_F(7):
			case KEY_F(8):
			case KEY_F(9):
			case KEY_F(10):
				next = key - KEY_F(1);
				break;
			case 'i':
				next = SCR_INFO;
				break;
			case 'q':
				next = SCR_QUIT;
				break;
			case KEY_F(4):
			case KEY_F(5):
			case KEY_F(6):
			default:
				next = cur;
			}
		} while (next == cur);

		delwin(w_menu);
		(*screens[cur].fini)();
		clear();
		refresh();
	}
	endwin();

	return EXIT_SUCCESS;
}
