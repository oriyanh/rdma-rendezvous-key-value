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
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <signal.h>

#include "client.h"
#include "common/constants.h"
#include "common/connection.h"
#include "client_internals/client_requests_management.h"
#include "tests/client_tests.h"
#include "common/buffers_management.h"

static int page_size;
static int sockfd_client = -1;
static bool immediate_shutdown = false;
static int client_id = -1;



struct tid_counter {
    union tid {
        uint16_t value;
        struct tid_meta {
            uint8_t client_id : 3;
            uint16_t last_used_tid : 13;
        } meta;
    } tid;
};

static struct tid_counter last_used_tid = {0};


int client_close_ctx(struct kv_context *ctx)
{
    if (ctx->qp) {
        struct ibv_qp_attr attr;
        struct ibv_qp_init_attr init_attr;
        if (ibv_query_qp(ctx->qp, &attr, IBV_QP_STATE, &init_attr) == 0) {
            if (attr.qp_state == IBV_QPS_RTS) {
                fprintf(stdout, "Sending CLIENT_SHUTDOWN\n");
                send_client_shutdown_message(ctx);
            }
            else {
                fprintf(stderr, "QP is in error state\n");
                // Handle the disconnection or error
            }

        }
        fprintf(stderr, "closing client ctx\n");
        if (ibv_destroy_qp(ctx->qp)) {
            fprintf(stderr, "Couldn't destroy QP\n");
        }
    }

    if (ibv_destroy_cq(ctx->cq)) {
        fprintf(stderr, "Couldn't destroy CQ\n");
    }
    
    if (ibv_dereg_mr(ctx->mr_in)) {
        fprintf(stderr, "Couldn't deregister MR\n");
    }


    if (ibv_dereg_mr(ctx->mr_out)) {
        fprintf(stderr, "Couldn't deregister MR\n");
    }

    if (ibv_dealloc_pd(ctx->pd)) {
        fprintf(stderr, "Couldn't deallocate PD\n");
    }

    if (ctx->channel) {
        if (ibv_destroy_comp_channel(ctx->channel)) {
            fprintf(stderr, "Couldn't destroy completion channel\n");
        }
    }

    if (ibv_close_device(ctx->context)) {
        fprintf(stderr, "Couldn't release context\n");
    }

    free(ctx->in_buf);
    free(ctx->out_buf);
    free(ctx);

    return 0;
}


int kv_open(char *servername, void **kv_handle){
    struct ibv_device      **dev_list;
    struct ibv_device       *ib_dev;
    struct kv_context        *ctx = NULL;
    struct kv_dest     my_dest;
    struct kv_dest    *rem_dest;
    int                      port = SERVER_PORT;
    int                      ib_port = 1;
    int                      rx_depth = SERVER_NUM_RECV_BUFFS;
    int                      tx_depth = SERVER_NUM_SEND_BUFFS;
    int                      in_size = SERVER_NUM_RECV_BUFFS * BUFFER_SIZE;
    int                      out_size = SERVER_NUM_SEND_BUFFS * BUFFER_SIZE;
    int                      use_event = 0;
    int                      gidx = -1;
    char                     gid[33];

    srand48(getpid() * time(NULL));
    page_size = sysconf(_SC_PAGESIZE);

    dev_list = ibv_get_device_list(NULL);
    if (!dev_list) {
        perror("Failed to get IB devices list");
        kv_close(ctx);
        exit(1);
    }

    ib_dev = *dev_list;
    if (!ib_dev) {
        fprintf(stderr, "No IB devices found\n");
        ibv_free_device_list(dev_list);
        dev_list = NULL;
        kv_close(ctx);
        exit(1);
    }

    ctx = client_init_ctx(ib_dev, in_size, out_size, rx_depth, tx_depth, ib_port, use_event, page_size);
    ibv_free_device_list(dev_list);
    dev_list = NULL;
    if(!ctx) {
        fprintf(stderr, "Couldn't create client context\n");
        kv_close(ctx);
        exit(1);
    }
    ctx->curr_recv_buff = client_post_recv_buffers(ctx, ctx->max_recv_buff);
    if (ctx->curr_recv_buff < ctx->max_recv_buff) {
        fprintf(stderr, "Couldn't post receive (%d)\n", ctx->curr_recv_buff);
        kv_close(ctx);
        exit(1);
    }
    if (get_port_info(ctx->context, ib_port, &ctx->portinfo)) {
        fprintf(stderr, "Couldn't get port info\n");
        kv_close(ctx);
        exit(1);
    }

    my_dest.lid = ctx->portinfo.lid;
    if (ctx->portinfo.link_layer == IBV_LINK_LAYER_INFINIBAND && !my_dest.lid) {
        fprintf(stderr, "Couldn't get local LID\n");
        kv_close(ctx);
        exit(1);
    }

    if (gidx >= 0) {
        if (ibv_query_gid(ctx->context, ib_port, gidx, &my_dest.gid)) {
            fprintf(stderr, "Could not get local gid for gid index %d\n", gidx);
            kv_close(ctx);
            exit(1);
        }
    } else {
        memset(&my_dest.gid, 0, sizeof my_dest.gid);
    }

    my_dest.qpn = ctx->qp->qp_num;
    my_dest.psn = lrand48() & 0xffffff;
    inet_ntop(AF_INET6, &my_dest.gid, gid, sizeof gid);
    rem_dest = client_exch_dest(ctx, servername, port, &my_dest);
    if (!rem_dest) {
        kv_close(ctx);
        exit(1);
    }

    inet_ntop(AF_INET6, &rem_dest->gid, gid, sizeof gid);
    if (connect_ctx(ctx->qp, my_dest.psn, rem_dest)) {
        fprintf(stderr, "Couldn't connect to remote QP\n");
        free(rem_dest);
        rem_dest = NULL;
        kv_close(ctx);
        exit(1);
    }
    (*kv_handle) = ctx;
    client_pending_requests_init();
    return 0;
}

