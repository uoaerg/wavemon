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
static WINDOW *w_levels, *w_stats, *w_menu;

static struct iw_stat cur;
void (*iw_stat_redraw)(void);

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
		init_stat_iv();
		stat_iv = conf.stat_iv;
	}
}

static void display_levels(void)
{
	char	nscale[2]   = { cur.dbm.signal - 20, cur.dbm.signal },
		lvlscale[2] = { -40, -20 },
		snrscale[2] = { 6, 12 };
	char	tmp[0x100];
	static float qual, noise, signal, ssnr;
	/* Spread out 'quality' and 'signal' if 'noise' is undefined */
	const bool offset = (cur.stat.qual.updated & IW_QUAL_NOISE_INVALID) != 0;
	int line = 1;

	if (!(cur.stat.qual.updated & IW_QUAL_QUAL_INVALID)) {
		line += offset;

		mvwaddstr(w_levels, line++, 1, "link quality: ");

		qual = ewma(qual, cur.stat.qual.qual, conf.meter_decay/100.0);
		sprintf(tmp, "%0.f/%d  ", qual, cur.range.max_qual.qual);
		waddstr_b(w_levels, tmp);
		waddbar(w_levels, qual, 0, cur.range.max_qual.qual,
			line++, 1, COLS - 1, lvlscale, true);
	}

	if (!(cur.stat.qual.updated & IW_QUAL_LEVEL_INVALID)) {
		signal = ewma(signal, cur.dbm.signal, conf.meter_decay/100.0);

		line += offset;
		mvwaddstr(w_levels, line++, 1, "signal level: ");

		sprintf(tmp, "%.0f dBm (%s)      ", signal, dbm2units(signal));
		waddstr_b(w_levels, tmp);
		waddbar(w_levels, signal, conf.sig_min,
			conf.sig_max, line, 1, COLS - 1, lvlscale, true);

		if (conf.lthreshold_action)
			waddthreshold(w_levels, signal, conf.lthreshold, conf.sig_min,
				      conf.sig_max, line, 1, COLS - 1, lvlscale, '>');
		if (conf.hthreshold_action)
			waddthreshold(w_levels, signal, conf.hthreshold, conf.sig_min,
				      conf.sig_max, line, 1, COLS - 1, lvlscale, '<');
	}

	if (!offset) {
		noise = ewma(noise, cur.dbm.noise, conf.meter_decay/100.0);

		line += 1;
		mvwaddstr(w_levels, line++, 1, "noise level:  ");

		sprintf(tmp, "%.0f dBm (%s)    ", noise, dbm2units(noise));
		waddstr_b(w_levels, tmp);
		waddbar(w_levels, noise, conf.noise_min, conf.noise_max,
			line++, 1, COLS - 1, nscale, false);
		/*
		 * Since we make sure (in iw_if.c) that invalid noise levels always imply
		 * invalid noise levels, we can display a valid SNR here.
		 */
		mvwaddstr(w_levels, line++, 1, "signal-to-noise ratio: ");

		ssnr = ewma(ssnr, cur.dbm.signal - cur.dbm.noise, conf.meter_decay/100.0);
		if (ssnr > 0)
			waddstr_b(w_levels, "+");
		sprintf(tmp, "%.0f dB   ", ssnr);
		waddstr_b(w_levels, tmp);
		waddbar(w_levels, ssnr, 0, 110, 8, 1, COLS - 1, snrscale, true);
	}
}

static void display_stats(void)
{
	struct if_stat nstat;
	char tmp[0x100];
	int width;

	if_getstat(conf.ifname, &nstat);
	width = num_int_digits(max(nstat.rx_packets, nstat.tx_packets));
	
	/*
	 * Interface RX stats
	 */
	mvwaddstr(w_stats, 1, 1, "RX: ");

	sprintf(tmp, "%*llu (%s)", width, nstat.rx_packets, byte_units(nstat.rx_bytes));
	waddstr_b(w_stats, tmp);

	waddstr(w_stats, ", invalid: ");
	sprintf(tmp, "%u", cur.stat.discard.nwid);
	
	waddstr_b(w_stats, tmp);
	waddstr(w_stats, " nwid, ");

	sprintf(tmp, "%u", cur.stat.discard.code);
	waddstr_b(w_stats, tmp);
	waddstr(w_stats, " crypt, ");

	if (cur.range.we_version_compiled > 11)	{
		sprintf(tmp, "%u", cur.stat.discard.fragment);
		waddstr_b(w_stats, tmp);
		waddstr(w_stats, " frag, ");
	}
	sprintf(tmp, "%u", cur.stat.discard.misc);
	waddstr_b(w_stats, tmp);
	waddstr(w_stats, " misc");

	/*
	 * Interface TX stats
	 */
	mvwaddstr(w_stats, 2, 1, "TX: ");

	sprintf(tmp, "%*llu (%s)", width, nstat.tx_packets, byte_units(nstat.tx_bytes));
	waddstr_b(w_stats, tmp);

	if (cur.range.we_version_compiled > 11)	{
		waddstr(w_stats, ", mac retries: ");
		sprintf(tmp, "%u", cur.stat.discard.retries);
		waddstr_b(w_stats, tmp);

		waddstr(w_stats, ", missed beacons: ");
		sprintf(tmp, "%u", cur.stat.miss.beacon);
		waddstr_b(w_stats, tmp);
	}
}

