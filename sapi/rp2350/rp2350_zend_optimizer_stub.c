#include "Zend/Optimizer/zend_optimizer.h"

ZEND_API void zend_optimize_script(zend_script *script, zend_long optimization_level, zend_long debug_level)
{
	(void) script;
	(void) optimization_level;
	(void) debug_level;
}

ZEND_API int zend_optimizer_register_pass(zend_optimizer_pass_t pass)
{
	(void) pass;
	return 0;
}

ZEND_API void zend_optimizer_unregister_pass(int idx)
{
	(void) idx;
}

zend_result zend_optimizer_startup(void)
{
	return SUCCESS;
}

zend_result zend_optimizer_shutdown(void)
{
	return SUCCESS;
}