int kv_get(void *kv_handle, const char *key, char **value) {
    struct kv_context *ctx = (struct kv_context *) kv_handle;
    int buff_idx = get_free_send_idx(ctx);
    while(buff_idx < 0) {
        client_wait_completions(ctx, 1);
        buff_idx = get_free_send_idx(ctx);
    }
    acquire_send_buffer(ctx, buff_idx);
    char *buff = get_send_buffer(ctx, buff_idx);
    struct client_request_headers_t request = {
        .tid = get_new_tid(),
        .opcode = GET_REQUEST_OPCODE,
        .keylen = strlen(key),
        .buflen = 0,
    };
    memcpy(buff, &request, sizeof(client_request_headers_t));
    strcpy(buff + sizeof(client_request_headers_t), key);
    
    add_client_pending_request(request.tid);
    client_pending_request_t *pending_req = get_client_pending_request(request.tid);
    pending_req->is_pending = true;
    strncpy(pending_req->key, key, request.keylen);

    pending_req->opcode = GET_REQUEST_OPCODE;
    if(client_post_send(ctx, sizeof(client_request_headers_t) + strlen(key) + 1, buff_idx, buff)) {
        fprintf(stderr, "Couldn't post send\n");
        return -1;
    }

    while (pending_req->is_pending) {
        client_wait_completions(ctx, 1);
    }
    *value = pending_req->in_buf;
    pending_req->in_buf = NULL;
    remove_client_pending_request(request.tid);
    return 0;
}

