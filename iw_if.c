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

/*
 * Obtain network device information
 */

/* Interface information */
void if_getinf(char *ifname, struct if_info *info)
{
	struct ifreq ifr;
	int skfd = socket(AF_INET, SOCK_DGRAM, 0);

	if (skfd < 0)
		fatal_error("cannot open socket");

	memset(&ifr, 0, sizeof(struct ifreq));
	memset(info, 0, sizeof(struct if_info));

	/* Copy the 6 byte Ethernet address and the 4 byte struct in_addrs */
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
	if (ioctl(skfd, SIOCGIFADDR, &ifr) >= 0)
		memcpy(&info->addr, &ifr.ifr_addr.sa_data[2], 4);
	if (ioctl(skfd, SIOCGIFHWADDR, &ifr) >= 0)
		memcpy(&info->hwaddr, &ifr.ifr_hwaddr.sa_data, 6);
	if (ioctl(skfd, SIOCGIFNETMASK, &ifr) >= 0)
		memcpy(&info->netmask, &ifr.ifr_netmask.sa_data[2], 4);
	if (ioctl(skfd, SIOCGIFBRDADDR, &ifr) >= 0)
		memcpy(&info->bcast, &ifr.ifr_broadaddr.sa_data[2], 4);

	close(skfd);
}

/*
 * Populate list of available wireless interfaces
 * Return index into array-of-lists ld.
 */
int iw_get_interface_list(void)
{
	char *lp, tmp[LISTVAL_MAX];
	int   ld = ll_create();
	FILE *fd = fopen("/proc/net/wireless", "r");

	if (fd == NULL)
		fatal_error("no /proc/net/wireless - not compiled in?");

	while (fgets(tmp, LISTVAL_MAX, fd))
		if ((lp = strchr(tmp, ':'))) {
			*lp = '\0';
			ll_push(ld, "s", tmp + strspn(tmp, " "));
		}
	fclose(fd);

	if (ll_size(ld) == 0)
		fatal_error("no wireless interfaces found!");
	return ld;
}

void if_getstat(char *ifname, struct if_stat *stat)
{
	char	line[0x100];
	unsigned long d;
	char	*lp;
	FILE	*fd = fopen("/proc/net/dev", "r");

	if (fd == NULL)
		fatal_error("can not open /proc/net/");
	/*
	 * Inter-|   Receive                                                | Transmit
	 *  face |bytes    packets errs drop fifo frame compressed multicast|bytes packets
	 */
	while (fgets(line, sizeof(line), fd)) {
		lp = line + strspn(line, " ");
		if (!strncmp(lp, ifname, strlen(ifname))) {
			lp += strlen(ifname) + 1;
			lp += strspn(lp, " ");

			sscanf(lp, "%llu %llu %lu %lu %lu %lu %lu %lu %llu %llu",
				&stat->rx_bytes, &stat->rx_packets, &d, &d, &d, &d, &d, &d,
				&stat->tx_bytes, &stat->tx_packets);
		}
	}

	fclose(fd);
}

/*
 * obtain dynamic device information
 */
void iw_getinf_dyn(char *ifname, struct iw_dyn_info *info)
{
	struct iwreq iwr;
	int skfd = socket(AF_INET, SOCK_DGRAM, 0);

	if (skfd < 0)
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
 *	Obtain periodic IW statistics
 */
static int rnd_signal(int min, int max)
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
	return rlvl;
}

static int rnd_noise(int min, int max)
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
	return rlvl;
}


/* Random signal/noise */
static void iw_getstat_random(struct iw_statistics *stat)
{
	stat->qual.level = rnd_signal(-102, 10);
	stat->qual.noise = rnd_noise(-102, -30);
}


/* Code in part taken from wireless extensions #30 */
static void iw_getstat_old_style(struct iw_statistics *stat)
{
	char line[0x100], *lp;
	int  tmp;
	FILE *fd =fopen("/proc/net/wireless", "r");

	if (fd < 0)
		fatal_error("cannot open /proc/net/wireless");

	while (fgets(line, sizeof(line), fd)) {
		for (lp = line; *lp && isspace(*lp); lp++)
			;
		if (strncmp(lp, conf.ifname, strlen(conf.ifname)) == 0 &&
		    lp[strlen(conf.ifname)] == ':') {
			lp += strlen(conf.ifname) + 1;

			/* status */
			lp = strtok(lp, " ");
			sscanf(lp, "%X", &tmp);
			stat->status = (unsigned short)tmp;

			/* link quality */
			lp = strtok(NULL, " ");
			if (strchr(lp,'.') != NULL)
				stat->qual.updated |= IW_QUAL_QUAL_UPDATED;
			sscanf(lp, "%d", &tmp);
			stat->qual.qual = (unsigned char)tmp;

			/* signal level */
			lp = strtok(NULL, " ");
			if (strchr(lp,'.') != NULL)
				stat->qual.updated |= IW_QUAL_LEVEL_UPDATED;
			sscanf(lp, "%d", &tmp);
			stat->qual.level = (unsigned char)tmp;

			/* noise level */
			lp = strtok(NULL, " ");
			if (strchr(lp,'.') != NULL)
				stat->qual.updated |= IW_QUAL_NOISE_UPDATED;
			sscanf(lp, "%d", &tmp);
			stat->qual.noise = (unsigned char)tmp;

			/* # of packets w/ invalid nwid */
			lp = strtok(NULL, " ");
			sscanf(lp, "%u", &stat->discard.nwid);

			/* # of packets w/ invalid key */
			lp = strtok(NULL, " ");
			sscanf(lp, "%u", &stat->discard.code);

			/* # of packets w/ bad attitude */
			lp = strtok(NULL, " ");
			sscanf(lp, "%u", &stat->discard.misc);

			/* each interface appears just once */
			break;
		}
	}
	fclose(fd);
}

