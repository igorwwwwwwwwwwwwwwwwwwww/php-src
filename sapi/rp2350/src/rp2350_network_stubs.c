#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "php.h"
#include "main/php_network.h"
#include "main/php_streams.h"

#include "rp2350_eval.h"

typedef struct {
	char host[96];
	uint16_t port;
	char *tx;
	size_t tx_len;
	size_t tx_cap;
	char *rx;
	size_t rx_len;
	size_t rx_pos;
	int timeout_ms;
} rp2350_tcp_stream_t;

static int rp2350_tcp_parse_endpoint(const char *name, char *host, size_t host_size, uint16_t *port)
{
	const char *prefix = "tcp://";
	const char *p = name;
	const char *host_start;
	const char *colon;
	size_t host_len;
	char *endptr;
	unsigned long port_ul;

	if (strncmp(p, prefix, strlen(prefix)) != 0) {
		return 0;
	}
	p += strlen(prefix);
	host_start = p;
	colon = strchr(host_start, ':');
	if (!colon) {
		return 0;
	}
	host_len = (size_t)(colon - host_start);
	if (host_len == 0 || host_len + 1 > host_size) {
		return 0;
	}
	memcpy(host, host_start, host_len);
	host[host_len] = '\0';

	port_ul = strtoul(colon + 1, &endptr, 10);
	if (*endptr != '\0' || port_ul == 0 || port_ul > 65535) {
		return 0;
	}
	*port = (uint16_t)port_ul;
	return 1;
}

static int rp2350_tcp_stream_fetch(rp2350_tcp_stream_t *s)
{
	if (s->rx != NULL) {
		return 1;
	}
	if (!rp2350_net_tcp_request(
		s->host,
		s->port,
		s->tx ? s->tx : "",
		s->tx ? s->tx_len : 0,
		(uint32_t)(s->timeout_ms > 0 ? s->timeout_ms : 8000),
		131072,
		&s->rx,
		&s->rx_len
	)) {
		return 0;
	}
	s->rx_pos = 0;
	return 1;
}

static ssize_t rp2350_sock_write(php_stream *stream, const char *buf, size_t count)
{
	rp2350_tcp_stream_t *s = (rp2350_tcp_stream_t *)stream->abstract;
	size_t needed;
	char *new_tx;

	if (!s) {
		return -1;
	}
	needed = s->tx_len + count;
	if (needed > s->tx_cap) {
		size_t new_cap = s->tx_cap ? s->tx_cap : 256;
		while (new_cap < needed) {
			new_cap *= 2;
		}
		new_tx = (char *)realloc(s->tx, new_cap);
		if (!new_tx) {
			return -1;
		}
		s->tx = new_tx;
		s->tx_cap = new_cap;
	}
	memcpy(s->tx + s->tx_len, buf, count);
	s->tx_len += count;
	return (ssize_t)count;
}

static ssize_t rp2350_sock_read(php_stream *stream, char *buf, size_t count)
{
	rp2350_tcp_stream_t *s = (rp2350_tcp_stream_t *)stream->abstract;
	size_t remaining;
	size_t n;

	if (!s) {
		return -1;
	}
	if (!rp2350_tcp_stream_fetch(s)) {
		stream->eof = 1;
		return 0;
	}
	if (s->rx_pos >= s->rx_len) {
		stream->eof = 1;
		return 0;
	}
	remaining = s->rx_len - s->rx_pos;
	n = (count < remaining) ? count : remaining;
	memcpy(buf, s->rx + s->rx_pos, n);
	s->rx_pos += n;
	if (s->rx_pos >= s->rx_len) {
		stream->eof = 1;
	}
	return (ssize_t)n;
}

static int rp2350_sock_close(php_stream *stream, int close_handle)
{
	rp2350_tcp_stream_t *s = (rp2350_tcp_stream_t *)stream->abstract;
	(void)close_handle;
	if (s) {
		free(s->tx);
		free(s->rx);
		free(s);
		stream->abstract = NULL;
	}
	return 0;
}

static int rp2350_sock_flush(php_stream *stream)
{
	(void)stream;
	return 0;
}

static int rp2350_sock_set_option(php_stream *stream, int option, int value, void *ptrparam)
{
	rp2350_tcp_stream_t *s = (rp2350_tcp_stream_t *)stream->abstract;
	(void)value;
	if (!s) {
		return PHP_STREAM_OPTION_RETURN_ERR;
	}
	if (option == PHP_STREAM_OPTION_READ_TIMEOUT && ptrparam) {
		struct timeval *tv = (struct timeval *)ptrparam;
		long ms = (tv->tv_sec * 1000L) + (tv->tv_usec / 1000L);
		if (ms < 100) {
			ms = 100;
		}
		s->timeout_ms = (int)ms;
		return PHP_STREAM_OPTION_RETURN_OK;
	}
	if (option == PHP_STREAM_OPTION_BLOCKING) {
		return PHP_STREAM_OPTION_RETURN_OK;
	}
	return PHP_STREAM_OPTION_RETURN_NOTIMPL;
}