int kv_set(void *kv_handle, const char *key, const char *value) {
    struct kv_context *ctx = (struct kv_context *) kv_handle;
    int keylen = strlen(key);
    int buflen = strlen(value);

    int buff_idx = get_free_send_idx(ctx);
    while(buff_idx < 0) {
        client_wait_completions(ctx, 1);
        buff_idx = get_free_send_idx(ctx);
    }
 
    int remaining_requests = num_pending_requests();
    while (remaining_requests >= 250) {
        fprintf(stdout, "too many outstanding requests (%d), waiting for some completions\n", remaining_requests);
        fflush(stdout);
        client_wait_completions(ctx, 200);
        remaining_requests = num_pending_requests();
    }
    struct client_request_headers_t request = {
        .tid = get_new_tid(),
        .opcode = SET_REQUEST_OPCODE,
        .keylen = keylen,
        .buflen = buflen,
    };
    acquire_send_buffer(ctx, buff_idx);
    void *buff = get_send_buffer(ctx, buff_idx);
    memcpy(buff, &request, sizeof(client_request_headers_t));
    strcpy(buff + sizeof(client_request_headers_t), key);

    int buffer_size = keylen + 1;

    if ((buffer_size + buflen + 1) <= CLIENT_REQUEST_PAYLOAD_LEN) {
        // Eager send
        strcpy(buff + sizeof(client_request_headers_t) + buffer_size, value);
        buffer_size += buflen + 1;
    } else {
        // RDMA send
        add_client_pending_request(request.tid);
        client_pending_request_t *pending_req = get_client_pending_request(request.tid);
        pending_req->is_pending = true;
        pending_req->out_size = request.buflen;
        pending_req->out_buf = value;
        strncpy(pending_req->key, key, request.keylen);
        pending_req->mr = ibv_reg_mr(ctx->pd, (void*)pending_req->out_buf, pending_req->out_size + 1,
                                    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
        pending_req->opcode = RDMA_SET_REQUEST_OPCODE;
    }
    if(client_post_send(ctx, sizeof(client_request_headers_t) + buffer_size, buff_idx, buff)) {
        fprintf(stderr, "Couldn't post send (SET)\n");
        return -1;
    }
    return 0;
}

void kv_release(char *value) {
    if (value)
        free(value);
}

int kv_close(void *kv_handle) {
    struct kv_context *ctx = (struct kv_context *) kv_handle;
    if (ctx) {
        int remaining_requests = num_pending_requests();
        while (remaining_requests > 1) {
            if (immediate_shutdown) {
                break;
            }
            client_wait_completions(ctx, 1);
            remaining_requests = num_pending_requests();
        }
    }
    if (sockfd_client >= 0) {
        close(sockfd_client);
    }

    if (ctx) {
        destroy_pending_request_queue();
        client_close_ctx(ctx);
    }
    return 0;
}


int get_new_tid(void){
    ++(last_used_tid.tid.meta.last_used_tid);
    return last_used_tid.tid.value;
}


struct kv_dest *client_exch_dest(struct kv_context *ctx, const char *servername, int port, const struct kv_dest *my_dest)
{
    struct addrinfo *res;
    struct addrinfo hints = {
            .ai_family   = AF_INET,
            .ai_socktype = SOCK_STREAM
    };
    char *service = calloc(6, sizeof(char));
    if (!service) {
        return NULL;
    }

    char msg[sizeof "0000:000000:000000:00000000000000000000000000000000"];
    char msg_in[sizeof "0000:0000:000000:000000:00000000000000000000000000000000"];
    int n;
    struct kv_dest *rem_dest = NULL;
    char gid[33];

    if (sprintf(service, "%d", port) < 0)
        return NULL;

    n = getaddrinfo(servername, service, &hints, &res);

    if (n < 0) {
        fprintf(stderr, "%s for %s:%d\n", gai_strerror(n), servername, port);
        free(service);
        return NULL;
    }

    if (res->ai_next) {
        fprintf(stderr, "Multiple addresses found for %s:%d\n", servername, port);
        free(service);
        return NULL;
    }

    int connect_res = 0;
    while(sockfd_client == -1)
    {
        if (immediate_shutdown) {
            kv_close(ctx);
            exit(EXIT_FAILURE);
        }

        sockfd_client = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sockfd_client >= 0) {
            connect_res = connect(sockfd_client, res->ai_addr, res->ai_addrlen);
            if (!connect_res)
                break;
            close(sockfd_client);
            sockfd_client = -1;
        }

    }
    freeaddrinfo(res);
    free(service);


    gid_to_wire_gid(&my_dest->gid, gid);
    sprintf(msg, "%04x:%06x:%06x:%s", my_dest->lid, my_dest->qpn, my_dest->psn, gid);
    if (write(sockfd_client, msg, sizeof msg) != sizeof msg) {
        perror("Client: write error");
        close(sockfd_client);
        return rem_dest;
    }

    size_t total_read = 0;
    int k = 0;
    while (total_read < sizeof msg_in) {
        k += 1;
        int j = read(sockfd_client, msg_in + total_read, sizeof msg_in - total_read);
        if (j < 0) {
            perror("Client: read error");
            close(sockfd_client);
            return NULL;
        } else if (j == 0) {
            // Connection closed by server
            fprintf(stderr, "Client: server closed connection unexpectedly\n");
            close(sockfd_client);
            return NULL;
        }
        total_read += j;
    }
    printf("Client: read %lu bytes in %d iterations\n", total_read, k);

    write(sockfd_client, "done", sizeof "done");

    rem_dest = calloc(1, sizeof(struct kv_dest));
    if (!rem_dest)
    {
        close(sockfd_client);
        return rem_dest;
    }

    int t;
    sscanf(msg_in, "%04x:%x:%x:%x:%s",&t, &rem_dest->lid, &rem_dest->qpn, &rem_dest->psn, gid);
    printf("Client number is %d\n", t);
    client_id = t;
    last_used_tid.tid.meta.client_id = client_id;
    wire_gid_to_gid(gid, &rem_dest->gid);
    printf("message received: %s\n", msg_in);
    return rem_dest;
    return NULL;
}

