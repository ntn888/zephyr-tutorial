/* udp.c - UDP specific code for echo server */

/*
 * Copyright (c) 2017 Intel Corporation.
 * Copyright (c) 2018 Nordic Semiconductor ASA.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_DECLARE(net_echo_server_sample, LOG_LEVEL_DBG);

#include <zephyr.h>
#include <errno.h>
#include <stdio.h>

#include <net/socket.h>
#include <net/tls_credentials.h>

#include "common.h"
//#include "certificate.h"

static void process_udp4(void);

K_THREAD_DEFINE(udp4_thread_id, STACK_SIZE,
		process_udp4, NULL, NULL, NULL,
		THREAD_PRIORITY,
		IS_ENABLED(CONFIG_USERSPACE) ? K_USER : 0, -1);

static int start_udp_proto(struct data *data, struct sockaddr *bind_addr,
			   socklen_t bind_addrlen)
{
	int ret;


	data->udp.sock = socket(bind_addr->sa_family, SOCK_DGRAM, IPPROTO_UDP);

	if (data->udp.sock < 0) {
		NET_ERR("Failed to create UDP socket (%s): %d", data->proto,
			errno);
		return -errno;
	}

	ret = bind(data->udp.sock, bind_addr, bind_addrlen);
	if (ret < 0) {
		NET_ERR("Failed to bind UDP socket (%s): %d", data->proto,
			errno);
		ret = -errno;
	}

	return ret;
}

static int process_udp(struct data *data)
{
	int ret = 0;
	int received;
	struct sockaddr client_addr;
	socklen_t client_addr_len;

	NET_INFO("Waiting for UDP packets on port %d (%s)...",
		 MY_PORT, data->proto);

	do {
		client_addr_len = sizeof(client_addr);
		received = recvfrom(data->udp.sock, data->udp.recv_buffer,
				    sizeof(data->udp.recv_buffer), 0,
				    &client_addr, &client_addr_len);

		if (received < 0) {
			/* Socket error */
			NET_ERR("UDP (%s): Connection error %d", data->proto,
				errno);
			ret = -errno;
			break;
		} else if (received) {
			atomic_add(&data->udp.bytes_received, received);
		}

		ret = sendto(data->udp.sock, data->udp.recv_buffer, received, 0,
			     &client_addr, client_addr_len);
		if (ret < 0) {
			NET_ERR("UDP (%s): Failed to send %d", data->proto,
				errno);
			ret = -errno;
			break;
		}

		if (++data->udp.counter % 1000 == 0U) {
			NET_INFO("%s UDP: Sent %u packets", data->proto,
				 data->udp.counter);
		}

		NET_DBG("UDP (%s): Received and replied with %d bytes",
			data->proto, received);
	} while (true);

	return ret;
}

static void process_udp4(void)
{
	int ret;
	struct sockaddr_in addr4;

	(void)memset(&addr4, 0, sizeof(addr4));
	addr4.sin_family = AF_INET;
	addr4.sin_port = htons(MY_PORT);

	ret = start_udp_proto(&conf.ipv4, (struct sockaddr *)&addr4,
			      sizeof(addr4));
	if (ret < 0) {
		quit();
		return;
	}

	k_work_reschedule(&conf.ipv4.udp.stats_print, K_SECONDS(STATS_TIMER));

	while (ret == 0) {
		ret = process_udp(&conf.ipv4);
		if (ret < 0) {
			quit();
		}
	}
}

static void print_stats(struct k_work *work)
{
	struct data *data = CONTAINER_OF(work, struct data, udp.stats_print);
	int total_received = atomic_get(&data->udp.bytes_received);

	if (total_received) {
		if ((total_received / STATS_TIMER) < 1024) {
			LOG_INF("%s UDP: Received %d B/sec", data->proto,
				total_received / STATS_TIMER);
		} else {
			LOG_INF("%s UDP: Received %d KiB/sec", data->proto,
				total_received / 1024 / STATS_TIMER);
		}

		atomic_set(&data->udp.bytes_received, 0);
	}

	k_work_reschedule(&data->udp.stats_print, K_SECONDS(STATS_TIMER));
}

void start_udp(void)
{
	if (IS_ENABLED(CONFIG_NET_IPV4)) {
		k_work_init_delayable(&conf.ipv4.udp.stats_print, print_stats);
		k_thread_name_set(udp4_thread_id, "udp4");
		k_thread_start(udp4_thread_id);
	}
}

void stop_udp(void)
{
	/* Not very graceful way to close a thread, but as we may be blocked
	 * in recvfrom call it seems to be necessary
	 */
	if (IS_ENABLED(CONFIG_NET_IPV4)) {
		k_thread_abort(udp4_thread_id);
		if (conf.ipv4.udp.sock >= 0) {
			(void)close(conf.ipv4.udp.sock);
		}
	}
}
