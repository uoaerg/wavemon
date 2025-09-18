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

/**
 * Return bonding mode of @bonding_iface, or NULL if not appropriate.
 * https://www.kernel.org/doc/Documentation/networking/bonding.txt for possible modes.
 */
const char *get_bonding_mode(const char *bonding_iface) {
	static char mode[64];
	char path[128];

	snprintf(path, sizeof(path)-1, "/sys/class/net/%s/bonding/mode", bonding_iface);
	if (read_file(path, mode, sizeof(mode)) > 0) {
		char *p = mode;

		// File contents look like: "active-backup 1". Return first word only.
		for (int i = strlen(mode); --i > 0 && !isspace(*p);)
			p++;
		*p = '\0';
		return mode;
	}
	return NULL;
}

/** Return true if @slave is the primary slave interface of @bonding_iface. */
bool is_primary_slave(const char *bonding_iface, const char *slave) {
	char path[128], primary[64];

	snprintf(path, sizeof(path)-1, "/sys/class/net/%s/bonding/primary", bonding_iface);
	if (read_file(path, primary, sizeof(primary)) > 0)
		return strncmp(slave, primary, strlen(slave)) == 0;
	return false;
}

/* if_info_link_cb fills in link information into @data. */
static void if_info_link_cb(struct nl_object *obj, void *data) {
	struct rtnl_link *link = (struct rtnl_link *)obj;
	struct if_info *info = data;

	if (link && rtnl_link_get_ifindex(link) == info->ifindex) {
		struct nl_addr *hwaddr = rtnl_link_get_addr(link);
		const char * const type = rtnl_link_get_type(link);

		memcpy(&info->hwaddr, nl_addr_get_binary_addr(hwaddr), nl_addr_get_len(hwaddr));
		if (type)
			strncpy(info->type, type, sizeof(info->type)-1);
		strncpy(info->ifname, rtnl_link_get_name(link), sizeof(info->ifname)-1);
		strncpy(info->qdisc, rtnl_link_get_qdisc(link), sizeof(info->qdisc)-1);
		rtnl_link_mode2str(rtnl_link_get_linkmode(link), info->mode, sizeof(info->mode)-1);

		info->flags   = rtnl_link_get_flags(link);
		info->carrier = rtnl_link_get_carrier(link);
		info->mtu     = rtnl_link_get_mtu(link);
		info->numtxq  = rtnl_link_get_num_tx_queues(link);
		info->txqlen  = rtnl_link_get_txqlen(link);

		if (info->flags & IFF_SLAVE) {
			info->master = calloc(1, sizeof(*info->master));
			if (!info->master)
				err_sys("failed to allocate master interface entry");

			info->master->ifindex = rtnl_link_get_master(link);
		}
	}
}

/* ifinfo_is_up if the interface specified by @if_info is up and has carrier. */
bool ifinfo_is_up(const struct if_info *const info) {
	if (info->master && !(info->master->flags & IFF_UP))
		return false;
	return (info->flags & IFF_UP) && info->carrier;
}

/* if_info_addr_cb fills in interface address information into @data. */
static void if_info_addr_cb(struct nl_object *obj, void *data) {
	struct rtnl_addr *addr = (struct rtnl_addr *)obj;
	struct if_info *info = data;

	if (!addr || rtnl_addr_get_ifindex(addr) != info->ifindex)
		return;

	// Only display addresses with global scope, omit site/link-local addresses.
	if (rtnl_addr_get_scope(addr) == RT_SCOPE_UNIVERSE) {
		struct addr_info *ai;

		switch (rtnl_addr_get_family(addr)) {
		case AF_INET:
			ai = &info->v4;
			break;
		case AF_INET6:
			ai = &info->v6;
			break;
		default:
			return;
		}

		if (!ai->count) {
			const struct nl_addr *local = rtnl_addr_get_local(addr);

			if (!local)
				return;
			if (!nl_addr2str(local, ai->addr, sizeof(ai->addr)))
				return;
			ai->preferred_lft = rtnl_addr_get_preferred_lifetime(addr);
			ai->valid_lft     = rtnl_addr_get_valid_lifetime(addr);
		}
		ai->count++;
	}
}

void if_getinf(const char *ifname, struct if_info *info)
{
	struct nl_cache *link_cache, *addr_cache;
	struct nl_sock *sock = nl_cli_alloc_socket();

	nl_cli_connect(sock, NETLINK_ROUTE);
	link_cache = nl_cli_link_alloc_cache(sock);
	addr_cache = nl_cli_addr_alloc_cache(sock);

	memset(info, 0, sizeof(struct if_info));
	info->ifindex = if_nametoindex(ifname);

	nl_cache_foreach(link_cache, if_info_link_cb, info);
	if (info->master)
		nl_cache_foreach(link_cache, if_info_link_cb, info->master);

	nl_cache_foreach(addr_cache, if_info_addr_cb, info->master ? info->master : info);

	/* Clean up. */
	nl_cache_mngt_unprovide(link_cache);
	nl_cache_put(link_cache);

	nl_cache_mngt_unprovide(addr_cache);
	nl_cache_put(addr_cache);

	nl_socket_free(sock);
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