int client_post_recv(struct kv_context *ctx, int buff_id)
{
    char *in_buf = get_recv_buffer(ctx, buff_id);
    memset(in_buf, 0, BUFFER_SIZE);
    struct ibv_sge list = {
            .addr	= (uintptr_t) in_buf,
            .length = BUFFER_SIZE,
            .lkey	= ctx->mr_in->lkey
    };

    wr_id_info wr_id = {
            .data = {
                    .wr_id = buff_id,
                    .wr_type = RECV_WRID
            }
    };
    struct ibv_recv_wr wr = {
            .wr_id	    = wr_id.value,
            .sg_list    = &list,
            .num_sge    = 1,
            .next       = NULL
    };
    struct ibv_recv_wr *bad_wr;
    return ibv_post_recv(ctx->qp, &wr, &bad_wr);
}

int client_post_recv_buffers(struct kv_context *ctx, int num_buffers) {
    int rx_outs = 0;
    int buff_idx = 0;
    int i;
    for (i = 0; i < num_buffers; i++) {
        buff_idx = get_free_recv_idx(ctx);
        if (buff_idx < 0) {
            fprintf(stderr, "No free recv buffers\n");
            return rx_outs;
        }
        if (!client_post_recv(ctx, buff_idx)) {
            ++rx_outs;
            acquire_receive_buffer(ctx, buff_idx);
        }
    }

    return rx_outs;
}
int client_post_send(struct kv_context *ctx, uint32_t size, int buff_id, char *out_buff)
{
    struct ibv_sge list = {
            .addr	= (uint64_t) out_buff,
            .length = size,
            .lkey	= ctx->mr_out->lkey
    };

    int send_flags = IBV_SEND_SIGNALED;
    if (ctx->max_inline_data >= size)
        send_flags |= IBV_SEND_INLINE;

    wr_id_info wr_id = {
        .data = {
            .wr_id = buff_id,
            .wr_type = SEND_WRID
        }
    };

    struct ibv_send_wr *bad_wr, wr = {
            .wr_id	    = wr_id.value,
            .sg_list    = &list,
            .num_sge    = 1,
            .opcode     = IBV_WR_SEND,
            .send_flags = send_flags,
            .next       = NULL
    };

    const char *buff = get_send_buffer(ctx, buff_id);
    const client_request_headers_t *headers = (client_request_headers_t *) buff;
    if (headers->opcode == 0) {
        fprintf(stderr, "opcode == 0\n");
        kv_close(ctx);
        exit(1);
    }
    return ibv_post_send(ctx->qp, &wr, &bad_wr);
}


int client_post_rdma_read(struct kv_context *ctx, uint32_t lkey, uint32_t size, char *laddr, void *raddr, uint32_t rkey, uint16_t tid)
{
    struct ibv_sge list = {
            .addr	= (uintptr_t)laddr,
            .length = size,
            .lkey	= lkey
    };

    int send_flags = IBV_SEND_SIGNALED;
    if (ctx->max_inline_data >= size)
        send_flags |= IBV_SEND_INLINE;

    wr_id_info wr_id = {
        .data = {
            .wr_id = tid,
            .wr_type = RDMA_READ_WRID
        }
    };

    struct ibv_send_wr *bad_wr, wr = {
            .wr_id	    = wr_id.value,
            .sg_list    = &list,
            .num_sge    = 1,
            .opcode     = IBV_WR_RDMA_READ,
            .send_flags = send_flags,
            .next       = NULL,
            .wr = {
                .rdma.remote_addr = (uintptr_t) raddr,
                .rdma.rkey = rkey,
            }
    };

    return ibv_post_send(ctx->qp, &wr, &bad_wr);
}

