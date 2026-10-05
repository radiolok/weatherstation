/* Blocking HTTP(S) GET, see http_get.h. */
#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/socket.h>

#include <ws/util.h>

#include "http_get.h"

LOG_MODULE_REGISTER(ws_http, LOG_LEVEL_INF);

struct ctx {
	struct ws_http_get *req;
	int err;
};

static int on_response(struct http_response *rsp, enum http_final_call final, void *user)
{
	struct ctx *c = user;
	struct ws_http_get *r = c->req;

	r->status = rsp->http_status_code;
	r->content_length = rsp->content_length;
	if (c->err || r->status != 200) {
		return 0;
	}
	if (rsp->body_frag_start && rsp->body_frag_len) {
		r->received += rsp->body_frag_len;
		if (r->body(rsp->body_frag_start, rsp->body_frag_len, r->received, r->user) < 0) {
			c->err = -ECONNABORTED;
			return -ECONNABORTED;
		}
	}
	return 0;
}

static int connect_to(const struct ws_url *u, const struct ws_http_get *req)
{
	struct zsock_addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
	struct zsock_addrinfo *res = NULL;
	char port[8];
	int sock = -1;
	int ret;

	snprintk(port, sizeof(port), "%u", u->port);
	ret = zsock_getaddrinfo(u->host, port, &hints, &res);
	if (ret || !res) {
		LOG_WRN("%s: name not resolved (%d)", u->host, ret);
		return -EHOSTUNREACH;
	}
	sock = zsock_socket(res->ai_family, SOCK_STREAM, u->tls ? IPPROTO_TLS_1_2 : IPPROTO_TCP);
	if (sock < 0) {
		ret = -errno;
		goto out;
	}
	if (u->tls) {
		int verify = TLS_PEER_VERIFY_REQUIRED;

		if (zsock_setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST, req->tags,
				     req->n_tags * sizeof(sec_tag_t)) ||
		    zsock_setsockopt(sock, SOL_TLS, TLS_HOSTNAME, u->host, strlen(u->host) + 1) ||
		    zsock_setsockopt(sock, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof(verify))) {
			ret = -errno;
			goto out;
		}
	}
	struct zsock_timeval tv = {.tv_sec = req->timeout_ms / 1000,
				   .tv_usec = (req->timeout_ms % 1000) * 1000};

	zsock_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	zsock_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	if (zsock_connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
		ret = -errno;
		LOG_WRN("%s:%u: connect failed (%d)", u->host, u->port, ret);
		goto out;
	}
	zsock_freeaddrinfo(res);
	return sock;
out:
	if (sock >= 0) {
		zsock_close(sock);
	}
	zsock_freeaddrinfo(res);
	return ret;
}

int ws_http_get(struct ws_http_get *req)
{
	static struct ws_url u;
	static uint8_t recv_buf[1024];
	static K_MUTEX_DEFINE(lock);
	struct ctx c = {.req = req};
	int ret;

	req->status = 0;
	req->content_length = 0;
	req->received = 0;
	k_mutex_lock(&lock, K_FOREVER);
	if (ws_url_parse(req->url, &u)) {
		ret = -EINVAL;
		goto out;
	}
	int sock = connect_to(&u, req);

	if (sock < 0) {
		ret = sock;
		goto out;
	}
	struct http_request hr = {
		.method = HTTP_GET,
		.url = u.path,
		.host = u.host,
		.protocol = "HTTP/1.1",
		.response = on_response,
		.recv_buf = recv_buf,
		.recv_buf_len = sizeof(recv_buf),
	};

	ret = http_client_req(sock, &hr, req->timeout_ms, &c);
	zsock_close(sock);
	if (c.err) {
		ret = c.err;
	} else if (ret < 0) {
		LOG_WRN("%s: %d", u.host, ret);
	} else if (req->status != 200) {
		LOG_WRN("%s: HTTP %u", u.host, req->status);
		ret = -EPROTO;
	} else {
		ret = 0;
	}
out:
	k_mutex_unlock(&lock);
	return ret;
}
