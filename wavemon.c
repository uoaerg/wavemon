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
#include <setjmp.h>

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
	[SCR_SCAN]	= {
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
	[SCR_PREFS]	= {
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
	enum wavemon_screen cur;

	nodelay(menu, TRUE);
	keypad(menu, TRUE);
	wmove(menu, 0, 0);
	for (cur = SCR_INFO; cur <= SCR_QUIT; cur++) {
		wattrset(menu, A_REVERSE | A_BOLD);
		wprintw(menu, "F%d", cur + 1);

		wattrset(menu, cur != active ? COLOR_PAIR(CP_INACTIVE)
					     : COLOR_PAIR(CP_ACTIVE) | A_BOLD);
		wprintw(menu, "%-6s", screens[cur].key_name);
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
	WINDOW *w_menu;
	enum wavemon_screen cur, next;
	sigset_t blockmask, oldmask;

	getconf(argc, argv);

	if (!isatty(STDIN_FILENO))
		errx(1, "input is not from a terminal");

	/* honour numeric separators if the environment defines them */
	setlocale(LC_NUMERIC, "");

	/* initialize the ncurses interface */
	initscr();
	check_geometry();
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

	/* Override signal handlers installed during ncurses initialisation. */
	xsignal(SIGCHLD, SIG_IGN);
	xsignal(SIGWINCH, sig_winch);	/* triggers only when env_winch_ready */
	sigemptyset(&blockmask);
	sigaddset(&blockmask, SIGWINCH);

	for (cur = conf.startup_scr; cur != SCR_QUIT; cur = next) {
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
				switch (key) {
				case 'i':
				case KEY_F(1):
					next = SCR_INFO;
					break;
				case 'l':
				case KEY_F(2):
					next = SCR_LHIST;
					break;
				case 's':
				case KEY_F(3):
					next = SCR_SCAN;
					break;
				case 'p':
				case KEY_F(7):
					next = SCR_PREFS;
					break;
				case 'h':
				case KEY_F(8):
					next = SCR_HELP;
					break;
				case 'a':
				case KEY_F(9):
					next = SCR_ABOUT;
					break;
				case 'q':
				case KEY_F(10):
					next = SCR_QUIT;
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
	endwin();

	return EXIT_SUCCESS;
}
