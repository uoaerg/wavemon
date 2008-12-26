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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <string.h>
#include <ncurses.h>

#include "conf.h"
#include "error.h"
#include "llist.h"
#include "iw_if.h"

struct iw_stat iw_stats;
struct iw_stat iw_stats_cache[IW_STACKSIZE];

struct wavemon_conf *conf;

/*
 * convert log dBm values to linear mW
 */

double dbm2mw(float in)
{
	return pow(10.0, in / 10.0);
}

char *dbm2units(float in)
{
	static char with_units[0x100];
	double val = dbm2mw(in);

	if (val < 0.00000001) {
		sprintf(with_units, "%.2f pW", val * 1e9);
	} else if (val < 0.00001) {
		sprintf(with_units, "%.2f nW", val * 1e6);
	} else if (val < 0.01) {
		sprintf(with_units, "%.2f uW", val * 1e3);
	} else {
		sprintf(with_units, "%.2f mW", val);
	}
	return with_units;
}

/*
 * convert linear mW values to log dBm
 */

double mw2dbm(float in)
{
	return pow(10.0, in / 10.0);
}

/*
 * convert frequency to GHz
 */

float freq2ghz(struct iw_freq *f)
{
	return (f->e ? f->m * pow(10, f->e) : f->m) / 1e9;
}

/*
 * Random signal generator
 */

signed int rnd_signal(int min, int max)
{
	static float rlvl, rlvl_next;
	static float step = 1.0;
	int i;

	for (i = 0; i < 1; i++)
		if (rlvl < rlvl_next) {
			if (rlvl_next - rlvl < step)
				step /= 2;
			rlvl += step;
		} else if (rlvl > rlvl_next) {
			if (rlvl - rlvl_next < step)
				step /= 2;
			rlvl -= step;
		}
	step += (rand() / (float)RAND_MAX) - 0.5;
	if ((rlvl == rlvl_next) || (step < 0.05)) {
		rlvl_next = (rand() / (float)RAND_MAX) * (max - min) + min;
		step = rand() / (float)RAND_MAX;
	}
	return (int)rlvl;
}

/* 
 * Random noise generator
 */

signed int rnd_noise(int min, int max)
{
	static float rlvl, rlvl_next;
	static float step = 1.0;
	int i;

	for (i = 0; i < 1; i++)
		if (rlvl < rlvl_next) {
			if (rlvl_next - rlvl < step)
				step /= 2;
			rlvl += step;
		} else if (rlvl > rlvl_next) {
			if (rlvl - rlvl_next < step)
				step /= 2;
			rlvl -= step;
		}
	step += (rand() / (float)RAND_MAX) - 0.5;
	if ((rlvl == rlvl_next) || (step < 0.05)) {
		rlvl_next = (rand() / (float)RAND_MAX) * (max - min) + min;
		step = rand() / (float)RAND_MAX;
	}
	return (int)rlvl;
}

/*
 * Notice for crossing the low threshold
 */
void low_signal()
{
	int fd;

	if (conf->lthreshold_action >= 2)
		flash();
	if (conf->lthreshold_action == 3 || conf->lthreshold_action == 1) {
		/*
		 * get console... no permissions? okay, take stdout
		 * instead and pray
		 */
		if ((fd = open("/dev/console", O_WRONLY)) < 0)
			fd = 1;

		if (ioctl(fd, KIOCSOUND, 1491) >= 0) {
			usleep(50000);
			ioctl(fd, KIOCSOUND, 1193);
			usleep(50000);
			ioctl(fd, KIOCSOUND, 0);
		} else
			beep();
		if (fd > 1)
			close(fd);
	}
}

/*
 * Notice for crossing the high threshold
 */
void high_signal()
{
	int fd;

	if (conf->hthreshold_action >= 2)
		flash();
	if (conf->hthreshold_action == 3 || conf->hthreshold_action == 1) {
		if ((fd = open("/dev/console", O_WRONLY)) < 0)
			fd = 1;

		if (ioctl(fd, KIOCSOUND, 4970) >= 0) {
			usleep(50000);
			ioctl(fd, KIOCSOUND, 0);
			usleep(50000);
			ioctl(fd, KIOCSOUND, 4970);
			usleep(50000);
			ioctl(fd, KIOCSOUND, 0);
		} else
			beep();
		if (fd > 1)
			close(fd);
	}
}

/*
 * get available interfaces
 */

