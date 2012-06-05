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
#define LISTVAL_MAX	32	/* maximum string length of `s' elements */

int	ll_create(void);
void 	*ll_get(int ld, unsigned long n);
void 	*ll_getall(int ld);
void 	ll_reset(int ld);
void 	ll_push(int ld, const char *format, ...);
void 	ll_replace(int ld, unsigned long n, const char *format, ...);
unsigned long ll_size(int ld);
void 	ll_destroy(int ld);
