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

/* GLOBALS */
static WINDOW *w_levels, *w_stats;

static struct iw_stat cur;
void (*iw_stat_redraw) (void);

/*
 * Statistics handler for period polling
 */
static void sampling_handler(int signum)
{
	iw_getstat(&cur);
	iw_cache_update(&cur);

	if (iw_stat_redraw)
		iw_stat_redraw();
}

static void init_stat_iv(void)
{
	struct itimerval i;
	div_t d = div(conf.stat_iv, 1000);	/* conf.stat_iv in msec */

	i.it_interval.tv_sec  = i.it_value.tv_sec  = d.quot;
	i.it_interval.tv_usec = i.it_value.tv_usec = d.rem * 1000;

	setitimer(ITIMER_REAL, &i, NULL);

	signal(SIGALRM, sampling_handler);
}

void reinit_on_changes(void)
{
	static int stat_iv = 0;

	if (conf.stat_iv != stat_iv) {
		iw_getinf_range(conf.ifname, &cur.range);
		init_stat_iv();
		stat_iv = conf.stat_iv;
	}
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
		sprintf(tmp, "%0.f/%d  ", qual, cur.range.max_qual.qual);
		waddstr_b(w_levels, tmp);

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

	if_getstat(conf.ifname, &nstat);

	/*
	 * Interface RX stats
	 */
	mvwaddstr(w_stats, 1, 1, "RX: ");

	sprintf(tmp, "%'llu (%s)",  nstat.rx_packets,
		byte_units(nstat.rx_bytes));
	waddstr_b(w_stats, tmp);

	waddstr(w_stats, ", invalid: ");
	sprintf(tmp, "%'u", cur.stat.discard.nwid);

	waddstr_b(w_stats, tmp);
	waddstr(w_stats, " nwid, ");

	sprintf(tmp, "%'u", cur.stat.discard.code);
	waddstr_b(w_stats, tmp);
	waddstr(w_stats, " crypt, ");

	if (cur.range.we_version_compiled > 11) {
		sprintf(tmp, "%'u", cur.stat.discard.fragment);
		waddstr_b(w_stats, tmp);
		waddstr(w_stats, " frag, ");
	}
	sprintf(tmp, "%'u", cur.stat.discard.misc);
	waddstr_b(w_stats, tmp);
	waddstr(w_stats, " misc");
	wclrtoborder(w_stats);

	/*
	 * Interface TX stats
	 */
	mvwaddstr(w_stats, 2, 1, "TX: ");

	sprintf(tmp, "%'llu (%s)",  nstat.tx_packets,
		byte_units(nstat.tx_bytes));
	waddstr_b(w_stats, tmp);

	if (cur.range.we_version_compiled > 11) {
		waddstr(w_stats, ", mac retries: ");
		sprintf(tmp, "%'u", cur.stat.discard.retries);
		waddstr_b(w_stats, tmp);

		waddstr(w_stats, ", missed beacons: ");
		sprintf(tmp, "%'u", cur.stat.miss.beacon);
		waddstr_b(w_stats, tmp);
	}
	wclrtoborder(w_stats);
	wrefresh(w_stats);
}

static void redraw_stats(void)
{
	display_levels();
	display_stats();
}

