/*
 * wavemon - a wireless network monitoring application
 *
 * Copyright (c) 2001-2002 Jan Morgenstern <jan@jm-music.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
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
