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

#include <sys/time.h>
#include <ncurses.h>
#include <stdlib.h>

#include "conf.h"
#include "ui.h"
#include "info_scr.h"
#include "lhist_scr.h"
#include "aplst_scr.h"
#include "conf_scr.h"
#include "help_scr.h"
#include "about_scr.h"
#include "iw_if.h"
#include "net_if.h"
#include "defs.h"

void reinit_on_changes(struct wavemon_conf *conf)
{
	static int stat_iv = 0;
	
	if (conf->stat_iv != stat_iv) {
		init_stat_iv(conf);
		stat_iv = conf->stat_iv;
	}
}

void dump_parameters(struct wavemon_conf *conf)
{
  char *opmodes[] = { "auto", "ad-hoc", "managed", "master", "repeater", "secondary" };

  struct iw_dyn_info info;
  struct iw_stat stat;
  struct iw_range range;
  struct if_stat nstat;
  int i;

  iw_getinf_dyn(conf->ifname, &info);
  iw_getinf_range(conf->ifname, &range);
  iw_getstat(conf->ifname, &stat, NULL, 2, 0);
  if_getstat(conf->ifname, &nstat);

  printf("\n           device: %s\n\n", conf->ifname);
  printf("            ESSID: %s", (info.cap_essid ? info.essid : "n/a"));
  if (!info.essid_on) printf(" (off)"); else printf("\n");
  printf("             nick: %s\n", (info.cap_nickname ? info.nickname : "n/a"));

  if (info.cap_freq)
	printf("        frequency: %.4f GHz\n", info.freq);
  else
	printf("        frequency: n/a\n");

  if (info.cap_sens)
	printf("      sensitivity: %ld/%d\n", info.sens, range.sensitivity);
  else
	printf("      sensitivity: n/a\n");

  if (info.cap_txpower)
	printf("         TX power: %d dBm (%.2f mW)\n", info.txpower_dbm, info.txpower_mw);
  else
	printf("         TX power: n/a\n");
  
  printf("             mode: %s\n", opmodes[info.mode]);

  if (info.mode != 1 && info.cap_ap) {
	printf("     access point: ");
	if (info.cap_ap)
	  printf("%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX\n", 
			  info.ap_addr.sa_data[0] & 0xFF,
			  info.ap_addr.sa_data[1] & 0xFF,
			  info.ap_addr.sa_data[2] & 0xFF,
			  info.ap_addr.sa_data[3] & 0xFF, 
			  info.ap_addr.sa_data[4] & 0xFF,
			  info.ap_addr.sa_data[5] & 0xFF);
	else printf("n/a\n");
  }

  if (info.cap_bitrate)
	printf("          bitrate: %g Mbit/s\n", (double)info.bitrate / 1000000);
  else
	printf("          bitrate: n/a\n");

  printf("          RTS thr: ");
  if (info.cap_rts && (!info.rts_on || info.rts)) {
	if (info.rts_on) printf("%d bytes\n", info.rts);
	else printf("off\n");
  } else printf("n/a\n");

  printf("         frag thr: ");
  if (info.cap_frag && (!info.frag_on || info.frag)) {
	if (info.frag_on) printf("%d bytes\n", info.frag);
	else printf("off\n");
  } else printf("n/a\n");

  printf("       encryption: ");
  if (info.cap_encode) {
	if (info.eflags.disabled || info.keysize == 0) {
	  printf("off");
	} else {
	  for (i = 0; i < info.keysize; i++) printf("%2X", info.key[i]);
	  if (info.eflags.index) printf(" [%d]", info.key_index);
	  if (info.eflags.restricted) printf(", restricted");
	  if (info.eflags.open) printf(", open");
	}
  } else printf("n/a");
  printf("\n");
  
  printf(" power management:");
  if (info.cap_power) {
	if (info.pflags.disabled) {
	  printf(" off");
	} else {
	  if (info.pflags.min) printf(" min"); else printf(" max");
	  if (info.pflags.timeout) printf(" timeout"); else printf(" period");
	  if (info.pflags.rel) {
		if (info.pmvalue > 0) printf(" +%ld", info.pmvalue);
		else printf(" %ld", info.pmvalue);
	  } else {
		if (info.pmvalue > 1000000) printf(" %ld s", info.pmvalue / 1000000);
		else if (info.pmvalue > 1000) printf(" %ld ms", info.pmvalue / 1000);
		else printf(" %ld us", info.pmvalue);
	  }
	  if (info.pflags.unicast && info.pflags.multicast)
		printf(", rcv all");
	  else if (info.pflags.multicast) printf(", rcv multicast");
	  else printf(", rcv unicast");
	  if (info.pflags.forceuc) printf(", force unicast");
	  if (info.pflags.repbc) printf(", repeat broadcast");
	}
  } else printf("n/a");
  printf("\n\n");
  
  printf("     link quality: %d/%d\n", stat.link, range.max_qual.qual);
  printf("     signal level: %d dBm (%.2f uW)\n", stat.signal, dbm2mw(stat.signal) * 1000);
  printf("      noise level: %d dBm (%.2f uW)\n", stat.noise, dbm2mw(stat.noise) * 1000);
  printf("              SNR: %d dB\n", stat.signal - stat.noise);
  printf("         total TX: %llu packets (%llu bytes)\n", nstat.tx_packets, nstat.tx_bytes);
  printf("         total RX: %llu packets (%llu bytes)\n", nstat.rx_packets, nstat.rx_bytes);
  printf("     invalid NWID: %lu packets\n", stat.dsc_nwid);
  printf("      invalid key: %lu packets\n", stat.dsc_enc);
  printf("      misc errors: %lu packets\n", stat.dsc_misc);

  printf("\n");
}

