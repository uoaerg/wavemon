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

static void read_cf(void)
{
	char tmp[0x100], lv[0x20], rv[0x20], *cfname;
	char *lp;
	FILE *fd;
	int lnum = 0;
	char found;
	struct conf_item *ci = NULL;
	struct passwd *pw = getpwuid(getuid());
	int v_int;
	char *conv_err;

	cfname = malloc(strlen(pw->pw_dir) + strlen(CFNAME) + 3);
	strcpy(cfname, pw->pw_dir);
	strcat(cfname, "/");
	strcat(cfname, CFNAME);

	if ((fd = fopen(cfname, "r"))) {
		while (fgets(tmp, 0x100, fd)) {
			found = 0;
			++lnum;
			lp = tmp + strspn(tmp, " ");
			if (*lp != '#' && *lp != '\n') {
				if (strcspn(lp, " =") > sizeof(lv)) {
					fclose(fd);
					fatal_error("parse error in %s, line %d: identifier too long",
						    cfname, lnum);
				}
				strncpy(lv, lp, strcspn(lp, " ="));
				lv[strcspn(lp, " =")] = '\0';

				ll_reset(conf_items);
				while (!found && (ci = ll_getall(conf_items)))
					if (ci->type != t_sep
					    && ci->type != t_func
					    && !strcasecmp(ci->cfname, lv))
						found = 1;
				if (!found) {
					fclose(fd);
					fatal_error("parse error in %s, line %d: unknown identifier '%s'",
						    cfname, lnum, lv);
				}

				lp += strlen(lv);
				lp += strspn(lp, " ");
				if (*lp++ != '=') {
					fclose(fd);
					fatal_error("parse error in %s, line %d: missing '=' operator in assignment",
						    cfname, lnum);
				}
				lp += strspn(lp, " ");

				if (strcspn(lp, " \n") > sizeof(rv)) {
					fclose(fd);
					fatal_error("parse error in %s, line %d: argument too long",
						    cfname, lnum);
				}
				if (*lp == '\n') {
					fclose(fd);
					fatal_error("parse error in %s, line %d: argument expected",
						    cfname, lnum);
				}
				strncpy(rv, lp, strcspn(lp, " \n"));
				rv[strcspn(lp, " \n")] = '\0';

				switch (ci->type) {
				case t_int:
					v_int = strtol(rv, &conv_err, 10);
					if (*conv_err == '\0') {
						if (v_int > ci->max) {
							fclose(fd);
							fatal_error("parse error in %s, line %d: value exceeds maximum of %d",
								    cfname, lnum, (int)ci->max);
						} else if (v_int < ci->min) {
							fclose(fd);
							fatal_error("parse error in %s, line %d: value is below minimum of %d",
								    cfname, lnum, (int)ci->min);
						} else {
							*ci->v.i = v_int;
						}
					} else {
						fclose(fd);
						fatal_error("parse error in %s, line %d: integer value expected, '%s' found instead",
							    cfname, lnum, rv);
					}
					break;
				case t_string:
					if (strlen(rv) <= ci->max) {
						strncpy(ci->v.s, rv, LISTVAL_MAX);
					} else {
						fclose(fd);
						fatal_error("parse error in %s, line %d: argument too long (max %d chars)",
							    cfname, lnum, ci->max);
					}
					break;
				case t_switch:
					if (!strcasecmp(rv, "on")
					    || !strcasecmp(rv, "enabled")
					    || !strcasecmp(rv, "yes")
					    || !strcasecmp(rv, "1")) {
						*(ci->v.b) = 1;
					} else if (!strcasecmp(rv, "off")
						   || !strcasecmp(rv, "disabled")
						   || !strcasecmp(rv, "no")
						   || !strcasecmp(rv, "0")) {
						*(ci->v.b) = 0;
					} else {
						fclose(fd);
						fatal_error("parse error in %s, line %d: boolean expected, '%s' found instead",
							    cfname, lnum, rv);
					}
					break;
				case t_list:
					if ((v_int =
					     ll_scan(ci->list, "S", rv)) >= 0) {
						*ci->v.b = v_int;
					} else {
						fclose(fd);
						fatal_error("parse error in %s, line %d: '%s' is not a valid argument here",
							    cfname, lnum, rv);
					}
					break;
				case t_listval:
					if ((v_int =
					     ll_scan(ci->list, "S", rv)) >= 0) {
						strncpy(ci->v.s, ll_get(ci->list, v_int), LISTVAL_MAX);
					} else {
						fclose(fd);
						fatal_error("parse error in %s, line %d: '%s' is not a valid argument here",
							    cfname, lnum, rv);
					}
				case t_sep:	/* These two cases are missing from the enum, they are not handled */
				case t_func:	/* To pacify gcc -Wall, fall through here */
					break;

				}
			}
		}
		fclose(fd);
	} else if (errno != ENOENT) {
		fatal_error("cannot open %s: %s", cfname, strerror(errno));
	}
}

static void write_cf(void)
{
	struct passwd *pw = getpwuid(getuid());
	struct conf_item *ci = NULL;
	char tmp[0x100], rv[0x40], *cfname;
	char *lp, *cp;
	FILE *fd;
	int cfld;
	int add;
	int i;

	cfname = malloc(strlen(pw->pw_dir) + strlen(CFNAME) + 3);
	strcpy(cfname, pw->pw_dir);
	strcat(cfname, "/");
	strcat(cfname, CFNAME);

	cfld = ll_create();
	if ((fd = fopen(cfname, "r"))) {
		while (fgets(tmp, 0x100, fd))
			ll_push(cfld, "s", tmp);
		fclose(fd);
	}

	ll_reset(conf_items);
	while ((ci = ll_getall(conf_items))) {
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
		fatal_error("cannot open %s", cfname);
	ll_reset(cfld);
	while ((lp = ll_getall(cfld)))
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