static void redraw_stats(void)
{
	display_levels();
	display_stats();
	wrefresh(w_levels);
	wrefresh(w_stats);
	wmove(w_menu, 1, 0);
	wrefresh(w_menu);
}

static void display_info(WINDOW *w_if, WINDOW *w_info)
{
	struct iw_dyn_info info;
	char 	tmp[0x100];
	int 	ysize, xsize;
	int 	i;
	
	getmaxyx(w_if, ysize, xsize);
	for (i = 1; i < ysize - 1; i++)
		mvwhline(w_if, i, 1, ' ', xsize - 2);
	getmaxyx(w_info, ysize, xsize);
	for (i = 1; i < ysize - 1; i++)
		mvwhline(w_info, i, 1, ' ', xsize - 2);
	
	iw_getinf_dyn(conf.ifname, &info);

	wmove(w_if, 1, 1);
	sprintf(tmp, "%s (%s)", conf.ifname, info.name);
	waddstr_b(w_if, tmp);
	
	waddstr(w_if, ",  ESSID: ");
	if (info.cap_essid) {
		sprintf(tmp, "\"%s\"", info.essid);
		waddstr_b(w_if, tmp);
	} else waddstr(w_if, "n/a");
	
	if (info.cap_nickname) {
		waddstr(w_if, ",  nick: ");
		sprintf(tmp, "\"%s\"", info.nickname);
		waddstr_b(w_if, tmp);
	}
		
	mvwaddstr(w_info, 1, 1, "frequency: ");
	if (info.cap_freq) {
		sprintf(tmp, "%.4f GHz", info.freq);
		waddstr_b(w_info, tmp);
	} else waddstr(w_if, "n/a");

	waddstr(w_info, ",  sensitivity: ");
	if (info.cap_sens) {
		sprintf(tmp, "%ld/%d", info.sens, cur.range.sensitivity);
		waddstr_b(w_info, tmp);
	} else waddstr(w_info, "n/a");

	waddstr(w_info, ",  TX power: ");
	if (info.cap_txpower) {
		sprintf(tmp, "%d dBm (%.2f mW)", info.txpower_dbm, info.txpower_mw);
		waddstr_b(w_info, tmp);
	} else waddstr(w_info, "n/a");
	
	mvwaddstr(w_info, 2, 1, "mode: ");
	if (info.cap_mode)
		waddstr_b(w_info, iw_opmode(info.mode));
	else waddstr(w_info, "n/a");

	if (info.mode != 1) {
		waddstr(w_info, ",  access point: ");
		if (info.cap_ap)
			waddstr_b(w_info, mac_addr(&info.ap_addr));
		else waddstr(w_info, "n/a");
	}
	
	mvwaddstr(w_info, 3, 1, "bitrate: ");
	if (info.cap_bitrate) {
		sprintf(tmp, "%g Mbit/s", (double)info.bitrate / 1000000);
		waddstr_b(w_info, tmp);
	} else waddstr(w_info, "n/a");
	
	waddstr(w_info, ",  RTS thr: ");
	if (info.cap_rts) {
		if (info.rts_on) sprintf(tmp, "%d bytes", info.rts);
			else sprintf(tmp, "off");
		waddstr_b(w_info, tmp);
	} else waddstr(w_info, "n/a");
	
	waddstr(w_info, ",  frag thr: ");
	if (info.cap_frag) {
		if (info.frag_on) sprintf(tmp, "%d bytes", info.frag);
			else sprintf(tmp, "off");
		waddstr_b(w_info, tmp);
	} else waddstr(w_info, "n/a");

	mvwaddstr(w_info, 4, 1, "encryption: ");
	if (info.cap_encode) {
		if (info.eflags.disabled || info.keysize == 0) {
			waddstr_b(w_info, "off");
		} else {
			for (i = 0; i < info.keysize; i++) {
				sprintf(tmp, "%2X", info.key[i]);
				waddstr_b(w_info, tmp);
			}
			if (info.eflags.index) {
				sprintf(tmp, " [%d]", info.key_index);
				waddstr_b(w_info, tmp);
			}
			if (info.eflags.restricted) waddstr(w_info, ", restricted");
			if (info.eflags.open) waddstr(w_info, ", open");
		}
	} else waddstr(w_info, "n/a");

	mvwaddstr(w_info, 5, 1, "power management:");
	if (info.cap_power) {
		if (info.pflags.disabled) {
			waddstr_b(w_info, " off");
		} else {
			if (info.pflags.min) waddstr_b(w_info, " min"); else waddstr_b(w_info, " max");
			if (info.pflags.timeout) waddstr_b(w_info, " timeout"); else waddstr_b(w_info, " period");
			if (info.pflags.rel) {
				if (info.pmvalue > 0) sprintf(tmp, " +%ld", info.pmvalue);
					else sprintf(tmp, " %ld", info.pmvalue);
				waddstr_b(w_info, tmp);
			} else {
				if (info.pmvalue > 1000000) sprintf(tmp, " %ld s", info.pmvalue / 1000000);
				else if (info.pmvalue > 1000) sprintf(tmp, " %ld ms", info.pmvalue / 1000);
				else sprintf(tmp, " %ld us", info.pmvalue);
				waddstr_b(w_info, tmp);
			}
			if (info.pflags.unicast && info.pflags.multicast)
				waddstr_b(w_info, ", rcv all");
			else if (info.pflags.multicast) waddstr_b(w_info, ", rcv multicast");
			else waddstr_b(w_info, ", rcv unicast");
			if (info.pflags.forceuc) waddstr_b(w_info, ", force unicast");
			if (info.pflags.repbc) waddstr_b(w_info, ", repeat broadcast");
		}
	} else waddstr(w_info, "n/a");

	mvwaddstr(w_info, 6, 1, "wireless extensions: ");
	sprintf(tmp, "%d", cur.range.we_version_compiled);
	waddstr_b(w_info, tmp);
	sprintf(tmp, " (source version %d)", cur.range.we_version_source);
	waddstr(w_info, tmp);
}

