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
#include "iw_nl80211.h"

/* GLOBALS */
static WINDOW *w_levels, *w_stats, *w_if, *w_info, *w_net;
static struct timer dyn_updates;
static struct iw_stat cur;
static struct iw_nl80211_linkstat ls;

void sampling_init(void (*sampling_handler)(int))
{
	struct itimerval i;
	div_t d = div(conf.stat_iv, 1000);	/* conf.stat_iv in msec */

	xsignal(SIGALRM, SIG_IGN);
	iw_getinf_range(conf_ifname(), &cur.range);
	i.it_interval.tv_sec  = i.it_value.tv_sec  = d.quot;
	i.it_interval.tv_usec = i.it_value.tv_usec = d.rem * 1000;
	xsignal(SIGALRM, sampling_handler);

	(*sampling_handler)(0);
	setitimer(ITIMER_REAL, &i, NULL);
}

void sampling_do_poll(void)
{
	iw_getstat(&cur);
	iw_nl80211_get_linkstat(&ls);
	iw_cache_update(&cur);
}

static void display_levels(void)
{
	char nscale[2]   = { cur.dbm.signal - 20, cur.dbm.signal },
	     lvlscale[2] = { -40, -20},
	     snrscale[2] = { 6, 12 };
	char tmp[0x100];
	static float qual, signal, noise, ssnr;
	int line;

	for (line = 1; line <= WH_LEVEL; line++)
		mvwclrtoborder(w_levels, line, 1);

	if ((cur.stat.qual.updated & IW_QUAL_ALL_INVALID) == IW_QUAL_ALL_INVALID) {
		wattron(w_levels, A_BOLD);
		waddstr_center(w_levels, (WH_LEVEL + 1)/2, "NO INTERFACE DATA");
		goto done_levels;
	}

	line = 1;

	/* Noise data is rare. Use the space for spreading out. */
	if (cur.stat.qual.updated & IW_QUAL_NOISE_INVALID)
		line++;

	if (cur.stat.qual.updated & IW_QUAL_QUAL_INVALID) {
		line++;
	} else {
		qual = ewma(qual, cur.stat.qual.qual, conf.meter_decay / 100.0);

		mvwaddstr(w_levels, line++, 1, "link quality: ");
		sprintf(tmp, "%0.f%%  ", (1e2 * qual)/cur.range.max_qual.qual);
		waddstr_b(w_levels, tmp);
		sprintf(tmp, "(%0.f/%d)  ", qual, cur.range.max_qual.qual);
		waddstr(w_levels, tmp);

		waddbar(w_levels, line++, qual, 0, cur.range.max_qual.qual,
			lvlscale, true);
	}

	if (cur.stat.qual.updated & IW_QUAL_NOISE_INVALID)
		line++;

	if (cur.stat.qual.updated & IW_QUAL_LEVEL_INVALID) {
		line++;
	} else {
		signal = ewma(signal, cur.dbm.signal, conf.meter_decay / 100.0);

		mvwaddstr(w_levels, line++, 1, "signal level: ");
		sprintf(tmp, "%.0f dBm (%s)      ", signal, dbm2units(signal));
		waddstr_b(w_levels, tmp);

		waddbar(w_levels, line, signal, conf.sig_min, conf.sig_max,
			lvlscale, true);
		if (conf.lthreshold_action)
			waddthreshold(w_levels, line, signal, conf.lthreshold,
				      conf.sig_min, conf.sig_max, lvlscale, '>');
		if (conf.hthreshold_action)
			waddthreshold(w_levels, line, signal, conf.hthreshold,
				      conf.sig_min, conf.sig_max, lvlscale, '<');
		line++;
	}

	if (! (cur.stat.qual.updated & IW_QUAL_NOISE_INVALID)) {
		noise = ewma(noise, cur.dbm.noise, conf.meter_decay / 100.0);

		mvwaddstr(w_levels, line++, 1, "noise level:  ");
		sprintf(tmp, "%.0f dBm (%s)    ", noise, dbm2units(noise));
		waddstr_b(w_levels, tmp);

		waddbar(w_levels, line++, noise, conf.noise_min, conf.noise_max,
			nscale, false);
		/*
		 * Since we make sure (in iw_if.c) that invalid signal levels always
		 * imply invalid noise levels, we can display a valid SNR here.
		 */
		ssnr = ewma(ssnr, cur.dbm.signal - cur.dbm.noise,
				  conf.meter_decay / 100.0);

		mvwaddstr(w_levels, line++, 1, "signal-to-noise ratio: ");
		if (ssnr > 0)
			waddstr_b(w_levels, "+");
		sprintf(tmp, "%.0f dB   ", ssnr);
		waddstr_b(w_levels, tmp);

		waddbar(w_levels, line, ssnr, 0, 110, snrscale, true);
	}

done_levels:
	wrefresh(w_levels);
}

