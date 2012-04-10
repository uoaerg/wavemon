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

/**
 * newwin_title  -  Create a new bordered window at (y, 0)
 * @y:		vertical row position to start at
 * @h:		height of the new window in lines
 * @title:	name of the window
 * @nobottom:   whether to keep the bottom of the box open
 */
WINDOW *newwin_title(int y, int h, const char *title, bool nobottom)
{
	WINDOW *win = newwin(h, WAV_WIDTH, y, 0);

	chtype top_left  = y > 0 ? ACS_LTEE : ACS_ULCORNER;
	chtype top_right = y > 0 ? ACS_RTEE : ACS_URCORNER;

	if (nobottom) {
		mvwaddch(win, 0, 0, top_left);
		mvwhline(win, 0, 1, ACS_HLINE, MAXXLEN);
		mvwvline(win, 1, 0, ACS_VLINE, h);
		mvwaddch(win, 0, WAV_WIDTH - 1, top_right);
		mvwvline(win, 1, WAV_WIDTH - 1, ACS_VLINE, h);
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
	if (x >= 1 && x <= MAXXLEN)
		mvwhline(win, y, x, ' ', 1 + MAXXLEN - x);
}

void wclrtoborder(WINDOW *win)
{
	int x, y;

	getyx(win, y, x);
	mvwclrtoborder(win, y, x);
}

void waddstr_center(WINDOW *win, int y, const char *s)
{
	mvwaddstr(win, y, (WAV_WIDTH - strlen(s)) / 2, s);
}

void wadd_attr_str(WINDOW *win, const int attrs, const char *s)
{
	wattron(win, attrs);
	waddstr(win, s);
	wattroff(win, attrs);
}

/* Enforce that @str is at most @len characters (excluding the terminal '\0') */
const char *curtail(const char *str, const char *sep, int len)
{
	static char out_buf[128];
	const char fallback_sep[] = "~";
	int l = 0, front, mid, back;

	if (len >= sizeof(out_buf))
		len = sizeof(out_buf) - 1;

	if (sep == NULL || *sep == '\0')
		sep = fallback_sep;
	mid = strlen(sep);
	if (mid > len) {
		sep = fallback_sep;
		mid = strlen(sep);
	}

	if (str != NULL)
		l = strlen(str);
	if (l <= len)
		return str;

	front = (len - mid)/2.0 + 0.5;
	back  = len - front - mid;

	strncpy(out_buf, str, front);
	strncpy(out_buf + front, sep, mid);
	strncpy(out_buf + front + mid, str + l - back, back + 1);

	return out_buf;
}

static double interpolate(const double val, const double min, const double max)
{
	return	val < min ? 0 :
		val > max ? 1 : (val - min) / (max - min);
}

void waddbar(WINDOW *win, int y, float v, float min, float max,
		    char *cscale, bool rev)
{
	chtype ch = '=' | A_BOLD | cp_from_scale(v, cscale, rev);
	int len = MAXXLEN * interpolate(v, min, max);

	mvwhline(win, y, 1, ch, len);
	mvwclrtoborder(win, y, len + 1);
}

void waddthreshold(WINDOW *win, int y, float v, float tv,
		   float minv, float maxv, char *cscale, chtype tch)
{
	if (tv > minv && tv < maxv) {
		if (v > tv)
			tch |= COLOR_PAIR(CP_STANDARD);
		else
			tch |= cp_from_scale(v, cscale, true);

		mvwaddch(win, y, 1 + MAXXLEN * interpolate(tv, minv, maxv), tch);
	}
}
