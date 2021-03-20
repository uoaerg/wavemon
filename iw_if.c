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
#include <netdb.h>
#include <ifaddrs.h>

/*
 * Obtain network device information
 */
static int if_get_flags(int skfd, const char *ifname)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(struct ifreq));
	strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);

	if (ioctl(skfd, SIOCGIFFLAGS, &ifr) < 0)
		err_sys("can not get interface flags for %s", ifname);
	return ifr.ifr_flags;
}

/* Return true if @ifname is known to be up. */
bool if_is_up(const char *ifname)
{
	int ret, skfd = socket(AF_INET, SOCK_DGRAM, 0);

	if (skfd < 0)
		err_sys("%s: can not open socket", __func__);
	ret = if_get_flags(skfd, ifname) & IFF_UP;
	close(skfd);
	return ret;
}


/** Change the up/down state of @ifname according to @up. */
static int if_set_up_or_down(const char *ifname, bool up)
{
	struct ifreq ifr;
	int ret, skfd = socket(AF_INET, SOCK_DGRAM, 0);

	if (skfd < 0)
		err_sys("%s: can not open socket", __func__);

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);

	ifr.ifr_flags = if_get_flags(skfd, ifname);
	if (up) {
		ifr.ifr_flags |= IFF_UP;
	} else {
		ifr.ifr_flags &= ~IFF_UP;
	}
	ret = ioctl(skfd, SIOCSIFFLAGS, &ifr);
	close(skfd);
	return ret;
}

/** Bring @ifname up. */
int if_set_up(const char *ifname)
{
	return if_set_up_or_down(ifname, true);
}

/** Set @ifname down. */
int if_set_down(const char *ifname)
{
	return if_set_up_or_down(ifname, false);
}

/** Exit handler to restore interface 'down' state on exit via atexit(3). */
void if_set_down_on_exit(void)
{
	const char *ifname = conf_ifname();

	if (ifname && if_set_down(ifname) < 0) {
		err_msg("unable to restore %s interface state - set down manually", ifname);
	}
}

/* Interface information */
void if_getinf(const char *ifname, struct if_info *info)
{
	struct ifaddrs *iface, *head;
	struct ifreq ifr;
	int skfd, rc;

	memset(&ifr, 0, sizeof(struct ifreq));
	memset(info, 0, sizeof(struct if_info));

	if (getifaddrs(&head) < 0)
		err_sys("%s: failed to query interface addresses", __func__);

	for (iface = head; iface != NULL; iface = iface->ifa_next) {
		const struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)iface->ifa_addr;
		char ip_string[INET6_ADDRSTRLEN+1];

		if (iface->ifa_addr == NULL || strcmp(iface->ifa_name, ifname))
			continue;

		if (iface->ifa_addr->sa_family == AF_INET) {
			rc = getnameinfo(iface->ifa_addr, sizeof(struct sockaddr_in),
					 ip_string, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
			if (rc != 0)
				snprintf(info->v4_addr, sizeof(info->v4_addr)-1,
					 "(error: %s)", gai_strerror(rc));
			else
				snprintf(info->v4_addr, sizeof(info->v4_addr)-1, "%s/%u",
					 ip_string, prefix_len(iface->ifa_netmask));
		} else if (iface->ifa_addr->sa_family == AF_INET6 &&
			   !IN6_IS_ADDR_LINKLOCAL(sin6->sin6_addr.s6_addr) &&
			   !IN6_IS_ADDR_SITELOCAL(sin6->sin6_addr.s6_addr) &&
			   !IN6_IS_ADDR_UNSPECIFIED(sin6->sin6_addr.s6_addr)) {
			rc = getnameinfo(iface->ifa_addr, sizeof(struct sockaddr_in6),
					 ip_string, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
			if (rc != 0)
				snprintf(info->v6_addr, sizeof(info->v6_addr)-1,
					 "(error: %s)", gai_strerror(rc));
			else
				snprintf(info->v6_addr, sizeof(info->v6_addr)-1, "%s/%u",
					 ip_string, prefix_len(iface->ifa_netmask));
		}
		info->flags = iface->ifa_flags;
	}
	freeifaddrs(head);

	skfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (skfd < 0)
		err_sys("%s: can not open socket", __func__);

	info->flags = if_get_flags(skfd, ifname);

	strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);
	if (ioctl(skfd, SIOCGIFMTU, &ifr) == 0)
		info->mtu = ifr.ifr_mtu;

	if (ioctl(skfd, SIOCGIFTXQLEN, &ifr) >= 0)
		info->txqlen = ifr.ifr_qlen;

	if (ioctl(skfd, SIOCGIFHWADDR, &ifr) >= 0)
		memcpy(&info->hwaddr, &ifr.ifr_hwaddr.sa_data, 6);
	close(skfd);
}

static int iface_list_handler(struct nl_msg *msg, void *arg)
{
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];
	struct interface_info **head = arg, *new;

	nla_parse(tb_msg, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	if (tb_msg[NL80211_ATTR_IFNAME]) {
		new = calloc(1, sizeof(*new));
		if (!new)
			err_sys("failed to allocate interface list entry");

		new->ifname = strdup(nla_get_string(tb_msg[NL80211_ATTR_IFNAME]));
		new->next   = *head;
		*head       = new;

		if (tb_msg[NL80211_ATTR_WIPHY])
			new->phy_id = nla_get_u32(tb_msg[NL80211_ATTR_WIPHY]);
		if (tb_msg[NL80211_ATTR_IFINDEX])
			new->ifindex = nla_get_u32(tb_msg[NL80211_ATTR_IFINDEX]);
		if (tb_msg[NL80211_ATTR_WDEV])
			new->wdev = nla_get_u64(tb_msg[NL80211_ATTR_WDEV]);
		if (tb_msg[NL80211_ATTR_MAC])
			memcpy(&new->mac_addr, nla_data(tb_msg[NL80211_ATTR_MAC]), ETH_ALEN);
	}
	return NL_SKIP;
}

/** Return information about the default interface in @data. */
int iw_nl80211_get_interface_data(struct interface_info **data)
{
	static struct cmd cmd_get_interface = {
		.cmd         = NL80211_CMD_GET_INTERFACE,
		.handler     = iface_list_handler,
	};

	cmd_get_interface.handler_arg = data;
	return handle_interface_cmd(&cmd_get_interface);
}

/** Populate singly-linked list of wireless interfaces starting at @head. */
int iw_nl80211_get_interface_list(struct interface_info **head)
{
	static struct cmd cmd_get_interfaces = {
		.cmd         = NL80211_CMD_GET_INTERFACE,
		.flags       = NLM_F_DUMP,
		.handler     = iface_list_handler,
	};

	cmd_get_interfaces.handler_arg = head;
	return handle_cmd(&cmd_get_interfaces);
}

/** Count the number of wireless interfaces starting at @head. */
size_t count_interface_list(struct interface_info *head)
{
	return head ? count_interface_list(head->next) + 1 : 0;
}

void free_interface_list(struct interface_info *head)
{
	if (head) {
		free_interface_list(head->next);
		free(head->ifname);
		free(head);
	}
}
