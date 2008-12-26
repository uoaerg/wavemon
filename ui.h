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

enum colour_pair {
	CP_STANDARD = 1,
	CP_SCALEHI,
	CP_SCALEMID,
	CP_SCALELOW,
	CP_WTITLE,
	CP_INACTIVE,
	CP_ACTIVE,
	CP_STATSIG,
	CP_STATNOISE,
	CP_STATSNR,
	CP_STATBKG,
	CP_STATSIG_S,
	CP_STATNOISE_S,
	CP_PREF_NORMAL,
	CP_PREF_SELECT,
	CP_PREF_ARROW
};

static inline int cp_from_scale(float value, const char *cscale, bool reverse)
{
	enum colour_pair cp;

	if (value < (float)cscale[0])
		cp = reverse ? CP_SCALEHI : CP_SCALELOW;
	else if (value < (float)cscale[1])
		cp = CP_SCALEMID;
	else
		cp = reverse ? CP_SCALELOW : CP_SCALEHI;

	return COLOR_PAIR(cp);
}

void wmenubar(WINDOW *win, int active);
WINDOW *newwin_title(int h, int w, int x, int y, char *title, char t, char b);
void waddstr_b(WINDOW *win, char *s);
void waddstr_center(WINDOW *win, int y, char *s);
void waddbar(WINDOW *win, float v, float minv, float maxv, int y, int x, int maxx, char *cscale, bool rev);
void waddthreshold(WINDOW *win, float v, float tv, float minv, float maxv, int y, int x, int maxx, char *cscale, chtype tch);
