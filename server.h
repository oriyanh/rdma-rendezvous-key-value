#ifndef __SERVER_H__
#define __SERVER_H__

#include "common/constants.h"
#include "common/connection.h"
#include "server_internals/db.h"
#include "server_internals/server_requests_management.h"
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

/*
    Connection management functions

    TCP connection handling and usage of 'select' based on IBM implementation:
    https://www.ibm.com/docs/en/i/7.3?topic=designs-example-nonblocking-io-select
*/
int init_server_connection(struct kv_context **ctx);
int accept_new_connection(struct kv_context *ctx);
int create_listen_socket(void);
int query_for_connections(struct kv_context *ctx);
int handle_received_connection_data(char *data, struct kv_context *ctx, int fd);
int send_address_back_to_client(char *msg, char*gid, struct kv_context * ctx, int sockfd_conn);
void teardown_server_connection(struct kv_context *ctx, int retcode);
int server_close_ctx(struct kv_context *ctx);
struct ibv_qp * create_new_qp(struct kv_context *ctx);

/*
    Internal client connections data management functions
*/

connection_t * add_new_connection(struct ibv_qp *qp, int max_recv_buff, int curr_recv_buff);
connection_t * get_connection(uint32_t qp_num);
int remove_connection(struct kv_context *ctx, uint32_t qp_num);
int check_available_connection_slots(void);

/*
    Server request\RDMA management functions
*/
int handle_recv_request(struct kv_context *ctx, wr_id_info info, uint32_t qp_num);
int handle_get_request(struct kv_context *ctx, struct ibv_qp *qp, client_request_headers_t *headers, const char *buffer, wr_id_info info);
int handle_set_request(struct kv_context *ctx, struct ibv_qp *qp, client_request_headers_t *headers, const char *buffer, wr_id_info info);
void handle_pending_request(struct kv_context *ctx);
int server_post_recv_buffers(struct kv_context *ctx, connection_t * connection);
int server_post_send(struct kv_context *ctx, struct ibv_qp *qp, uint32_t size, int buff_id, char *out_buf);
int send_server_shutdown_message(struct kv_context *ctx, struct ibv_qp *qp);
int server_wait_completions(struct kv_context *ctx);
/*
    Server main loop
 */
int server_loop(void);


#endif // __SERVER_H__