int iw_getif()
{
	FILE *fd;
	int ld;
	int interfaces = 0;
	char tmp[0x20];
	char *lp;

	ld = ll_create();

	if (!(fd = fopen("/proc/net/wireless", "r")))
		fatal_error("no wireless extensions found!");

	while (fgets(tmp, 0x20, fd)) {
		if (strchr(tmp, ':')) {
			lp = tmp + strspn(tmp, " ");
			lp[strcspn(lp, ":")] = '\0';
			ll_push(ld, "s", lp);
			interfaces++;
		}
	}
	fclose(fd);

	if (!interfaces)
		fatal_error("no wireless interfaces found!");

	return ld;
}

/*
 * obtain dynamic device information
 */
void iw_getinf_dyn(char *ifname, struct iw_dyn_info *info)
{
	struct iwreq iwr;
	int skfd;

	if ((skfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		fatal_error("cannot open socket");

	memset(info, 0, sizeof(struct iw_dyn_info));

	strncpy(iwr.ifr_name, ifname, IFNAMSIZ);
	if (ioctl(skfd, SIOCGIWNAME, &iwr) < 0)
		fatal_error("cannot open device '%s'", iwr.u.name);
	strncpy(info->name, iwr.u.name, IFNAMSIZ);

	iwr.u.essid.pointer = (caddr_t) & (info->essid);
	iwr.u.essid.length = IW_ESSID_MAX_SIZE + 1;
	iwr.u.essid.flags = 0;
	if (ioctl(skfd, SIOCGIWESSID, &iwr) >= 0) {
		info->cap_essid = 1;
		info->essid_on = iwr.u.essid.flags;
		strncpy(info->essid, iwr.u.essid.pointer, IW_ESSID_MAX_SIZE);
	}

	if (ioctl(skfd, SIOCGIWNWID, &iwr) >= 0) {
		info->cap_nwid = 1;
		info->nwid = iwr.u.nwid.value;
		info->nwid_on = iwr.u.nwid.flags;
	}

	iwr.u.essid.pointer = (caddr_t) & (info->nickname);
	iwr.u.essid.length = IW_ESSID_MAX_SIZE + 1;
	iwr.u.essid.flags = 0;
	if (ioctl(skfd, SIOCGIWNICKN, &iwr) >= 0)
		if (iwr.u.data.length > 1)
			info->cap_nickname = 1;

	if (ioctl(skfd, SIOCGIWFREQ, &iwr) >= 0) {
		info->cap_freq = 1;
		if (iwr.u.freq.e)
			info->freq =
			    (iwr.u.freq.m * pow(10, iwr.u.freq.e)) / 1000000000;
		else
			info->freq = iwr.u.freq.m;
	}

	if (ioctl(skfd, SIOCGIWSENS, &iwr) >= 0) {
		info->cap_sens = 1;
		info->sens = iwr.u.sens.value;
	}

	if (ioctl(skfd, SIOCGIWRATE, &iwr) >= 0) {
		info->cap_bitrate = 1;
		info->bitrate = iwr.u.bitrate.value;
	}
#ifdef SIOCGIWTXPOW
	if (ioctl(skfd, SIOCGIWTXPOW, &iwr) >= 0) {
		info->cap_txpower = 1;
		if (iwr.u.txpower.flags == IW_TXPOW_DBM) {
			info->txpower_dbm = iwr.u.txpower.value;
			info->txpower_mw = dbm2mw(iwr.u.txpower.value);
		} else {
			info->txpower_mw = iwr.u.txpower.value;
			info->txpower_dbm = mw2dbm(iwr.u.txpower.value);
		}
	}
#endif

	if (ioctl(skfd, SIOCGIWRTS, &iwr) >= 0) {
		info->cap_rts = 1;
		info->rts = iwr.u.rts.value;
	}

	if (ioctl(skfd, SIOCGIWFRAG, &iwr) >= 0) {
		info->cap_frag = 1;
		info->frag = iwr.u.frag.value;
	}

	if (ioctl(skfd, SIOCGIWMODE, &iwr) >= 0) {
		info->cap_mode = 1;
		info->mode = iwr.u.mode;
	}

	iwr.u.data.pointer = (caddr_t) & info->key;
	iwr.u.data.length = IW_ENCODING_TOKEN_MAX;
	iwr.u.data.flags = 0;
	if (ioctl(skfd, SIOCGIWENCODE, &iwr) >= 0) {
		info->cap_encode = 1;
		info->keysize = iwr.u.data.length;
		if (iwr.u.data.flags & IW_ENCODE_DISABLED)
			info->eflags.disabled = 1;
		if (iwr.u.data.flags & IW_ENCODE_INDEX)
			info->eflags.index = 1;
		if (iwr.u.data.flags & IW_ENCODE_RESTRICTED)
			info->eflags.restricted = 1;
		if (iwr.u.data.flags & IW_ENCODE_OPEN)
			info->eflags.open = 1;
#ifdef IW_ENCODE_NOKEY
		if (iwr.u.data.flags & IW_ENCODE_NOKEY)
			info->eflags.nokey = 1;
#endif
	}

	if (ioctl(skfd, SIOCGIWPOWER, &iwr) >= 0) {
		info->cap_power = 1;
		if (iwr.u.power.disabled)
			info->pflags.disabled = 1;
		info->pmvalue = iwr.u.power.value;
		if (iwr.u.power.flags & IW_POWER_TIMEOUT)
			info->pflags.timeout = 1;
		if (iwr.u.power.flags & IW_POWER_UNICAST_R)
			info->pflags.unicast = 1;
		if (iwr.u.power.flags & IW_POWER_MULTICAST_R)
			info->pflags.multicast = 1;
		if (iwr.u.power.flags & IW_POWER_FORCE_S)
			info->pflags.forceuc = 1;
		if (iwr.u.power.flags & IW_POWER_REPEATER)
			info->pflags.repbc = 1;

#ifdef IW_POWER_MIN
		if (iwr.u.power.flags & IW_POWER_MIN)
			info->pflags.min = 1;
#endif

#ifdef IW_POWER_RELATIVE
		if (iwr.u.power.flags & IW_POWER_RELATIVE)
			info->pflags.rel = 1;
#endif

	}

	if (ioctl(skfd, SIOCGIWAP, &iwr) >= 0) {
		info->cap_ap = 1;
		memcpy(&info->ap_addr, &iwr.u.ap_addr, sizeof(struct sockaddr));
	}

	close(skfd);
}

/*
 * check availability of wireless extensions
 */
int iw_check_extensions(char *ifname)
{
	int skfd;
	struct iwreq iwr;
	int res;

	if ((skfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		fatal_error("cannot open socket\n");

	strncpy(iwr.ifr_name, ifname, IFNAMSIZ);
	res = ioctl(skfd, SIOCGIWNAME, &iwr);
	close(skfd);

	return res;
}

/*
 * get range information
 */
void iw_getinf_range(char *ifname, struct iw_range *range)
{
	int skfd;
	struct iwreq iwr;

	if ((skfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		fatal_error("cannot open socket");

	memset(range, 0, sizeof(struct iw_range));

	strncpy(iwr.ifr_name, ifname, IFNAMSIZ);
	if (ioctl(skfd, SIOCGIWNAME, &iwr) < 0)
		fatal_error("cannot open device '%s'", iwr.u.name);

	iwr.u.data.pointer = (caddr_t) range;
	iwr.u.data.length = sizeof(struct iw_range);
	iwr.u.data.flags = 0;
	if (ioctl(skfd, SIOCGIWRANGE, &iwr) < 0)
		fatal_error("could not get range information");

	close(skfd);
}

/*
 * obtain statistics
 */
int iw_getstat(char *ifname, struct iw_stat *stat, struct iw_stat *stack,
	       int slotsize, char random)
{
	FILE *fd;
	char tmp[0x100], buf[0x100];
	static int slot = 0;
	static float avg_signal = 0, avg_noise = 0;
	char *lp;

	if ((fd = fopen("/proc/net/wireless", "r")) < 0)
		fatal_error("cannot open /proc/net/wireless");

	while (fgets(tmp, 0x100, fd)) {
		lp = tmp + strspn(tmp, " ");
		if (!strncmp(lp, ifname, strlen(ifname))) {
			lp += strlen(ifname) + 1;
			lp += strspn(lp, " ");

			/* status */
			memset(buf, 0, sizeof(buf));
			strncpy(buf, lp, strcspn(lp, " "));
			/* insert your favourite status handler here. */
			lp += strlen(buf);
			lp += strspn(lp, " ");

			/* link quality */
			memset(buf, 0, sizeof(buf));
			strncpy(buf, lp, strcspn(lp, ". "));
			sscanf(buf, "%d", &stat->link);
			lp += strlen(buf);
			lp += strspn(lp, ". ");

			/* signal level */
			memset(buf, 0, sizeof(buf));
			strncpy(buf, lp, strcspn(lp, ". "));
			sscanf(buf, "%d", &stat->signal);
			if (random)
				stat->signal = rnd_signal(-102, 10);
			avg_signal += stat->signal / (float)slotsize;
			lp += strlen(buf);
			lp += strspn(lp, ". ");

			/* noise level */
			memset(buf, 0, sizeof(buf));
			strncpy(buf, lp, strcspn(lp, ". "));
			sscanf(buf, "%d", &stat->noise);
			if (random)
				stat->noise = rnd_noise(-102, -30);
			avg_noise += stat->noise / (float)slotsize;
			lp += strlen(buf);
			lp += strspn(lp, ". ");

			/* # of packets w/ invalid nwid */
			memset(buf, 0, sizeof(buf));
			strncpy(buf, lp, strcspn(lp, ". "));
			sscanf(buf, "%lu", &stat->dsc_nwid);
			lp += strlen(buf);
			lp += strspn(lp, ". ");

			/* # of packets w/ invalid key */
			memset(buf, 0, sizeof(buf));
			strncpy(buf, lp, strcspn(lp, ". "));
			sscanf(buf, "%lu", &stat->dsc_enc);
			lp += strlen(buf);
			lp += strspn(lp, ". ");

			/* # of packets w/ bad attitude */
			memset(buf, 0, sizeof(buf));
			strncpy(buf, lp, strcspn(lp, ". "));
			sscanf(buf, "%lu", &stat->dsc_misc);
		}
	}
	fclose(fd);

	if (++slot >= slotsize) {
		slot = 0;
		memmove(&stack[1], &stack[0],
			(IW_STACKSIZE - 1) * sizeof(struct iw_stat));
		stack->signal = avg_signal;
		stack->noise = avg_noise;
		avg_signal = avg_noise = 0;
		if (conf->lthreshold_action
		    && ((stack + 1)->signal < conf->lthreshold
			&& stack->signal >= conf->lthreshold))
			low_signal();
		else if (conf->hthreshold_action
			 && ((stack + 1)->signal > conf->hthreshold
			     && stack->signal <= conf->hthreshold))
			high_signal();
		return 1;
	}
	return 0;
}

/*
 * gather statistics periodically
 */
void s_handler(int signum)
{
	iw_getstat(ll_get(((struct conf_item *)ll_get(conf_items, 0))->list, 0),
		   &iw_stats, iw_stats_cache, conf->slotsize, conf->random);
	if (iw_stat_redraw)
		iw_stat_redraw();
}

/*
 * init statistics handler
 */
void init_stat_iv(struct wavemon_conf *wconf)
{
	struct itimerval i, iold;

	conf = wconf;

	i.it_interval.tv_sec = i.it_value.tv_sec = (int)(conf->stat_iv / 1000);
	i.it_interval.tv_usec = i.it_value.tv_usec =
	    fmod(conf->stat_iv, 1000) * 1000;

	setitimer(ITIMER_REAL, &i, &iold);

	signal(SIGALRM, s_handler);
}

/*
 * get a list of access points in range
 * for now this uses the deprecated SIOCGIWAPLIST facility, next revision
 * will use SIOCSIWSCAN (if available)
 */
int iw_get_aplist(char *ifname, struct iw_aplist *lst)
{
	int skfd;
	struct iwreq iwr;
	char buf[(sizeof(struct iw_quality) +
		  sizeof(struct sockaddr)) * IW_MAX_AP];
	int i, rv = 1;

	if ((skfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		fatal_error("could not open socket");

	memset(lst, 0, sizeof(struct iw_aplist));

	strncpy(iwr.ifr_name, ifname, IFNAMSIZ);
	iwr.u.data.pointer = (caddr_t) buf;
	iwr.u.data.length = IW_MAX_AP;
	iwr.u.data.flags = 0;
	if (ioctl(skfd, SIOCGIWAPLIST, &iwr) >= 0) {
		lst->num = iwr.u.data.length;

		/*
		 * copy addresses and quality information (if available) to list array
		 */

		for (i = 0; i < lst->num; i++)
			memcpy(&lst->aplist[i].addr,
			       buf + i * sizeof(struct sockaddr),
			       sizeof(struct sockaddr));

		if ((lst->has_quality = iwr.u.data.flags))
			for (i = 0; i < lst->num; i++)
				memcpy(&lst->aplist[i].quality,
				       buf +
				       lst->num * sizeof(struct sockaddr) +
				       i * sizeof(struct iw_quality),
				       sizeof(struct iw_quality));
	} else {
		rv = 0;
	}
	close(skfd);
	return rv;
}
