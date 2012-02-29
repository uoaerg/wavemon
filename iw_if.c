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

/* Return true if @ifname is known to be up */
bool if_is_up(int skfd, const char *ifname)
{
	return if_get_flags(skfd, ifname) & IFF_UP;
}

/** Bring @ifname up if not already up. Return 0 if ok, < 0 on error. */
int if_set_up(int skfd, const char *ifname)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);

	ifr.ifr_flags = if_get_flags(skfd, ifname);
	if (ifr.ifr_flags & IFF_UP)
		return 0;

	ifr.ifr_flags |= IFF_UP;
	return ioctl(skfd, SIOCSIFFLAGS, &ifr);
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
 * iw_get_interface_list  -  Return NULL-terminated array of WiFi interfaces.
 * Use the safe route of checking /proc/net/dev/ for wireless interfaces:
 * - SIOCGIFCONF only returns running interfaces that have an IP address;
 * - /proc/net/wireless may exist, but may not list all wireless interfaces.
 */
char **iw_get_interface_list(void)
{
	char **if_list = NULL, *p, tmp[BUFSIZ];
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
			strncpy(wrq.ifr_name, p, IFNAMSIZ);
			if (ioctl(skfd, SIOCGIWNAME, &wrq) < 0)
				continue;

			if_list = realloc(if_list, sizeof(char *) * (nifs + 1));
			if_list[nifs-1] = strdup(p);
			if_list[nifs++] = NULL;
		}
	}
	close(skfd);
	fclose(fp);
	return if_list;
}

void if_getstat(const char *ifname, struct if_stat *stat)
{
	char line[0x100];
	unsigned long d;
	char *lp;
	const char path[] = "/proc/net/dev";
	FILE *fp = fopen(path, "r");

	if (fp == NULL)
		err_sys("can not open %s", path);
	/*
	 * Inter-|   Receive                                                | Transmit
	 *  face |bytes    packets errs drop fifo frame compressed multicast|bytes packets
	 */
	while (fgets(line, sizeof(line), fp)) {
		lp = line + strspn(line, " ");
		if (!strncmp(lp, ifname, strlen(ifname))) {
			lp += strlen(ifname) + 1;
			lp += strspn(lp, " ");

			sscanf(lp, "%llu %llu %lu %lu %lu %lu %lu %lu %llu %llu",
				&stat->rx_bytes, &stat->rx_packets, &d, &d, &d, &d, &d, &d,
				&stat->tx_bytes, &stat->tx_packets);
		}
	}
	fclose(fp);
}

/**
 * iw_dyn_info_get  -  populate dynamic information
 * @info:   information to populate
 * @ifname: interface name
 * @if:	    range information to use (number of encryption keys)
 */