static void display_stats(void)
{
	struct if_stat nstat;
	char tmp[0x100];

	if_getstat(conf_ifname(), &nstat);

	/*
	 * Interface RX stats
	 */
	mvwaddstr(w_stats, 1, 1, "RX: ");

	if (ls.rx_packets) {
		sprintf(tmp, "%'u (%s)",  ls.rx_packets,
			byte_units(ls.rx_bytes));
		waddstr(w_stats, tmp);
	} else {
		waddstr(w_stats, "n/a");
	}

	if (ls.rx_bitrate[0]) {
		waddstr(w_stats, ", rate: ");
		waddstr_b(w_stats, ls.rx_bitrate);
	}

	if (ls.expected_thru) {
		sprintf(tmp, " (exp: %'u kB/s)",  ls.expected_thru);
		waddstr(w_stats, tmp);
	}

	if (ls.rx_drop_misc) {
		waddstr(w_stats, ", drop: ");
		sprintf(tmp, "%'llu", (unsigned long long)ls.rx_drop_misc);
		waddstr_b(w_stats, tmp);
	}

	wclrtoborder(w_stats);

	/*
	 * Interface TX stats
	 */
	mvwaddstr(w_stats, 2, 1, "TX: ");

	if (ls.tx_packets) {
		sprintf(tmp, "%'u (%s)",  ls.tx_packets,
			byte_units(ls.tx_bytes));
		waddstr_b(w_stats, tmp);
	} else {
		waddstr(w_stats, "n/a");
	}

	if (ls.tx_bitrate[0]) {
		waddstr(w_stats, ", rate: ");
		waddstr_b(w_stats, ls.tx_bitrate);
	}
	if (ls.tx_retries) {
		waddstr(w_stats, ", retries: ");
		sprintf(tmp, "%'u", ls.tx_retries);
		waddstr_b(w_stats, tmp);
	}

	if (ls.tx_failed) {
		waddstr(w_stats, ", failed: ");
		sprintf(tmp, "%'u", ls.tx_failed);
		waddstr_b(w_stats, tmp);
	}

	wclrtoborder(w_stats);
	wrefresh(w_stats);
}

