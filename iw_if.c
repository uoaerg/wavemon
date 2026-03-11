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

/*
 * Driver and chip vendor identification via sysfs.
 */
static const struct {
	uint16_t	id;
	const char	*name;
} pci_wifi_vendors[] = {
	{ 0x8086, "Intel" },
	{ 0x168c, "Qualcomm Atheros" },
	{ 0x14e4, "Broadcom" },
	{ 0x10ec, "Realtek" },
	{ 0x1814, "Ralink" },
	{ 0x02d0, "Broadcom (SDIO)" },
	{ 0x1ae9, "Wilocity" },
	{ 0x17cb, "Qualcomm" },
	{ 0x1a3b, "AzureWave" },
	{ 0,      NULL }
};

static const struct {
	uint16_t	id;
	const char	*name;
} usb_wifi_vendors[] = {
	{ 0x0cf3, "Qualcomm Atheros" },
	{ 0x0bda, "Realtek" },
	{ 0x148f, "Ralink" },
	{ 0x8086, "Intel" },
	{ 0x2357, "TP-Link" },
	{ 0x0b05, "ASUS" },
	{ 0x7392, "Edimax" },
	{ 0x2001, "D-Link" },
	{ 0x0846, "Netgear" },
	{ 0x057c, "AVM" },
	{ 0x0e8d, "MediaTek" },
	{ 0x2c4e, "MediaTek" },
	{ 0x0411, "Buffalo" },
	{ 0x13b1, "Linksys" },
	{ 0x050d, "Belkin" },
	{ 0x20f4, "TRENDnet" },
	{ 0x1058, "Western Digital" },
	{ 0x0586, "ZyXEL" },
	{ 0x1737, "Linksys" },
	{ 0x1740, "Senao" },
	{ 0x0df6, "Sitecom" },
	{ 0x07b8, "AboCom" },
	{ 0x15a9, "Gemtek" },
	{ 0x04bb, "I-O Data" },
	{ 0x1eda, "AirTies" },
	{ 0x2604, "Tenda" },
	{ 0x0e66, "Hawking" },
	{ 0x1286, "Marvell" },
	{ 0x1b75, "Ovislink" },
	{ 0x13d3, "IMC Networks" },
	{ 0x0ace, "ZyDAS" },
	{ 0x0cde, "Z-Com" },
	{ 0x1435, "Wistron NeWeb" },
	{ 0x083a, "Accton" },
	{ 0x0409, "NEC" },
	{ 0x0471, "Philips" },
	{ 0x06f8, "Guillemot" },
	{ 0,      NULL }
};

static const char *lookup_vendor(uint16_t id, bool usb)
{
	if (usb) {
		for (int i = 0; usb_wifi_vendors[i].name; i++)
			if (usb_wifi_vendors[i].id == id)
				return usb_wifi_vendors[i].name;
	} else {
		for (int i = 0; pci_wifi_vendors[i].name; i++)
			if (pci_wifi_vendors[i].id == id)
				return pci_wifi_vendors[i].name;
	}
	return NULL;
}

void if_get_driver(const char *ifname, char *buf, size_t len)
{
	char link[256], resolved[256];

	snprintf(link, sizeof(link), "/sys/class/net/%s/device/driver", ifname);
	ssize_t n = readlink(link, resolved, sizeof(resolved) - 1);
	if (n > 0) {
		resolved[n] = '\0';
		const char *base = strrchr(resolved, '/');
		snprintf(buf, len, "%s", base ? base + 1 : resolved);
	} else {
		snprintf(buf, len, "unknown");
	}
}

/**
 * Lookup vendor + device name from /usr/share/misc/pci.ids.
 * Format: vendor line at col 0 ("8086  Intel Corporation"),
 *         device line indented by tab ("\t51f1  Raptor Lake PCH CNVi WiFi").
 * Writes "Vendor Device" into @buf, e.g. "Intel Corporation Raptor Lake PCH CNVi WiFi".
 */
