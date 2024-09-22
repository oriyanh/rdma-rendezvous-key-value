#ifndef __CLIENT_LIB_H__
#define __CLIENT_LIB_H__

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/time.h>
#include <stdlib.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include "common/connection.h"
#include "common/constants.h"

/*
 * Main Client API
 */
 int kv_open(char *servername, void **kv_handle); /*Connect to server*/
 int kv_set(void *kv_handle, const char *key, const char *value);
 int kv_get(void *kv_handle, const char *key, char **value);
 void kv_release(char *value);/* Called after get() on value pointer */
 int kv_close(void *kv_handle); /* Destroys the QP */

 /*
  * Inner Functions
  */
int client_post_send(struct kv_context *ctx, uint32_t size, int buff_id, char *out_buff);
int client_post_rdma_read(struct kv_context *ctx, uint32_t lkey, uint32_t size, char *laddr, void *raddr, uint32_t rkey, uint16_t tid);
int client_post_rdma_write(struct kv_context *ctx, uint32_t lkey, uint32_t size, char *laddr, void *raddr, uint32_t rkey, uint16_t tid);
void client_send_rdma_fin(struct kv_context *ctx, uint16_t tid);
void client_handle_recv_rdma_rkey_get(struct kv_context *ctx, struct server_response_t *response);
void client_handle_recv_rdma_rkey_set(struct kv_context *ctx, struct server_response_t *response);
void client_handle_get_response(struct server_response_t *response);
void client_handle_recv(struct kv_context *ctx, int buff_id);
struct kv_dest *client_exch_dest(struct kv_context *ctx, const char *servername, int port, const struct kv_dest *my_dest);
int send_client_shutdown_message(struct kv_context *ctx);

/*
 * Client Input Functions
 */
int check_for_input_validity(const char *filename);
void execute_jobs_according_to_input_file(struct kv_context *ctx, const char *filename);
int client_post_recv_buffers(struct kv_context *ctx, int num_buffers);
int get_new_tid(void);
int client_wait_completions(struct kv_context *ctx, int iters);
int client_loop(char * servername, char * input_file);



#endif // __CLIENT_LIB_H__