static void display_info(WINDOW *w_if, WINDOW *w_info)
{
	struct iw_dyn_info info;
	struct iw_nl80211_ifstat ifs;
	struct iw_nl80211_reg ir;
	char tmp[0x100];
	int i;

	dyn_info_get(&info, conf_ifname(), &cur.range);
	iw_nl80211_getifstat(&ifs);
	iw_nl80211_getreg(&ir);

	/*
	 * Interface Part
	 */
	wmove(w_if, 1, 1);
	waddstr_b(w_if, conf_ifname());
	if (cur.range.enc_capa & IW_WPA_MASK)
		sprintf(tmp, " (%s, %s)", info.name, format_wpa(&cur.range));
	else
		sprintf(tmp, " (%s)", info.name);
	waddstr(w_if, tmp);

	/* PHY */
	waddstr(w_if, ", phy ");
	sprintf(tmp, "%d", ifs.phy);
	waddstr_b(w_if, tmp);

	/* Regulatory domain */
	waddstr(w_if, ", reg: ");
	if (ir.region > 0) {
		waddstr_b(w_if, ir.country);
		sprintf(tmp, " (%s)", dfs_domain_name(ir.region));
		waddstr(w_if, tmp);
	} else {
		waddstr_b(w_if, "n/a");
	}

	if (ifs.ssid[0]) {
		waddstr(w_if, ", SSID: ");
		waddstr_b(w_if, ifs.ssid);
	}

	wclrtoborder(w_if);
	wrefresh(w_if);

	/*
	 * Info window:
	 */
	wmove(w_info, 1, 1);
	waddstr(w_info, "mode: ");
	waddstr_b(w_info, iftype_name(ifs.iftype));

	if (!ether_addr_is_zero(&ls.bssid)) {
		waddstr_b(w_info, ", ");

		switch (ls.status) {
		case NL80211_BSS_STATUS_ASSOCIATED:
			waddstr(w_info, "connected to: ");
			break;
		case NL80211_BSS_STATUS_AUTHENTICATED:
			waddstr(w_info, "authenticated with: ");
			break;
		case NL80211_BSS_STATUS_IBSS_JOINED:
			waddstr(w_info, "joined IBSS: ");
			break;
		default:
			waddstr(w_info, "station: ");
		}
		waddstr_b(w_info, ether_lookup(&ls.bssid));
		/* XXX FIXME: tidy up */
#if 0
		sprintf(tmp, " autho: %d authe %d wme %d mfp %d tdls %d",
				ls.authorized, ls.authenticated, ls.wme, ls.mfp, ls.tdls);
		waddstr_b(w_info, tmp);
#endif
		if (ls.status == NL80211_BSS_STATUS_ASSOCIATED) {
			waddstr_b(w_info, ",");
			waddstr(w_info, " time: ");
			waddstr_b(w_info, pretty_time(ls.connected_time));

			waddstr(w_info, ", inactive: ");
			sprintf(tmp, "%.1fs", (float)ls.inactive_time/1e3);
			waddstr_b(w_info, tmp);
		}
	}
	wclrtoborder(w_info);

	wmove(w_info, 2, 1);
	/* Frequency / channel */
	if (ifs.freq) {
		waddstr(w_info, "freq: ");
		sprintf(tmp, "%d MHz", ifs.freq);
		waddstr_b(w_info, tmp);

		if (ifs.freq_ctr1 && ifs.freq_ctr1 != ifs.freq) {
			waddstr(w_info, ", ctr1: ");
			sprintf(tmp, "%d MHz", ifs.freq_ctr1);
			waddstr_b(w_info, tmp);
		}
		if (ifs.freq_ctr2 && ifs.freq_ctr2 != ifs.freq_ctr1 && ifs.freq_ctr2 != ifs.freq) {
			waddstr(w_info, ", ctr2: ");
			sprintf(tmp, "%d MHz", ifs.freq_ctr2);
			waddstr_b(w_info, tmp);
		}

		waddstr(w_info, ", channel: ");
		sprintf(tmp, "%d", ieee80211_frequency_to_channel(ifs.freq));
		waddstr_b(w_info, tmp);

		if (ifs.chan_width >= 0) {
			sprintf(tmp, " (width: %s)", channel_width_name(ifs.chan_width));
			waddstr(w_info, tmp);
		} else if (ifs.chan_type >= 0) {
			sprintf(tmp, " (%s)", channel_type_name(ifs.chan_type));
			waddstr(w_info, tmp);
		}

	} else {
		waddstr(w_info, "frequency/channel: n/a");
	}
	wclrtoborder(w_info);

	wmove(w_info, 3, 1);
	/* Beacons */
	waddstr(w_info, "beacons: ");

	if (ls.beacons) {
		sprintf(tmp, "%'llu", (unsigned long long)ls.beacons);
		waddstr_b(w_info, tmp);

		if (ls.beacon_loss) {
			waddstr(w_info, ", lost: ");
			sprintf(tmp, "%'u", ls.beacon_loss);
			waddstr_b(w_info, tmp);
		}
		waddstr(w_info, ", avg sig: ");
		sprintf(tmp, "%d dBm", (int8_t)ls.beacon_avg_sig);
		waddstr_b(w_info, tmp);

		waddstr(w_info, ", interval: ");
		sprintf(tmp, "%.1fs", (ls.beacon_int * 1024.0)/1e6);
		waddstr_b(w_info, tmp);

		waddstr(w_info, ", DTIM: ");
		sprintf(tmp, "%u", ls.dtim_period);
		waddstr_b(w_info, tmp);
	} else {
		waddstr(w_info, "n/a");
	}

	if (info.cap_sens) {
		waddstr(w_info, ",  sensitivity: ");
		if (info.sens < 0)
			sprintf(tmp, "%d dBm", info.sens);
		else
			sprintf(tmp, "%d/%d", info.sens,
				cur.range.sensitivity);
		waddstr_b(w_info, tmp);
	}

	wclrtoborder(w_info);

	wmove(w_info, 4, 1);
	waddstr(w_info, "power mgt: ");
	if (info.cap_power)
		waddstr_b(w_info, format_power(&info.power, &cur.range));
	else
		waddstr(w_info, "n/a");

	if (info.cap_txpower && info.txpower.disabled) {
		waddstr(w_info, ",  tx-power: off");
	} else if (info.cap_txpower) {
		/*
		 * Convention: auto-selected values start with a capital
		 *             letter, otherwise with a small letter.
		 */
		if (info.txpower.fixed)
			waddstr(w_info, ",  tx-power: ");
		else
			waddstr(w_info, ",  TX-power: ");
		waddstr_b(w_info, format_txpower(&info.txpower));
	}
	wclrtoborder(w_info);

	wmove(w_info, 5, 1);
	waddstr(w_info, "retry: ");
	if (info.cap_retry)
		waddstr_b(w_info, format_retry(&info.retry, &cur.range));
	else
		waddstr(w_info, "n/a");

	waddstr(w_info, ",  ");
	if (info.cap_rts) {
		waddstr(w_info, info.rts.fixed ? "rts/cts: " : "RTS/cts: ");
		if (info.rts.disabled)
			sprintf(tmp, "off");
		else
			sprintf(tmp, "%d B", info.rts.value);
		waddstr_b(w_info, tmp);
	} else {
		waddstr(w_info, "rts/cts: n/a");
	}

	waddstr(w_info, ",  ");
	if (info.cap_frag) {
		waddstr(w_info, info.frag.fixed ? "frag: " : "Frag: ");
		if (info.frag.disabled)
			sprintf(tmp, "off");
		else
			sprintf(tmp, "%d B", info.frag.value);
		waddstr_b(w_info, tmp);
	} else {
		waddstr(w_info, "frag: n/a");
	}
	wclrtoborder(w_info);

	wmove(w_info, 6, 1);
	waddstr(w_info, "encryption: ");
	if (info.keys) {
		int cnt = dyn_info_active_keys(&info);

		if (cnt == 0) {
			waddstr_b(w_info, "off (no key set)");
		} else if (info.active_key) {
			i = info.active_key - 1;
			waddstr_b(w_info, curtail(format_key(info.keys + i),
					  "..", MAXXLEN/2));

			if (info.keys[i].flags & IW_ENCODE_RESTRICTED)
				waddstr(w_info, ", restricted");
			if (info.keys[i].flags & IW_ENCODE_OPEN)
				waddstr(w_info, ", open");

			/* First key = default */
			if (cnt > 1 || info.active_key != 1) {
				sprintf(tmp, " [%d]", info.active_key);
				waddstr_b(w_info, tmp);
			}
			if (cnt > 1) {
				sprintf(tmp, " (%d other key%s)", cnt - 1,
					cnt == 2 ? "" : "s");
				waddstr(w_info, tmp);
			}
		} else  if (dyn_info_wep_keys(&info) == cnt) {
			waddstr_b(w_info, "off ");
			sprintf(tmp, "(%d disabled WEP key%s)", cnt,
				cnt == 1 ? "" : "s");
			waddstr(w_info, tmp);
		} else {
			uint8_t j = 0, k = 0;

			do  if (info.keys[j].size &&
				!(info.keys[j].flags & IW_ENCODE_DISABLED))
					info.keys[k++].size = info.keys[j].size;
			while (k < cnt && ++j < info.nkeys);

			if (cnt == 1)
				j = sprintf(tmp, "1 key (index #%u), ", j + 1);
			else
				j = sprintf(tmp, "%d keys with ", k);
			for (i = 0; i < k; i++)
				j += sprintf(tmp + j, "%s%d",	i ? "/" : "",
					     info.keys[i].size * 8);
			sprintf(tmp + j, " bits");
			waddstr_b(w_info, tmp);
		}
	} else if (has_net_admin_capability()) {
		waddstr(w_info, "no information available");
	} else {
		waddstr(w_info, "n/a (requires CAP_NET_ADMIN permissions)");
	}

	dyn_info_cleanup(&info);
	wclrtoborder(w_info);
	wrefresh(w_info);
}

