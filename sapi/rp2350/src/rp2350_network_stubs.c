#include <errno.h>
#include <stddef.h>

#include "php.h"
#include "main/php_network.h"

PHPAPI const php_stream_ops php_stream_socket_ops = {0};

PHPAPI php_stream *_php_stream_sock_open_from_socket(php_socket_t socket, const char *persistent_id STREAMS_DC)
{
	(void)socket;
	(void)persistent_id;
	return NULL;
}

PHPAPI php_stream *_php_stream_sock_open_host(const char *host, unsigned short port,
		int socktype, struct timeval *timeout, const char *persistent_id STREAMS_DC)
{
	(void)host;
	(void)port;
	(void)socktype;
	(void)timeout;
	(void)persistent_id;
	return NULL;
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
	(void)name;
	(void)namelen;
	(void)options;
	(void)flags;
	(void)persistent_id;
	(void)timeout;
	(void)context;
	if (error_string) {
		*error_string = NULL;
	}
	if (error_code) {
		*error_code = ENOSYS;
	}
	return NULL;
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
	(void)stream;
	(void)want_peer;
	(void)textaddr;
	(void)addr;
	(void)addrlen;
	return FAILURE;
}

PHPAPI int php_stream_xport_recvfrom(php_stream *stream, char *buf, size_t buflen,
		int flags, void **addr, socklen_t *addrlen,
		zend_string **textaddr)
{
	(void)stream;
	(void)buf;
	(void)buflen;
	(void)flags;
	(void)addr;
	(void)addrlen;
	(void)textaddr;
	return -1;
}

PHPAPI int php_stream_xport_sendto(php_stream *stream, const char *buf, size_t buflen,
		int flags, void *addr, socklen_t addrlen)
{
	(void)stream;
	(void)buf;
	(void)buflen;
	(void)flags;
	(void)addr;
	(void)addrlen;
	return -1;
}

PHPAPI int php_stream_xport_shutdown(php_stream *stream, stream_shutdown_t how)
{
	(void)stream;
	(void)how;
	return FAILURE;
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
