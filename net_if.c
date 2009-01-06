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

#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <string.h>

#include "net_if.h"

void if_getinf(char *ifname, struct if_info *info)
{
	int 	skfd;
	struct ifreq ifr;
	
	if ((skfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		fprintf(stderr, "fatal error: cannot open socket\n");
		exit(-1);
	}
	
	memset(&ifr, 0, sizeof(struct ifreq));
	memset(info, 0, sizeof(struct if_info));
	
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
	if (ioctl(skfd, SIOCGIFADDR, &ifr) >= 0)
		memcpy(&info->addr, &ifr.ifr_addr.sa_data[2], 4);
	if (ioctl(skfd, SIOCGIFHWADDR, &ifr) >= 0)
		memcpy(&info->hwaddr, &ifr.ifr_hwaddr.sa_data, 6);
	if (ioctl(skfd, SIOCGIFNETMASK, &ifr) >= 0)
		memcpy(&info->netmask, &ifr.ifr_netmask.sa_data[2], 4); 
	if (ioctl(skfd, SIOCGIFBRDADDR, &ifr) >= 0)
		memcpy(&info->bcast, &ifr.ifr_broadaddr.sa_data[2], 4);
		
	close(skfd);
}

void if_getstat(char *ifname, struct if_stat *stat)
{
	FILE	*fd;
	char	tmp[0x100];
	unsigned long d;
	char 	*lp;
	
	if ((fd = fopen("/proc/net/dev", "r")) < 0) {
		fprintf(stderr, "fatal error: cannot open /proc/net/dev\n");
		exit(-1);
	}
	
	while (fgets(tmp, 0x100, fd)) {
		lp = tmp + strspn(tmp, " ");
		if (!strncmp(lp, ifname, strlen(ifname))) {
			lp += strlen(ifname) + 1;
			lp += strspn(lp, " ");

			sscanf(lp, "%llu %llu %lu %lu %lu %lu %lu %lu %llu %llu",
				&stat->rx_bytes, &stat->rx_packets, &d, &d, &d, &d, &d, &d,
				&stat->tx_bytes, &stat->tx_packets);
		}
	}
	
	fclose(fd);
}
