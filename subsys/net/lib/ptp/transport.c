/*
 * Copyright (c) 2024 BayLibre SAS
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_ptp_transport, CONFIG_PTP_LOG_LEVEL);

#include <zephyr/net/socket.h>

#include "transport.h"

#define INTERFACE_NAME_LEN (32)
#define SOCKET_EVENT_PORT (319)
#define SOCKET_GENERAL_PORT (320)

#define IP_MULTICAST_IP "224.0.1.129"
#define IP6_MULTICAST_IP "FF0E:0:0:0:0:0:0:181"

static int socket_open(struct net_if *iface, struct sockaddr *addr, const char *name)
{
	static const int feature_on = 1;
	int socket = zsock_socket(PF_PACKET, SOCK_DGRAM, IPPROTO_UDP);

	if (net_if_get_by_iface(port->iface) < 0) {
		LOG_ERR("Failed to obtain interface index");
		return -1;
	}

	if (socket < 0) {
		LOG_ERR("Failed to open socket");
		return -1;
	}

	if (zsock_setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &feature_on, sizeof(feature_on))) {
		LOG_ERR("Failed to set REUSEADDR socket option");
		goto error;
	}

	if (zsock_bind(socket, &addr, sizeof(*addr))) {
		LOG_ERR("Faild to bind socket");
		goto error;
	}

	if (zsock_setsockopt(socket, SOL_SOCKET, SO_BINDTODEVICE, name, strlen(name))) {
		LOG_ERR("Failed to set socket binding to an interface");
		goto error;
	}

	return 0;
error:
	zsock_close(socket);
	return -1;
}

#if CONFIG_PTP_UDP_IPv4_PROTOCOL
static int transport_udp_open(struct net_if *iface, uint16_t port)
{
	uint8_t tos;
	int socket, ret, ttl = 1;
	char buf[INTERFACE_NAME_LEN];
	struct in_addr mcast_addr;
	struct ip_mreqn mreqn;
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_addr = htonl(INADDR_ANY),
		.sin_port = htons(port),
	};

	ret = net_if_get_name(iface, buf, INTERFACE_NAME_LEN);
	socket = socket_open(iface, &addr, ret == -ENOTSUP ? NULL : buf);

	if (socket < 0) {
		return -1;
	}

	if (zsock_setsockopt(socket, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl))) {
		LOG_ERR("Failed to set ip multicast ttl socket option");
		goto error;
	}

	if (net_addr_pton(addr.sin_addr, IP_MULTICAST_IP, &mcast_addr)) {
		LOG_ERR("Couldn't resolve multicast IP address");
		goto error;
	}

	memcpy(&mreqn.imr_multiaddr, mcast_addr, sizeof(struct in_addr));
	mreqn.imr_ifindex = net_if_get_by_iface(iface);
	mreqn.imr_address = addr.sin_addr;

	if (zsock_setsockopt(socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreqn, sizeof(mreqn))) {
		LOG_ERR("Failed to join multicast group");
		goto error;
	}

	if (zsock_getsockopt(socket, IPPROTO_IP, IP_TOS, &tos, sizeof(tos))) {
		tos = 0;
	}

	tos &= ~0xFC;
	tos = CONFIG_PTP_DSCP_VALUE << 2;

	if (zsock_setsockopt(socket, IPPROTO_IP, IP_TOS, &tos, sizeof(tos))) {
		LOG_WRN("Failed to set DSCP priority");
	}

	return socket;
error:
	zsock_close(socket);
	return -1;
}
#elif CONFIG_PTP_UDP_IPv6_PROTOCOL
static int transport_udp_open(struct net_if *iface, uint16_t port)
{
	uint8_t tclass;
	int socket, ret, hops = 1;
	char buf[INTERFACE_NAME_LEN];
	struct in6_addr mcast_addr;
	struct ipv6_mreq mreqn;
	struct sockaddr_in6 addr = {
		.sin6_family = AF_INET6,
		.sin6_addr = htonl(INADDR_ANY),
		.sin6_port = htons(port),
	};

	ret = net_if_get_name(iface, buf, INTERFACE_NAME_LEN);
	socket = socket_open(iface, &addr, ret == -ENOTSUP ? NULL : buf);

	if (socket < 0) {
		return -1;
	}

	if (zsock_setsockopt(socket, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hops, sizeof(hops))) {
		LOG_ERR("Failed to set ip multicast hops socket option");
		goto error;
	}

	if (net_addr_pton(addr.sin_addr, IP6_MULTICAST_IP, &mcast_addr)) {
		LOG_ERR("Couldn't resolve multicast IP address");
		goto error;
	}

	memcpy(&mreqn.ipv6mr_multiaddr, mcast_addr, sizeof(struct in6_addr));
	mreqn.ipv6mr_ifindex = net_if_get_by_iface(iface);
	mreqn.

	if (zsock_setsockopt(socket, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &mreqn, sizeof(mreqn))) {
		LOG_ERR("Failed to join multicast group");
		goto error;
	}

	if (zsock_getsockopt(socket, IPPROTO_IPV6, IPV6_TCLASS, &tclass, sizeof(tclass))) {
		tclass = 0;
	}

	tclass &= ~0xFC;
	tclass = CONFIG_PTP_DSCP_VALUE << 2;

	if (zsock_setsockopt(socket, IPPROTO_IPV6, IPV6_TCLASS, &tclass, sizeof(tclass))) {
		LOG_WRN("Failed to set priority");
	}

	return socket;
error:
	zsock_close(socket);
	return -1;
}
#else
#error "Choosen PTP transport protocol not implemented"
#endif

int ptp_transport_open(struct ptp_port *port)
{
	int socket = transport_udp_open(port->iface, SOCKET_EVENT_PORT);

	if (socket == -1) {
		return -1;
	}

	port->socket = socket;
	return 0;
}

int ptp_transport_close(struct ptp_port *port)
{
	if (port->socket >= 0) {
		if (zsock_close(port->socket)) {
			LOG_ERR("Failed to close socket on PTP Port %d",
				port->port_ds.id.port_number);
			return -1;
		}
	}

	port->socket = -1;
	return 0;
}

int ptp_transport_send(struct ptp_port *port, struct ptp_msg *msg)
{
	return 0;
}

int ptp_transport_sendto(struct ptp_port *port, struct ptp_msg *msg)
{
	return 0;
}

int ptp_transport_send_peer(struct ptp_port *port, struct ptp_msg *msg)
{
	return 0;
}

int ptp_transport_recv(struct ptp_port *port, struct ptp_msg *msg)
{
	return 0;
}

int ptp_transport_protocol_addr(struct ptp_port *port, uint8_t *addr)
{
	int length = 0;

	if (IS_ENABLED(CONFIG_PTP_IPv4_PROTOCOL)) {
		struct in_addr ip = net_if_ipv4_get_global_addr(port->iface, NET_ADDR_PREFERRED);

		length = NET_IPV4_ADDR_SIZE;
		*addr = ip.s_addr;
	} else if (IS_ENABLED(CONFIG_PTP_IPv6_PROTOCOL)) {
		struct in6_addr ip = net_if_ipv6_get_global_addr(NET_ADDR_PREFERRED, &port->iface);

		length = NET_IPV6_ADDR_SIZE;
		memcpy(addr, &ip, length);
	}

	return length;
}

struct net_linkaddr *ptp_transport_physical_addr(struct ptp_port *port)
{
	return net_if_get_link_addr(port->iface);
}