int client_post_rdma_write(struct kv_context *ctx, uint32_t lkey, uint32_t size, char *laddr, void *raddr, uint32_t rkey, uint16_t tid)
{
    struct ibv_sge list = {
            .addr	= (uintptr_t)laddr,
            .length = size,
            .lkey	= lkey
    };

    int send_flags = IBV_SEND_SIGNALED;
    if (ctx->max_inline_data >= size)
        send_flags |= IBV_SEND_INLINE;

    wr_id_info wr_id = {
        .data = {
            .wr_id = tid,
            .wr_type = RDMA_WRITE_WRID
        }
    };

    struct ibv_send_wr *bad_wr, wr = {
            .wr_id	    = wr_id.value,
            .sg_list    = &list,
            .num_sge    = 1,
            .opcode     = IBV_WR_RDMA_WRITE,
            .send_flags = send_flags,
            .next       = NULL,
            .wr = {
                .rdma.remote_addr = (uintptr_t) raddr,
                .rdma.rkey = rkey,
            }
    };

    
    return ibv_post_send(ctx->qp, &wr, &bad_wr);
}

void client_send_rdma_fin(struct kv_context *ctx, uint16_t tid) {
    client_pending_request_t *pending_req = get_client_pending_request(tid);
    int buff_idx = get_free_send_idx(ctx);
    if(buff_idx < 0) {
        fprintf(stderr, "No free send buffers\n");
        kv_close(ctx);
        exit(1);
    }
    acquire_send_buffer(ctx, buff_idx);
    char *buff = get_send_buffer(ctx, buff_idx);

    const char *key = pending_req->key;
    struct client_request_headers_t request = {
        .tid = tid,
        .opcode = RDMA_FIN_CLIENT,
        .keylen = strlen(key),
        .buflen = 0,
    };

    memcpy(buff, &request, sizeof(client_request_headers_t));
    strncpy(buff + sizeof(client_request_headers_t), key, strlen(key));
    client_post_send(ctx, sizeof(client_request_headers_t) + request.keylen, buff_idx, buff);
}


