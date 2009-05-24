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
struct wavemon_conf conf;
int conf_items;			/* index into array storing menu items */
static int if_list;		/* index into array of WiFi interface names */

static void version(void)
{
	printf("wavemon wireless monitor %s\n", PACKAGE_VERSION);
	printf("Distributed under the terms of the GPLv3.\n");
}

static void usage(void)
{
	printf("Usage: wavemon [ -dhlrv ] [ -i ifname ]\n\n");
	printf("  -d            Dump the current device status to stdout and exit\n");
	printf("  -h            This help screen\n");
	printf("  -i <ifname>   Use specified network interface (default: auto)\n");
	printf("  -r            Generate random levels (for testing purposes)\n");
	printf("  -v            Print version number and exit\n\n");
}

static void getargs(int argc, char *argv[])
{
	int arg, tmp;

	while ((arg = getopt(argc, argv, "dhi:rv")) >= 0)
		switch (arg) {
		case 'd':
			dump_parameters();
			exit(EXIT_SUCCESS);
		case 'h':
			usage();
			exit(EXIT_SUCCESS);
		case 'i':
			if ((tmp = ll_scan(if_list, "S", optarg)) >= 0) {
				strncpy(conf.ifname, ll_get(if_list, tmp),
					sizeof(conf.ifname));
				break;
			}
			fatal_error("no wireless extensions found on '%s'", optarg);
		case 'r':
			conf.random = true;
			break;
		case 'v':
			version();
			exit(EXIT_SUCCESS);
		default:
			/* bad argument. bad bad */
			exit(EXIT_FAILURE);
		}
}