PHPAPI const php_stream_ops php_stream_socket_ops = {
	rp2350_sock_write,
	rp2350_sock_read,
	rp2350_sock_close,
	rp2350_sock_flush,
	"rp2350-tcp",
	NULL,
	NULL,
	NULL,
	rp2350_sock_set_option
};

PHPAPI php_stream *_php_stream_sock_open_from_socket(php_socket_t socket, const char *persistent_id STREAMS_DC)
{
	(void)socket;
	(void)persistent_id;
	return NULL;
}

PHPAPI php_stream *_php_stream_sock_open_host(const char *host, unsigned short port,
		int socktype, struct timeval *timeout, const char *persistent_id STREAMS_DC)
{
	char name[160];
	(void)socktype;
	(void)timeout;
	(void)persistent_id;
	snprintf(name, sizeof(name), "tcp://%s:%u", host, (unsigned)port);
	return _php_stream_xport_create(
		name,
		strlen(name),
		0,
		STREAM_XPORT_CLIENT | STREAM_XPORT_CONNECT,
		persistent_id,
		timeout,
		NULL,
		NULL,
		NULL
		STREAMS_CC
	);
}

PHPAPI zend_result php_network_parse_network_address_with_port(const char *addr,
		size_t addrlen, struct sockaddr *sa, socklen_t *sl)
{
	(void)addr;
	(void)addrlen;
	(void)sa;
	(void)sl;
	return FAILURE;
}

PHPAPI void _php_emit_fd_setsize_warning(int max_fd)
{
	(void)max_fd;
}

PHPAPI php_stream *_php_stream_xport_create(const char *name, size_t namelen, int options,
		int flags, const char *persistent_id,
		struct timeval *timeout,
		php_stream_context *context,
		zend_string **error_string,
		int *error_code
		STREAMS_DC)
{
	rp2350_tcp_stream_t *s = NULL;
	php_stream *stream = NULL;
	const char *xname = name;

	(void)namelen;
	(void)options;
	(void)flags;
	(void)persistent_id;
	(void)context;

	if (strncmp(xname, "tcp://", 6) != 0) {
		if (error_string) {
			*error_string = NULL;
		}
		if (error_code) {
			*error_code = ENOSYS;
		}
		return NULL;
	}

	s = (rp2350_tcp_stream_t *)calloc(1, sizeof(*s));
	if (!s) {
		if (error_code) {
			*error_code = ENOMEM;
		}
		return NULL;
	}
	s->timeout_ms = timeout ? php_tvtoto(timeout) : 8000;
	if (s->timeout_ms < 100) {
		s->timeout_ms = 100;
	}

	if (!rp2350_tcp_parse_endpoint(xname, s->host, sizeof(s->host), &s->port)) {
		free(s);
		if (error_code) {
			*error_code = EINVAL;
		}
		return NULL;
	}

	stream = php_stream_alloc_rel(&php_stream_socket_ops, s, persistent_id, "r+b");
	if (!stream) {
		free(s);
		if (error_code) {
			*error_code = ENOMEM;
		}
		return NULL;
	}
	return stream;
}

PHPAPI int php_stream_xport_accept(php_stream *stream, php_stream **client,
		zend_string **textaddr,
		void **addr, socklen_t *addrlen,
		struct timeval *timeout,
		zend_string **error_text)
{
	(void)stream;
	(void)client;
	(void)textaddr;
	(void)addr;
	(void)addrlen;
	(void)timeout;
	(void)error_text;
	return FAILURE;
}

PHPAPI int php_stream_xport_get_name(php_stream *stream, int want_peer,
		zend_string **textaddr,
		void **addr, socklen_t *addrlen)
{
	rp2350_tcp_stream_t *s = stream ? (rp2350_tcp_stream_t *)stream->abstract : NULL;
	(void)want_peer;
	(void)addr;
	(void)addrlen;
	if (!s || !textaddr) {
		return FAILURE;
	}
	*textaddr = strpprintf(0, "%s:%u", s->host, (unsigned)s->port);
	return SUCCESS;
}

PHPAPI int php_stream_xport_recvfrom(php_stream *stream, char *buf, size_t buflen,
		int flags, void **addr, socklen_t *addrlen,
		zend_string **textaddr)
{
	(void)flags;
	(void)addr;
	(void)addrlen;
	(void)textaddr;
	return (int)rp2350_sock_read(stream, buf, buflen);
}

PHPAPI int php_stream_xport_sendto(php_stream *stream, const char *buf, size_t buflen,
		int flags, void *addr, socklen_t addrlen)
{
	(void)flags;
	(void)addr;
	(void)addrlen;
	return (int)rp2350_sock_write(stream, buf, buflen);
}

PHPAPI int php_stream_xport_shutdown(php_stream *stream, stream_shutdown_t how)
{
	(void)stream;
	(void)how;
	return SUCCESS;
}

PHPAPI int php_stream_xport_crypto_setup(php_stream *stream, php_stream_xport_crypt_method_t crypto_method, php_stream *session_stream)
{
	(void)stream;
	(void)crypto_method;
	(void)session_stream;
	return FAILURE;
}

PHPAPI int php_stream_xport_crypto_enable(php_stream *stream, int activate)
{
	(void)stream;
	(void)activate;
	return FAILURE;
}
