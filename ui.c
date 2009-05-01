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
			(t ? ACS_LTEE : ACS_ULCORNER),
			(t ? ACS_RTEE : ACS_URCORNER), ACS_LLCORNER,
			ACS_LRCORNER);

	wmove(win, 0, 2);
	wattrset(win, COLOR_PAIR(CP_WTITLE));
	waddstr(win, title);
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
