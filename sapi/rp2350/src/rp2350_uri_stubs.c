#include "php.h"
#include "ext/uri/php_uri.h"

static PHP_MINIT_FUNCTION(rp2350_uri_stub)
{
	(void)type;
	(void)module_number;
	return SUCCESS;
}

zend_module_entry uri_module_entry = {
	STANDARD_MODULE_HEADER,
	"uri",
	NULL,
	PHP_MINIT(rp2350_uri_stub),
	NULL,
	NULL,
	NULL,
	NULL,
	NO_VERSION_YET,
	STANDARD_MODULE_PROPERTIES
};

static void *rp2350_uri_parse_stub(const char *uri_str, size_t uri_str_len, const void *base_url, zval *errors, bool silent)
{
	(void)uri_str;
	(void)uri_str_len;
	(void)base_url;
	(void)errors;
	(void)silent;
	return NULL;
}

static void *rp2350_uri_clone_stub(void *uri)
{
	(void)uri;
	return NULL;
}

static zend_string *rp2350_uri_to_string_stub(void *uri, php_uri_recomposition_mode mode, bool exclude_fragment)
{
	(void)uri;
	(void)mode;
	(void)exclude_fragment;
	return NULL;
}

static void rp2350_uri_destroy_stub(void *uri)
{
	(void)uri;
}

static const php_uri_parser rp2350_uri_parser_stub = {
	.name = PHP_URI_PARSER_PHP_PARSE_URL,
	.parse = rp2350_uri_parse_stub,
	.clone = rp2350_uri_clone_stub,
	.to_string = rp2350_uri_to_string_stub,
	.destroy = rp2350_uri_destroy_stub,
};

PHPAPI zend_result php_uri_parser_register(const php_uri_parser *uri_parser)
{
	(void)uri_parser;
	return SUCCESS;
}

PHPAPI const php_uri_parser *php_uri_get_parser(zend_string *uri_parser_name)
{
	(void)uri_parser_name;
	return &rp2350_uri_parser_stub;
}

ZEND_ATTRIBUTE_NONNULL PHPAPI php_uri *php_uri_parse_to_struct(
		const php_uri_parser *uri_parser, const char *uri_str, size_t uri_str_len, php_uri_component_read_mode read_mode, bool silent
)
{
	(void)uri_parser;
	(void)uri_str;
	(void)uri_str_len;
	(void)read_mode;
	(void)silent;
	return NULL;
}

ZEND_ATTRIBUTE_NONNULL PHPAPI void php_uri_struct_free(php_uri *uri)
{
	(void)uri;
}
