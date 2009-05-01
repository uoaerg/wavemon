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

void waddstr_center(WINDOW *win, int y, const char *s)
{
	mvwaddstr(win, y, (COLS >> 1) - (strlen(s) >> 1), s);
}

/**
 * newwin_title  -  Create a new bordered window
 * @x:		vertical x position to start at
 * @h:		height of the new window in lines
 * @title:	name of the window
 * @nobottom:   whether to keep the bottom of the box open
 * Creates a new window starting column 0 in the line number @x,
 * spanning all COLS. Two columns are taken up by borders, hence
 * the inner width is COLS - 2.
 * The maximum inner height of the window is LINES - 2 - 1, as
 * one line is used by the menubar at the bottom of each screen.
 */
WINDOW *newwin_title(int x, int h, const char *title, bool nobottom)
{
	WINDOW *win = newwin(h, COLS, x, 0);
	chtype top_left  = x > 0 ? ACS_LTEE : ACS_ULCORNER;
	chtype top_right = x > 0 ? ACS_RTEE : ACS_URCORNER;

	if (nobottom) {
		mvwaddch(win, 0, 0, top_left);
		mvwhline(win, 0, 1, ACS_HLINE, COLS - 2);
		mvwvline(win, 1, 0, ACS_VLINE, h);
		mvwaddch(win, 0, COLS - 1, top_right);
		mvwvline(win, 1, COLS - 1, ACS_VLINE, h);
	} else {
		wborder(win, ACS_VLINE, ACS_VLINE, ACS_HLINE, ACS_HLINE,
			top_left, top_right, ACS_LLCORNER, ACS_LRCORNER);
	}
	wattrset(win, COLOR_PAIR(CP_WTITLE));
	mvwaddstr(win, 0, 2, title);
	wattroff(win, COLOR_PAIR(CP_WTITLE));

	return win;
}

/* clear inside window content up to the right border */
void mvwclrtoborder(WINDOW *win, int y, int x)
{
	if (x < COLS - 1 && x > 0)
		mvwhline(win, y, x, ' ', COLS - 1 - x);
}

void wclrtoborder(WINDOW *win)
{
	int x, y;

	getyx(win, y, x);
	mvwclrtoborder(win, y, x);
}

void waddstr_b(WINDOW *win, const char *s)
{
	wattron(win, A_BOLD);
	waddstr(win, s);
	wattroff(win, A_BOLD);
}

static double interpolate(const double val, const double min, const double max)
{
	return	val < min ? 0 :
		val > max ? 1 : (val - min) / (max - min);
}

void waddbar(WINDOW *win, float v, float minv, float maxv, int y, int x,
	     int maxx, char *cscale, bool rev)
{
	int len = (maxx - x) * interpolate(v, minv, maxv);
	chtype ch = '=' | A_BOLD | cp_from_scale(v, cscale, rev);

	mvwhline(win, y, x, ch, len);
	mvwclrtoborder(win, y, x + len);
}

void waddthreshold(WINDOW *win, float v, float tv, float minv, float maxv,
		   int y, int x, int maxx, char *cscale, chtype tch)
{
	if (tv > minv && tv < maxv) {
		if (v > tv)
			tch |= COLOR_PAIR(CP_STANDARD);
		else
			tch |= cp_from_scale(v, cscale, true);

		mvwaddch(win, y, x + (float)(maxx - x) * (tv - minv) / (maxv - minv), tch);
	}
}