static bool pci_ids_lookup(uint16_t vendor, uint16_t device, char *buf, size_t len)
{
	static const char *pci_ids_paths[] = {
		"/usr/share/misc/pci.ids",
		"/usr/share/hwdata/pci.ids",
		NULL
	};
	FILE *fp = NULL;
	char line[256], vendor_name[256] = "", dev_name[256] = "";
	char vhex[8], dhex[8];
	bool in_vendor = false;

	for (int i = 0; pci_ids_paths[i]; i++) {
		fp = fopen(pci_ids_paths[i], "r");
		if (fp)
			break;
	}
	if (!fp)
		return false;

	snprintf(vhex, sizeof(vhex), "%04x", vendor);
	snprintf(dhex, sizeof(dhex), "%04x", device);

	while (fgets(line, sizeof(line), fp)) {
		if (line[0] == '#' || line[0] == '\n')
			continue;

		if (!isspace(line[0])) {
			/* Vendor line */
			if (in_vendor)
				break;	/* past our vendor, device not found */
			if (strncmp(line, vhex, 4) == 0 && line[4] == ' ') {
				char *p = line + 4;
				while (*p == ' ') p++;
				char *nl = strchr(p, '\n');
				if (nl) *nl = '\0';
				snprintf(vendor_name, sizeof(vendor_name), "%s", p);
				in_vendor = true;
			}
		} else if (in_vendor && line[0] == '\t' && line[1] != '\t') {
			/* Device line (single tab) */
			if (strncmp(line + 1, dhex, 4) == 0 && line[5] == ' ') {
				char *p = line + 5;
				while (*p == ' ') p++;
				char *nl = strchr(p, '\n');
				if (nl) *nl = '\0';
				snprintf(dev_name, sizeof(dev_name), "%s", p);
				break;
			}
		}
	}
	fclose(fp);

	if (vendor_name[0] && dev_name[0]) {
		snprintf(buf, len, "%s %s", vendor_name, dev_name);
		return true;
	}
	if (vendor_name[0]) {
		snprintf(buf, len, "%s", vendor_name);
		return true;
	}
	return false;
}

void if_get_product(const char *ifname, char *buf, size_t len)
{
	char path[256], val[128];

	/* USB devices: read product string directly */
	snprintf(path, sizeof(path), "/sys/class/net/%s/device/../product", ifname);
	if (read_file(path, val, sizeof(val)) > 0) {
		snprintf(buf, len, "%s", val);
		return;
	}

	/* PCI devices: lookup vendor+device in pci.ids */
	{
		char vbuf[16], dbuf[16];
		uint16_t vendor_id, device_id;

		snprintf(path, sizeof(path), "/sys/class/net/%s/device/vendor", ifname);
		if (read_file(path, vbuf, sizeof(vbuf)) <= 0)
			goto fallback;
		snprintf(path, sizeof(path), "/sys/class/net/%s/device/device", ifname);
		if (read_file(path, dbuf, sizeof(dbuf)) <= 0)
			goto fallback;

		if (sscanf(vbuf, "%hx", &vendor_id) == 1 &&
		    sscanf(dbuf, "%hx", &device_id) == 1 &&
		    pci_ids_lookup(vendor_id, device_id, buf, len))
			return;

		/* pci.ids lookup failed — fall back to vendor table */
		const char *name = lookup_vendor(vendor_id, false);
		if (name) {
			snprintf(buf, len, "%s (0x%04x)", name, device_id);
			return;
		}
		snprintf(buf, len, "0x%04x:0x%04x", vendor_id, device_id);
		return;
	}

fallback:
	snprintf(buf, len, "unknown");
}

void if_check_driver_quirks(const char *ifname)
{
	char drv[32], path[256], val[8];

	if_get_driver(ifname, drv, sizeof(drv));

	if (strcmp(drv, "carl9170") == 0) {
		snprintf(path, sizeof(path),
			 "/sys/module/carl9170/parameters/nohwcrypt");
		if (read_file(path, val, sizeof(val)) > 0 && val[0] == 'N')
			err_quit("carl9170 on %s: hardware crypto is enabled.\n"
				 "This causes instability. Disable it:\n\n"
				 "  sudo modprobe -r carl9170 && sudo modprobe carl9170 nohwcrypt=1\n\n"
				 "To make it permanent:\n"
				 "  echo 'options carl9170 nohwcrypt=1' | sudo tee /etc/modprobe.d/carl9170.conf",
				 ifname);
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

		if (!ai->count || (ai == &info->v6 && rtnl_addr_get_prefixlen(addr) < 128)) {
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