void dyn_info_get(struct iw_dyn_info *info,
		  const char *ifname, struct iw_range *ir)
{
	struct iwreq iwr;
	int i, skfd = socket(AF_INET, SOCK_DGRAM, 0);

	if (skfd < 0)
		err_sys("%s: can not open socket", __func__);

	memset(info, 0, sizeof(struct iw_dyn_info));
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

	if (ioctl(skfd, SIOCGIWNWID, &iwr) >= 0) {
		info->cap_nwid = 1;
		memcpy(&info->nwid, &iwr.u.nwid, sizeof(info->nwid));
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

	info->nkeys = ir->max_encoding_tokens;
	if (info->nkeys) {
		info->keys = calloc(info->nkeys, sizeof(*info->keys));
		if (info->keys == NULL)
			err_sys("malloc(key array)");

		/* Get index of default key first */
		iwr.u.data.pointer = info->keys[0].key;
		iwr.u.data.length  = sizeof(info->keys[0].key);
		iwr.u.data.flags   = 0;
		if (ioctl(skfd, SIOCGIWENCODE, &iwr) < 0) {
			free(info->keys);
			info->keys  = NULL;
			info->nkeys = 0;
		} else {
			info->active_key = iwr.u.data.flags & IW_ENCODE_INDEX;
		}
	}
	/* If successful, populate the key array */
	for (i = 0; i < info->nkeys; i++) {
		iwr.u.data.pointer = info->keys[i].key;
		iwr.u.data.length  = sizeof(info->keys->key);
		iwr.u.data.flags   = i + 1;	/* counts 1..n instead of 0..n-1 */
		if (ioctl(skfd, SIOCGIWENCODE, &iwr) < 0) {
			free(info->keys);
			info->nkeys = 0;
			break;
		}
		info->keys[i].size  = iwr.u.data.length;
		info->keys[i].flags = iwr.u.data.flags;

		/* Validate whether the current key is indeed active */
		if (i + 1 == info->active_key && (info->keys[i].size == 0 ||
		    (info->keys[i].flags & IW_ENCODE_DISABLED)))
			info->active_key = 0;
	}

	if (ioctl(skfd, SIOCGIWAP, &iwr) >= 0) {
		info->cap_ap = 1;
		memcpy(&info->ap_addr, &iwr.u.ap_addr, sizeof(struct sockaddr));
	}
	close(skfd);
}

void dyn_info_cleanup(struct iw_dyn_info *info)
{
	if (info)
		free(info->keys);
}


/*
 * get range information
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

/*
 *	Obtain periodic IW statistics
 */
static int rand_wave(float *rlvl, float *step, float *rlvl_next, float range)
{
	int i;

	for (i = 0; i < WAVE_RAND_SPREAD; i++)
		if (*rlvl < *rlvl_next) {
			if (*rlvl_next - *rlvl < *step)
				*step /= 2.0;
			*rlvl += *step;
		} else if (*rlvl > *rlvl_next) {
			if (*rlvl - *rlvl_next < *step)
				*step /= 2.0;
			*rlvl -= *step;
		}
	*step += (random() / (float)RAND_MAX) - 0.5;
	if (*rlvl == *rlvl_next || *step < 0.05) {
		*rlvl_next = (range * random()) / RAND_MAX;
		*step      = random() / (float)RAND_MAX;
	}
	return *rlvl;
}

/* Random signal/noise/quality levels */
static void iw_getstat_random(struct iw_stat *iw)
{
	static float rnd_sig, snext, sstep = 1.0, rnd_noise, nnext, nstep = 1.0;

	rand_wave(&rnd_sig, &sstep, &snext, conf.sig_max - conf.sig_min);
	rand_wave(&rnd_noise, &nstep, &nnext, conf.noise_max - conf.noise_min);

	if (iw->range.max_qual.qual == 0)
		iw->range.max_qual.qual = WAVE_RAND_QUAL_MAX;

	iw->stat.qual.level	= dbm_to_u8(conf.sig_min + rnd_sig);
	iw->stat.qual.noise	= dbm_to_u8(conf.noise_min + rnd_noise);
	iw->stat.qual.updated	= IW_QUAL_DBM;
	iw->stat.qual.qual	= map_range(conf.sig_min + rnd_sig,
					    conf.sig_min, conf.sig_max,
					    0, iw->range.max_qual.qual);
}

static void iw_getstat_real(struct iw_statistics *stat)
{
	struct iwreq wrq;
	int skfd = socket(AF_INET, SOCK_DGRAM, 0);

	if (skfd < 0)
		err_sys("%s: can not open socket", __func__);

	wrq.u.data.pointer = (caddr_t) stat;
	wrq.u.data.length  = sizeof(*stat);
	wrq.u.data.flags   = 0;
	strncpy(wrq.ifr_name, conf_ifname(), IFNAMSIZ);

	if (ioctl(skfd, SIOCGIWSTATS, &wrq) < 0) {
		/*
		 * iw_handler_get_iwstats() returns EOPNOTSUPP if
		 * there are no statistics. Bail out in this case.
		 */
		if (errno != EOPNOTSUPP)
			err_sys("can not obtain iw statistics");
		errno = 0;
		memset(&wrq, 0, sizeof(wrq));
	}
	close(skfd);
}

/*
 * Generate dBm values and perform sanity checks on values.
 * Code in part taken from wireless extensions #30
 * @range: range information, read-only
 * @qual:  wireless statistics, read-write
 * @dbm:   dBm level information, write-only
 */
void iw_sanitize(struct iw_range *range, struct iw_quality *qual,
		 struct iw_levelstat *dbm)
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

		} else if ((qual->updated & IW_QUAL_DBM) ||
			   qual->level > range->max_qual.level) {
			if (!(qual->updated & IW_QUAL_LEVEL_INVALID))
				dbm->signal = u8_to_dbm(qual->level);
			if (!(qual->updated & IW_QUAL_NOISE_INVALID))
				dbm->noise  = u8_to_dbm(qual->noise);
		} else {
			/*
			 * Relative values (0 -> max)
			 */
			if (!(qual->updated & IW_QUAL_LEVEL_INVALID))
				dbm->signal = mw2dbm(qual->level);
			if (!(qual->updated & IW_QUAL_NOISE_INVALID))
				dbm->noise  = mw2dbm(qual->noise);
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
		iw_getstat_random(iw);
	else
		iw_getstat_real(&iw->stat);

	iw_sanitize(&iw->range, &iw->stat.qual, &iw->dbm);
}

