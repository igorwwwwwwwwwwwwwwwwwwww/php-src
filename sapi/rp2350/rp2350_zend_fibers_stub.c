#include "Zend/zend_fibers.h"

ZEND_API zend_class_entry *zend_ce_fiber = NULL;

void zend_register_fiber_ce(void)
{
}

void zend_fiber_init(void)
{
}

void zend_fiber_shutdown(void)
{
}

ZEND_API zend_result zend_fiber_start(zend_fiber *fiber, zval *return_value)
{
	(void) fiber;
	(void) return_value;
	return FAILURE;
}

ZEND_API void zend_fiber_resume(zend_fiber *fiber, zval *value, zval *return_value)
{
	(void) fiber;
	(void) value;
	if (return_value) {
		ZVAL_NULL(return_value);
	}
}

ZEND_API void zend_fiber_suspend(zend_fiber *fiber, zval *value, zval *return_value)
{
	(void) fiber;
	(void) value;
	if (return_value) {
		ZVAL_NULL(return_value);
	}
}

ZEND_API zend_result zend_fiber_init_context(zend_fiber_context *context, void *kind, zend_fiber_coroutine coroutine, size_t stack_size)
{
	(void) context;
	(void) kind;
	(void) coroutine;
	(void) stack_size;
	return FAILURE;
}

ZEND_API void zend_fiber_destroy_context(zend_fiber_context *context)
{
	(void) context;
}

ZEND_API void zend_fiber_switch_context(zend_fiber_transfer *transfer)
{
	(void) transfer;
}

#ifdef ZEND_CHECK_STACK_LIMIT
ZEND_API void* zend_fiber_stack_limit(zend_fiber_stack *stack)
{
	(void) stack;
	return NULL;
}

ZEND_API void* zend_fiber_stack_base(zend_fiber_stack *stack)
{
	(void) stack;
	return NULL;
}
#endif

ZEND_API void zend_fiber_switch_block(void)
{
}

ZEND_API void zend_fiber_switch_unblock(void)
{
}

ZEND_API bool zend_fiber_switch_blocked(void)
{
	return false;
}