static void display_netinfo(WINDOW *w_net)
{
	struct if_info info;
	char tmp[0x40];

	if_getinf(conf_ifname(), &info);

	wmove(w_net, 1, 1);
	wclrtoborder(w_net);
	if (getmaxy(w_net) == WH_NET_MAX) {
		waddstr(w_net, conf_ifname());

		waddstr_b(w_net, " (");
		waddstr(w_net, info.flags & IFF_UP ? "UP" : "DOWN");
		if (info.flags & IFF_RUNNING)		/* Interface RFC2863 OPER_UP	*/
			waddstr(w_net, " RUNNING");
#ifdef IFF_LOWER_UP	/* Linux 2.6.17 */
		if (info.flags & IFF_LOWER_UP)		/* Driver signals L1 up		*/
			waddstr(w_net, " LOWER_UP");
#endif
#ifdef IFF_DORMANT	/* Linux 2.6.17 */
		if (info.flags & IFF_DORMANT)		/* Driver signals dormant	*/
			waddstr(w_net, " DORMANT");
#endif
		if (info.flags & IFF_MASTER)		/* Master of a load balancer 	*/
			waddstr(w_net, " MASTER");
		if (info.flags & IFF_SLAVE)		/* Slave of a load balancer 	*/
			waddstr(w_net, " SLAVE");
		if (info.flags & IFF_POINTOPOINT)	/* Is a point-to-point link	*/
			waddstr(w_net, " POINTOPOINT");
		if (info.flags & IFF_DYNAMIC)		/* Address is volatile		*/
			waddstr(w_net, " DYNAMIC");
		if (info.flags & IFF_BROADCAST)		/* Valid broadcast address set	*/
			waddstr(w_net, " BROADCAST");
		if (info.flags & IFF_MULTICAST)		/* Supports multicast		*/
			waddstr(w_net, " MULTICAST");
		if (info.flags & IFF_ALLMULTI)		/* Receive all mcast  packets	*/
			waddstr(w_net, " ALLMULTI");
		if (info.flags & IFF_NOARP)		/* No ARP protocol		*/
			waddstr(w_net, " NOARP");
		if (info.flags & IFF_NOTRAILERS)	/* Avoid use of trailers	*/
			waddstr(w_net, " NOTRAILERS");
		if (info.flags & IFF_PROMISC)		/* Is in promiscuous mode	*/
			waddstr(w_net, " PROMISC");
		if (info.flags & IFF_DEBUG)		/* Internal debugging flag	*/
			waddstr(w_net, " DEBUG");
		waddstr_b(w_net, ")");

		wmove(w_net, 2, 1);
		wclrtoborder(w_net);
	}
	waddstr(w_net, "mac: ");
	waddstr_b(w_net, ether_lookup(&info.hwaddr));

	if (getmaxy(w_net) == WH_NET_MAX) {
		waddstr(w_net, ", qlen: ");
		sprintf(tmp, "%u", info.txqlen);
		waddstr_b(w_net, tmp);

		wmove(w_net, 3, 1);
		wclrtoborder(w_net);
	} else {
		waddstr(w_net, ", ");
	}
	waddstr(w_net, "ip: ");

	if (!info.addr.s_addr) {
		waddstr(w_net, "n/a");
	} else {
		sprintf(tmp, "%s/%u", inet_ntoa(info.addr),
				      prefix_len(&info.netmask));
		waddstr_b(w_net, tmp);

		/* only show bcast address if not set to the obvious default */
		if (info.bcast.s_addr !=
		    (info.addr.s_addr | ~info.netmask.s_addr)) {
			waddstr(w_net, ",  bcast: ");
			waddstr_b(w_net, inet_ntoa(info.bcast));
		}
	}

	/* 802.11 MTU may be greater than Ethernet MTU (1500) */
	if (info.mtu && info.mtu != ETH_DATA_LEN) {
		waddstr(w_net, ",  mtu: ");
		sprintf(tmp, "%u", info.mtu);
		waddstr_b(w_net, tmp);
	}

	wrefresh(w_net);
}

