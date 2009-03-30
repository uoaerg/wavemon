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

struct iw_range range;
void (*iw_stat_redraw)(void);

/*
 * Statistics handler for period polling
 */
static void sampling_handler(int signum)
{
	iw_getstat(&iw_stats, iw_stats_cache);
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
	char   nscale[2],
	     lvlscale[2],
	     snrscale[2] = { 6, 12 };
	char tmp[0x100];
	int snr;
	
	if (conf.linear) {
		lvlscale[0] = 10;
		lvlscale[1] = 50;
	} else {
		lvlscale[0] = -40;
		lvlscale[1] = -20;
	}

	wmove(w_levels, 1, 1);
	waddstr(w_levels, "link quality: ");
	sprintf(tmp, "%d/%d  ", iw_stats.link, range.max_qual.qual);
	waddstr_b(w_levels, tmp);
	waddbar(w_levels, iw_stats.link, 0, range.max_qual.qual, 2, 1, COLS - 1, lvlscale, true);

	wmove(w_levels, 3, 1);
	waddstr(w_levels, "signal level: ");
	sprintf(tmp, "%d dBm (%s)", iw_stats.signal, dbm2units(iw_stats.signal));
	waddstr_b(w_levels, tmp);

	if (conf.linear) {
		waddbar(w_levels, dbm2mw(iw_stats.signal), dbm2mw(conf.sig_min),
			dbm2mw(conf.sig_max), 4, 1, COLS - 1, lvlscale, true);
		if (conf.lthreshold_action)
			waddthreshold(w_levels, dbm2mw(iw_stats.signal), dbm2mw(conf.lthreshold),
				dbm2mw(conf.sig_min), dbm2mw(conf.sig_max), 4, 1, COLS - 1, lvlscale, '>');
		if (conf.hthreshold_action)
			waddthreshold(w_levels, dbm2mw(iw_stats.signal), dbm2mw(conf.hthreshold),
				dbm2mw(conf.sig_min), dbm2mw(conf.sig_max), 4, 1, COLS - 1, lvlscale, '<');
	} else {
		waddbar(w_levels, iw_stats.signal, conf.sig_min, conf.sig_max, 4, 1, COLS - 1, lvlscale, true);
		if (conf.lthreshold_action)
			waddthreshold(w_levels, iw_stats.signal, conf.lthreshold, conf.sig_min, conf.sig_max, 4, 1,
				COLS - 1, lvlscale, '>');
		if (conf.hthreshold_action)
			waddthreshold(w_levels, iw_stats.signal, conf.hthreshold, conf.sig_min, conf.sig_max, 4, 1,
				COLS - 1, lvlscale, '<');
	}


	if (conf.linear) {
		nscale[0] = dbm2mw(iw_stats.signal) - 10;
		nscale[1] = dbm2mw(iw_stats.signal);
	} else {
		nscale[0] = iw_stats.signal - 20;
		nscale[1] = iw_stats.signal;
	}
	wmove(w_levels, 5, 1);
	waddstr(w_levels, "noise level: ");
	sprintf(tmp, "%4d dBm (%s)", iw_stats.noise, dbm2units(iw_stats.noise));
	waddstr_b(w_levels, tmp);
	if (conf.linear)
		waddbar(w_levels, dbm2mw(iw_stats.noise), dbm2mw(conf.noise_min),
			dbm2mw(conf.noise_max), 6, 1, COLS - 1, nscale, false);
	else
		waddbar(w_levels, iw_stats.noise, conf.noise_min, conf.noise_max, 6, 1, COLS - 1, nscale, false);
	
	snr = iw_stats.signal - iw_stats.noise;
	wmove(w_levels, 7, 1);
	waddstr(w_levels, "signal-to-noise ratio: ");
	if (snr > 0)
		waddstr_b(w_levels, "+");
	sprintf(tmp, "%d dB   ", snr);	
	waddstr_b(w_levels, tmp);
	waddbar(w_levels, snr, 0, 110, 8, 1, COLS - 1, snrscale, true);
	
}

