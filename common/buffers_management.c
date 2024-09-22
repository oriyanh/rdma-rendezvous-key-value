#include "buffers_management.h"
#include <stdio.h>

void* get_send_buffer(struct kv_context *ctx, int buff_id) {
    return ctx->out_buf + (BUFFER_SIZE * buff_id);
}

void* get_recv_buffer(struct kv_context *ctx, int buff_id) {
    return ctx->in_buf + (BUFFER_SIZE * buff_id);
}

void release_send_buffer(struct kv_context *ctx, int buff_id) {
    ctx->in_use_send[buff_id] = false;
}

void acquire_send_buffer(struct kv_context *ctx, int buff_id) {
    ctx->in_use_send[buff_id] = true;
}

void release_receive_buffer(struct kv_context *ctx, int buff_id) {
    ctx->in_use_recv[buff_id] = false;
}

void acquire_receive_buffer(struct kv_context *ctx, int buff_id) {
    ctx->in_use_recv[buff_id] = true;
}

void free_send_buffer(struct kv_context *ctx, int buff_id) {
    char *buff = get_send_buffer(ctx, buff_id);
    memset(buff, 0, BUFFER_SIZE);
    release_send_buffer(ctx, buff_id);
}

int get_free_recv_idx(struct kv_context *ctx) {
    int curr_idx = ctx->last_used_recv_buff;
    bool *in_use = ctx->in_use_recv;
    while (in_use[curr_idx]) {
        ++curr_idx;
        curr_idx %= SERVER_NUM_RECV_BUFFS;
        if (curr_idx == ctx->last_used_recv_buff) {
            fprintf(stderr, "error, no free recv buffers");
            return -1;
        }
    }
    ctx->last_used_recv_buff = curr_idx;
    return curr_idx;
}

int get_free_send_idx(struct kv_context *ctx) {
    int curr_idx = ctx->last_used_send_buff;
    bool *in_use = ctx->in_use_send;
    while (in_use[curr_idx]) {
        ++curr_idx;
        curr_idx %= SERVER_NUM_SEND_BUFFS;
        if (curr_idx == ctx->last_used_send_buff) {
            fprintf(stderr, "error, no free send buffers");
            return -1;
        }
    }
    ctx->last_used_send_buff = curr_idx;
    return curr_idx;
}