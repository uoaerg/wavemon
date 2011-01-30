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
static WINDOW *w_about;

static char *about_lines[] = {
	"wavemon - status monitor for wireless network devices",
	"version " PACKAGE_VERSION " (built " BUILD_DATE ")",
	"",
	"original by jan morgenstern <jan@jm-music.de>",
	"distributed under the GNU general public license v3",
	"",
	"wavemon uses the wireless extensions by",
	"jean tourrilhes <jt@hpl.hp.com>",
	"",
	"please send suggestions and bug reports to ",
	PACKAGE_BUGREPORT,
	"",
	PACKAGE_URL
};

static int *linecd[ARRAY_SIZE(about_lines)], i, j;

void scr_about_init(void)
{
	w_about = newwin_title(0, WAV_HEIGHT, "About", false);

	for (i = 0; i < ARRAY_SIZE(about_lines); i++) {
		linecd[i] = malloc(strlen(about_lines[i]) * sizeof(int));
		for (j = 0; j < strlen(about_lines[i]); j++)
			linecd[i][j] = (rand() / (float)RAND_MAX) * 120 + 60;
	}
}

int scr_about_loop(WINDOW *w_menu)
{
	char buf[0x100];

	for (i = 0; i < ARRAY_SIZE(about_lines); i++) {
		for (j = 0; j < strlen(about_lines[i]); j++) {
			if (linecd[i][j] > 60) {
				buf[j] = ' ';
				linecd[i][j]--;
			} else if (linecd[i][j]) {
				buf[j] = (rand() / (float)RAND_MAX) * 54 + 65;
				linecd[i][j]--;
			} else {
				buf[j] = about_lines[i][j];
			}
		}
		buf[j] = '\0';
		waddstr_center(w_about, (WAV_HEIGHT - ARRAY_SIZE(about_lines))/2 + i, buf);
	}
	wrefresh(w_about);
	return wgetch(w_menu);
}

void scr_about_fini(void)
{
	delwin(w_about);
	for (i = 0; i < ARRAY_SIZE(about_lines); i++)
		free(linecd[i]);
}
