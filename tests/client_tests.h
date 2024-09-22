#ifndef __CLIENT_TESTS_H__
#define __CLIENT_TESTS_H__

#include <stdlib.h>
#include "../common/constants.h"


void kv_test_client_side(struct kv_context *ctx, int iters);
void kv_test(struct kv_context *ctx);
void kv_test_long(struct kv_context *ctx);
void kv_test_every_size(struct kv_context *ctx);
void kv_test_single_key(struct kv_context *ctx);

#endif // __CLIENT_TESTS_H__
