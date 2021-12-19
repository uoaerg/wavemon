/*
 * wavemon - a wireless network monitoring application
 *
 * Copyright (c) 2001-2002 Jan Morgenstern <jan@jm-music.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "iw_if.h"
#include "iw_nl80211.h"

/* GLOBALS */
static WINDOW *w_levels, *w_stats, *w_if, *w_info, *w_net;
static pthread_t sampling_thread;
static time_t last_update;

/* Linkstat pointers, shared between sampling thread and UI main thread. */
static struct iw_nl80211_linkstat *ls_tmp = NULL,
				  *ls_cur = NULL,
				  *ls_new = NULL;
static pthread_mutex_t linkstat_mutex;

/** Sampling pthread - shared by info and histogram screen. */
static void *sampling_loop(void *arg)
{
	const bool do_not_swap_pointers = (bool)arg;
	sigset_t blockmask;

	/* See comment in iw_scan.c for rationale of blocking SIGWINCH. */
	sigemptyset(&blockmask);
	sigaddset(&blockmask, SIGWINCH);
	pthread_sigmask(SIG_BLOCK, &blockmask, NULL);

	do {
		if (!ls_new && ls_tmp) {
			iw_nl80211_get_linkstat(ls_tmp);
			iw_cache_update(ls_tmp);

			if (do_not_swap_pointers)
				continue;
			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
			pthread_mutex_lock(&linkstat_mutex);
			ls_new = ls_tmp;
			ls_tmp = NULL;
			pthread_mutex_unlock(&linkstat_mutex);
			pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		}
	} while (usleep(conf.stat_iv * 1000) == 0);
	return NULL;
}

/* Start thread. Ensure that !ls_new && ls_tmp. */
void sampling_init(bool do_not_swap_pointers)
{
	if (!ls_tmp && !ls_cur) {
		ls_tmp = calloc(1, sizeof(*ls_tmp));
		ls_cur = calloc(1, sizeof(*ls_cur));
		if (!ls_tmp || !ls_cur)
			err_sys("Out of memory");
	} else if (ls_new && !ls_tmp) { /* Old state from previous run. */
		ls_tmp = ls_cur;
		ls_cur = ls_new;
		ls_new = NULL;
	}
	pthread_create(&sampling_thread, NULL, sampling_loop, (void*)do_not_swap_pointers);
}

void sampling_stop(void)
{
	pthread_cancel(sampling_thread);
	pthread_join(sampling_thread, NULL);
}

