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
#include "wavemon.h"
#include <locale.h>
#include <setjmp.h>

/* GLOBALS */

/**
 * screen switching table
 * @key_name:	name under which the screen appears in the menu bar
 * @shortcut:   1-character shortcut key for @key_name
 * @init:	screen initialisation function pointer
 * @loop:	screen update function pointer (connected to menu)
 * @fini:	screen cleanup function pointer
 */
static const struct {
	const char *const	key_name;
	const char              shortcut;
	void		 	(*init)(void);
	int			(*loop)(WINDOW *);
	void			(*fini)(void);
} screens[] = {
	[SCR_INFO]	= {
		.key_name = "info",
		.shortcut = 'i',
		.init	  = scr_info_init,
		.loop	  = scr_info_loop,
		.fini	  = scr_info_fini
	},
	[SCR_LHIST]	= {
		.key_name = "lhist",
		.shortcut = 'l',
		.init	  = scr_lhist_init,
		.loop	  = scr_lhist_loop,
		.fini	  = scr_lhist_fini
	},
	[SCR_SCAN]	= {
		.key_name = "scan",
		.shortcut = 's',
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
	[SCR_PREFS]	= {
		.key_name = "prefs",
		.shortcut = 'p',
		.init	  = scr_conf_init,
		.loop	  = scr_conf_loop,
		.fini	  = scr_conf_fini
	},
	[SCR_HELP]	= {
		.key_name = "help",
		.shortcut = 'h',
		.init	  = scr_help_init,
		.loop	  = scr_help_loop,
		.fini	  = scr_help_fini
	},
	[SCR_ABOUT]	= {
		.key_name = "about",
		.shortcut = 'a',
		.init	  = scr_about_init,
		.loop	  = scr_about_loop,
		.fini	  = scr_about_fini
	},
	[SCR_QUIT]	= {
		.key_name = "quit",
		.shortcut = 'q',
	}
};

/*
 * SIGWINCH handling with buffer synchronisation variable
 */
static sigjmp_buf		env_winch;
static volatile sig_atomic_t	env_winch_ready;

static void sig_winch(int signo)
{
	if (env_winch_ready) {
		env_winch_ready = false;
		siglongjmp(env_winch, 1);
	}
}

static WINDOW *init_menubar(const enum wavemon_screen active)
{
	WINDOW *menu = newwin(1, WAV_WIDTH, WAV_HEIGHT, 0);
	char fkey[8];

	nodelay(menu, TRUE);
	keypad(menu, TRUE);
	wmove(menu, 0, 0);
	for (enum wavemon_screen cur = SCR_INFO; cur <= SCR_QUIT; cur++) {
		const char *p = screens[cur].key_name;

		if (*p) {
			snprintf(fkey, sizeof(fkey), "F%d", cur + 1);
			wattrset(menu, A_REVERSE | A_BOLD);
			waddstr(menu, fkey);

			wattrset(menu, cur != active ? COLOR_PAIR(CP_CYAN)
						     : COLOR_PAIR(CP_CYAN_ON_BLUE) | A_BOLD);

			for (int i = 0; i < MAX_MENU_KEY; i++) {
				if (*p == screens[cur].shortcut)	{
					wattron(menu, A_UNDERLINE);
					waddch(menu, *p++);
					wattroff(menu, A_UNDERLINE);
				} else if (*p) {
					waddch(menu, *p++);
				} else {
					waddch(menu, ' ');
				}
			}
		}
	}
	wrefresh(menu);

	return menu;
}

static void check_geometry(void)
{
	if (conf.check_geometry &&
	    (LINES < MIN_SCREEN_LINES || COLS < MIN_SCREEN_COLS))
		err_quit("need at least a screen of %ux%u, have only %ux%u",
			    MIN_SCREEN_LINES, MIN_SCREEN_COLS, LINES, COLS);
}

int main(int argc, char *argv[])
{
	int bg_color = COLOR_BLACK;
	enum wavemon_screen cur, next;
	sigset_t blockmask, oldmask;

	getconf(argc, argv);

	if (!isatty(STDIN_FILENO))
		errx(1, "input is not from a terminal");

	setlocale(LC_ALL, "");

	/* initialize the ncurses interface */
	initscr();
	noecho();
	nonl();
	cbreak();
	curs_set(0);
	clear();
	check_geometry();

	start_color();
	if (conf.transparent_bg) {
		bg_color = -1;
		use_default_colors();
	}

	init_pair(CP_STANDARD,	COLOR_WHITE,	bg_color);
	init_pair(CP_RED,	COLOR_RED,	bg_color);
	init_pair(CP_YELLOW,	COLOR_YELLOW,	bg_color);
	init_pair(CP_GREEN,	COLOR_GREEN,	bg_color);
	init_pair(CP_CYAN,	COLOR_CYAN,	bg_color);
	init_pair(CP_BLUE,	COLOR_BLUE,	bg_color);

	init_pair(CP_RED_ON_BLUE,	COLOR_RED,	COLOR_BLUE);
	init_pair(CP_GREEN_ON_BLUE,	COLOR_GREEN,	COLOR_BLUE);
	init_pair(CP_CYAN_ON_BLUE,	COLOR_CYAN,	COLOR_BLUE);
	init_pair(CP_BLUE_ON_BLUE,	COLOR_BLUE,	COLOR_BLUE);

	/* Override signal handlers installed during ncurses initialisation. */
	xsignal(SIGCHLD, SIG_IGN);
	xsignal(SIGWINCH, sig_winch);	/* triggers only when env_winch_ready */
	sigemptyset(&blockmask);
	sigaddset(&blockmask, SIGWINCH);

	for (cur = conf.startup_scr; cur != SCR_QUIT; cur = next) {
		WINDOW *w_menu;
		int escape = 0;

		if (sigprocmask(SIG_BLOCK, &blockmask, &oldmask) < 0)
			err_sys("cannot block SIGWINCH");

		next = cur;
		w_menu = init_menubar(cur);
		(*screens[cur].init)();

		if (sigprocmask(SIG_SETMASK, &oldmask, NULL) < 0)
			err_sys("cannot unblock SIGWINCH");

		if (sigsetjmp(env_winch, true) == 0) {
			env_winch_ready = true;

			do {
				int key = (*screens[cur].loop)(w_menu);

				if (key <= 0)
					usleep(5000);
				/*
				 * Translate vt100 PF1..4 escape sequences sent
				 * by some X terminals (e.g. aterm) into F1..F4.
				 */
				switch (key) {
				case 033:
					escape = 1;
					break;
				case 'O':
					escape = 2;
					break;
				case 'P' ... 'S':
					if (escape == 2)
						key = KEY_F(key - 'P' + 1);
					/* fall through */
				default:
					escape = 0;
				}

				/* Main menu */
				for (enum wavemon_screen s = SCR_INFO; s <= SCR_QUIT; s++) {
					if (*screens[s].key_name && (
					    key == (s == SCR_QUIT ? '0' : '1' + s) ||
					    key == KEY_F(s + 1) ||
					    key == screens[s].shortcut)) {
						next = s;
						break;
					}
				}
			} while (next == cur);
		}

		delwin(w_menu);
		(*screens[cur].fini)();

		/*
		 * next = cur is set in the protected critical section before
		 * sigsetjmp. Due to the loop condition, it can not occur when
		 * no SIGWINCH occurred, hence it indicates a resizing event.
		 */
		if (next == cur) {
			struct winsize size;

			if (ioctl(STDIN_FILENO, TIOCGWINSZ, &size) < 0)
				err_sys("can not determine terminal size");
			resizeterm(size.ws_row, size.ws_col);
			check_geometry();
		}
		clear();
		refresh();
	}
	curs_set(1);
	endwin();

	return EXIT_SUCCESS;
}
