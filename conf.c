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
#include <pwd.h>
#include <sys/types.h>
#include <netlink/version.h>

/* GLOBALS */
#define MAX_IFLIST_ENTRIES 64
static char *if_list[MAX_IFLIST_ENTRIES]; /* array of WiFi interface names */
int conf_items;			/* index into array storing menu items */

static char *on_off_names[] = { [false] = "Off", [true] = "On", NULL };
static char *action_items[] = {
	[TA_DISABLED]	= "Disabled",
	[TA_BEEP]	= "Beep",
	[TA_FLASH]	= "Flash",
	[TA_BEEP_FLASH]	= "Beep+Flash",
	NULL
};

static char *sort_order[] = {
	[SO_CHAN]	= "Channel",
	[SO_SIGNAL]	= "Signal",
	[SO_MAC]	= "MAC",
	[SO_ESSID]	= "Essid",
	[SO_OPEN]	= "Open",
	[SO_CHAN_SIG]	= "Chan/Sig",
	[SO_OPEN_SIG]	= "Open/Sig",
	NULL
};

static char *screen_names[] = {
	[SCR_INFO]	= "Info screen",
	[SCR_LHIST]	= "Histogram",
	[SCR_SCAN]	= "Scan window",
	NULL
};

struct wavemon_conf conf = {
	.if_idx			= 0,

	.stat_iv		= 100,
	.info_iv		= 10,
	.slotsize		= 4,
	.meter_decay		= 0,

	.check_geometry		= false,
	.cisco_mac		= false,
	.override_bounds	= false,

	.sig_min		= -100,
	.sig_max		= -10,
	.noise_min		= -120,
	.noise_max		= -40,

	.scan_sort_order	= SO_CHAN_SIG,
	.scan_sort_asc		= false,
	.lthreshold_action	= TA_DISABLED,
	.lthreshold		= -80,
	.hthreshold_action	= TA_DISABLED,
	.hthreshold		= -10,

	.startup_scr		= 0,
};

/** Populate interface list */
void conf_get_interface_list(void)
{
	char *old_if = NULL;
	int idx;

	for (idx = 0; if_list[idx]; idx++) {
		if (idx == conf.if_idx)
			old_if = if_list[idx];
		else
			free(if_list[idx]);
	}
	iw_get_interface_list(if_list, MAX_IFLIST_ENTRIES);
	if (!if_list[0])
		err_quit("no supported wireless interfaces found! Check manpage for help.");

	conf.if_idx = 0;
	if (old_if) {
		idx = argv_find(if_list, old_if);
		if (idx > 0)
			conf.if_idx = idx;
		free(old_if);
	}
}

/** Return currently selected interface name */
const char *conf_ifname(void)
{
	return if_list[0] && if_list[conf.if_idx] ? if_list[conf.if_idx] : "(none)";
}

/* Return full path of rcfile. Allocates string which must bee free()-d. */
static char *get_confname(void)
{
	char *full_path, *homedir = getenv("HOME");
	struct passwd *pw;

	if (homedir == NULL) {
		pw = getpwuid(getuid());
		if (pw == NULL)
			err_quit("can not determine $HOME");
		homedir = pw->pw_dir;
	}
	full_path = malloc(strlen(homedir) + strlen(CFNAME) + 3);
	sprintf(full_path, "%s/%s", homedir, CFNAME);

	return full_path;
}

static void write_cf(void)
{
	char tmp[0x100], rv[0x40];
	struct conf_item *ci = NULL;
	char *lp, *cp;
	int add, i;
	char *cfname = get_confname();
	int cfld = ll_create();
	FILE *fd = fopen(cfname, "w");

	if (fd == NULL)
		err_sys("failed to open configuration file '%s'", cfname);

	for (ll_reset(conf_items); (ci = ll_getall(conf_items)); ) {
		if (ci->type != t_sep && ci->type != t_func &&
		    (!ci->dep || (ci->dep && *ci->dep))) {
			switch (ci->type) {
			case t_int:
				sprintf(rv, "%d", *ci->v.i);
				break;
			case t_list:
				if (!argv_count(ci->list))
					continue;
				sprintf(rv, "%s", ci->list[*ci->v.i]);
				str_tolower(rv);
				break;
			case t_sep:
			case t_func:
				break;
			}

			add = 1;

			for (i = 0; i < ll_size(cfld); i++) {
				lp = ll_get(cfld, i);
				cp = lp += strspn(lp, " ");
				if (!strncasecmp(cp, ci->cfname, strcspn(cp, " ="))
				    && strlen(ci->cfname) == strcspn(cp, " =")) {
					add = 0;
					cp += strcspn(cp, "=") + 1;
					cp += strspn(cp, " ");
					strncpy(tmp, cp, strcspn(cp, " #\n"));
					if (strcasecmp(tmp, rv)) {
						strncpy(tmp, lp, strcspn(lp, " ="));
						tmp[strcspn(lp, " =")] = '\0';
						strcat(tmp, " = ");
						strcat(tmp, rv);
						strcat(tmp, "\n");
						ll_replace(cfld, i, "s", tmp);
					}
				}
			}

			if (add) {
				strcpy(tmp, ci->cfname);
				strcat(tmp, " = ");
				strcat(tmp, rv);
				strcat(tmp, "\n");
				ll_push(cfld, "s", tmp);
			}
		}
	}

	for (ll_reset(cfld); (lp = ll_getall(cfld)); )
		fputs(lp, fd);
	fclose(fd);

	ll_destroy(cfld);
	free(cfname);
}

