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

struct if_info {
	char	addr[4];
	char	hwaddr[6];
	char	netmask[4];
	char	bcast[4];
};

struct if_stat {
	unsigned long long rx_packets, tx_packets;
	unsigned long long rx_bytes, tx_bytes;
};

void if_getinf(char *ifname, struct if_info *info);
void if_getstat(char *ifname, struct if_stat *stat);

char *byte_units(unsigned long long bytes);
