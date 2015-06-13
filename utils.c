/*
 * General-purpose utilities used by multiple files.
 */
#include "wavemon.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ether.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <sys/socket.h>
#include <linux/if.h>

/* Maximum length of a MAC address: 2 * 6 hex digits, 6 - 1 colons, plus '\0' */
#define MAC_ADDR_MAX	18

/* Print a mac-address, include leading zeroes (unlike ether_ntoa(3)) */
char *ether_addr(const struct ether_addr *ea)
{
	static char mac[MAC_ADDR_MAX];
	char *d = mac, *a = ether_ntoa(ea);
next_chunk:
	if (a[0] == '\0' || a[1] == '\0' || a[1] == ':')
		*d++ = '0';
	while ((*d++ = conf.cisco_mac ? (*a == ':' ? '.' : *a) : toupper(*a)))
		if (*a++ == ':')
			goto next_chunk;
	return mac;
}

/* Print mac-address translation from /etc/ethers if available */
char *ether_lookup(const struct ether_addr *ea)
{
	static char hostname[BUFSIZ];

	if (ether_ntohost(hostname, ea) == 0)
		return hostname;
	return ether_addr(ea);
}

/* Format an Ethernet mac address */
char *mac_addr(const struct sockaddr *sa)
{
	if (sa->sa_family != ARPHRD_ETHER)
		return "00:00:00:00:00:00";
	return ether_lookup((const struct ether_addr *)sa->sa_data);
}

/* Format a (I)BSSID */
char *format_bssid(const struct sockaddr *ap)
{
	uint8_t bcast_addr[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	uint8_t  zero_addr[ETH_ALEN] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	if (memcmp(ap->sa_data, zero_addr, ETH_ALEN) == 0)
		return "Not-Associated";
	if (memcmp(ap->sa_data, bcast_addr, ETH_ALEN) == 0)
		return "Invalid";
	return mac_addr(ap);
}

/* count bits set in @mask the Brian Kernighan way */
uint8_t bit_count(uint32_t mask)
{
	uint8_t bits_set;

	for (bits_set = 0; mask; bits_set++)
		mask &= mask - 1;

	return bits_set;
}

/* netmask = contiguous 1's followed by contiguous 0's */
uint8_t prefix_len(const struct in_addr *netmask)
{
	return bit_count(netmask->s_addr);
}

/* Absolute power measurement in dBm (IW_QUAL_DBM): map into -192 .. 63 range */
int u8_to_dbm(const int power)
{
	return power > 63 ? power - 0x100 : power;
}
uint8_t dbm_to_u8(const int dbm)
{
	return dbm < 0 ? dbm + 0x100 : dbm;
}

/* Convert log dBm values to linear mW */
double dbm2mw(const double in)
{
	return pow(10.0, in / 10.0);
}

char *dbm2units(const double in)
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
double mw2dbm(const double in)
{
	return 10.0 * log10(in);
}






