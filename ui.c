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

#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <ncurses.h>

#include "ui.h"
#include "defs.h"

char *screens[] = { "info", "lhist", "aplst", "", "", "", "prefs", "help", "about", "quit" };

void wmenubar(WINDOW *win, int active)
{
	char tmp[12];
	int i, j, n = 0;
	
	wmove(win, 0, 0);
	for (i = 0; i < 10; i++) {
		sprintf(tmp, "F%d", i + 1);
		wattrset(win, A_REVERSE | A_BOLD);
		waddstr(win, tmp);
		wattroff(win, A_REVERSE | A_BOLD);
		sprintf(tmp, "%s%n", screens[i], &n);
		wattrset(win, (i == active ? COLOR_PAIR(CP_ACTIVE) | A_BOLD : COLOR_PAIR(CP_INACTIVE)));
		waddstr(win, tmp);
		for (j = 0; j < 6 - n; j++) waddstr(win, " ");
		wattroff(win, COLOR_PAIR(6));
	}
}

void waddstr_center(WINDOW *win, int y, char *s)
{
	mvwaddstr(win, y, (COLS >> 1) - (strlen(s) >> 1), s);
}

WINDOW *newwin_title(int h, int w, int x, int y, char *title, char t, char b)
{
	WINDOW *win = newwin(h, w, x, y);
	if (b) {
		wmove(win, 0, 0);
		waddch(win, (t ? ACS_LTEE : ACS_ULCORNER));
		whline(win, ACS_HLINE, w - 2);
		mvwaddch(win, 0, w - 1, (t ? ACS_RTEE : ACS_URCORNER));
		mvwvline(win, 1, 0, ACS_VLINE, h);
		mvwvline(win, 1, w - 1, ACS_VLINE, h);
	} else
		wborder(win, ACS_VLINE, ACS_VLINE, ACS_HLINE, ACS_HLINE,
			(t ? ACS_LTEE : ACS_ULCORNER), (t ? ACS_RTEE : ACS_URCORNER),
			ACS_LLCORNER, ACS_LRCORNER);
	
	wmove(win, 0, 2);
	wattrset(win, COLOR_PAIR(5));
	waddstr(win, title);
	wattroff(win, COLOR_PAIR(5));
			
	return win;
}

void waddstr_b(WINDOW *win, char *s)
{
	wattron(win, A_BOLD);
	waddstr(win, s);
	wattroff(win, A_BOLD);
}

void waddbar(WINDOW *win, float v, float minv, float maxv, char y, char x, char maxx, char *cscale, char rev)
{
	int c;
	int col;
	int val = 0;

	char steps = maxx - x;

	val = (v <= maxv ? v : val);

	if (v < cscale[0]) col = (rev ? 2 : 4);
		else if (v < cscale[1]) col = 3;
			else col = (rev ? 4 : 2);

	for (c = 0; c < steps / (float)(maxv - minv) * (val - minv); c++)
		if (c < COLS -2) mvwaddch(win, y, x + c, '=' | A_BOLD | COLOR_PAIR(col));
	while (x + c < maxx) mvwaddch(win, y, x + c++, 183);
}

void waddthreshold(WINDOW *win, float v, float tv, float minv, float maxv, char y, char x, char maxx, char *cscale, char rev, char tch)
{
	int col;
	char steps = maxx - x;

	if (v < cscale[0]) col = (rev ? 2 : 4) | A_BOLD;
		else if (v < cscale[1]) col = 3 | A_BOLD;
			else col = (rev ? 4 : 2) | A_BOLD;

	if (tv > minv && tv < maxv)
		mvwaddch(win, y, x + (steps / (float)(maxv - minv) * (tv - minv)),
			tch | (v > tv ? COLOR_PAIR(col) | A_BOLD : COLOR_PAIR(CP_STANDARD)));
}