static void display_levels(void)
{
	static float qual, signal, noise, ssnr;
	/*
	 * FIXME: revise the scale implementation. It does not work
	 *        satisfactorily, maybe it is better to have a simple
	 *        solution using 3 levels of different colour.
	 */
	int8_t nscale[2] = { conf.noise_min, conf.noise_max },
	     lvlscale[2] = { -40, -20};
	char tmp[0x100];
	int line;
	bool noise_data_valid;
	int sig_qual = -1, sig_qual_max = 0, sig_level = 0;

	noise_data_valid = iw_nl80211_have_survey_data(ls_cur);
	sig_level = ls_cur->signal;

	/* See comments in iw_cache_update */
	if (sig_level == 0)
		sig_level = ls_cur->signal_avg;
	if (sig_level == 0)
		sig_level = ls_cur->bss_signal;

	/* If the signal level is positive, assume it is an absolute value (#100). */
	if (sig_level > 0)
		sig_level *= -1;

	for (line = 1; line <= WH_LEVEL; line++)
		mvwclrtoborder(w_levels, line, 1);

	if (ls_cur->bss_signal_qual) {
		/* BSS_SIGNAL_UNSPEC is scaled 0..100 */
		sig_qual     = ls_cur->bss_signal_qual;
		sig_qual_max = 100;
	} else if (sig_level) {
		if (sig_level < -110)
			sig_qual = 0;
		else if (sig_level > -40)
			sig_qual = 70;
		else
			sig_qual = sig_level + 110;
		sig_qual_max = 70;
	}

	line = 1;

	/* Noise data is rare. Use the space for spreading out. */
	if (!noise_data_valid)
		line++;

	if (sig_qual == -1) {
		line++;
	} else {
		qual = ewma(qual, sig_qual, conf.meter_decay / 100.0);

		mvwclrtoborder(w_levels, line, 1);
		mvwaddstr(w_levels, line++, 1, "link quality: ");
		sprintf(tmp, "%0.f%%  ", (1e2 * qual)/sig_qual_max);
		waddstr_b(w_levels, tmp);
		sprintf(tmp, "(%0.f/%d)  ", qual, sig_qual_max);
		waddstr(w_levels, tmp);

		waddbar(w_levels, line++, qual, 0, sig_qual_max, lvlscale, true);
	}

	/* Spacer */
	line++;
	if (!noise_data_valid)
		line++;

	if (sig_level != 0) {
		signal = ewma(signal, sig_level, conf.meter_decay / 100.0);

		mvwclrtoborder(w_levels, line, 1);
		mvwaddstr(w_levels, line++, 1, "signal level: ");
		sprintf(tmp, "%.0f dBm (%s)", signal, dbm2units(signal));
		waddstr_b(w_levels, tmp);

		waddbar(w_levels, line, signal, conf.sig_min, conf.sig_max,
			lvlscale, true);
		if (conf.lthreshold_action)
			waddthreshold(w_levels, line, signal, conf.lthreshold,
				      conf.sig_min, conf.sig_max, lvlscale, '>');
		if (conf.hthreshold_action)
			waddthreshold(w_levels, line, signal, conf.hthreshold,
				      conf.sig_min, conf.sig_max, lvlscale, '<');
	}

	line++;

	if (noise_data_valid) {
		noise = ewma(noise, ls_cur->survey.noise, conf.meter_decay / 100.0);

		mvwaddstr(w_levels, line++, 1, "noise level:  ");
		sprintf(tmp, "%.0f dBm (%s)", noise, dbm2units(noise));
		waddstr_b(w_levels, tmp);

		waddbar(w_levels, line++, noise, conf.noise_min, conf.noise_max,
			nscale, false);

		if (sig_level) {
			ssnr = ewma(ssnr, sig_level - ls_cur->survey.noise,
					  conf.meter_decay / 100.0);

			mvwaddstr(w_levels, line++, 1, "SNR:           ");
			sprintf(tmp, "%.0f dB", ssnr);
			waddstr_b(w_levels, tmp);
		}
	} else {
		// Force redraw on next line (needed on some terminals).
		mvwaddstr(w_levels, line++, 1, "");
	}
	wrefresh(w_levels);
}