void dump_parameters(void)
{
	struct iw_dyn_info info;
	struct iw_stat iw;
	struct if_stat nstat;
	int i;

	iw_getinf_range(conf_ifname(), &iw.range);
	dyn_info_get(&info, conf_ifname(), &iw.range);
	iw_getstat(&iw);
	if_getstat(conf_ifname(), &nstat);

	printf("\n");
	printf("Configured device: %s (%s)\n", conf_ifname(), info.name);
	printf("         Security: %s\n", iw.range.enc_capa ?
			format_enc_capab(iw.range.enc_capa, ", ") : "WEP");
	if (iw.range.num_encoding_sizes &&
	    iw.range.num_encoding_sizes < IW_MAX_ENCODING_SIZES) {

		printf("        Key sizes: ");
		for (i = 0; i < iw.range.num_encoding_sizes; i++) {
			if (i)
				printf(", ");
			if (iw.range.encoding_size[i] == 5)
				printf("WEP-40");
			else if (iw.range.encoding_size[i] == 13)
				printf("WEP-104");
			else
				printf("%u bits",
					iw.range.encoding_size[i] * 8);
		}
		printf("\n");
	}
	printf("       WE version: %d (source version %d)\n\n",
	       iw.range.we_version_compiled, iw.range.we_version_source);

	if (info.cap_essid) {
		if (info.essid_ct > 1)
			printf("            essid: \"%s\" [%d]\n",
						info.essid, info.essid_ct);
		else if (info.essid_ct)
			printf("            essid: \"%s\"\n", info.essid);
		else
			printf("            essid: off/any\n");
	}

	if (info.cap_nickname)
		printf("             nick: \"%s\"\n", info.nickname);

	if (info.cap_nwid) {
		if (info.nwid.disabled)
			printf("             nwid: off/any\n");
		else
			printf("             nwid: %X\n", info.nwid.value);
	}

	/* Some drivers only return the channel (e.g. ipw2100) */
	if (info.cap_freq && info.freq < 256)
		info.freq = channel_to_freq(info.freq, &iw.range);
	if (info.cap_freq && info.freq > 1e3) {
		i = freq_to_channel(info.freq, &iw.range);
		if (i >= 0)
			printf("          channel: %d\n", i);
		printf("        frequency: %g GHz\n", info.freq / 1.0e9);
	} else
		printf("        frequency: n/a\n");

	if (info.cap_sens) {
		if (info.sens < 0)
			printf("      sensitivity: %d dBm\n", info.sens);
		else
			printf("      sensitivity: %d/%d\n", info.sens,
						   iw.range.sensitivity);
	}

	if (info.cap_txpower && info.txpower.disabled)
		printf("         tx-power: off\n");
	else if (info.cap_txpower && info.txpower.fixed)
		printf("         tx-power: %s\n", format_txpower(&info.txpower));
	else if (info.cap_txpower)
		printf("         TX-power: %s\n", format_txpower(&info.txpower));

	printf("             mode: %s\n", iw_opmode(info.mode));

	if (info.mode != 1 && info.cap_ap)
		printf("     access point: %s\n", format_bssid(&info.ap_addr));

	if (info.bitrate)
		printf("          bitrate: %g Mbit/s\n", info.bitrate / 1.0e6);
	else
		printf("          bitrate: n/a\n");

	printf("            retry: ");
	if (info.cap_retry)
		printf("%s\n", format_retry(&info.retry, &iw.range));
	else
		printf("n/a\n");

	printf("          rts thr: ");
	if (info.cap_rts) {
		if (info.rts.disabled)
			printf("off\n");
		else
			printf("%d B %s\n", info.rts.value,
			       info.rts.fixed ? "" : "(auto-select)");
	} else
		printf("n/a\n");

	printf("         frag thr: ");
	if (info.cap_frag) {
		if (info.frag.disabled)
			printf("off\n");
		else
			printf("%d B %s\n", info.frag.value,
			       info.frag.fixed ? "" : "(auto-select)");
	} else {
		printf("n/a\n");
	}

	printf("       encryption: ");
	if (!info.nkeys && has_net_admin_capability())
		printf("no information available\n");
	else if (!info.nkeys)
		printf("n/a (requires CAP_NET_ADMIN permissions)\n");
	for (i = 0; i < info.nkeys; i++) {
		if (i)
			printf("                   ");
		/* Current key is marked by `=' sign */
		printf("[%u]%s ", i + 1, i + 1 == info.active_key ? "=" : ":");

		if (info.keys[i].flags & IW_ENCODE_DISABLED || !info.keys[i].size) {
			printf("off\n");
		} else {
			printf("%s", format_key(info.keys + i));
			if (info.keys[i].flags & IW_ENCODE_RESTRICTED)
				printf(", restricted");
			if (info.keys[i].flags & IW_ENCODE_OPEN)
				printf(", open");
			printf("\n");
		}
	}

	printf(" power management: ");
	if (info.cap_power)
		printf("%s\n", format_power(&info.power, &iw.range));
	else
		printf("n/a\n");

	printf("\n");
	printf("     link quality: %d/%d\n", iw.stat.qual.qual,
	       iw.range.max_qual.qual);
	printf("     signal level: %.0f dBm (%s)\n", iw.dbm.signal,
	       dbm2units(iw.dbm.signal));
	printf("      noise level: %.0f dBm (%s)\n", iw.dbm.noise,
	       dbm2units(iw.dbm.noise));
	printf("              SNR: %.0f dB\n", iw.dbm.signal - iw.dbm.noise);

	/* RX stats */
	printf("         RX total: %'llu packets (%s)\n", nstat.rx_packets,
	       byte_units(nstat.rx_bytes));
	printf("     invalid nwid: %'u\n", iw.stat.discard.nwid);
	printf("      invalid key: %'u\n", iw.stat.discard.code);
	printf("   invalid fragm.: %'u\n", iw.stat.discard.fragment);
	printf("   missed beacons: %'u\n", iw.stat.miss.beacon);
	printf("      misc errors: %'u\n", iw.stat.discard.misc);

	/* TX stats */
	printf("         TX total: %'llu packets (%s)\n", nstat.tx_packets,
	       byte_units(nstat.tx_bytes));
	printf(" exc. MAC retries: %'u\n", iw.stat.discard.retries);

	printf("\n");
	dyn_info_cleanup(&info);
}
