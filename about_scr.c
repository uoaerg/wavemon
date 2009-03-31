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
static char *about_lines[] = {	"wavemon - status monitor for wireless network devices",
				"version " PACKAGE_VERSION " (built " BUILD_DATE ")",
				"",
				"original by jan morgenstern <jan@jm-music.de>",
				"distributed under the GNU general public license v3",
				"",
				"wavemon uses the wireless extensions by",
				"jean tourrilhes <jt@hpl.hp.com>",
				"",
				"http://www.jm-music.de/projects.html",
				"",
				"please send suggestions and bug reports to " PACKAGE_BUGREPORT
};

static int *linecd[ARRAY_SIZE(about_lines)];


static void init_scramble(void)
{
	int 	i, j;
	
	for (i = 0; i < ARRAY_SIZE(about_lines); i++) {
		linecd[i] = malloc(strlen(about_lines[i]) * sizeof(int));
		for (j = 0; j < strlen(about_lines[i]); j++)
			linecd[i][j] = (rand() / (float)RAND_MAX) * 120 + 60;
	}
}

static void free_scramble(void)
{
	int i;
	
	for (i = 0; i < ARRAY_SIZE(about_lines); i++)
		free(linecd[i]);
}

static void draw_lines(WINDOW *w_about)
{
	int	i, j;
	char	buf[0x100];
	
	for (i = 0; i < ARRAY_SIZE(about_lines); i++) {
		for (j = 0; j < strlen(about_lines[i]); j++)
			if (linecd[i][j] > 60) {
				buf[j] = ' ';
				linecd[i][j]--;
			} else if (linecd[i][j]) {
				buf[j] = (rand() / (float)RAND_MAX) * 54 + 65;
				linecd[i][j]--;
			} else buf[j] = about_lines[i][j];
		buf[j] = '\0';
		waddstr_center(w_about, LINES/2 - ARRAY_SIZE(about_lines)/2 + i, buf);
	}
}	

int scr_about(void)
{
	WINDOW	*w_about, *w_menu;
	int	key = 0;
	
	w_about = newwin_title(LINES - 1, COLS, 0, 0, "About", 0, 0);
	w_menu = newwin(1, COLS, LINES - 1, 0);
	
	wmenubar(w_menu, 8);
	nodelay(w_menu, TRUE); keypad(w_menu, TRUE);

	init_scramble();
	
	while (key < KEY_F(1) || key > KEY_F(10)) {
		do {
			draw_lines(w_about);
			wrefresh(w_about);
			wmove(w_menu, 1, 0);
			wrefresh(w_menu);
			key = wgetch(w_menu);
			usleep(5000); 
		} while (key <= 0);

		/* Keyboard shortcuts */
		if (key == 'q')
			key = KEY_F(10);
		else if (key == 'i')
			key = KEY_F(1);
	}
	
	free_scramble();
	
	werase(w_about); wrefresh(w_about); delwin(w_about);
	werase(w_menu); wrefresh(w_menu); delwin(w_menu);
	
	return key - KEY_F(1);
}
