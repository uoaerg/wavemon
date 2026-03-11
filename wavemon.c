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
#include "iw_if.h"
#include <locale.h>
#include <setjmp.h>
#include <fcntl.h>

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

static void sig_winch(int __attribute__((unused))signo)
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
		const int attrs = cur != active ? COLOR_PAIR(CP_CYAN)
						: COLOR_PAIR(CP_CYAN_ON_BLUE) | A_BOLD;

		if (*p) {
			snprintf(fkey, sizeof(fkey), "F%d", cur + 1);
			wattrset(menu, A_REVERSE | A_BOLD);
			waddstr(menu, fkey);

			wattrset(menu, attrs);

			for (int i = 0; i < MAX_MENU_KEY; i++) {
				if (*p == screens[cur].shortcut)	{
					wattron(menu, A_UNDERLINE);
					waddch(menu, *p++);
					wattroff(menu, A_UNDERLINE);
				} else if (*p) {
					waddch(menu, *p++);
				} else {
					wattroff(menu, attrs);
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
	volatile enum wavemon_screen cur, next;
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

	init_pair(CP_RED,	COLOR_RED,	bg_color);
	init_pair(CP_YELLOW,	COLOR_YELLOW,	bg_color);
	init_pair(CP_GREEN,	COLOR_GREEN,	bg_color);
	init_pair(CP_CYAN,	COLOR_CYAN,	bg_color);
	init_pair(CP_BLUE,	COLOR_BLUE,	bg_color);

	init_pair(CP_RED_ON_BLUE,	COLOR_RED,	COLOR_BLUE);
	init_pair(CP_GREEN_ON_BLUE,	COLOR_GREEN,	COLOR_BLUE);
	init_pair(CP_CYAN_ON_BLUE,	COLOR_CYAN,	COLOR_BLUE);
	init_pair(CP_BLUE_ON_BLUE,	COLOR_BLUE,	COLOR_BLUE);

	/* If no interface was specified and multiple are available, let user pick. */
	{
		size_t n_ifaces = conf_interface_count();

		if (!conf.iface_given && n_ifaces > 1) {
			int w = COLS > 72 ? 70 : COLS - 2;
			WINDOW *sel = newwin(n_ifaces + 4, w,
					     (LINES - (int)n_ifaces - 4) / 2,
					     (COLS - w) / 2);
			box(sel, 0, 0);
			mvwaddstr(sel, 1, 2, "select wireless interface:");
			for (size_t i = 0; i < n_ifaces; i++) {
				char line[128], product[96];

				if_get_product(conf_interface_name(i),
					       product, sizeof(product));
				snprintf(line, sizeof(line), " %zu) %s (%s)",
					 i + 1, conf_interface_name(i), product);
				if ((int)i == conf.if_idx)
					wattron(sel, A_REVERSE);
				mvwaddnstr(sel, (int)i + 2, 2, line, w - 4);
				wattroff(sel, A_REVERSE);
			}
			mvwaddstr(sel, (int)n_ifaces + 2, 2,
				  "arrows/j/k to move, enter to select");
			wrefresh(sel);
			keypad(sel, TRUE);

			int choice = conf.if_idx;
			int key;

			for (;;) {
				key = wgetch(sel);
				if (key == '\n' || key == '\r')
					break;
				if (key == KEY_UP || key == 'k') {
					if (choice > 0)
						choice--;
				} else if (key == KEY_DOWN || key == 'j') {
					if (choice < (int)n_ifaces - 1)
						choice++;
				} else if (key >= '1' && key < '1' + (int)n_ifaces) {
					choice = key - '1';
					break;
				} else if (key == 'q' || key == 27) {
					endwin();
					exit(EXIT_SUCCESS);
				}

				for (size_t i = 0; i < n_ifaces; i++) {
					char line[128], product[96];

					if_get_product(conf_interface_name(i),
						       product, sizeof(product));
					snprintf(line, sizeof(line),
						 " %zu) %s (%s)",
						 i + 1, conf_interface_name(i),
						 product);
					if ((int)i == choice)
						wattron(sel, A_REVERSE);
					mvwaddnstr(sel, (int)i + 2, 2, line, w - 4);
					wattroff(sel, A_REVERSE);
				}
				wrefresh(sel);
			}
			conf.if_idx = choice;
			delwin(sel);
			clear();
			refresh();
		}
	}

	/* Check for driver-specific quirks after interface is known. */
	if_check_driver_quirks(conf_ifname());

	/* Save USB path for carl9170 crash recovery (before ncurses). */
	carl9170_recovery_init(conf_ifname());

	/* Override signal handlers installed during ncurses initialisation. */
	xsignal(SIGCHLD, SIG_IGN);
	xsignal(SIGWINCH, sig_winch);	/* triggers only when env_winch_ready */
	sigemptyset(&blockmask);
	sigaddset(&blockmask, SIGWINCH);

	for (cur = conf.startup_scr; cur != SCR_QUIT; cur = next) {
		WINDOW *w_menu;
		volatile int escape = 0;

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
				default:
					if ('P' <= key && key <= 'S' && escape == 2)
						key = KEY_F(key - 'P' + 1);
					escape = 0;
				}

				/* Main menu */
				for (enum wavemon_screen s = SCR_INFO; s <= SCR_QUIT; s++) {
					if (*screens[s].key_name && (
					    (unsigned)key == (s == SCR_QUIT ? '0' : '1' + s) ||
					    (unsigned)key == KEY_F(s + 1) ||
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
		 * carl9170 crash: USB present but driver dead.
		 * Show banner, attempt recovery, show result.
		 */
		if (interface_crash) {
			interface_crash = 0;
			int mid = LINES / 2, i;
			int attempts;
			bool recovered = false;
			const char *l1 = conf_ifname();
			char line1[128];

			snprintf(line1, sizeof(line1),
				 "%s crashed — recovering", l1);
			attron(COLOR_PAIR(CP_RED) | A_BOLD | A_REVERSE);
			for (i = mid - 2; i <= mid + 2; i++)
				mvhline(i, 1, ' ', COLS - 2);
			mvprintw(mid,
				 (COLS - (int)strlen(line1)) / 2,
				 "%s", line1);
			attroff(COLOR_PAIR(CP_RED) | A_BOLD | A_REVERSE);
			refresh();

			/* Suppress stderr during recovery (ncurses active) */
			{
				int saved_fd = dup(STDERR_FILENO);
				int null_fd = open("/dev/null", O_WRONLY);
				if (null_fd >= 0) {
					dup2(null_fd, STDERR_FILENO);
					close(null_fd);
				}

				for (attempts = 0;
				     attempts < CARL9170_MAX_REBIND_ATTEMPTS;
				     attempts++) {
					if (carl9170_recovery_attempt()) {
						recovered = true;
						break;
					}
				}

				if (saved_fd >= 0) {
					dup2(saved_fd, STDERR_FILENO);
					close(saved_fd);
				}
			}

			if (recovered) {
				char ok[128];

				snprintf(ok, sizeof(ok),
					 "%s recovered", l1);
				attron(COLOR_PAIR(CP_GREEN) | A_BOLD | A_REVERSE);
				for (i = mid - 2; i <= mid + 2; i++)
					mvhline(i, 1, ' ', COLS - 2);
				mvprintw(mid,
					 (COLS - (int)strlen(ok)) / 2,
					 "%s", ok);
				attroff(COLOR_PAIR(CP_GREEN) | A_BOLD | A_REVERSE);
				refresh();
				sleep(3);

				next = cur;
				goto screen_done;
			}

			/* Recovery failed — fall through to interface_lost */
			interface_lost = 1;
		}

		/*
		 * Interface lost: try to fall back to another wireless
		 * interface, or exit if none are available.
		 */
		if (interface_recovered) {
			interface_recovered = 0;
			conf_get_interface_list();

			/* Find the preferred interface in the new list */
			for (size_t i = 0; i < conf_interface_count(); i++) {
				if (strcmp(conf_interface_name(i),
					   preferred_ifname) == 0) {
					conf.if_idx = (int)i;
					break;
				}
			}

			carl9170_recovery_init(conf_ifname());
			if_check_driver_quirks(conf_ifname());

			{
				char line1[128], line2[64];
				int mid = LINES / 2, i;

				snprintf(line1, sizeof(line1),
					 "%s is back", preferred_ifname);
				snprintf(line2, sizeof(line2),
					 "switching from %s", conf_ifname());
				attron(COLOR_PAIR(CP_GREEN) | A_BOLD | A_REVERSE);
				for (i = mid - 2; i <= mid + 2; i++)
					mvhline(i, 1, ' ', COLS - 2);
				mvprintw(mid - 1,
					 (COLS - (int)strlen(line1)) / 2,
					 "%s", line1);
				mvprintw(mid + 1,
					 (COLS - (int)strlen(line2)) / 2,
					 "%s", line2);
				attroff(COLOR_PAIR(CP_GREEN) | A_BOLD | A_REVERSE);
				refresh();
				sleep(3);
			}

			preferred_ifname[0] = '\0';
			next = cur;
			goto screen_done;
		}

		if (interface_lost) {
			struct interface_info *head = NULL;

			interface_lost = 0;
			iw_nl80211_get_interface_list(&head);

			if (head) {
				const char *old_if = conf_ifname();
				char old_name[IF_NAMESIZE];

				snprintf(old_name, sizeof(old_name),
					 "%s", old_if);

				/* Remember preferred interface for comeback */
				if (!preferred_ifname[0])
					snprintf(preferred_ifname,
						 sizeof(preferred_ifname),
						 "%s", old_name);

				conf.if_idx = 0;
				conf_get_interface_list();
				free_interface_list(head);

				carl9170_recovery_init(conf_ifname());
				if_check_driver_quirks(conf_ifname());

				{
					char line1[128], line2[64];
					int mid = LINES / 2, i;

					snprintf(line1, sizeof(line1),
						 "%s lost", old_name);
					snprintf(line2, sizeof(line2),
						 "switching to %s",
						 conf_ifname());
					attron(COLOR_PAIR(CP_RED) | A_BOLD | A_REVERSE);
					for (i = mid - 2; i <= mid + 2; i++)
						mvhline(i, 1, ' ', COLS - 2);
					mvprintw(mid - 1,
						 (COLS - (int)strlen(line1)) / 2,
						 "%s", line1);
					mvprintw(mid + 1,
						 (COLS - (int)strlen(line2)) / 2,
						 "%s", line2);
					attroff(COLOR_PAIR(CP_RED) | A_BOLD | A_REVERSE);
					refresh();
					sleep(3);
				}

				next = cur;
				goto screen_done;
			}

			/* No wireless interfaces at all */
			endwin();
			err_quit("all wireless interfaces lost");
		}

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
screen_done:
		clear();
		refresh();
	}
	curs_set(1);
	endwin();

	return EXIT_SUCCESS;
}
