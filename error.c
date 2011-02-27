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
#if HAVE_LIBCAP
#include <sys/capability.h>

static bool has_capability(cap_value_t cap)
{
	cap_t cap_proc = cap_get_proc();
	cap_flag_value_t cur_val;

	if (cap_get_flag(cap_proc, cap, CAP_EFFECTIVE, &cur_val))
		err_sys("cap_get_flag(CAP_EFFECTIVE)");
	cap_free(cap_proc);

	return cur_val == CAP_SET;
}

bool has_net_admin_capability(void)
{
	return has_capability(CAP_NET_ADMIN);
}
#else	/* !HAVE_LIBCAP */
bool has_net_admin_capability(void)
{
	return geteuid() == 0;
}
#endif

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

/**
 * terminate_all_processes  -  terminate wavemon and reset screen
 * @fmt:     printf-like format string
 * @strerr:  set to non-0 if termination is due to failed system call
 * @ap:	     argument list for @fmt
 */
static void terminate_all_processes(const char *fmt, int strerr, va_list ap)
{
	int saved_errno = strerr ? errno : 0;
	/*
	 * wavemon runs in its own process group. Block TERM in this process,
	 * but send to all others (parent or child), which by default do not
	 * block TERM.
	 */
	xsignal(SIGTERM, SIG_IGN);
	endwin();
	kill(0, SIGTERM);
	reset_shell_mode();
	if (saved_errno) {
		errno = saved_errno;
		vwarn(fmt, ap);
	} else {
		vwarnx(fmt, ap);
	}
	va_end(ap);
	exit(EXIT_FAILURE);
}

/*
 * Abort on fatal error unrelated to system call.
 */
void err_quit(const char *format, ...)
{
	va_list argp;

	va_start(argp, format);
	terminate_all_processes(format, false, argp);
}

/*
 * Abort on fatal error related to system call.
 */
void err_sys(const char *format, ...)
{
	va_list argp;

	va_start(argp, format);
	terminate_all_processes(format, true, argp);
}
