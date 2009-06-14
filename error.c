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
#include <stdarg.h>

/*
 * For displaying warning messages that are unrelated to system calls,
 * outside ncurses mode for %WARN_DISPLAY_DELAY seconds.
 */
void err_msg(const char *format, ...)
{
	va_list argp;

	va_start(argp, format);
	vwarnx(format, argp);
	va_end(argp);
	sleep(WARN_DISPLAY_DELAY);
}

/*
 * Abort on fatal error unrelated to system call.
 */
void err_quit(const char *format, ...)
{
	va_list argp;

	endwin();

	va_start(argp, format);
	verrx(EXIT_FAILURE, format, argp);
	va_end(argp);
}

/*
 * Abort on fatal error related to system call.
 */
void err_sys(const char *format, ...)
{
	va_list argp;

	endwin();

	va_start(argp, format);
	verr(EXIT_FAILURE, format, argp);
	va_end(argp);
}