static void display_packet_counts(void)
{
	char tmp[0x120];

	/*
	 * Interface RX stats
	 */
	mvwaddstr(w_stats, 1, 1, "RX: ");

	if (ls_cur->rx_packets) {
		sprintf(tmp, "%s (%s)", int_counts(ls_cur->rx_packets),
			byte_units(ls_cur->rx_bytes));
		waddstr_b(w_stats, tmp);
	} else {
		waddstr(w_stats, "n/a");
	}

	if (iw_nl80211_have_survey_data(ls_cur)) {
		if (ls_cur->rx_bitrate[0]) {
			waddstr(w_stats, ", rate: ");
			waddstr_b(w_stats, ls_cur->rx_bitrate);
		}

		if (ls_cur->expected_thru) {
			if (ls_cur->expected_thru >= 1024)
				sprintf(tmp, " (expected: %.1f MB/s)",  ls_cur->expected_thru/1024.0);
			else
				sprintf(tmp, " (expected: %u kB/s)",  ls_cur->expected_thru);
			waddstr(w_stats, tmp);
		}
	}

	if (ls_cur->rx_drop_misc) {
		waddstr(w_stats, ", drop: ");
		sprintf(tmp, "%llu (%.2g%%)", (unsigned long long)ls_cur->rx_drop_misc,
				(1e2 * ls_cur->rx_drop_misc)/ls_cur->rx_packets);
		waddstr_b(w_stats, tmp);
	}

	wclrtoborder(w_stats);

	/*
	 * Interface TX stats
	 */
	mvwaddstr(w_stats, 2, 1, "TX: ");

	if (ls_cur->tx_packets) {
		sprintf(tmp, "%s (%s)", int_counts(ls_cur->tx_packets),
			byte_units(ls_cur->tx_bytes));
		waddstr_b(w_stats, tmp);
	} else {
		waddstr(w_stats, "n/a");
	}

	if (iw_nl80211_have_survey_data(ls_cur) && ls_cur->tx_bitrate[0]) {
		waddstr(w_stats, ", rate: ");
		waddstr_b(w_stats, ls_cur->tx_bitrate);
	}

	if (ls_cur->tx_retries) {
		waddstr(w_stats, ", retries: ");
		sprintf(tmp, "%s (%.2g%%)", int_counts(ls_cur->tx_retries),
			(1e2 * ls_cur->tx_retries)/ls_cur->tx_packets);
		waddstr_b(w_stats, tmp);
	}

	if (ls_cur->tx_failed) {
		waddstr(w_stats, ", failed: ");
		waddstr_b(w_stats, int_counts(ls_cur->tx_failed));
	}
	wclrtoborder(w_stats);
	wrefresh(w_stats);
}

/** Wireless interface information */
static void display_interface(WINDOW *w_if, struct iw_nl80211_ifstat *ifs, bool if_is_up)
{
	struct iw_nl80211_reg ir;
	char tmp[0x100];

	iw_nl80211_getreg(&ir);

	wmove(w_if, 1, 1);
	waddstr_b(w_if, conf_ifname());
	waddstr(w_if, " - ");

	if (if_is_up) {
		/* Wireless device index */
		waddstr(w_if, "wdev ");
		sprintf(tmp, "%d", ifs->wdev);
		waddstr_b(w_if, tmp);

		/* PHY */
		waddstr(w_if, ", phy ");
		sprintf(tmp, "%d", ifs->phy_id);
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

		if (ifs->ssid[0]) {
			waddstr(w_if, ", SSID: ");
			waddstr_b(w_if, ifs->ssid);
		}
	} else {
		rfkill_state_t rfkill_state = get_rfkill_state(ifs->wdev);

		if (is_rfkill_blocked_state(rfkill_state)) {
			sprintf(tmp, "Interface is blocked by %s", rfkill_state_name(rfkill_state));
		} else {
			sprintf(tmp, "Interface is DOWN");
		}
		wadd_attr_str(w_if, COLOR_PAIR(CP_RED) | A_REVERSE, tmp);
	}

	wclrtoborder(w_if);
	wrefresh(w_if);
}

