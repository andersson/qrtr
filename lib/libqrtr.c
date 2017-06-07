#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <poll.h>

#include "qrtr.h"
#include "libqrtr.h"
#include "ns.h"

#define LOGW(fmt, ...) do { fprintf(stderr, "W|qrtr: " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOGE(fmt, ...) do { fprintf(stderr, "E|qrtr: " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOGE_errno(fmt, ...) do { fprintf(stderr, "E|qrtr: " fmt ": %s\n", ##__VA_ARGS__, strerror(errno)); } while (0)

static int qrtr_getname(int sock, struct sockaddr_qrtr *sq)
{
	socklen_t sl = sizeof(*sq);
	int rc;

	rc = getsockname(sock, (void *)sq, &sl);
	if (rc) {
		LOGE_errno("getsockname()");
		return -1;
	}

	if (sq->sq_family != AF_QIPCRTR || sl != sizeof(*sq))
		return -1;

	return 0;
}

int qrtr_open(int rport)
{
	struct timeval tv;
	int sock;
	int rc;

	sock = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
	if (sock < 0) {
		LOGE_errno("socket(AF_QIPCRTR)");
		return -1;
	}

	tv.tv_sec = 1;
	tv.tv_usec = 0;

	rc = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	if (rc) {
		LOGE_errno("setsockopt(SO_RCVTIMEO)");
		goto err;
	}

	if (rport != 0) {
		struct sockaddr_qrtr sq;

		sq.sq_family = AF_QIPCRTR;
		sq.sq_node = 1;
		sq.sq_port = rport;

		rc = bind(sock, (void *)&sq, sizeof(sq));
		if (rc < 0) {
			LOGE_errno("bind(%d)", rport);
			goto err;
		}
	}

	return sock;
err:
	close(sock);
	return -1;
}

void qrtr_close(int sock)
{
	close(sock);
}

int qrtr_sendto(int sock, uint32_t node, uint32_t port, const void *data, unsigned int sz)
{
	struct sockaddr_qrtr sq;
	int rc;

	sq.sq_family = AF_QIPCRTR;
	sq.sq_node = node;
	sq.sq_port = port;

	rc = sendto(sock, data, sz, 0, (void *)&sq, sizeof(sq));
	if (rc < 0) {
		LOGE_errno("sendto()");
		return -1;
	}

	return 0;
}

int qrtr_new_server(int sock, uint32_t service, uint16_t version, uint16_t instance)
{
	struct qrtr_ctrl_pkt pkt;
	struct sockaddr_qrtr sq;

	if (qrtr_getname(sock, &sq))
		return -1;

	memset(&pkt, 0, sizeof(pkt));

	if (!sq.sq_port) {
		LOGE("unable to register server on unbound port");
		return -1;
	}

	pkt.cmd = cpu_to_le32(QRTR_CMD_NEW_SERVER);
	pkt.server.service = cpu_to_le32(service);
	pkt.server.instance = cpu_to_le32(instance << 16 | version);
	pkt.server.node = cpu_to_le32(sq.sq_node);
	pkt.server.port = cpu_to_le32(sq.sq_port);

	return qrtr_sendto(sock, sq.sq_node, QRTR_CTRL_PORT, &pkt, sizeof(pkt));
}

int qrtr_remove_server(int sock, uint32_t service, uint16_t version, uint16_t instance)
{
	struct qrtr_ctrl_pkt pkt;
	struct sockaddr_qrtr sq;

	if (qrtr_getname(sock, &sq))
		return -1;

	memset(&pkt, 0, sizeof(pkt));

	pkt.cmd = cpu_to_le32(QRTR_CMD_DEL_SERVER);
	pkt.server.service = cpu_to_le32(service);
	pkt.server.instance = cpu_to_le32(instance << 16 | version);
	pkt.server.node = cpu_to_le32(sq.sq_node);
	pkt.server.port = cpu_to_le32(sq.sq_port);

	return qrtr_sendto(sock, sq.sq_node, QRTR_CTRL_PORT, &pkt, sizeof(pkt));
}

int qrtr_publish(int sock, uint32_t service, uint16_t version, uint16_t instance)
{
	return qrtr_new_server(sock, service, version, instance);
}

int qrtr_bye(int sock, uint32_t service, uint16_t version, uint16_t instance)
{
	return qrtr_remove_server(sock, service, version, instance);
}