/* Return full path of rcfile. Allocates string which must bee free()-d. */
static char *get_confname(void)
{
	char *full_path, *homedir = getenv("HOME");
	struct passwd *pw;

	if (homedir == NULL) {
		pw = getpwuid(getuid());
		if (pw == NULL)
			fatal_error("cannot determine $HOME");
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
		fatal_error("cannot read configuration file '%s'", cfname);

	for (lnum = 1; fgets(tmp, sizeof(tmp), fd); lnum++) {

		lp = tmp + strspn(tmp, " ");
		if (*lp == '#' || *lp == '\n')
			continue;

		len = strcspn(lp, " =");
		if (len > sizeof(lv))
			fatal_error("parse error in %s, line %d: identifier too long",
				    cfname, lnum);
		strncpy(lv, lp, len);
		lv[len] = '\0';
		lp += len;

		ll_reset(conf_items);
		for (found = 0; !found && (ci = ll_getall(conf_items)); )
			found = (ci->type != t_sep && ci->type != t_func &&
				 strcasecmp(ci->cfname, lv) == 0);
		if (!found)
			fatal_error("parse error in %s, line %d: unknown identifier '%s'",
				    cfname, lnum, lv);

		lp += strspn(lp, " ");
		if (*lp++ != '=')
			fatal_error("parse error in %s, line %d: missing '=' operator in assignment",
				    cfname, lnum);
		lp += strspn(lp, " ");

		len = strcspn(lp, " \n");
		if (len > sizeof(rv))
			fatal_error("parse error in %s, line %d: argument too long", cfname, lnum);
		else if (*lp == '\n')
			fatal_error("parse error in %s, line %d: argument expected", cfname, lnum);
		strncpy(rv, lp, len);
		rv[len] = '\0';

		switch (ci->type) {
		case t_int:
			v_int = strtol(rv, &conv_err, 10);
			if (*conv_err != '\0') {
				fatal_error("parse error in %s, line %d: integer value expected, '%s' found instead",
					    cfname, lnum, rv);
			} else if (v_int > ci->max) {
				fatal_error("parse error in %s, line %d: value exceeds maximum of %d",
					    cfname, lnum, (int)ci->max);
			} else if (v_int < ci->min) {
				fatal_error("parse error in %s, line %d: value is below minimum of %d",
					    cfname, lnum, (int)ci->min);
			} else {
				*ci->v.i = v_int;
			}
			break;
		case t_string:
			if (strlen(rv) > ci->max)
				fatal_error("parse error in %s, line %d: argument too long (max %d chars)",
					    cfname, lnum, ci->max);
			strncpy(ci->v.s, rv, LISTVAL_MAX);
			break;
		case t_switch:
			if (!strcasecmp(rv, "on") || !strcasecmp(rv, "yes") ||
			    !strcasecmp(rv, "enabled") || !strcasecmp(rv, "1")) {
				*(ci->v.b) = 1;
			} else if (!strcasecmp(rv, "off") || !strcasecmp(rv, "no") ||
				   !strcasecmp(rv, "disabled") || !strcasecmp(rv, "0")) {
				*(ci->v.b) = 0;
			} else {
				fatal_error("parse error in %s, line %d: boolean expected, '%s' found instead",
					    cfname, lnum, rv);
			}
			break;
		case t_list:
			v_int = ll_scan(ci->list, "S", rv);
			if (v_int < 0)
				fatal_error("parse error in %s, line %d: '%s' is not a valid argument here",
					    cfname, lnum, rv);
			*ci->v.b = v_int;
			break;
		case t_listval:
			v_int = ll_scan(ci->list, "S", rv);
			if (v_int < 0)
				fatal_error("parse error in %s, line %d: '%s' is not a valid argument here",
					    cfname, lnum, rv);
			strncpy(ci->v.s, ll_get(ci->list, v_int), LISTVAL_MAX);
			break;
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
			fatal_error("cannot read configuration file '%s'", cfname);
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
			case t_string:	/* fall through */
			case t_listval:
				strcpy(rv, ci->v.s);
				break;
			case t_switch:
				sprintf(rv, "%s", *ci->v.b ? "on" : "off");
				break;
			case t_list:
				sprintf(rv, "%s", (char *)ll_get(ci->list, *ci->v.b));
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
		fatal_error("cannot write to '%s'", cfname);

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
	item->type	= t_listval;
	item->v.s	= conf.ifname;
	item->max	= 10;
	item->list	= if_list;
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
	item->type	= t_switch;
	item->v.b	= &conf.override_bounds;
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
	item->type	= t_switch;
	item->v.b	= &conf.random;
	ll_push(conf_items, "*", item);

	/* thresholds */
	item = calloc(1, sizeof(*item));
	item->name	= strdup("Low threshold action");
	item->cfname	= strdup("lo_threshold_action");
	item->type	= t_list;
	item->v.b	= &conf.lthreshold_action;
	item->list	= ll_create();
	ll_push(item->list, "s", "Disabled");
	ll_push(item->list, "s", "Beep");
	ll_push(item->list, "s", "Flash");
	ll_push(item->list, "s", "Beep+Flash");
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
	item->v.b	= &conf.hthreshold_action;
	item->list	= ll_create();
	ll_push(item->list, "s", "Disabled");
	ll_push(item->list, "s", "Beep");
	ll_push(item->list, "s", "Flash");
	ll_push(item->list, "s", "Beep+Flash");
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
	item->v.b	= &conf.startup_scr;
	item->list	= ll_create();
	ll_push(item->list, "s", "Info");
	ll_push(item->list, "s", "Histogram");
	ll_push(item->list, "s", "Access points");
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

static void set_defaults(void)
{
	strncpy(conf.ifname, ll_get(if_list, 0), sizeof(conf.ifname));

	conf.stat_iv		= 100;
	conf.info_iv		= 10;
	conf.slotsize		= 4;
	conf.meter_decay	= 0;

	conf.override_bounds	= false;
	conf.random		= false;

	conf.sig_min		= -102;
	conf.sig_max		= 10;
	conf.noise_min		= -102;
	conf.noise_max		= 10;


	conf.lthreshold_action	= TA_DISABLED;
	conf.lthreshold		= -80;
	conf.hthreshold_action	= TA_DISABLED;
	conf.hthreshold		= -10;

	conf.startup_scr	= 0;
}

void getconf(int argc, char *argv[])
{
	if_list = iw_get_interface_list();
	set_defaults();
	init_conf_items();
	read_cf();
	getargs(argc, argv);
}
