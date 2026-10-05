/*
 * MCUmgr (SMP) over UDP for development builds (overlay-smp.conf).
 *
 * Zephyr's own UDP transport waits in a blocking recvfrom(). With the
 * native_sim host sockets (NSOS) a blocking call keeps the descriptor lock,
 * so the answer (sendto() on the same socket from the SMP work queue) waits
 * for the next request. Here the thread waits in poll(), which holds no
 * lock, and reads without blocking: the same code on every board.
 */
#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/mgmt/mcumgr/transport/smp.h>
#include <zephyr/net/socket.h>

/* smp_rx_req(): what every transport calls (no public header) */
#include <mgmt/mcumgr/transport/smp_internal.h>

#include "smp_udp.h"

LOG_MODULE_REGISTER(ws_smp, LOG_LEVEL_INF);

#define SMP_MTU 1024

BUILD_ASSERT(CONFIG_MCUMGR_TRANSPORT_NETBUF_USER_DATA_SIZE >= sizeof(struct sockaddr_in),
	     "the peer address is kept in the buffer user data");
BUILD_ASSERT(CONFIG_MCUMGR_TRANSPORT_NETBUF_SIZE >= SMP_MTU, "a whole datagram in a buffer");

static int sock = -1;
static struct smp_transport transport;
static uint8_t rx_buf[SMP_MTU];
static K_THREAD_STACK_DEFINE(rx_stack, 2048);
static struct k_thread rx_thread;

static int smp_tx(struct net_buf *nb)
{
	const struct sockaddr_in *to = net_buf_user_data(nb);
	int ret =
		zsock_sendto(sock, nb->data, nb->len, 0, (const struct sockaddr *)to, sizeof(*to));

	smp_packet_free(nb);
	return ret < 0 ? MGMT_ERR_EINVAL : MGMT_ERR_EOK;
}

static uint16_t smp_mtu(const struct net_buf *nb)
{
	ARG_UNUSED(nb);
	return SMP_MTU;
}

static int smp_ud_copy(struct net_buf *dst, const struct net_buf *src)
{
	memcpy(net_buf_user_data(dst), net_buf_user_data(src), sizeof(struct sockaddr_in));
	return MGMT_ERR_EOK;
}

static void rx_fn(void *a, void *b, void *c)
{
	for (;;) {
		struct zsock_pollfd p = {.fd = sock, .events = ZSOCK_POLLIN};

		if (zsock_poll(&p, 1, -1) <= 0) {
			k_sleep(K_MSEC(100));
			continue;
		}
		struct sockaddr_in from;
		socklen_t from_len = sizeof(from);
		int len = zsock_recvfrom(sock, rx_buf, sizeof(rx_buf), ZSOCK_MSG_DONTWAIT,
					 (struct sockaddr *)&from, &from_len);

		if (len <= 0) {
			continue;
		}
		struct net_buf *nb = smp_packet_alloc();

		if (!nb) {
			LOG_WRN("no SMP buffer, request dropped");
			continue;
		}
		net_buf_add_mem(nb, rx_buf, len);
		memcpy(net_buf_user_data(nb), &from, sizeof(from));
		smp_rx_req(&transport, nb);
	}
}

int ws_smp_udp_open(uint16_t port)
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(port),
		.sin_addr = {.s_addr = htonl(INADDR_ANY)},
	};
	int ret;

	if (sock >= 0) {
		return 0;
	}
	transport.functions.output = smp_tx;
	transport.functions.get_mtu = smp_mtu;
	transport.functions.ud_copy = smp_ud_copy;
	ret = smp_transport_init(&transport);
	if (ret) {
		return ret;
	}
	int s = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (s < 0) {
		return -errno;
	}
	if (zsock_bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		ret = -errno;
		zsock_close(s);
		return ret;
	}
	sock = s;
	k_thread_create(&rx_thread, rx_stack, K_THREAD_STACK_SIZEOF(rx_stack), rx_fn, NULL, NULL,
			NULL, K_PRIO_PREEMPT(8), 0, K_NO_WAIT);
	k_thread_name_set(&rx_thread, "ws_smp");
	return 0;
}
