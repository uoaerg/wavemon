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