static void display_stats(void)
{
	struct if_stat nstat;
	char tmp[0x100];

	if_getstat(conf.ifname, &nstat);
	wmove(w_stats, 1, 1);
	
	waddstr(w_stats, "RX: ");
	sprintf(tmp, "%llu (%s)", nstat.rx_packets, byte_units(nstat.rx_bytes));
	waddstr_b(w_stats, tmp);
	
	waddstr(w_stats, ",  TX: ");
	sprintf(tmp, "%llu (%s)", nstat.tx_packets, byte_units(nstat.tx_bytes));
	waddstr_b(w_stats, tmp);
	
	waddstr(w_stats, ",  inv: ");
	sprintf(tmp, "%lu", iw_stats.dsc_nwid);
	waddstr_b(w_stats, tmp);
	waddstr(w_stats, " nwid, ");
	sprintf(tmp, "%lu", iw_stats.dsc_enc);
	waddstr_b(w_stats, tmp);
	waddstr(w_stats, " key, ");
	sprintf(tmp, "%lu", iw_stats.dsc_misc);
	waddstr_b(w_stats, tmp);
	waddstr(w_stats, " misc");
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
	
	waddstr(w_if, ",  nick: ");
	if (info.cap_nickname) {
		sprintf(tmp, "\"%s\"", info.nickname);
		waddstr_b(w_if, tmp);
	} else waddstr(w_if, "n/a");
		
	wmove(w_info, 1, 1);
	waddstr(w_info, "frequency: ");
	if (info.cap_freq) {
		sprintf(tmp, "%.4f GHz", info.freq);
		waddstr_b(w_info, tmp);
	} else waddstr(w_if, "n/a");

	waddstr(w_info, ",  sensitivity: ");
	if (info.cap_sens) {
		sprintf(tmp, "%ld/%d", info.sens, range.sensitivity);
		waddstr_b(w_info, tmp);
	} else waddstr(w_info, "n/a");

	waddstr(w_info, ",  TX power: ");
	if (info.cap_txpower) {
		sprintf(tmp, "%d dBm (%.2f mW)", info.txpower_dbm, info.txpower_mw);
		waddstr_b(w_info, tmp);
	} else waddstr(w_info, "n/a");
	
	wmove(w_info, 2, 1);
	waddstr(w_info, "mode: ");
	if (info.cap_mode)
		waddstr_b(w_info, iw_opmode(info.mode));
	else waddstr(w_info, "n/a");

	if (info.mode != 1) {
		waddstr(w_info, ",  access point: ");
		if (info.cap_ap)
			waddstr_b(w_info, mac_addr((unsigned char *)info.ap_addr.sa_data));
		else waddstr(w_info, "n/a");
	}
	
	wmove(w_info, 3, 1);
	waddstr(w_info, "bitrate: ");
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

	wmove(w_info, 4, 1);
	waddstr(w_info, "encryption: ");
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

	wmove(w_info, 5, 1);
	waddstr(w_info, "power management:");
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
	waddstr_b(w_net, mac_addr(info.hwaddr));

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
	
	w_if = newwin_title(2, COLS, 0, 0, "Interface", 0, 1);
	w_levels = newwin_title(9, COLS, 2, 0, "Levels", 1, 1);
	w_stats = newwin_title(2, COLS, 11, 0, "Statistics", 1, 1);
	w_info = newwin_title(6, COLS, 13, 0, "Info", 1, 1);
	w_net = newwin_title(4, COLS, 19, 0, "Network", 1, 0);
	w_menu = newwin(1, COLS, LINES - 1, 0);
	
	display_info(w_if, w_info);
	wrefresh(w_if);
	wrefresh(w_info);
	display_netinfo(w_net);
	wrefresh(w_net);
	wmenubar(w_menu, 0);
	wrefresh(w_menu);
	
	nodelay(w_menu, TRUE); keypad(w_menu, TRUE);
	
	iw_stat_redraw = redraw_stats;

	iw_getinf_range(conf.ifname, &range);

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