int qrtr_new_lookup(int sock, uint32_t service, uint16_t version, uint16_t instance)
{
	struct qrtr_ctrl_pkt pkt;
	struct sockaddr_qrtr sq;

	if (qrtr_getname(sock, &sq))
		return -1;

	memset(&pkt, 0, sizeof(pkt));

	if (!sq.sq_port) {
		LOGE("unable to register server on unbound port");
		return -1;
	}

	pkt.cmd = cpu_to_le32(QRTR_CMD_NEW_LOOKUP);
	pkt.server.service = cpu_to_le32(service);
	pkt.server.instance = cpu_to_le32(instance << 16 | version);
	pkt.server.node = cpu_to_le32(sq.sq_node);
	pkt.server.port = cpu_to_le32(sq.sq_port);

	return qrtr_sendto(sock, sq.sq_node, QRTR_CTRL_PORT, &pkt, sizeof(pkt));
}

int qrtr_remove_lookup(int sock, uint32_t service, uint16_t version, uint16_t instance)
{
	struct qrtr_ctrl_pkt pkt;
	struct sockaddr_qrtr sq;

	if (qrtr_getname(sock, &sq))
		return -1;

	memset(&pkt, 0, sizeof(pkt));

	pkt.cmd = cpu_to_le32(QRTR_CMD_DEL_LOOKUP);
	pkt.server.service = cpu_to_le32(service);
	pkt.server.instance = cpu_to_le32(instance << 16 | version);
	pkt.server.node = cpu_to_le32(sq.sq_node);
	pkt.server.port = cpu_to_le32(sq.sq_port);

	return qrtr_sendto(sock, sq.sq_node, QRTR_CTRL_PORT, &pkt, sizeof(pkt));
}

int qrtr_poll(int sock, unsigned int ms)
{
	struct pollfd fds;

	fds.fd = sock;
	fds.revents = 0;
	fds.events = POLLIN | POLLERR;

	return poll(&fds, 1, ms);
}

int qrtr_recv(int sock, void *buf, unsigned int bsz)
{
	int rc;

	rc = recv(sock, buf, bsz, 0);
	if (rc < 0)
		LOGE_errno("recv()");
	return rc;
}

int qrtr_recvfrom(int sock, void *buf, unsigned int bsz, uint32_t *node, uint32_t *port)
{
	struct sockaddr_qrtr sq;
	socklen_t sl;
	int rc;

	sl = sizeof(sq);
	rc = recvfrom(sock, buf, bsz, 0, (void *)&sq, &sl);
	if (rc < 0) {
		LOGE_errno("recvfrom()");
		return rc;
	}
	if (node)
		*node = sq.sq_node;
	if (port)
		*port = sq.sq_port;
	return rc;
}

int qrtr_is_ctrl_addr(struct sockaddr_qrtr *sq)
{
	return sq->sq_port == QRTR_CTRL_PORT;
}

int qrtr_handle_ctrl_msg(struct sockaddr_qrtr *sq,
			 const void *buf,
			 size_t len,
			 struct qrtr_ind_ops *ops,
			 void *data)
{
	const struct qrtr_ctrl_pkt *pkt = buf;
	uint16_t instance;
	uint32_t service;
	uint16_t version;
	uint32_t node;
	uint32_t port;
	int cmd;

	if (len < sizeof(__le32))
		return -EINVAL;

	cmd = le32_to_cpu(pkt->cmd);

	if (cmd == QRTR_CMD_BYE && ops->bye) {
		return ops->bye(sq->sq_node, data);
	} else if (cmd == QRTR_CMD_DEL_CLIENT && ops->del_client) {
		if (len < 3 * sizeof(__le32))
			return -EINVAL;

		return ops->del_client(pkt->client.node, pkt->client.port, data);
	} else if (cmd == QRTR_CMD_NEW_SERVER && ops->new_server) {
		if (len < 5 * sizeof(__le32))
			return -EINVAL;

		node = le32_to_cpu(pkt->server.node);
		port = le32_to_cpu(pkt->server.port);
		service = le32_to_cpu(pkt->server.service);
		version = le32_to_cpu(pkt->server.instance) & 0xffff;
		instance = le32_to_cpu(pkt->server.instance) >> 16;

		return ops->new_server(service, version, instance, node, port, data);
	} else if (cmd == QRTR_CMD_DEL_SERVER && ops->del_server) {
		if (len < 5 * sizeof(__le32))
			return -EINVAL;

		node = le32_to_cpu(pkt->server.node);
		port = le32_to_cpu(pkt->server.port);
		service = le32_to_cpu(pkt->server.service);
		version = le32_to_cpu(pkt->server.instance) & 0xffff;
		instance = le32_to_cpu(pkt->server.instance) >> 16;

		return ops->del_server(service, version, instance, node, port, data);
	}

	return 0;
}
