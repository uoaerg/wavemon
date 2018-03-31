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
#include "iw_if.h"

/* Determine the artificial spreading of random samples (best: 1..10) */
#define WAVE_RAND_SPREAD	1
/* Fallback maximum quality level when using random samples. */
#define WAVE_RAND_QUAL_MAX	100

/*
 * Obtain network device information
 */
static int if_get_flags(int skfd, const char *ifname)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(struct ifreq));
	strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);

	if (ioctl(skfd, SIOCGIFFLAGS, &ifr) < 0)
		err_sys("can not get interface flags for %s", ifname);
	return ifr.ifr_flags;
}

/* Return true if @ifname is known to be up. */
bool if_is_up(const char *ifname)
{
	int ret, skfd = socket(AF_INET, SOCK_DGRAM, 0);

	if (skfd < 0)
		err_sys("%s: can not open socket", __func__);
	ret = if_get_flags(skfd, ifname) & IFF_UP;
	close(skfd);
	return ret;
}


/** Change the up/down state of @ifname according to @up. */
static int if_set_up_or_down(const char *ifname, bool up)
{
	struct ifreq ifr;
	int ret, skfd = socket(AF_INET, SOCK_DGRAM, 0);

	if (skfd < 0)
		err_sys("%s: can not open socket", __func__);

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);

	ifr.ifr_flags = if_get_flags(skfd, ifname);
	if (up) {
		ifr.ifr_flags |= IFF_UP;
	} else {
		ifr.ifr_flags &= ~IFF_UP;
	}
	ret = ioctl(skfd, SIOCSIFFLAGS, &ifr);
	close(skfd);
	return ret;
}

/** Bring @ifname up. */
int if_set_up(const char *ifname)
{
	return if_set_up_or_down(ifname, true);
}

/** Set @ifname down. */
int if_set_down(const char *ifname)
{
	return if_set_up_or_down(ifname, false);
}

/** Exit handler to restore interface 'down' state on exit via on_exit(3). */
void if_set_down_on_exit(int rc, void *arg)
{
	if (if_set_down(arg) < 0) {
		err_msg("unable to restore %s interface state - set down manually", arg);
	}
}

/* Interface information */
void if_getinf(const char *ifname, struct if_info *info)
{
	struct ifreq ifr;
	int skfd = socket(AF_INET, SOCK_DGRAM, 0);

	if (skfd < 0)
		err_sys("%s: can not open socket", __func__);

	memset(&ifr, 0, sizeof(struct ifreq));
	memset(info, 0, sizeof(struct if_info));

	info->flags = if_get_flags(skfd, ifname);

	strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
	if (ioctl(skfd, SIOCGIFMTU, &ifr) == 0)
		info->mtu = ifr.ifr_mtu;

	if (ioctl(skfd, SIOCGIFTXQLEN, &ifr) >= 0)
		info->txqlen = ifr.ifr_qlen;

	/* Copy the 6 byte Ethernet address and the 4 byte struct in_addrs */
	if (ioctl(skfd, SIOCGIFHWADDR, &ifr) >= 0)
		memcpy(&info->hwaddr, &ifr.ifr_hwaddr.sa_data, 6);
	if (ioctl(skfd, SIOCGIFADDR, &ifr) >= 0)
		memcpy(&info->addr, &ifr.ifr_addr.sa_data[2], 4);
	if (ioctl(skfd, SIOCGIFNETMASK, &ifr) >= 0)
		memcpy(&info->netmask, &ifr.ifr_netmask.sa_data[2], 4);
	if (ioctl(skfd, SIOCGIFBRDADDR, &ifr) >= 0)
		memcpy(&info->bcast, &ifr.ifr_broadaddr.sa_data[2], 4);
	close(skfd);
}

/**
 * iw_get_interface_list  -  fill if_list with NULL-terminated array of WiFi
 * interfaces.
 * Use the safe route of checking /proc/net/dev/ for wireless interfaces:
 * - SIOCGIFCONF only returns running interfaces that have an IP address;
 * - /proc/net/wireless may exist, but may not list all wireless interfaces.
 */
void iw_get_interface_list(char** if_list, size_t max_entries)
{
	char *p, tmp[BUFSIZ];
	int  nifs = 1;		/* if_list[nifs-1] = NULL */
	struct iwreq wrq;
	FILE *fp;
	int skfd = socket(AF_INET, SOCK_DGRAM, 0);

	if (skfd < 0)
		err_sys("%s: can not open socket", __func__);

	fp = fopen("/proc/net/dev", "r");
	if (fp == NULL)
		err_sys("can not open /proc/net/dev");

	while (fgets(tmp, sizeof(tmp), fp)) {
		if ((p = strchr(tmp, ':'))) {
			for (*p = '\0', p = tmp; isspace(*p); )
				p++;
			/*
			 * Use SIOCGIWNAME as indicator: if interface does not
			 * support this ioctl, it has no wireless extensions.
			 */
			snprintf(wrq.ifr_name, IFNAMSIZ, "%s", p);
			if (ioctl(skfd, SIOCGIWNAME, &wrq) < 0)
				continue;
			if(nifs >= max_entries) break;
			if_list[nifs-1] = strdup(p);
			if_list[nifs++] = NULL;
		}
	}
	close(skfd);
	fclose(fp);
}

