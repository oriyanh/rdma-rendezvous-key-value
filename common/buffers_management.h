#ifndef __BUFFERS_MANAGEMENT_H__
#define __BUFFERS_MANAGEMENT_H__

#include "constants.h"

void* get_send_buffer(struct kv_context *ctx, int buff_id);

void* get_recv_buffer(struct kv_context *ctx, int buff_id);

void release_send_buffer(struct kv_context *ctx, int buff_id);

void acquire_send_buffer(struct kv_context *ctx, int buff_id);

void release_receive_buffer(struct kv_context *ctx, int buff_id);

void acquire_receive_buffer(struct kv_context *ctx, int buff_id);

void free_send_buffer(struct kv_context *ctx, int buff_id);

int get_free_recv_idx(struct kv_context *ctx);

int get_free_send_idx(struct kv_context *ctx);

#endif // __BUFFERS_MANAGEMENT_H__