static void display_info(WINDOW *w_if, WINDOW *w_info)
{
	struct iw_dyn_info info;
	char tmp[0x100];
	int i;

	dyn_info_get(&info, conf.ifname, &cur.range);

	wmove(w_if, 1, 1);
	waddstr_b(w_if, conf.ifname);
	if (cur.range.enc_capa & IW_WPA_MASK)
		sprintf(tmp, " (%s, %s)", info.name, format_wpa(&cur.range));
	else
		sprintf(tmp, " (%s)", info.name);
	waddstr(w_if, tmp);

	if (info.cap_essid) {
		waddstr_b(w_if, ",");
		waddstr(w_if, "  ESSID: ");
		if (info.essid_ct > 1)
			sprintf(tmp, "\"%s\" [%d]", info.essid,
						    info.essid_ct);
		else if (info.essid_ct)
			sprintf(tmp, "\"%s\"", info.essid);
		else
			sprintf(tmp, "off/any");
		waddstr_b(w_if, tmp);
	}

	if (info.cap_nickname) {
		waddstr(w_if, ",  nick: ");
		sprintf(tmp, "\"%s\"", info.nickname);
		waddstr_b(w_if, tmp);
	}

	if (info.cap_nwid) {
		waddstr(w_if, ",  nwid: ");
		if (info.nwid.disabled)
			sprintf(tmp, "off/any");
		else
			sprintf(tmp, "%X", info.nwid.value);
		waddstr_b(w_if, tmp);
	}
	wclrtoborder(w_if);
	wrefresh(w_if);

	wmove(w_info, 1, 1);
	waddstr(w_info, "mode: ");
	if (info.cap_mode)
		waddstr_b(w_info, iw_opmode(info.mode));
	else
		waddstr(w_info, "n/a");

	if (info.mode != IW_MODE_MASTER &&
	    info.mode != IW_MODE_SECOND && info.mode != IW_MODE_MONITOR) {
		if (info.mode == IW_MODE_ADHOC)
			waddstr(w_info, ",  cell: ");
		else
			waddstr(w_info, ",  access point: ");

		if (info.cap_ap)
			waddstr_b(w_info, format_bssid(&info.ap_addr));
		else
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

	wmove(w_info, 2, 1);
	if (info.cap_freq && info.freq < 256)
		info.freq = channel_to_freq(info.freq, &cur.range);
	if (info.cap_freq && info.freq > 1e3) {
		waddstr(w_info, "freq: ");
		sprintf(tmp, "%g GHz", info.freq / 1.0e9);
		waddstr_b(w_info, tmp);

		i = freq_to_channel(info.freq, &cur.range);
		if (i >= 0) {
			waddstr(w_info, ", channel: ");
			sprintf(tmp, "%d", i);
			waddstr_b(w_info, tmp);
		}
	} else {
		waddstr(w_info, "frequency/channel: n/a");
	}

	if (! (info.mode >= IW_MODE_MASTER && info.mode <= IW_MODE_MONITOR)) {
		waddstr(w_info, ",  bitrate: ");
		if (info.bitrate) {
			sprintf(tmp, "%g Mbit/s", info.bitrate / 1.0e6);
			waddstr_b(w_info, tmp);
		} else
			waddstr(w_info, "n/a");
	}
	wclrtoborder(w_info);

	wmove(w_info, 3, 1);
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

	wmove(w_info, 4, 1);
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

	wmove(w_info, 5, 1);
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

	if_getinf(conf.ifname, &info);

	mvwaddstr(w_net, 1, 1, "ip: ");
	if (!info.addr.s_addr) {
		waddstr(w_net, "no address");
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
	waddstr(w_net, ",  mac: ");
	waddstr_b(w_net, ether_lookup(&info.hwaddr));

	wclrtoborder(w_net);
	wrefresh(w_net);
}

enum wavemon_screen scr_info(WINDOW *w_menu)
{
	WINDOW *w_if, *w_info, *w_net;
	struct timer t1;
	int key = 0, line = 0;

	iw_stat_redraw = NULL;

	w_if	 = newwin_title(line, WH_IFACE, "Interface", true);
	line += WH_IFACE;
	w_levels = newwin_title(line, WH_LEVEL, "Levels", true);
	line += WH_LEVEL;
	w_stats	 = newwin_title(line, WH_STATS, "Statistics", true);
	line += WH_STATS;
	w_info	 = newwin_title(line, WH_INFO_MIN, "Info", true);
	line += WH_INFO_MIN;
	w_net	 = newwin_title(line, WH_NET_MIN, "Network", false);

	while (key < KEY_F(1) || key > KEY_F(10)) {
		display_info(w_if, w_info);
		display_netinfo(w_net);
		iw_stat_redraw = redraw_stats;

		start_timer(&t1, conf.info_iv * 1000000);
		while (!end_timer(&t1) && (key = wgetch(w_menu)) <= 0)
			sleep(1);

		/* Keyboard shortcuts */
		if (key == 'q')
			key = KEY_F(10);
		else if (key == 'i')
			key = KEY_F(1);
	}

	iw_stat_redraw = NULL;

	delwin(w_if);
	delwin(w_levels);
	delwin(w_stats);
	delwin(w_info);
	delwin(w_net);

	return key - KEY_F(1);
}