static void iw_getstat_new_style(struct iw_statistics *stat)
{
	struct iwreq		wrq;
	int skfd = socket(AF_INET, SOCK_DGRAM, 0);

	if (skfd < 0)
		fatal_error("cannot open socket");

	wrq.u.data.pointer = (caddr_t) stat;
	wrq.u.data.length  = sizeof(*stat);
	wrq.u.data.flags   = 0;
	strncpy(wrq.ifr_name, conf.ifname, IFNAMSIZ);

	if (ioctl(skfd, SIOCGIWSTATS, &wrq) < 0)
		fatal_error("cannot obtain iw statistics");
	close(skfd);
}

/*
 * Generate dBm values and perform sanity checks on values.
 * Code in part taken from wireless extensions #30
 * @range: range information, read-only
 * @qual:  wireless statistics, read-write
 * @dbm:   dBm level information, write-only
 */
void iw_sanitize(struct iw_range *range, struct iw_quality *qual, struct iw_levelstat *dbm)
{
	memset(dbm, 0, sizeof(*dbm));

	if (qual->level != 0 || (qual->updated & (IW_QUAL_DBM | IW_QUAL_RCPI))) {
		/*
		 * RCPI (IEEE 802.11k) statistics:
		 *    RCPI = int{(Power in dBm +110)*2}
		 *    for 0 dBm > Power > -110 dBm
		 */
		if (qual->updated & IW_QUAL_RCPI) {
			if (!(qual->updated & IW_QUAL_LEVEL_INVALID))
				dbm->signal = (double)(qual->level / 2.0) - 110.0;
			if (!(qual->updated & IW_QUAL_NOISE_INVALID))
				dbm->noise  = (double)(qual->noise / 2.0) - 110.0;
		/*
		 * Statistics in dBm (absolute power measurement)
		 * These are encoded in the range -192 .. 63
		 */
		} else	if ((qual->updated & IW_QUAL_DBM) ||
			     qual->level > range->max_qual.level) {

			if (!(qual->updated & IW_QUAL_LEVEL_INVALID)) {
				dbm->signal = qual->level;
				if (qual->level >= 64)
					dbm->signal -= 0x100;
			}
			if (!(qual->updated & IW_QUAL_NOISE_INVALID)) {
				dbm->noise = qual->noise;
				if (qual->noise >= 64)
					dbm->noise -= 0x100;
			}
		/*
		 * Relative values (0 -> max)
		 */
		} else {
			if (!(qual->updated & IW_QUAL_LEVEL_INVALID))
				dbm->signal = mw2dbm(qual->level);
			if (!(qual->updated & IW_QUAL_NOISE_INVALID))
				dbm->noise = mw2dbm(qual->noise);
		}
	} else {
		qual->updated |= IW_QUAL_ALL_INVALID;
	}

	/*
	 * Value sanity checks
	 *
	 * These rules serve to avoid "insensible" level displays. Please do send
	 * comments and/or bug reports if you encounter room for improvement.
	 *
	 *  1) if noise level is valid, but signal level is not, displaying just
	 *     the noise level does not reveal very much - can be omitted;
	 *  2) if the noise level is below an "invalid" magic value (see iw_if.h),
	 *     declare the noise value to be invalid;
	 *  3) SNR is only displayed if both signal and noise values are valid.
	 */
	if (qual->updated & IW_QUAL_LEVEL_INVALID)
		qual->updated |= IW_QUAL_NOISE_INVALID;
	if (dbm->noise <= NOISE_DBM_SANE_MIN)
		qual->updated |= IW_QUAL_NOISE_INVALID;
}

void iw_getstat(struct iw_stat *iw)
{
	memset(&iw->stat, 0, sizeof(iw->stat));

	if (conf.random)
		iw_getstat_random(&iw->stat);
	else if (iw->range.we_version_compiled > 11)
		iw_getstat_new_style(&iw->stat);
	else
		iw_getstat_old_style(&iw->stat);

	iw_sanitize(&iw->range, &iw->stat.qual, &iw->dbm);
}