/**
 * iw_dyn_info_get  -  populate dynamic information
 * @info:   information to populate
 * @ifname: interface name
 * @if:	    range information to use (number of encryption keys)
 */
void dyn_info_get(struct iw_dyn_info *info, const char *ifname)
{
	struct iwreq iwr;
	int skfd = socket(AF_INET, SOCK_DGRAM, 0);

	if (skfd < 0)
		err_sys("%s: can not open socket", __func__);

	memset(info, 0, sizeof(*info));
	strncpy(iwr.ifr_name, ifname, IFNAMSIZ);

	if (ioctl(skfd, SIOCGIWNAME, &iwr) < 0)
		err_sys("can not open device '%s'", ifname);
	strncpy(info->name, iwr.u.name, IFNAMSIZ);

	iwr.u.essid.pointer = (caddr_t) info->essid;
	iwr.u.essid.length  = sizeof(info->essid);
	iwr.u.essid.flags   = 0;
	if (ioctl(skfd, SIOCGIWESSID, &iwr) >= 0) {
		info->cap_essid = 1;
		/* Convert potential ESSID index to count > 0 */
		info->essid_ct  = iwr.u.essid.flags & IW_ENCODE_INDEX ?  : 1;
		info->essid[iwr.u.essid.length] = '\0';
	}

	iwr.u.essid.pointer = (caddr_t) info->nickname;
	iwr.u.essid.length  = sizeof(info->nickname);
	iwr.u.essid.flags   = 0;
	if (ioctl(skfd, SIOCGIWNICKN, &iwr) >= 0 &&
	    iwr.u.data.length > 1)
		info->cap_nickname = 1;

	if (ioctl(skfd, SIOCGIWFREQ, &iwr) >= 0) {
		info->cap_freq = 1;
		info->freq     = freq_to_hz(&iwr.u.freq);
	}

	if (ioctl(skfd, SIOCGIWSENS, &iwr) >= 0) {
		info->cap_sens = 1;
		info->sens     = iwr.u.sens.value;
	}

	if (ioctl(skfd, SIOCGIWRATE, &iwr) >= 0)
		info->bitrate = iwr.u.bitrate.value;

	if (ioctl(skfd, SIOCGIWTXPOW, &iwr) >= 0) {
		info->cap_txpower = 1;
		memcpy(&info->txpower, &iwr.u.txpower, sizeof(info->txpower));
	}

	if (ioctl(skfd, SIOCGIWPOWER, &iwr) >= 0) {
		info->cap_power = 1;
		memcpy(&info->power, &iwr.u.power, sizeof(info->power));
	}

	if (ioctl(skfd, SIOCGIWRETRY, &iwr) >= 0) {
		info->cap_retry = 1;
		memcpy(&info->retry, &iwr.u.retry, sizeof(info->retry));
	}

	if (ioctl(skfd, SIOCGIWRTS, &iwr) >= 0) {
		info->cap_rts = 1;
		memcpy(&info->rts, &iwr.u.rts, sizeof(info->rts));
	}

	if (ioctl(skfd, SIOCGIWFRAG, &iwr) >= 0) {
		info->cap_frag = 1;
		memcpy(&info->frag, &iwr.u.frag, sizeof(info->frag));
	}

	if (ioctl(skfd, SIOCGIWMODE, &iwr) >= 0) {
		info->cap_mode = 1;
		info->mode     = iwr.u.mode;
	}

	if (ioctl(skfd, SIOCGIWAP, &iwr) >= 0) {
		info->cap_ap = 1;
		memcpy(&info->ap_addr, &iwr.u.ap_addr, sizeof(struct sockaddr));
	}
	close(skfd);
}

/*
 * Request range information for a given wireless interface.
 * @ifname: name of the wireless argument
 * @range:  storage location to populate with range information.
 */
void iw_getinf_range(const char *ifname, struct iw_range *range)
{
	struct iwreq iwr;
	int skfd = socket(AF_INET, SOCK_DGRAM, 0);

	if (skfd < 0)
		err_sys("%s: can not open socket", __func__);

	memset(range, 0, sizeof(struct iw_range));
	strncpy(iwr.ifr_name, ifname, IFNAMSIZ);

	iwr.u.data.pointer = (caddr_t) range;
	iwr.u.data.length  = sizeof(struct iw_range);
	iwr.u.data.flags   = 0;
	if (ioctl(skfd, SIOCGIWRANGE, &iwr) < 0)
		err_sys("can not get range information");
	close(skfd);
}
