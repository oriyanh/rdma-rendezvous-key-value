#ifndef __CONNECTION_H__
#define __CONNECTION_H__

#include <stdio.h>
#include <infiniband/verbs.h>

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
#include <stdbool.h>
#include "constants.h"

int get_port_info(struct ibv_context *context, int port, struct ibv_port_attr *attr);
void wire_gid_to_gid(const char *wgid, union ibv_gid *gid);
void gid_to_wire_gid(const union ibv_gid *gid, char wgid[]);
int connect_ctx(struct ibv_qp *qp,  int my_psn, struct kv_dest *rem_dest);
struct kv_context *server_init_ctx(struct ibv_device *ib_dev, int in_size, int out_size, int max_recv_buf, int max_send_buf, size_t page_size);
struct kv_context *client_init_ctx(struct ibv_device *ib_dev, int in_size, int out_size, int rx_depth, int tx_depth, int port, int use_event, size_t page_size);
void print_client_request(void *request);

#endif // __CONNECTION_H__