void dump_parameters(void)
{
	struct iw_dyn_info info;
	struct iw_stat iw;
	struct if_stat nstat;
	int i;

	iw_getinf_dyn(conf.ifname, &info);
	iw_getinf_range(conf.ifname, &iw.range);
	iw_getstat(&iw);
	if_getstat(conf.ifname, &nstat);

	printf("\n");
	printf("Configured device: %s\n", conf.ifname);
	printf("       WE version: %d (source version %d)\n\n",
				iw.range.we_version_compiled, iw.range.we_version_source);

	printf("            essid: %s %s\n", info.cap_essid ? info.essid : "n/a",
					     info.essid_on  ? "" : "(off)");
	printf("             nick: %s\n", info.cap_nickname ? info.nickname : "n/a");

	if (info.cap_freq)
		printf("        frequency: %.4f GHz\n",	info.freq);
	else
		printf("        frequency: n/a\n");

	if (info.cap_sens)
		printf("      sensitivity: %ld/%d\n",	info.sens,
							iw.range.sensitivity);
	else
		printf("      sensitivity: n/a\n");

	if (info.cap_txpower)
		printf("         TX power: %d dBm (%.2f mW)\n",
					info.txpower_dbm, info.txpower_mw);
	else
		printf("         TX power: n/a\n");

	printf("             mode: %s\n", iw_opmode(info.mode));

	if (info.mode != 1 && info.cap_ap)
		printf("     access point: %s\n",
			mac_addr((unsigned char *)info.ap_addr.sa_data));

	if (info.cap_bitrate)
		printf("          bitrate: %g Mbit/s\n",
					(double)info.bitrate / 1.0e6);
	else
		printf("          bitrate: n/a\n");

	printf("          RTS thr: ");
	if (info.cap_rts && (!info.rts_on || info.rts)) {
		if (info.rts_on)
			printf("%d bytes\n", info.rts);
		else
			printf("off\n");
	} else
		printf("n/a\n");

	printf("         frag thr: ");
	if (info.cap_frag && (!info.frag_on || info.frag)) {
		if (info.frag_on)
			printf("%d bytes\n", info.frag);
		else
			printf("off\n");
	} else {
		printf("n/a\n");
	}

	printf("       encryption: ");
	if (info.cap_encode) {
		if (info.eflags.disabled || info.keysize == 0) {
			printf("off");
		} else {
			for (i = 0; i < info.keysize; i++)
				printf("%2X", info.key[i]);
			if (info.eflags.index)
				printf(" [%d]", info.key_index);
			if (info.eflags.restricted)
				printf(", restricted");
			if (info.eflags.open)
				printf(", open");
		}
	} else {
		printf("n/a");
	}

	printf("\n");
	printf(" power management:");
	if (info.cap_power) {
		if (info.pflags.disabled) {
			printf(" off");
		} else {
			if (info.pflags.min)
				printf(" min");
			else
				printf(" max");
			if (info.pflags.timeout)
				printf(" timeout");
			else
				printf(" period");
			if (info.pflags.rel) {
				if (info.pmvalue > 0)
					printf(" +%ld", info.pmvalue);
				else
					printf(" %ld", info.pmvalue);
			} else {
				if (info.pmvalue > 1000000)
					printf(" %ld s",
					       info.pmvalue / 1000000);
				else if (info.pmvalue > 1000)
					printf(" %ld ms", info.pmvalue / 1000);
				else
					printf(" %ld us", info.pmvalue);
			}
			if (info.pflags.unicast && info.pflags.multicast)
				printf(", rcv all");
			else if (info.pflags.multicast)
				printf(", rcv multicast");
			else
				printf(", rcv unicast");
			if (info.pflags.forceuc)
				printf(", force unicast");
			if (info.pflags.repbc)
				printf(", repeat broadcast");
		}
	} else {
		printf("n/a");
	}

	printf("\n\n");
	printf("     link quality: %d/%d\n",		 iw.stat.qual.qual,
							 iw.range.max_qual.qual);
	printf("     signal level: %.0f dBm (%s)\n",	 iw.dbm.signal,
	       dbm2units(iw.dbm.signal));
	printf("      noise level: %.0f dBm (%s)\n",	 iw.dbm.noise,
	       dbm2units(iw.dbm.noise));
	printf("              SNR: %.0f dB\n",		 iw.dbm.signal - iw.dbm.noise);

	/* RX stats */
	printf("         RX total: %llu packets (%s)\n", nstat.rx_packets,
							 byte_units(nstat.rx_bytes));
	printf("     invalid nwid: %u\n", iw.stat.discard.nwid);
	printf("      invalid key: %u\n", iw.stat.discard.code);
	if (iw.range.we_version_compiled > 11)	{
		printf("   invalid fragm.: %u\n", iw.stat.discard.fragment);
		printf("   missed beacons: %u\n", iw.stat.miss.beacon);
	}
	printf("      misc errors: %u\n", iw.stat.discard.misc);

	/* TX stats */
	printf("         TX total: %llu packets (%s)\n", nstat.tx_packets,
							 byte_units(nstat.tx_bytes));
	if (iw.range.we_version_compiled > 11)
		printf(" exc. MAC retries: %u\n", iw.stat.discard.retries);

	printf("\n");
}
