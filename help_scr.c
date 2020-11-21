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

/* GLOBALS */
static WINDOW *w_help;

void scr_help_init(void)
{
	w_help = newwin_title(0, WAV_HEIGHT, "Help", false);
	waddstr_center(w_help, WAV_HEIGHT/2 - 1, "don't panic.");
	wrefresh(w_help);
}

int scr_help_loop(WINDOW *w_menu)
{
	return wgetch(w_menu);
}

void scr_help_fini(void)
{
	delwin(w_help);
}
