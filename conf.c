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

/* GLOBALS */
static char **if_list;		/* array of WiFi interface names */
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
	[SO_CHAN_REV]	= "Rev Channel",
	[SO_SIGNAL]	= "Signal",
	[SO_OPEN]	= "Open",
	[SO_CHAN_SIG]	= "Chan/Sig",
	[SO_OPEN_SIG]	= "Open/Sig",
	[SO_OPEN_CH_SI]	= "Open/Chan/Sig",
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
	.random			= false,

	.sig_min		= -102,
	.sig_max		= 10,
	.noise_min		= -102,
	.noise_max		= 10,

	.scan_sort_order	= SO_CHAN,
	.lthreshold_action	= TA_DISABLED,
	.lthreshold		= -80,
	.hthreshold_action	= TA_DISABLED,
	.hthreshold		= -10,

	.startup_scr		= 0,
};

/** Populate interface list */
void conf_get_interface_list(bool init)
{
	char *old_if = NULL;
	int idx;

	if (if_list) {
		for (idx = 0; if_list[idx]; idx++)
			if (idx == conf.if_idx)
				old_if = if_list[idx];
			else
				free(if_list[idx]);
		free(if_list);
	}
	if_list = iw_get_interface_list();
	if (if_list == NULL && !init)
		err_quit("no wireless interfaces found!");

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
	return if_list ? if_list[conf.if_idx] : "(none)";
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

static void read_cf(void)
{
	char tmp[0x100], lv[0x20], rv[0x20];
	struct conf_item *ci = NULL;
	FILE *fd;
	size_t len;
	int lnum, found, v_int;
	char *lp, *conv_err;
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
				err_quit("parse error in %s, line %d: value exceeds maximum of %d",
					 cfname, lnum, (int)ci->max);
			} else if (v_int < ci->min) {
				err_quit("parse error in %s, line %d: value is below minimum of %d",
					 cfname, lnum, (int)ci->min);
			} else {
				*ci->v.i = v_int;
			}
			break;
		case t_list:
			v_int = ci->list ? argv_find(ci->list, rv) : -1;
			if (v_int < 0)
				err_msg("%s, line %d: '%s = %s' is not valid - using defaults",
					 cfname, lnum, lv, rv);
			else
				*ci->v.i = v_int;
		case t_sep:	/* These two cases are missing from the enum, they are not handled */
		case t_func:	/* To pacify gcc -Wall, fall through here */
			break;
		}
	}
	fclose(fd);
done:
	free(cfname);
}

static void write_cf(void)
{
	char tmp[0x100], rv[0x40];
	struct conf_item *ci = NULL;
	char *lp, *cp;
	int add, i;
	FILE *fd;
	char *cfname = get_confname();
	int cfld = ll_create();

	if (access(cfname, F_OK) == 0) {
		fd = fopen(cfname, "r");
		if (fd == NULL)
			err_sys("can not read configuration file '%s'", cfname);
		while (fgets(tmp, sizeof(tmp), fd))
			ll_push(cfld, "s", tmp);
		fclose(fd);
	}

	for (ll_reset(conf_items); (ci = ll_getall(conf_items)); ) {
		if (ci->type != t_sep && ci->type != t_func &&
		    (!ci->dep || (ci->dep && *ci->dep))) {
			switch (ci->type) {
			case t_int:
				sprintf(rv, "%d", *ci->v.i);
				break;
			case t_list:
				sprintf(rv, "%s", ci->list[*ci->v.i]);
				str_tolower(rv);
				break;
				/* Fall through, the rest are dummy statements to pacify gcc -Wall */
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

	fd = fopen(cfname, "w");
	if (fd == NULL)
		err_sys("can not write to configuration file '%s'", cfname);

	for (ll_reset(cfld); (lp = ll_getall(cfld)); )
		fputs(lp, fd);
	fclose(fd);

	ll_destroy(cfld);
	free(cfname);
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
	item->name	= strdup("Scan sort order");
	item->cfname	= strdup("sort_order");
	item->type	= t_list;
	item->v.i	= &conf.scan_sort_order;
	item->list	= sort_order;
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
	item->min	= -128;
	item->max	= -60;
	item->inc	= 1;
	item->unit	= strdup("dBm");
	item->dep	= &conf.override_bounds;
	ll_push(conf_items, "*", item);

	item = calloc(1, sizeof(*item));
	item->name	= strdup("Maximum signal level");
	item->cfname	= strdup("max_signal_level");
	item->type	= t_int;
	item->v.i	= &conf.sig_max;
	item->min	= -59;
	item->max	= 120;
	item->inc	= 1;
	item->unit	= strdup("dBm");
	item->dep	= &conf.override_bounds;
	ll_push(conf_items, "*", item);

	item = calloc(1, sizeof(*item));
	item->name	= strdup("Minimum noise level");
	item->cfname	= strdup("min_noise_level");
	item->type	= t_int;
	item->v.i	= &conf.noise_min;
	item->min	= -128;
	item->max	= -60;
	item->inc	= 1;
	item->unit	= strdup("dBm");
	item->dep	= &conf.override_bounds;
	ll_push(conf_items, "*", item);

	item = calloc(1, sizeof(*item));
	item->name	= strdup("Maximum noise level");
	item->cfname	= strdup("max_noise_level");
	item->type	= t_int;
	item->v.i	= &conf.noise_max;
	item->min	= -60;
	item->max	= 120;
	item->inc	= 1;
	item->unit	= strdup("dBm");
	item->dep	= &conf.override_bounds;
	ll_push(conf_items, "*", item);

	item = calloc(1, sizeof(*item));
	item->name	= strdup("Random signals");
	item->cfname	= strdup("random");
	item->type	= t_list;
	item->v.i	= &conf.random;
	item->list	= on_off_names;
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
	int arg, dump = 0, help = 0, version = 0;

	conf_get_interface_list(true);
	init_conf_items();
	read_cf();

	while ((arg = getopt(argc, argv, "dghi:rv")) >= 0) {
		switch (arg) {
		case 'd':
			if (if_list)
				dump++;
			break;
		case 'g':
			conf.check_geometry = true;
			break;
		case 'h':
			help++;
			break;
		case 'i':
			conf.if_idx = if_list ? argv_find(if_list, optarg) : -1;
			if (conf.if_idx < 0)
				err_quit("no wireless extensions found on '%s'",
					 optarg);
			break;
		case 'r':
			conf.random = true;
			break;
		case 'v':
			version++;
			break;
		default:
			/* bad argument. bad bad */
			exit(EXIT_FAILURE);
		}
	}

	if (version) {
		printf("wavemon wireless monitor %s\n", PACKAGE_VERSION);
		printf("Distributed under the terms of the GPLv3.\n%s", help ? "\n" : "");
	}
	if (help) {
		printf("usage: wavemon [ -dhlrv ] [ -i ifname ]\n");
		printf("  -d            Dump the current device status to stdout and exit\n");
		printf("  -g            Ensure screen is sufficiently dimensioned\n");
		printf("  -h            This help screen\n");
		printf("  -i <ifname>   Use specified network interface (default: auto)\n");
		printf("  -r            Generate random levels (for testing purposes)\n");
		printf("  -v            Print version number\n");
	} else if (dump) {
		dump_parameters();
	}

	if (version || help || dump)
		exit(EXIT_SUCCESS);
	else if (if_list == NULL)
		err_quit("no supported wireless interfaces found");
}