static void display_netinfo(WINDOW *w_net)
{
	struct if_info info;
	int	ysize, xsize;
	int	i;
	
	getmaxyx(w_net, ysize, xsize);
	for (i = 1; i < ysize - 1; i++)
		mvwhline(w_net, i, 1, ' ', xsize - 2);

	if_getinf(conf.ifname, &info);

	mvwaddstr(w_net, 1, 1, "if: ");
	waddstr_b(w_net, conf.ifname);

	waddstr(w_net, ",  hwaddr: ");
	waddstr_b(w_net, ether_addr(&info.hwaddr));

	mvwaddstr(w_net, 2, 1, "addr: ");
	waddstr_b(w_net, inet_ntoa(info.addr));

	waddstr(w_net, ",  netmask: ");
	waddstr_b(w_net, inet_ntoa(info.netmask));

	waddstr(w_net, ",  bcast: ");
	waddstr_b(w_net, inet_ntoa(info.bcast));
}

int scr_info(void)
{
	WINDOW *w_if, *w_info, *w_net;
	struct timer	t1;
	int		key = 0;
	
	iw_getinf_range(conf.ifname, &cur.range);

	w_if	 = newwin_title(2, COLS,  0, 0, "Interface", 0, 1);
	w_levels = newwin_title(9, COLS,  2, 0, "Levels", 1, 1);
	w_stats	 = newwin_title(3, COLS, 11, 0, "Statistics", 1, 1);
	w_info	 = newwin_title(7, COLS, 14, 0, "Info", 1, 1);
	w_net	 = newwin_title(4, COLS, 21, 0, "Network", 1, 0);
	w_menu	 = newwin(1, COLS, LINES - 1,  0);
	
	display_info(w_if, w_info);
	wrefresh(w_if);
	wrefresh(w_info);
	display_netinfo(w_net);
	wrefresh(w_net);
	wmenubar(w_menu, 0);
	wrefresh(w_menu);
	
	nodelay(w_menu, TRUE); keypad(w_menu, TRUE);
	
	iw_stat_redraw = redraw_stats;


	while (key < KEY_F(1) || key > KEY_F(10)) {
		display_info(w_if, w_info);
		display_netinfo(w_net);
		wrefresh(w_if);
		wrefresh(w_info);
		wrefresh(w_net);
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
	
	werase(w_if); wrefresh(w_if); delwin(w_if);
	werase(w_levels); wrefresh(w_levels); delwin(w_levels);
	werase(w_stats); wrefresh(w_stats); delwin(w_stats);
	werase(w_info); wrefresh(w_info); delwin(w_info);
	werase(w_net); wrefresh(w_net); delwin(w_net);
	werase(w_menu); wrefresh(w_menu); delwin(w_menu);
	
	return key - KEY_F(1);
}
