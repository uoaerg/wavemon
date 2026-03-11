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
#include "wavemon.h"
#include "iw_if.h"
#include "iw_nl80211.h"

/**
 * newwin_title  -  Create a new bordered window at (y, 0)
 * @y:		vertical row position to start at
 * @h:		height of the new window in lines
 * @title:	name of the window
 * @nobottom:   whether to keep the bottom of the box open
 */
WINDOW *newwin_title(int y, int h, const char *title, bool nobottom)
{
	WINDOW *win = newwin(h, WAV_WIDTH, y, 0);

#ifdef HAVE_LIBNCURSESW
	const cchar_t * top_left  = y > 0 ? WACS_LTEE : WACS_ULCORNER;
	const cchar_t * top_right = y > 0 ? WACS_RTEE : WACS_URCORNER;

	if (nobottom) {
		mvwadd_wch(win, 0, 0, top_left);
		mvwhline_set(win, 0, 1, WACS_HLINE, MAXXLEN);
		mvwvline_set(win, 1, 0, WACS_VLINE, h);
		mvwadd_wch(win, 0, WAV_WIDTH - 1, top_right);
		mvwvline_set(win, 1, WAV_WIDTH - 1, WACS_VLINE, h);
	} else {
		wborder_set(win, WACS_VLINE, WACS_VLINE, WACS_HLINE, WACS_HLINE,
			    top_left, top_right, WACS_LLCORNER, WACS_LRCORNER);
	}
#else
	chtype top_left  = y > 0 ? ACS_LTEE : ACS_ULCORNER;
	chtype top_right = y > 0 ? ACS_RTEE : ACS_URCORNER;

	if (nobottom) {
		mvwaddch(win, 0, 0, top_left);
		mvwhline(win, 0, 1, ACS_HLINE, MAXXLEN);
		mvwvline(win, 1, 0, ACS_VLINE, h);
		mvwaddch(win, 0, WAV_WIDTH - 1, top_right);
		mvwvline(win, 1, WAV_WIDTH - 1, ACS_VLINE, h);
	} else {
		wborder(win, ACS_VLINE, ACS_VLINE, ACS_HLINE, ACS_HLINE,
			top_left, top_right, ACS_LLCORNER, ACS_LRCORNER);
	}
#endif
	wattrset(win, COLOR_PAIR(CP_CYAN));
	mvwaddstr(win, 0, 2, title);
	wattroff(win, COLOR_PAIR(CP_CYAN));

	return win;
}

/* clear inside window content up to the right border */
void mvwclrtoborder(WINDOW *win, int y, int x)
{
	if (x >= 1 && x <= MAXXLEN)
		mvwhline(win, y, x, ' ', 1 + MAXXLEN - x);
}

void wclrtoborder(WINDOW *win)
{
	int x, y;

	getyx(win, y, x);
	mvwclrtoborder(win, y, x);
}

void waddstr_center(WINDOW *win, int y, const char *s)
{
	mvwaddstr(win, y, (WAV_WIDTH - strlen(s)) / 2, s);
}

void wadd_attr_str(WINDOW *win, const int attrs, const char *s)
{
	wattron(win, attrs);
	waddstr(win, s);
	wattroff(win, attrs);
}

/* Enforce that @str is at most @len characters (excluding the terminal '\0') */
const char *curtail(const char *str, const char *sep, size_t len)
{
	static char out_buf[128];
	const char fallback_sep[] = "~";
	size_t l = 0, front, mid, back;

	if (len >= sizeof(out_buf))
		len = sizeof(out_buf) - 1;

	if (sep == NULL || *sep == '\0')
		sep = fallback_sep;
	mid = strlen(sep);
	if (mid > len) {
		sep = fallback_sep;
		mid = strlen(sep);
	}

	if (str != NULL)
		l = strlen(str);
	if (l <= len)
		return str;

	front = (len - mid)/2.0 + 0.5;
	back  = len - front - mid;

	strncpy(out_buf, str, front);
	strncpy(out_buf + front, sep, mid);
	strncpy(out_buf + front + mid, str + l - back, back + 1);

	return out_buf;
}

static double interpolate(const double val, const double min, const double max)
{
	return	val < min ? 0 :
		val > max ? 1 : (val - min) / (max - min);
}

void waddbar(WINDOW *win, int y, float v, float min, float max,
		    int8_t *cscale, bool rev)
{
	chtype ch = '=' | A_BOLD | cp_from_scale(v, cscale, rev);
	int len = MAXXLEN * interpolate(v, min, max);

	mvwhline(win, y, 1, ch, len);
	mvwclrtoborder(win, y, len + 1);
}

