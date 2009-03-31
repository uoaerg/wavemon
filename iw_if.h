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
#include "wavemon.h"
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/wireless.h>

/* Maximum length of a MAC address: 2 * 6 hex digits, 6 - 1 colons, plus '\0' */
#define MAC_ADDR_MAX	18

/*
 * Threshold for 'sane' noise levels.
 *
 * Some drivers simply set an arbitrary minimum noise level to mean 'invalid',
 * but do not set IW_QUAL_NOISE_INVALID so that the display gets stuck at a
 * "house number". The value below is suggested by and taken from the iwl3945
 * driver (constant IWL_NOISE_MEAS_NOT_AVAILABLE in iwl-3945.h).
 */
#define NOISE_DBM_SANE_MIN	-127

/* Static network interface information - see netdevice(7) */
struct if_info {			/* modified ifreq */
	unsigned char	hwaddr[6];
	struct in_addr	addr,
			netmask,
			bcast;
};
extern void if_getinf(char *ifname, struct if_info *info);

struct iw_dyn_info {			/* modified iwreq */
	char	cap_essid	: 1,
			cap_nwid	: 1,
			cap_nickname : 1,
			cap_freq	: 1,
			cap_sens	: 1,
			cap_bitrate	: 1,
			cap_txpower	: 1,
			cap_rts		: 1,
			cap_frag	: 1,
			cap_mode	: 1,
			cap_ap		: 1,
			cap_encode	: 1,
			cap_power	: 1,
			cap_aplist	: 1;
	char	name[IFNAMSIZ];
	char	essid[IW_ESSID_MAX_SIZE];
	char	essid_on : 1;
	char	nickname[IW_ESSID_MAX_SIZE];
	unsigned long nwid;
	char	nwid_on : 1;
	unsigned short rts;
	char	rts_on	: 1;
	unsigned short frag;
	char	frag_on	: 1;
	float 	freq;
	signed long sens;
	unsigned long bitrate;
	signed short txpower_dbm;
	float	txpower_mw;
	int		mode;
	char 	keysize;
	int		key_index;
	char	key[IW_ENCODING_TOKEN_MAX];

	struct crypt_flags {
		char	disabled 	: 1,
				index		: 1,
				restricted 	: 1,
				open		: 1,
				nokey		: 1;
	} eflags;

	unsigned long pmvalue;

	struct pm_flags {
		char	disabled	: 1,
				timeout		: 1,
				unicast		: 1,
				multicast	: 1,
				forceuc		: 1,
				repbc		: 1,
				min			: 1,
				rel			: 1;
	} pflags;

	struct sockaddr ap_addr;
};

struct if_stat {
	unsigned long long rx_packets, tx_packets;
	unsigned long long rx_bytes, tx_bytes;
};

extern void if_getstat(char *ifname, struct if_stat *stat);

/*
 *	 Structs to communicate WiFi statistics
 */
struct iw_levelstat {
	float	signal;		/* signal level in dBm */
	float	noise;		/* noise  level in dBm */
};

/**
 * struct iw_stat - record current WiFi state
 * @range:	current range information
 * @stats:	current signal level statistics
 * @dbm:	the noise/signal of @stats in dBm
 */
struct iw_stat {
	struct iw_range		range;
	struct iw_statistics	stat;
	struct iw_levelstat	dbm;
};

extern void iw_sanitize(struct iw_range *range,
			struct iw_quality *qual,
			struct iw_levelstat *dbm);
extern void iw_getstat(struct iw_stat *stat);
extern void iw_cache_update(struct iw_stat *stat);

extern void iw_getinf_dyn(char *ifname, struct iw_dyn_info *info);
extern void iw_getinf_range(char *ifname, struct iw_range *range);

extern void (*iw_stat_redraw)(void);

/*
 *	Helper routines
 */
static inline const char *iw_opmode(const uint8_t mode)
{
	static char *modes[] = {"Auto",
				"Ad-Hoc",
				"Managed",
				"Master",
				"Repeater",
				"Secondary",
				"Monitor"
	};
	return mode > 6 ? "Unknown/bug" : modes[mode];
}

/* Pretty-print a mac-address. `mac' must be of length 6 or greater */
static inline char *mac_addr(const unsigned char *mac)
{
	static char str[MAC_ADDR_MAX];

	sprintf(str, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
						      mac[3], mac[4], mac[5]);
	return str;
}

/* Convert log dBm values to linear mW */
static inline double dbm2mw(const double in)
{
	return pow(10.0, in / 10.0);
}

static inline char *dbm2units(const double in)
{
	static char with_units[0x100];
	double val = dbm2mw(in);

	if (val < 0.00000001) {
		sprintf(with_units, "%.2f pW", val * 1e9);
	} else if (val < 0.00001) {
		sprintf(with_units, "%.2f nW", val * 1e6);
	} else if (val < 0.01) {
		sprintf(with_units, "%.2f uW", val * 1e3);
	} else {
		sprintf(with_units, "%.2f mW", val);
	}
	return with_units;
}

/* Convert linear mW values to log dBm */
static inline double mw2dbm(const double in)
{
	return 10.0 * log10(in);
}

/* Convert frequency to GHz */
static inline float freq2ghz(const struct iw_freq *f)
{
	return (f->e ? f->m * pow(10, f->e) : f->m) / 1e9;
}