int main(int argc, char *argv[]) {
	struct wavemon_conf conf;
	int		(*current_scr)(struct wavemon_conf *conf) = NULL;
	int 	nextscr;

	getconf(&conf, argc, argv);

	if (conf.dump == 1) {
	  dump_parameters(&conf);
	  exit(0);
	}

	/* initialize the ncurses interface */
	initscr(); cbreak(); noecho();
	nonl(); clear();

	start_color();
	init_pair(CP_STANDARD, COLOR_WHITE, COLOR_BLACK);
	init_pair(CP_SCALEHI, COLOR_RED, COLOR_BLACK);
	init_pair(CP_SCALEMID, COLOR_YELLOW, COLOR_BLACK);
	init_pair(CP_SCALELOW, COLOR_GREEN, COLOR_BLACK);
	init_pair(CP_WTITLE, COLOR_CYAN, COLOR_BLACK);
	init_pair(CP_INACTIVE, COLOR_CYAN, COLOR_BLACK);
	init_pair(CP_ACTIVE, COLOR_CYAN, COLOR_BLUE);
	init_pair(CP_STATSIG, COLOR_GREEN, COLOR_BLACK);
	init_pair(CP_STATNOISE, COLOR_RED, COLOR_BLACK);
	init_pair(CP_STATSNR, COLOR_BLUE, COLOR_BLUE);
	init_pair(CP_STATBKG, COLOR_BLUE, COLOR_BLACK);
	init_pair(CP_STATSIG_S, COLOR_GREEN, COLOR_BLUE);
	init_pair(CP_STATNOISE_S, COLOR_RED, COLOR_BLUE);
	init_pair(CP_PREF_NORMAL, COLOR_WHITE, COLOR_BLACK);
	init_pair(CP_PREF_SELECT, COLOR_WHITE, COLOR_BLUE);
	init_pair(CP_PREF_ARROW, COLOR_RED, COLOR_BLACK);

	switch (conf.startup_scr) {
		case 0:	current_scr = scr_info;
			break;
		case 1:	current_scr = scr_lhist;
			break;
		case 2:	current_scr = scr_aplst;
			break;
	}

	do {
		reinit_on_changes(&conf);
		switch (nextscr = current_scr(&conf)) {
			case 0:	current_scr = scr_info;
					break;
			case 1: current_scr = scr_lhist;
					break;
			case 2: current_scr = scr_aplst;
					break;
			case 6: current_scr = scr_conf;
					break;
			case 7: current_scr = scr_help;
					break;
			case 8: current_scr = scr_about;
					break;
		}
	} while (nextscr != 9);

	endwin();
	dealloc_on_exit();
	
	return 0;
}