static void read_cf(void)
{
	char tmp[0x100], lv[0x20], rv[0x20];
	struct conf_item *ci = NULL;
	FILE *fd;
	size_t len;
	int lnum, found, v_int;
	char *lp, *conv_err;
	bool file_needs_update = false;
	char *cfname = get_confname();

	if (access(cfname, F_OK) != 0)
		goto done;

	fd = fopen(cfname, "r");
	if (fd == NULL)
		err_sys("can not read configuration file '%s'", cfname);

	for (lnum = 1; fgets(tmp, sizeof(tmp), fd); lnum++) {
		lp = tmp + strspn(tmp, " ");
		if (*lp == '#' || *lp == '\n')
			continue;

		len = strcspn(lp, " =");
		if (len > sizeof(lv))
			err_quit("parse error in %s, line %d: identifier too long",
				 cfname, lnum);
		strncpy(lv, lp, len);
		lv[len] = '\0';
		lp += len;

		ll_reset(conf_items);
		for (found = 0; !found && (ci = ll_getall(conf_items)); )
			found = (ci->type != t_sep && ci->type != t_func &&
				 strcasecmp(ci->cfname, lv) == 0);
		if (!found) {
			err_msg("%s, line %d: ignoring unknown identifier '%s'",
				cfname, lnum, lv);
			file_needs_update = true;
			continue;
		}

		lp += strspn(lp, " ");
		if (*lp++ != '=')
			err_quit("parse error in %s, line %d: missing '=' operator in assignment",
				 cfname, lnum);
		lp += strspn(lp, " ");

		len = strcspn(lp, " \n");
		if (len > sizeof(rv))
			err_quit("parse error in %s, line %d: argument too long", cfname, lnum);
		else if (*lp == '\n')
			err_quit("parse error in %s, line %d: argument expected", cfname, lnum);
		strncpy(rv, lp, len);
		rv[len] = '\0';

		switch (ci->type) {
		case t_int:
			v_int = strtol(rv, &conv_err, 10);
			if (*conv_err != '\0') {
				err_quit("parse error in %s, line %d: integer value expected, '%s' found instead",
					 cfname, lnum, rv);
			} else if (v_int > ci->max) {
				err_msg("%s, line %d: value exceeds maximum of %d - using maximum",
					 cfname, lnum, (int)ci->max);
				*ci->v.i = ci->max;
				file_needs_update = true;
			} else if (v_int < ci->min) {
				err_msg("%s, line %d: value is below minimum of %d - using minimum",
					 cfname, lnum, (int)ci->min);
				*ci->v.i = ci->min;
				file_needs_update = true;
			} else {
				*ci->v.i = v_int;
			}
			break;
		case t_list:
			assert(ci->list != NULL);
			if (!argv_count(ci->list))
				err_quit("no usable %s candidates available for '%s'", ci->name, rv);
			v_int = argv_find(ci->list, rv);
			if (v_int < 0) {
				err_msg("%s, line %d: '%s = %s' is not valid - using defaults",
					 cfname, lnum, lv, rv);
				file_needs_update = true;
			} else {
				*ci->v.i = v_int;
			}
		case t_sep:
		case t_func:
			break;
		}
	}
	fclose(fd);
done:
	free(cfname);

	if (file_needs_update) {
		write_cf();
	}
}