/** General information section */
static void display_info(WINDOW *w_info, struct iw_nl80211_ifstat *ifs)
{
	char tmp[0x100];

	iw_nl80211_get_power_save(ifs);
	iw_nl80211_get_phy(ifs);

	wmove(w_info, 1, 1);
	waddstr(w_info, "mode: ");
	waddstr_b(w_info, iftype_name(ifs->iftype));

	if (!ether_addr_is_zero(&ls_cur->bssid)) {
		waddstr_b(w_info, ", ");

		switch (ls_cur->status) {
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
		waddstr_b(w_info, ether_lookup(&ls_cur->bssid));

		if (ls_cur->status == NL80211_BSS_STATUS_ASSOCIATED) {
			waddstr_b(w_info, ",");
			waddstr(w_info, " time: ");
			waddstr_b(w_info, pretty_time(ls_cur->connected_time));

			waddstr(w_info, ", inactive: ");
			if  (ls_cur->inactive_time > 100)
				sprintf(tmp, "%.1fs", (float)ls_cur->inactive_time/1e3);
			else
				sprintf(tmp, "%dms", ls_cur->inactive_time);
			waddstr_b(w_info, tmp);
		}
	}
	wclrtoborder(w_info);

	/* Frequency / channel */
	wmove(w_info, 2, 1);
	if (ifs->freq) {
		waddstr(w_info, "freq: ");
		sprintf(tmp, "%d MHz", ifs->freq);
		waddstr_b(w_info, tmp);

		/* The following condition should in theory never happen: */
		if (ls_cur->survey.freq && ls_cur->survey.freq != ifs->freq) {
			sprintf(tmp, " [survey freq: %d MHz]", ls_cur->survey.freq);
			waddstr(w_info, tmp);
		}

		if (ifs->freq_ctr1 && ifs->freq_ctr1 != ifs->freq) {
			waddstr(w_info, ", ctr1: ");
			sprintf(tmp, "%d MHz", ifs->freq_ctr1);
			waddstr_b(w_info, tmp);
		}
		if (ifs->freq_ctr2 &&
		    ifs->freq_ctr2 != ifs->freq_ctr1 &&
		    ifs->freq_ctr2 != ifs->freq) {
			waddstr(w_info, ", ctr2: ");
			sprintf(tmp, "%d MHz", ifs->freq_ctr2);
			waddstr_b(w_info, tmp);
		}

		waddstr(w_info, ", channel: ");
		sprintf(tmp, "%d", ieee80211_frequency_to_channel(ifs->freq));
		waddstr_b(w_info, tmp);

		if (ifs->chan_width != (uint32_t)-1) {
			sprintf(tmp, " (width: %s)", channel_width_name(ifs->chan_width));
			waddstr(w_info, tmp);
		} else if (ifs->chan_type != (uint32_t)-1) {
			sprintf(tmp, " (%s)", channel_type_name(ifs->chan_type));
			waddstr(w_info, tmp);
		}
	} else if (iw_nl80211_have_survey_data(ls_cur)) {
		waddstr(w_info, "freq: ");
		sprintf(tmp, "%d MHz", ls_cur->survey.freq);
		waddstr_b(w_info, tmp);
	} else {
		waddstr(w_info, "frequency/channel: n/a");
	}

	waddstr(w_info, ", bands: ");
	sprintf(tmp, "%u", ifs->phy.bands);
	waddstr_b(w_info, tmp);

	wclrtoborder(w_info);

	/* Beacons */
	wmove(w_info, 3, 1);
	if (ls_cur->beacons) {
		waddstr(w_info, "beacons: ");
		sprintf(tmp, "%llu", (unsigned long long)ls_cur->beacons);
		waddstr_b(w_info, tmp);

		if (ls_cur->beacon_loss) {
			waddstr(w_info, ", lost: ");
			waddstr_b(w_info, int_counts(ls_cur->beacon_loss));
		}
		waddstr(w_info, ", avg sig: ");
		sprintf(tmp, "%d dBm", (int8_t)ls_cur->beacon_avg_sig);
		waddstr_b(w_info, tmp);

		waddstr(w_info, ", interval: ");
		sprintf(tmp, "%.1fs", (ls_cur->beacon_int * 1024.0)/1e6);
		waddstr_b(w_info, tmp);

		waddstr(w_info, ", DTIM: ");
		sprintf(tmp, "%u", ls_cur->dtim_period);
		waddstr_b(w_info, tmp);
	} else {
		waddstr(w_info, "station flags:");
		if (ls_cur->cts_protection)
			waddstr_b(w_info, " CTS");
		if (ls_cur->wme)
			waddstr_b(w_info, " WME");
		if (ls_cur->tdls)
			waddstr_b(w_info, " TDLS");
		if (ls_cur->mfp)
			waddstr_b(w_info, " MFP");
		if (!(ls_cur->cts_protection | ls_cur->wme | ls_cur->tdls | ls_cur->mfp))
			waddstr_b(w_info, " (none)");
		waddstr(w_info, ", preamble:");
		if (ls_cur->long_preamble)
			waddstr_b(w_info, " long");
		else
			waddstr_b(w_info, " short");
		waddstr(w_info, ", slot:");
		if (ls_cur->short_slot_time)
			waddstr_b(w_info, " short");
		else
			waddstr_b(w_info, " long");
	}
	wclrtoborder(w_info);

	/* Channel data */
	wmove(w_info, 4, 1);
	if (iw_nl80211_have_survey_data(ls_cur)) {
		waddstr(w_info, "channel ");
		waddstr(w_info, "active: ");
		waddstr_b(w_info, pretty_time_ms(ls_cur->survey.time.active));

		waddstr(w_info, ", busy: ");
		waddstr_b(w_info, pretty_time_ms(ls_cur->survey.time.busy));

		if (ls_cur->survey.time.ext_busy) {
			waddstr(w_info, ", ext-busy: ");
			waddstr_b(w_info, pretty_time_ms(ls_cur->survey.time.ext_busy));
		}

		wmove(w_info, 5, 1);
		waddstr(w_info, "channel rx: ");
		waddstr_b(w_info, pretty_time_ms(ls_cur->survey.time.rx));

		waddstr(w_info, ", tx: ");
		waddstr_b(w_info, pretty_time_ms(ls_cur->survey.time.tx));

		if (ls_cur->survey.time.scan) {
			waddstr(w_info, ", scan: ");
			waddstr_b(w_info, pretty_time_ms(ls_cur->survey.time.scan));
		}
	} else {
		wclrtoborder(w_info);
		waddstr(w_info, "rx rate: ");
		waddstr_b(w_info, ls_cur->rx_bitrate[0] ? ls_cur->rx_bitrate : "n/a");

		if (ls_cur->expected_thru) {
			if (ls_cur->expected_thru >= 1024)
				sprintf(tmp, " (exp: %.1f MB/s)",  ls_cur->expected_thru/1024.0);
			else
				sprintf(tmp, " (exp: %u kB/s)",  ls_cur->expected_thru);
			waddstr(w_info, tmp);
		}
		wmove(w_info, 5, 1);
		waddstr(w_info, "tx rate: ");
		waddstr_b(w_info, ls_cur->tx_bitrate[0] ? ls_cur->tx_bitrate : "n/a");
	}

	wclrtoborder(w_info);

	/* TX Power */
	wmove(w_info, 6, 1);
	waddstr(w_info, "tx power: ");
	sprintf(tmp, "%g dBm (%.2f mW)", ifs->tx_power, dbm2mw(ifs->tx_power));
	waddstr_b(w_info, tmp);

	/* Power-saving mode */
	waddstr(w_info, ", power save: ");
	sprintf(tmp, "%s", ifs->power_save ? "on" : "off");
	waddstr_b(w_info, tmp);

	wclrtoborder(w_info);

	/* Retry handling */
	wmove(w_info, 7, 1);
	waddstr(w_info, "retry short/long: ");
	sprintf(tmp, "%u", ifs->phy.retry_short);
	waddstr_b(w_info, tmp);

	waddstr(w_info, "/");

	sprintf(tmp, "%u", ifs->phy.retry_long);
	waddstr_b(w_info, tmp);

	/* RTS/CTS handshake threshold */
	waddstr(w_info, ", rts/cts: ");
	if (ifs->phy.rts_threshold != (uint32_t)-1) {
		sprintf(tmp, "%u", ifs->phy.rts_threshold);
	} else {
		sprintf(tmp, "off");
	}
	waddstr_b(w_info, tmp);

	/* Fragmentation threshold */
	waddstr(w_info, ", frag: ");
	if (ifs->phy.frag_threshold != (uint32_t)-1) {
		sprintf(tmp, "%u", ifs->phy.frag_threshold);
	} else {
		sprintf(tmp, "off");
	}
	waddstr_b(w_info, tmp);

	wclrtoborder(w_info);
	wrefresh(w_info);
}

/** Network information pertaining to interface with interface index @ifindex. */
static void display_netinfo(WINDOW *w_net, struct if_info *info)
{
	struct if_info *active = info->master ? info->master : info;
	char tmp[0x40];

	wmove(w_net, 1, 1);
	wclrtoborder(w_net);

	if (!ifinfo_is_up(info)) {
		waddstr(w_net, info->ifname);

		sprintf(tmp, " #%u ", info->ifindex);
		waddstr(w_net, tmp);

		waddstr_b(w_net, "(");

		wadd_attr_str(w_net, COLOR_PAIR(CP_RED) | A_REVERSE,
			!(info->flags & IFF_UP) ? "DOWN" : "no carrier");

	} else if (info->master) {
		waddstr_b(w_net, info->ifname);

		sprintf(tmp, " #%u, %sslave of ", info->ifindex,
			is_primary_slave(info->master->ifname, info->ifname)
			? "primary " : "");
		waddstr(w_net, tmp);

		waddstr_b(w_net, info->master->ifname);
		sprintf(tmp, " #%u ", info->master->ifindex);
		waddstr(w_net, tmp);

		waddstr_b(w_net, "(");

		if (!(active->flags & IFF_UP)) {
			waddstr(w_net, "DOWN");
		} else {
			const char *mode = NULL;

			if (strcmp(info->master->type, "bond") == 0)
				mode = get_bonding_mode(info->master->ifname);

			if (mode) {
				sprintf(tmp, "%s mode", mode);
				waddstr(w_net, tmp);
			} else if (*info->master->type) {
				sprintf(tmp, "type %s", info->master->type);
				waddstr(w_net, tmp);
			} else {
				waddstr(w_net, "UP");
				if (active->flags & IFF_RUNNING)
					waddstr(w_net, " RUNNING");
				if (active->flags & IFF_MASTER)
					waddstr(w_net, " MASTER");
			}
		}
	} else {
		waddstr(w_net, info->ifname);
		sprintf(tmp, " #%u ", info->ifindex);
		waddstr(w_net, tmp);
		waddstr_b(w_net, "(");

		waddstr(w_net, "UP");

		if (info->flags & IFF_RUNNING)		/* Interface RFC2863 OPER_UP	*/
			waddstr(w_net, " RUNNING");
#ifdef IFF_LOWER_UP	/* Linux 2.6.17 */
		if (info->flags & IFF_LOWER_UP)		/* Driver signals L1 up		*/
			waddstr(w_net, " LOWER_UP");
#endif
#ifdef IFF_DORMANT	/* Linux 2.6.17 */
		if (info->flags & IFF_DORMANT)		/* Driver signals dormant	*/
			waddstr(w_net, " DORMANT");
#endif
		if (info->flags & IFF_MASTER)		/* Master of a load balancer 	*/
			waddstr(w_net, " MASTER");
		if (info->flags & IFF_SLAVE)		/* Slave of a load balancer 	*/
			waddstr(w_net, " SLAVE");
		if (info->flags & IFF_POINTOPOINT)	/* Is a point-to-point link	*/
			waddstr(w_net, " POINTOPOINT");
		if (info->flags & IFF_DYNAMIC)		/* Address is volatile		*/
			waddstr(w_net, " DYNAMIC");
		if (info->flags & IFF_BROADCAST)	/* Valid broadcast address set	*/
			waddstr(w_net, " BROADCAST");
		if (info->flags & IFF_MULTICAST)	/* Supports multicast		*/
			waddstr(w_net, " MULTICAST");
		if (info->flags & IFF_ALLMULTI)		/* Receive all mcast  packets	*/
			waddstr(w_net, " ALLMULTI");
		if (info->flags & IFF_NOARP)		/* No ARP protocol		*/
			waddstr(w_net, " NOARP");
		if (info->flags & IFF_NOTRAILERS)	/* Avoid use of trailers	*/
			waddstr(w_net, " NOTRAILERS");
		if (info->flags & IFF_PROMISC)		/* Is in promiscuous mode	*/
			waddstr(w_net, " PROMISC");
		if (info->flags & IFF_DEBUG)		/* Internal debugging flag	*/
			waddstr(w_net, " DEBUG");
	}
	waddstr_b(w_net, ")");

	wmove(w_net, 2, 1);
	wclrtoborder(w_net);

	waddstr(w_net, "mode: ");
	if (ifinfo_is_up(info)) {
		waddstr_b(w_net, info->mode);

		// Queueing discipline
		if (*info->qdisc) {
			waddstr(w_net, ", qdisc: ");
			waddstr_b(w_net, info->qdisc);
			if (info->master && *info->master->qdisc &&
			    strcmp(info->master->qdisc, info->qdisc)) {
				waddstr(w_net, "/");
				waddstr_b(w_net, info->master->qdisc);
			}
		}

		// Number of TX queues
		if (info->numtxq > 1 || (info->master && info->master->numtxq > 1)) {
			waddstr(w_net, ", txq: ");
			sprintf(tmp, "%u", info->numtxq);
			waddstr_b(w_net, tmp);
			if (info->master && info->master->numtxq != info->numtxq) {
				waddstr(w_net, "/");
				sprintf(tmp, "%u", info->master->numtxq);
				waddstr_b(w_net, tmp);
			}
		}

		// Queue lengths
		waddstr(w_net, ", qlen: ");
		sprintf(tmp, "%u", info->txqlen);
		waddstr_b(w_net, tmp);
		if (info->master && info->master->txqlen != info->txqlen) {
			waddstr(w_net, "/");
			sprintf(tmp, "%u", info->master->txqlen);
			waddstr_b(w_net, tmp);
		}
	}

	wmove(w_net, 3, 1);
	wclrtoborder(w_net);

	/* Layer 2 information (display hardware address of active interface). */
	waddstr(w_net, "mac: ");
	if (ether_addr_is_zero(&active->hwaddr)) {
		waddstr_b(w_net, "n/a");
	} else if (!ifinfo_is_up(info)) {
		wadd_attr_str(w_net, COLOR_PAIR(CP_RED) | A_REVERSE,
			ether_lookup(&active->hwaddr));
	} else {
		waddstr_b(w_net, ether_lookup(&active->hwaddr));
	}


	if (ifinfo_is_up(info)) {
		/* 802.11 MTU may be greater than Ethernet MTU (1500) */
		if (info->mtu && info->mtu != ETH_DATA_LEN) {
			waddstr(w_net, ", mtu: ");
			sprintf(tmp, "%u", info->mtu);
			waddstr_b(w_net, tmp);
			if (info->master && info->master->mtu != info->mtu) {
				waddstr(w_net, "/");
				sprintf(tmp, "%u", info->master->mtu);
				waddstr_b(w_net, tmp);
			}
		}
	}

	wclrtoborder(w_net);
	wmove(w_net, 4, 1);

	/* Layer 3 information of active interface */
	waddstr(w_net, "ip4: ");
	if (!*active->v4.addr) {
		waddstr_b(w_net, "n/a");
	} else {
		waddstr_b(w_net, active->v4.addr);
		if (active->v4.count > 1) {
			sprintf(tmp, " (+%d)", active->v4.count-1);
			waddstr(w_net, tmp);
		}
		if (active->v4.valid_lft && active->v4.valid_lft >= active->v4.preferred_lft) {
			waddstr(w_net, ", valid: ");
			waddstr_b(w_net, lft2str(active->v4.valid_lft));
		} else if (active->v4.preferred_lft) {
			waddstr(w_net, ", preferred: ");
			waddstr_b(w_net, lft2str(active->v4.preferred_lft));
		}
	}
	wclrtoborder(w_net);

	wmove(w_net, 5, 1);
	waddstr(w_net, "ip6: ");
	if (!*active->v6.addr) {
		waddstr_b(w_net, "n/a");
	} else {
		waddstr_b(w_net, active->v6.addr);
		if (active->v6.count > 1) {
			sprintf(tmp, " (+%d)", active->v6.count-1);
			waddstr(w_net, tmp);
		}
		if (active->v6.valid_lft && active->v6.valid_lft >= active->v6.preferred_lft) {
			waddstr(w_net, ", valid: ");
			waddstr_b(w_net, lft2str(active->v6.valid_lft));
		} else if (active->v6.preferred_lft) {
			waddstr(w_net, ", preferred: ");
			waddstr_b(w_net, lft2str(active->v6.preferred_lft));
		}
	}

	free(info->master);

	wclrtoborder(w_net);
	wrefresh(w_net);
}

static void display_static_parts(WINDOW *w_if, WINDOW *w_info, WINDOW *w_net)
{
	struct iw_nl80211_ifstat ifs;
	struct if_info net_info;
	int i;

	iw_nl80211_getifstat(&ifs);
	if_getinf(conf_ifname(), &net_info);

	display_interface(w_if, &ifs, net_info.flags & IFF_UP);

	if (ifinfo_is_up(&net_info)) {
		display_info(w_info, &ifs);
	} else {
		for (i = 1; i <= WH_INFO; i++)
			mvwclrtoborder(w_info, i, 1);
	}
	wrefresh(w_info);

	display_netinfo(w_net, &net_info);
}

void scr_info_init(void)
{
	int line = 0;
	bool ready = false;
	static bool initialized = false;

	w_if	 = newwin_title(line, WH_IFACE, "Interface", true);
	line += WH_IFACE;
	w_levels = newwin_title(line, WH_LEVEL, "Levels", true);
	line += WH_LEVEL;
	w_stats	 = newwin_title(line, WH_STATS, "Packet Counts", true);
	line += WH_STATS;
	w_info	 = newwin_title(line, WH_INFO, "Info", true);
	line += WH_INFO;
	w_net = newwin_title(line, WH_NET, "Network", false);

	if (!initialized) {
		pthread_mutexattr_t attr;

		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
		pthread_mutex_init(&linkstat_mutex, &attr);
		initialized = true;
	}

	sampling_init(false);

	while (!ready) {
		pthread_mutex_lock(&linkstat_mutex);
		ready = ls_new && !ls_tmp;
		pthread_mutex_unlock(&linkstat_mutex);
	}
}

int scr_info_loop(WINDOW *w_menu)
{
	time_t now = time(NULL);

	if (pthread_mutex_trylock(&linkstat_mutex) == 0) {
		if (ls_new && !ls_tmp) {
			ls_tmp = ls_cur;
			ls_cur = ls_new;
			ls_new = NULL;
		}
		pthread_mutex_unlock(&linkstat_mutex);
	}
	display_levels();
	display_packet_counts();

	if (now - last_update >= conf.info_iv) {
		last_update = now;
		display_static_parts(w_if, w_info, w_net);
	}
	return wgetch(w_menu);
}

void scr_info_fini(void)
{
	/* Unlock mutex in case it was taken when scr_info_loop got interrupted by a SIGWINCH. */
	pthread_mutex_unlock(&linkstat_mutex);
	sampling_stop();
	last_update = 0;

	delwin(w_net);
	delwin(w_info);
	delwin(w_stats);
	delwin(w_levels);
	delwin(w_if);
}