static void redraw_stat_levels(int signum)
{
	sampling_do_poll();
	display_levels();
	display_stats();
}

void scr_info_init(void)
{
	int line = 0;

	w_if	 = newwin_title(line, WH_IFACE, "Interface", true);
	line += WH_IFACE;
	w_levels = newwin_title(line, WH_LEVEL, "Levels", true);
	line += WH_LEVEL;
	w_stats	 = newwin_title(line, WH_STATS, "Statistics", true);
	line += WH_STATS;
	w_info	 = newwin_title(line, WH_INFO_MIN, "Info", true);
	line += WH_INFO_MIN;
	if (LINES >= WH_INFO_SCR_MIN + (WH_NET_MAX - WH_NET_MIN))
		w_net = newwin_title(line, WH_NET_MAX, "Network", false);
	else
		w_net = newwin_title(line, WH_NET_MIN, "Network", false);

	display_info(w_if, w_info);
	display_netinfo(w_net);
	start_timer(&dyn_updates, conf.info_iv * 1000000);
	sampling_init(redraw_stat_levels);
}

int scr_info_loop(WINDOW *w_menu)
{
	if (end_timer(&dyn_updates)) {
		display_info(w_if, w_info);
		display_netinfo(w_net);
		start_timer(&dyn_updates, conf.info_iv * 1000000);
	}
	return wgetch(w_menu);
}

void scr_info_fini(void)
{
	sampling_stop();

	delwin(w_net);
	delwin(w_info);
	delwin(w_stats);
	delwin(w_levels);
	delwin(w_if);
}