static void init_conf_items(void)
{
	struct conf_item *item;

	conf_items = ll_create();

	item = calloc(1, sizeof(*item));
	item->name = strdup("Interface");
	item->type = t_sep;
	ll_push(conf_items, "*", item);

	item = calloc(1, sizeof(*item));
	item->name	= strdup("Interface");
	item->cfname	= strdup("interface");
	item->type	= t_list;
	item->v.i	= &conf.if_idx;
	item->list	= if_list;
	ll_push(conf_items, "*", item);

	item = calloc(1, sizeof(*item));
	item->name	= strdup("Cisco-style MAC addresses");
	item->cfname	= strdup("cisco_mac");
	item->type	= t_list;
	item->v.i	= &conf.cisco_mac;
	item->list	= on_off_names;
	ll_push(conf_items, "*", item);

	item = calloc(1, sizeof(*item));
	item->name	= strdup("Scan sort type");
	item->cfname	= strdup("sort_order");
	item->type	= t_list;
	item->v.i	= &conf.scan_sort_order;
	item->list	= sort_order;
	ll_push(conf_items, "*", item);

	item = calloc(1, sizeof(*item));
	item->name	= strdup("Scan sort in ascending order");
	item->cfname	= strdup("sort_ascending");
	item->type	= t_list;
	item->v.i	= &conf.scan_sort_asc;
	item->list	= on_off_names;
	ll_push(conf_items, "*", item);

	item = calloc(1, sizeof(*item));
	item->name	= strdup("Statistics updates");
	item->cfname	= strdup("stat_updates");
	item->type	= t_int;
	item->v.i	= &conf.stat_iv;
	item->min	= 10;
	item->max	= 4000;
	item->inc	= 10;
	item->unit	= strdup("ms");
	ll_push(conf_items, "*", item);

	item = calloc(1, sizeof(*item));
	item->name	= strdup("Histogram update cycles");
	item->cfname	= strdup("lhist_slot_size");
	item->type	= t_int;
	item->v.i	= &conf.slotsize;
	item->min	= 1;
	item->max	= 64;
	item->inc	= 1;
	ll_push(conf_items, "*", item);

	item = calloc(1, sizeof(*item));
	item->name	= strdup("Level meter smoothness");
	item->cfname	= strdup("meter_smoothness");
	item->type	= t_int;
	item->v.i	= &conf.meter_decay;
	item->min	= 0;
	item->max	= 99;
	item->inc	= 1;
	item->unit	= strdup("%");
	ll_push(conf_items, "*", item);

	item = calloc(1, sizeof(*item));
	item->name	= strdup("Dynamic info updates");
	item->cfname	= strdup("info_updates");
	item->type	= t_int;
	item->v.i	= &conf.info_iv;
	item->min	= 1;
	item->max	= 60;
	item->inc	= 1;
	item->unit	= strdup("s");
	ll_push(conf_items, "*", item);

	/* level scale items */
	item = calloc(1, sizeof(*item));
	item->type = t_sep;
	ll_push(conf_items, "*", item);

	item = calloc(1, sizeof(*item));
	item->name = strdup("Level scales");
	item->type = t_sep;
	ll_push(conf_items, "*", item);

	item = calloc(1, sizeof(*item));
	item->name	= strdup("Override scale autodetect");
	item->cfname	= strdup("override_auto_scale");
	item->type	= t_list;
	item->v.i	= &conf.override_bounds;
	item->list	= on_off_names;
	ll_push(conf_items, "*", item);

	item = calloc(1, sizeof(*item));
	item->name	= strdup("Minimum signal level");
	item->cfname	= strdup("min_signal_level");
	item->type	= t_int;
	item->v.i	= &conf.sig_min;
	item->min	= -100;
	item->max	= -39;
	item->inc	= 1;
	item->unit	= strdup("dBm");
	item->dep	= &conf.override_bounds;
	ll_push(conf_items, "*", item);

	item = calloc(1, sizeof(*item));
	item->name	= strdup("Maximum signal level");
	item->cfname	= strdup("max_signal_level");
	item->type	= t_int;
	item->v.i	= &conf.sig_max;
	item->min	= -40;
	item->max	= -10;
	item->inc	= 1;
	item->unit	= strdup("dBm");
	item->dep	= &conf.override_bounds;
	ll_push(conf_items, "*", item);

	item = calloc(1, sizeof(*item));
	item->name	= strdup("Minimum noise level");
	item->cfname	= strdup("min_noise_level");
	item->type	= t_int;
	item->v.i	= &conf.noise_min;
	item->min	= -120;
	item->max	= -70;
	item->inc	= 1;
	item->unit	= strdup("dBm");
	item->dep	= &conf.override_bounds;
	ll_push(conf_items, "*", item);

	item = calloc(1, sizeof(*item));
	item->name	= strdup("Maximum noise level");
	item->cfname	= strdup("max_noise_level");
	item->type	= t_int;
	item->v.i	= &conf.noise_max;
	item->min	= -69;
	item->max	= -40;
	item->inc	= 1;
	item->unit	= strdup("dBm");
	item->dep	= &conf.override_bounds;
	ll_push(conf_items, "*", item);

	/* thresholds */
	item = calloc(1, sizeof(*item));
	item->name	= strdup("Low threshold action");
	item->cfname	= strdup("lo_threshold_action");
	item->type	= t_list;
	item->v.i	= &conf.lthreshold_action;
	item->list	= action_items;
	ll_push(conf_items, "*", item);

	item = calloc(1, sizeof(*item));
	item->name	= strdup("Low threshold");
	item->cfname	= strdup("lo_threshold");
	item->type	= t_int;
	item->v.i	= &conf.lthreshold;
	item->min	= -120;
	item->max	= -60;
	item->inc	= 1;
	item->unit	= strdup("dBm");
	item->dep	= &conf.lthreshold_action;
	ll_push(conf_items, "*", item);

	item = calloc(1, sizeof(*item));
	item->name	= strdup("High threshold action");
	item->cfname	= strdup("hi_threshold_action");
	item->type	= t_list;
	item->v.i	= &conf.hthreshold_action;
	item->list	= action_items;
	ll_push(conf_items, "*", item);

	item = calloc(1, sizeof(*item));
	item->name	= strdup("High threshold");
	item->cfname	= strdup("hi_threshold");
	item->type	= t_int;
	item->v.i	= &conf.hthreshold;
	item->min	= -59;
	item->max	= 120;
	item->inc	= 1;
	item->unit	= strdup("dBm");
	item->dep	= &conf.hthreshold_action;
	ll_push(conf_items, "*", item);

	/* start-up items */
	item = calloc(1, sizeof(*item));
	item->type = t_sep;
	ll_push(conf_items, "*", item);

	item = calloc(1, sizeof(*item));
	item->name = strdup("Startup");
	item->type = t_sep;
	ll_push(conf_items, "*", item);

	item = calloc(1, sizeof(*item));
	item->name	= strdup("Startup screen");
	item->cfname	= strdup("startup_screen");
	item->type	= t_list;
	item->v.i	= &conf.startup_scr;
	item->list	= screen_names;
	ll_push(conf_items, "*", item);

	/* separator (dummy entry) */
	item = calloc(1, sizeof(*item));
	item->type	= t_sep;
	ll_push(conf_items, "*", item);

	/* functions */
	item = calloc(1, sizeof(*item));
	item->name	= strdup("Save configuration");
	item->type	= t_func;
	item->v.fp	= write_cf;
	ll_push(conf_items, "*", item);
}