void client_handle_recv_rdma_rkey_get(struct kv_context *ctx, server_response_t *response) {
    uint16_t tid = response->headers.tid;
    client_pending_request_t *pending_req = get_client_pending_request(tid);
    pending_req->in_buf = calloc(1, response->headers.buflen + 1);
    pending_req->mr = ibv_reg_mr(ctx->pd, pending_req->in_buf, response->headers.buflen + 1,
                IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
    pending_req->opcode = RDMA_GET_REQUEST_OPCODE;

    client_post_rdma_read(ctx,
                            pending_req->mr->lkey,
                            response->headers.buflen,
                            pending_req->mr->addr,
                            response->headers.rkey_info.raddr,
                            response->headers.rkey_info.rkey,
                            tid);
}

void client_handle_recv_rdma_rkey_set(struct kv_context *ctx, server_response_t *response) {
    int tid = response->headers.tid;
    client_pending_request_t *pending_req = get_client_pending_request(tid);

    client_post_rdma_write(ctx,
                        pending_req->mr->lkey,
                        pending_req->out_size,
                        pending_req->mr->addr,
                        response->headers.rkey_info.raddr,
                        response->headers.rkey_info.rkey,
                        tid);
}

void client_handle_get_response(server_response_t *response) {
    uint16_t tid = response->headers.tid;
    client_pending_request_t *pending_req = get_client_pending_request(tid);
    char *in_buf;
    
    if (response->headers.buflen > 0) {
        in_buf = calloc(1, response->headers.buflen + 1);
        memcpy(in_buf, response->buffer, response->headers.buflen + 1);
    } else {
        in_buf = calloc(1, 1);
    }
    pending_req->in_buf = in_buf;
    pending_req->is_pending = false;
}

void client_handle_recv(struct kv_context *ctx, int buff_id) {
    char *buff = get_recv_buffer(ctx, buff_id);
    server_response_headers_t *headers = (server_response_headers_t *) buff;
    char *data = buff + sizeof(server_response_headers_t);
    server_response_t response = {
        .headers = *headers,
        .buffer = data,
    };

    switch (headers->response) {
        case GET_RESPONSE:
            client_handle_get_response(&response);
            break;
        case RDMA_RKEY_GET:
            client_handle_recv_rdma_rkey_get(ctx, &response);
            break;
        case RDMA_RKEY_SET:
            client_handle_recv_rdma_rkey_set(ctx, &response);
            break;
        case SERVER_SHUTDOWN:
            fprintf(stderr, "Received SERVER_SHUTDOWN\n");
            immediate_shutdown = true;
            kv_close(ctx);
            exit(1);
            break;
        default:
            fprintf(stderr, "Invalid response type %d\n", headers->response);
    }
}


int client_wait_completions(struct kv_context *ctx, int iters) {
    int rcnt = 0, scnt = 0;
    while (rcnt + scnt < iters) {
        if (immediate_shutdown) {
            kv_close(ctx);
            exit(1);
        }

        struct ibv_wc wc[WC_BATCH];
        int ne, i;

        ne = ibv_poll_cq(ctx->cq, WC_BATCH, wc);
        if (ne < 0) {
            fprintf(stderr, "poll CQ failed %d\n", ne);
            immediate_shutdown = true;
            continue;
        }

        for (i = 0; i < ne; ++i) {
            if (wc[i].status != IBV_WC_SUCCESS) {
                fprintf(stderr, "Failed status %s (%d) for wr_id %d\n",
                        ibv_wc_status_str(wc[i].status),
                        wc[i].status, (int) wc[i].wr_id);
                immediate_shutdown = true;
                kv_close(ctx);
                exit(2);
            }

            wr_id_info info = {
                .value = (uint64_t) wc[i].wr_id
            };

            uint16_t tid = info.data.wr_id;

            client_pending_request_t *pending_req = NULL;
            switch (info.data.wr_type) {
            case SEND_WRID:
                free_send_buffer(ctx, info.data.wr_id);
                ++scnt;
                break;

            case RECV_WRID:
                if (--ctx->curr_recv_buff <= 10) {  // Need to post more recv buffers to recv queue
                    ctx->curr_recv_buff += client_post_recv_buffers(ctx, ctx->max_recv_buff - ctx->curr_recv_buff - 1);
                    if (ctx->curr_recv_buff < (ctx->max_recv_buff - 1)) {
                        fprintf(stderr,
                                "Couldn't post receive (%d)\n",
                                ctx->curr_recv_buff);
                        return 1;
                    }
                }
                client_handle_recv(ctx, info.data.wr_id);
                release_receive_buffer(ctx, info.data.wr_id);
                ++rcnt;
                break;

            case RDMA_READ_WRID:
                client_send_rdma_fin(ctx, tid);
                pending_req = get_client_pending_request(tid);
                pending_req->is_pending = false;
                ++rcnt;
                break;
            case RDMA_WRITE_WRID:
                client_send_rdma_fin(ctx, tid);
                pending_req = get_client_pending_request(tid);
                pending_req->out_buf = NULL;
                remove_client_pending_request(tid);
                ++rcnt;
                break;
            default:
                fprintf(stderr, "Completion for unknown wr_id %d\n",
                        info.data.wr_type);
                return 1;
            }
        }
    }
    return 0;
}

void execute_jobs_according_to_input_file(struct kv_context *ctx, const char *filename) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        fprintf(stderr, "Error opening file %s\n", filename);
        perror("Error code");
        kv_close(ctx);
        exit(1);
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    while ((read = getline(&line, &len, file)) != -1) {
        if (immediate_shutdown) {
            kv_close(ctx);
            exit(1);
        }

        // Remove newline character at the end of the line if present
        if (line[read - 1] == '\n') {
            line[read - 1] = '\0';
            --read;
        }

        // Parse the line based on the first word (SET or GET)
        if (strncmp(line, "GET", 3) == 0) {
            int key_len;
            char *key;

            // Read the key length and key itself
            sscanf(line, "GET,%d,", &key_len);

            // Find the start of the key in the line
            key = strchr(line, ',') + 1;
            key = strchr(key, ',') + 1;

            char *output = NULL;
            kv_get(ctx, key, &output);
            // printf("GET request: key length = %d, key = '%s', value='%.25s...(+%lu more)'\n", key_len, key, output, strlen(output) > 25 ? strlen(output) - 25 : 0);
            kv_release(output);

        } else if (strncmp(line, "SET", 3) == 0) {
            int key_len, value_len;
            char *key, *value;

            // Read the key length
            sscanf(line, "SET,%d,", &key_len);

            // Find the start of the key in the line
            key = strchr(line, ',') + 1;
            key = strchr(key, ',') + 1;

            // Find the start of the value length in the line
            sscanf(key + key_len + 1, "%d,", &value_len);

            // Find the start of the value in the line
            value = strchr(key + key_len + 1, ',') + 1;

            // Place null terminators at the end of the key and value
            key[key_len] = '\0';
            value[value_len] = '\0';

            // Print the SET request details
            kv_set(ctx, key, value);
            // printf("SET request: key length = %lu, key = '%s', value length = %lu, value = '%.25s' (+%lu more)\n",
            //        key_len, key, value_len, value, value_len > 25 ? value_len - 25 : 0);
        } else {
            printf("Invalid line format: %s\n", line);
        }
    }

    free(line);  // Free the dynamically allocated buffer
    fclose(file);
}


