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

#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/wireless.h>

#define IW_STACKSIZE 1024

struct iw_dyn_info {			// modified iwreq
	char	cap_essid	: 1,	// capability flags
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

struct iw_aplist {
	unsigned short num;
	char	has_quality : 1;

	struct {
	  struct sockaddr   addr;
	  struct iw_quality quality;
	} aplist[IW_MAX_AP];
};

struct iw_stat {
	int		link;
	int		signal, noise;
	unsigned long dsc_nwid, dsc_enc, dsc_misc;
};

extern struct iw_stat iw_stats;
extern struct iw_stat iw_stats_cache[IW_STACKSIZE];
extern int iw_getstat(char *ifname, struct iw_stat *stat, struct iw_stat *stack,
		      int slotsize, char random);
void (*iw_stat_redraw)(void);

void iw_getinf_dyn(char *ifname, struct iw_dyn_info *info);
void iw_getinf_range(char *ifname, struct iw_range *range);
int iw_get_aplist(char *ifname, struct iw_aplist *lst);
int iw_getif();

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
double dbm2mw(float in);
char *dbm2units(float in);
double mw2dbm(float in);
float freq2ghz(struct iw_freq *f);

extern void dump_parameters(void);