void getconf(int argc, char *argv[])
{
	int arg, help = 0, version = 0;
	const char *iface = NULL;

	while ((arg = getopt(argc, argv, "ghi:v")) >= 0) {
		switch (arg) {
		case 'g':
			conf.check_geometry = true;
			break;
		case 'h':
			help++;
			break;
		case 'i':
			iface = optarg;
			break;
		case 'v':
			version++;
			break;
		default:
			exit(EXIT_FAILURE);
		}
	}

	if (version) {
		printf("wavemon %s", PACKAGE_VERSION);
		printf(" with %s and libnl %s.\n", curses_version(), LIBNL_VERSION);
		printf("Distributed under the terms of the GPLv3.\n%s", help ? "\n" : "");
	}
	if (help) {
		printf("usage: wavemon [ -hgv ] [ -i ifname ]\n");
		printf("  -g            Ensure screen is sufficiently dimensioned\n");
		printf("  -h            This help screen\n");
		printf("  -i <ifname>   Use specified network interface (default: auto)\n");
		printf("  -v            Print version details\n");
	}

	if (version || help) {
		exit(EXIT_SUCCESS);
	}

	/* Actual initialization. */
	conf_get_interface_list();
	init_conf_items();
	read_cf();

	if (iface) {
		conf.if_idx = argv_find(if_list, iface);
		if (conf.if_idx < 0)
			err_quit("%s is not a usable wireless interface", iface);
	}
}
