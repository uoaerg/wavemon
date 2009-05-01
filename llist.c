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

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "llist.h"

/*
 * llist - a library of functions for transparent handling of linked lists.
 * programmed by Jan Morgenstern <jan@jm-music.de>
 *
 * format parameters are specified as follows:
 * d = int
 * c = char
 * f = float
 * s = char *
 * S = char *, case-insensitive and fuzzy (for ll_scan)
 * * = void *
 */

#define NUM_LISTS 0x100

typedef struct chain {
	void *e;
	char type;
	struct chain *next;
} llist;
static llist *lists[NUM_LISTS];

/* position pointer for ll_getall() */
static struct {
	unsigned long n;
	char eol;
} lp[NUM_LISTS];

/*
 * helper function for generating an element from an argument
 */
static llist *arg2element(char type, va_list * ap, llist * next)
{
	llist *l;

	l = (llist *) malloc(sizeof(llist));

	switch (type) {
	case 'd':
		l->e = (void *)malloc(sizeof(int));
		*((int *)l->e) = va_arg(*ap, int);
		break;
	case 's':
		l->e = (void *)malloc(sizeof(char *));
		l->e = strdup(va_arg(*ap, char *));
		break;
	case 'f':
		l->e = (void *)malloc(sizeof(double));
		*((double *)l->e) = va_arg(*ap, double);
		break;
	case '*':
		l->e = (void *)malloc(sizeof(void *));
		l->e = va_arg(*ap, void *);
	}
	l->next = next;
	return l;
}

/*
 * start a new list
 */
int ll_create(void)
{
	unsigned long i;
	static char firstcall = 1;

	if (firstcall) {
		for (i = 0; i < NUM_LISTS; i++)
			lists[i] = NULL;
		firstcall = 0;
	}

	i = 0;
	while (i < NUM_LISTS && lists[i] != NULL)
		i++;
	if (i < NUM_LISTS) {
		lists[i] = (llist *) calloc(1, sizeof(llist));
		return i;
	} else
		return -1;
}

/*
 * get an element without modifying the list
 */
void *ll_get(int ld, unsigned long n)
{
	llist *l = lists[ld]->next;
	int i;

	for (i = 0; i < n && l->next; i++)
		l = l->next;
	return l->e;
}

/*
 * return all elements successively
 */
void *ll_getall(int ld)
{
	llist *l = lists[ld]->next;
	void *rv;
	static char firstcall = 1;
	unsigned long i;

	if (firstcall) {
		for (i = 0; i < NUM_LISTS; i++)
			lp[i].n = lp[i].eol = 0;
		firstcall = 0;
	}

	if (!lp[ld].eol) {
		for (i = 0; i < lp[ld].n; i++)
			l = l->next;
		if (!l->next)
			lp[ld].eol = 1;
		rv = l->e;
		lp[ld].n++;
	} else {
		rv = NULL;
		lp[ld].eol = lp[ld].n = 0;
	}
	return rv;
}

/*
 * reset the position pointer for ll_getall
 */
void ll_reset(int ld)
{
	lp[ld].n = lp[ld].eol = 0;
}

/*
 * push an element onto the end of the list
 */
void ll_push(int ld, const char *format, ...)
{
	llist *l = lists[ld];
	va_list ap;

	while (l->next)
		l = l->next;

	va_start(ap, format);
	while (*format)
		l = l->next = arg2element(*format++, &ap, NULL);
	va_end(ap);
	l->next = NULL;
}

/*
 * replace an element it with a new one
 */
void ll_replace(int ld, unsigned long n, const char *format, ...)
{
	llist *prevl = lists[ld], *l = lists[ld]->next;
	int i;
	va_list ap;

	for (i = 0; i < n && l->next; i++) {
		prevl = l;
		l = l->next;
	}

	va_start(ap, format);
	if (*format)
		prevl->next = arg2element(*format, &ap, l->next);
	va_end(ap);

	free(l->e);
	free(l);
}

/*
 * return the position of a given element in list (or -1)
 */
signed long ll_scan(int ld, const char *format, ...)
{
	llist *l = lists[ld];
	va_list ap;
	int len, i, rv = -1, int_v;
	double double_v;
	char *string_v;

	va_start(ap, format);
	switch (*format) {
	case 'd':
		int_v = va_arg(ap, int);
		for (i = 0; (l = l->next); i++)
			if (*(int *)l->e == int_v)
				rv = i;
		break;
	case 'f':
		double_v = va_arg(ap, double);
		for (i = 0; (l = l->next); i++)
			if (*(double *)l->e == double_v)
				rv = i;
		break;
	case 's':
		string_v = strdup(va_arg(ap, char *));
		for (i = 0; (l = l->next); i++)
			if (!strcmp(l->e, string_v))
				rv = i;
		free(string_v);
		break;
	case 'S':
		string_v = strdup(va_arg(ap, char *));
		len = strlen(string_v);
		for (i = 0; (l = l->next); i++)
			if (strncasecmp(l->e, string_v, len) == 0) {
				rv = i;
				break;
			}
		free(string_v);
	}
	va_end(ap);
	return rv;
}

/*
 * return the number of elements in a given list
 */
unsigned long ll_size(int ld)
{
	llist *l = lists[ld];
	unsigned long i;

	for (i = 0; (l = l->next); i++)
		/* do nothing */ ;
	return i;
}

/*
 * destroy a list and free the memory
 */
void ll_destroy(int ld)
{
	llist *l = lists[ld], *lnext;

	while (l) {
		lnext = l->next;
		free(l->e);
		free(l);
		l = lnext;
	}

	lp[ld].n = lp[ld].eol = 0;

	lists[ld] = NULL;
}