static void signal_handler(int signal_num) {
    switch (signal_num) {
        case SIGTERM:
        case SIGINT:
            printf("Caught signal %d\n", signal_num);
            immediate_shutdown = true;
            if (sockfd_client) {
                close(sockfd_client);
            }
            break;
        default:
            break;
    }
}


int send_client_shutdown_message(struct kv_context *ctx) {
    int buff_id = get_free_send_idx(ctx);
    if (buff_id < 0) {
        return -1;
    }

    acquire_send_buffer(ctx, buff_id);
    void *out_buffer = get_send_buffer(ctx, buff_id);

    client_request_headers_t req_headers = {
        .keylen = 0,
        .tid = 0,
        .buflen = 0,
        .opcode = CLIENT_SHUTDOWN_OPCODE,
    };

    fprintf(stderr, "posting CLIENT_SHUTDOWN\n");
    memcpy(out_buffer, &req_headers, sizeof(client_request_headers_t));
    if(client_post_send(ctx, sizeof(client_request_headers_t), buff_id, out_buffer)) {
        fprintf(stderr, "Couldn't post CLIENT_SHUTDOWN\n");
        return -1;
    }
    if (!immediate_shutdown) {
        client_wait_completions(ctx, 1);
    }

    return 0;
}


int check_for_input_validity(const char *filename){
    struct stat buffer;
    // Use stat to get information about the file
    return (stat(filename, &buffer) == 0 && S_ISREG(buffer.st_mode));
}

void kv_test_get_set_from_file(struct kv_context *ctx, const char *filename);

int client_loop(char * servername, char * input_file) {
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    if(!check_for_input_validity(input_file)){
        fprintf(stderr, "Invalid input file\n");
        return EXIT_FAILURE;
    }

    struct kv_context *ctx = NULL;
    kv_open(servername, (void **) &ctx);
    printf("Connected to server\n");

    int num_iters = 10;
    int i = 0;
    while (i < num_iters) {
        kv_test_get_set_from_file(ctx, input_file);
        ++i;
    }
    kv_close(ctx);
    fprintf(stdout, "client end\n");

    return 0;
}


