#include "php.h"
#include "ext/lexbor/php_lexbor.h"

static PHP_MINIT_FUNCTION(rp2350_lexbor_stub)
{
	(void)type;
	(void)module_number;
	return SUCCESS;
}

zend_module_entry lexbor_module_entry = {
	STANDARD_MODULE_HEADER,
	"lexbor",
	NULL,
	PHP_MINIT(rp2350_lexbor_stub),
	NULL,
	NULL,
	NULL,
	NULL,
	NO_VERSION_YET,
	STANDARD_MODULE_PROPERTIES
};