void waddthreshold(WINDOW *win, int y, float v, float tv,
		   float minv, float maxv, int8_t *cscale, chtype tch)
{
	if (tv > minv && tv < maxv) {
		if (v > tv)
			tch |= COLOR_PAIR(CP_STANDARD);
		else
			tch |= cp_from_scale(v, cscale, true);

		mvwaddch(win, y, 1 + MAXXLEN * interpolate(tv, minv, maxv), tch);
	}
}

void display_link_header(WINDOW *w, const struct ether_addr *bssid)
{
	char buf[512];
	struct iw_nl80211_ifstat ifs;
	bool iface_exists;

	wmove(w, 1, 1);
	wclrtoborder(w);

	wattrset(w, COLOR_PAIR(CP_STANDARD));
	waddstr(w, "if: ");
	waddstr_b(w, conf_ifname());

	iface_exists = if_nametoindex(conf_ifname()) != 0;

	if (!iface_exists) {
		wattrset(w, COLOR_PAIR(CP_RED) | A_BOLD);
		waddstr(w, " | device unavailable");
		if (carl9170_needs_root()) {
			wattrset(w, COLOR_PAIR(CP_YELLOW) | A_BOLD);
			waddstr(w, " | run with sudo for auto-recovery");
		}
		wattrset(w, COLOR_PAIR(CP_STANDARD));
		wrefresh(w);
		return;
	}

	memset(&ifs, 0, sizeof(ifs));
	iw_nl80211_getifstat(&ifs);

	{
		char drv[32], product[128];

		if_get_driver(conf_ifname(), drv, sizeof(drv));
		if_get_product(conf_ifname(), product, sizeof(product));

		snprintf(buf, sizeof(buf), " (%s, %s)", product, drv);
		wattrset(w, COLOR_PAIR(CP_STANDARD) | A_DIM);
		waddstr(w, buf);
		wattrset(w, COLOR_PAIR(CP_STANDARD));
	}

	if (ifs.ssid[0]) {
		const char *ap_name = bssid ? ap_names_lookup(bssid) : NULL;
		int chan = ieee80211_frequency_to_channel(ifs.freq);
		const char *width = channel_width_name(ifs.chan_width);

		waddstr(w, " | ssid: ");
		wattrset(w, COLOR_PAIR(CP_YELLOW) | A_BOLD);
		waddstr(w, ifs.ssid);
		wattrset(w, COLOR_PAIR(CP_STANDARD));

		if (ap_name) {
			snprintf(buf, sizeof(buf), " (%s)", ap_name);
			wattrset(w, COLOR_PAIR(CP_CYAN));
			waddstr(w, buf);
			wattrset(w, COLOR_PAIR(CP_STANDARD));
		}

		if (bssid) {
			snprintf(buf, sizeof(buf),
				 " | bssid: %02X:%02X:%02X:%02X:%02X:%02X",
				 bssid->ether_addr_octet[0],
				 bssid->ether_addr_octet[1],
				 bssid->ether_addr_octet[2],
				 bssid->ether_addr_octet[3],
				 bssid->ether_addr_octet[4],
				 bssid->ether_addr_octet[5]);
			waddstr(w, buf);
		}

		snprintf(buf, sizeof(buf),
			 " | freq: %u MHz | ch: %d | width: %s | tx-pwr: %.0f dBm",
			 ifs.freq, chan, width, ifs.tx_power);
		waddstr(w, buf);
	} else {
		wattrset(w, COLOR_PAIR(CP_YELLOW));
		waddstr(w, " | not associated");
		wattrset(w, COLOR_PAIR(CP_STANDARD));
	}

	wrefresh(w);
}

void display_root_warning(void)
{
	const char *line1 = "carl9170 USB crash recovery needs root";
	const char *line2 = "restart with:  sudo wavemon";
	int mid = WAV_HEIGHT / 2;
	int i;

	if (!carl9170_needs_root())
		return;

	/* Full-width red bars: blank line, message, blank line, hint, blank line */
	attron(COLOR_PAIR(CP_RED) | A_BOLD | A_REVERSE);
	for (i = mid - 2; i <= mid + 2; i++) {
		mvhline(i, 1, ' ', COLS - 2);
	}
	mvprintw(mid - 1, (COLS - (int)strlen(line1)) / 2, "%s", line1);
	mvprintw(mid + 1, (COLS - (int)strlen(line2)) / 2, "%s", line2);
	attroff(COLOR_PAIR(CP_RED) | A_BOLD | A_REVERSE);
	refresh();
}