void kv_test_get_set_from_file(struct kv_context *ctx, const char *filename) {
    // Assumes file format is 
    // #request N:
    // SET,KEY_N
    // GET KEY_N
    unsigned long long total_send_len = 0;
    unsigned long long total_read_len = 0;
    struct timespec start_time, end_time;
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        fprintf(stderr, "Error opening file %s\n", filename);
        perror("Error code");
        kv_close(ctx);
        exit(1);
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    int num_lines = 0;
    size_t allocated_size = 50;
    struct request_handle {
        size_t num_requests;
        opcode_t *opcodes;
        char **keys;
        char **values;
    } requests;
    requests.num_requests = 0;
    requests.opcodes = calloc(allocated_size, sizeof(opcode_t));
    requests.keys = calloc(allocated_size, sizeof(char *));
    requests.values = calloc(allocated_size, sizeof(char *));

    read = getline(&line, &len, file);
    while (read > 0) {
        if (immediate_shutdown) {
            kv_close(ctx);
            exit(1);
        }
        

        // Parse the line based on the first word (SET or GET)
        if (strncmp(line, "GET", 3) == 0) {
            int key_len;
            char *key;

            // Read the key length and key itself
            sscanf(line, "GET,%d,", &key_len);

            // Find the start of the key in the line
            key = strchr(line, ',') + 1;
            key = strchr(key, ',') + 1;

            ++requests.num_requests;
            requests.opcodes[num_lines] = GET_REQUEST_OPCODE;
            requests.keys[num_lines] = calloc(1, key_len + 1);
            strncpy(requests.keys[num_lines], key, key_len);
        } else if (strncmp(line, "SET", 3) == 0) {
            int key_len, value_len;
            char *key, *value;

            // Read the key length
            sscanf(line, "SET,%d,", &key_len);

            // Find the start of the key in the line
            key = strchr(line, ',') + 1;
            key = strchr(key, ',') + 1;

            // Find the start of the value length in the line
            sscanf(key + key_len + 1, "%d,", &value_len);

            // Find the start of the value in the line
            value = strchr(key + key_len + 1, ',') + 1;


            ++requests.num_requests;
            requests.opcodes[num_lines] = SET_REQUEST_OPCODE;
            requests.keys[num_lines] = calloc(1, key_len + 1);
            requests.values[num_lines] = calloc(1, value_len + 1);
            strncpy(requests.keys[num_lines], key, key_len);
            strncpy(requests.values[num_lines], value, value_len);

            total_send_len += value_len;
        } else {
            printf("Invalid line format: %s\n", line);
            exit(1);
        }

        if (requests.num_requests == (allocated_size-1)) {
            char **keys = requests.keys;
            char **values = requests.values;
            opcode_t *opcodes = requests.opcodes;
            requests.keys = calloc(allocated_size * 2, sizeof(char*));
            requests.values = calloc(allocated_size * 2, sizeof(char*));
            requests.opcodes = (opcode_t*)calloc(allocated_size * 2, sizeof(opcode_t));
            unsigned int j;
            for (j = 0; j < requests.num_requests; j++) {
                requests.keys[j] = keys[j];
                requests.values[j] = values[j];
                requests.opcodes[j] = opcodes[j];
            }
            free(keys);
            keys = NULL;
            free(values);
            allocated_size *= 2;
        }
        ++num_lines;
        read = getline(&line, &len, file);
    }
    char *output = NULL;
    size_t i;
    
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    for (i = 0; i < requests.num_requests; i++) {
        switch (requests.opcodes[i]) {
            case SET_REQUEST_OPCODE:
                kv_set(ctx, requests.keys[i], requests.values[i]);
                break;
            case GET_REQUEST_OPCODE:
                kv_get(ctx, requests.keys[i], &output);
                total_read_len += strlen(output);
                kv_release(output);
	    break;
            default:
                printf("Invalid request type %d\n", requests.opcodes[i]);
                exit(1);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);

    if (requests.opcodes) {
        free(requests.opcodes);
        requests.opcodes = NULL;
    }

    for (i = 0; i < allocated_size; i++) {
        if (requests.keys[i]) {
            free(requests.keys[i]);
            requests.keys[i] = NULL;
        }

        if (requests.values[i]) {
            free(requests.values[i]);
            requests.values[i] = NULL;
        }

    }
    if (requests.keys) {
        free(requests.keys);
        requests.keys = NULL;
    }
    if (requests.values) {
        free(requests.values);
        requests.values = NULL;
    }

    unsigned total_secs = end_time.tv_sec - start_time.tv_sec;
    unsigned total_nsecs = end_time.tv_nsec - start_time.tv_nsec;
    double fraction = total_nsecs / 1000000000.0;
    double total_time = total_secs + fraction;
    double total_mbytes = (total_read_len / 1000000.0) + (total_send_len / 1000000.0);

    printf("Total time it took: %f sec. B/W=%fMBit/sec\n",
            total_time, total_mbytes * 8 / total_time);
    free(line);  // Free the dynamically allocated buffer
    fclose(file);
}
