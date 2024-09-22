#include <stdlib.h>
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
#include <sys/ioctl.h>
#include <signal.h>

#include "server.h"
#include "db.h"
#include "common/buffers_management.h"

static int last_client_id = -1;
struct ibv_device **dev_list;
static struct connection_t connections[MAX_CLIENTS_SUPPORTED] = {0};
static bool immediate_shutdown = false;

static int listen_sd = NOT_CONNECTED;
static int current_number_conns = 0;


struct timespec last_qp_state_check, curr_time;

int get_free_send_buffer(struct kv_context *ctx, void **out_buf) {
    int buff_id = get_free_send_idx(ctx);
    while (buff_id < 0) {
        server_wait_completions(ctx);
        fprintf(stderr, "no free send buffers, waiting completions\n");
        fflush(stderr);
        buff_id = get_free_send_idx(ctx);
    }

    *out_buf = get_send_buffer(ctx, buff_id);
    acquire_send_buffer(ctx, buff_id);
    return buff_id;
}

int handle_received_connection_data(char * msg, struct kv_context * ctx, int fd) { 
    char gid[33];
    struct ibv_qp *new_qp = create_new_qp(ctx);
    if(!new_qp) {
        perror("Failed to create new QP");
        return EXIT_FAILURE;
    }
    ctx->my_dest.qpn = new_qp->qp_num;
    ctx->my_dest.psn = lrand48() & 0xffffff;
    struct kv_dest rem_dest = {0};
    sscanf(msg, "%x:%x:%x:%s", &(rem_dest.lid), &(rem_dest.qpn), &(rem_dest.psn), gid);
    wire_gid_to_gid(gid, &(rem_dest.gid));

    if (connect_ctx(new_qp, ctx->my_dest.psn, &rem_dest)) {
        fprintf(stderr, "Couldn't connect to remote QP\n");
        return EXIT_FAILURE;
    }
    gid_to_wire_gid(&(ctx->my_dest.gid), gid);

    printf("Got new connection qp #%u\n", new_qp->qp_num);
    fflush(stdout);
    connection_t * new_connection = add_new_connection(new_qp, MAX_RECV_BUFFS, 0);
    if(send_address_back_to_client(msg, gid, ctx, fd) == EXIT_FAILURE){
        fprintf(stderr, "Failed to send address back to client\n");
        exit(EXIT_FAILURE);
    }
    inet_ntop(AF_INET6, &(rem_dest.gid), gid, sizeof gid);
    new_connection->curr_recv_buff = server_post_recv_buffers(ctx, new_connection);
    if (new_connection->curr_recv_buff < new_connection->max_recv_buff) {
        // We should get all the buffers posted
        fprintf(stderr, "Couldn't post receive (%d)\n", new_connection->curr_recv_buff);
        teardown_server_connection(ctx, EXIT_FAILURE);
    }
    return EXIT_SUCCESS;
}

int accept_new_connection(struct kv_context *ctx) {
    int rc;
    rc = check_available_connection_slots();
    if(rc == FALSE){
        // fprintf(stderr, "No free connection slots\n");
        return EXIT_FAILURE;
    }
    if(listen_sd == NOT_CONNECTED){
        rc = create_listen_socket();
        if(rc){
            fprintf(stderr, "Failed to create listen socket\n");
            return EXIT_FAILURE;
        }
    }
    rc = query_for_connections(ctx);
    if(rc){
        fprintf(stderr, "Failed to query for connections\n");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int create_listen_socket(){
    struct addrinfo *res;
    struct addrinfo hints = {
            .ai_flags    = AI_PASSIVE,
            .ai_family   = AF_INET,
            .ai_socktype = SOCK_STREAM
    };
    int rc, n = 1;

    char *service = calloc(6, sizeof(char));
    if (!service) {
        fprintf(stderr, "Failed to allocate memory for service.\n");
        return EXIT_FAILURE;
    }

    if (sprintf(service, "%d", SERVER_PORT) < 0){
        fprintf(stderr, "Failed to write port to service.\n");
        return EXIT_FAILURE;
    }

    if (getaddrinfo(NULL, service, &hints, &res) < 0) {
        fprintf(stderr, "%s for port %d\n", gai_strerror(n), SERVER_PORT);
        free(service);
        return EXIT_FAILURE;
    }
    if (res->ai_next) {
        fprintf(stderr, "Multiple addresses found for port %d\n", SERVER_PORT);
        free(service);
        return EXIT_FAILURE;
    }
    //Initial connection
    listen_sd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (listen_sd < 0) {
        perror("socket() failed");
        return EXIT_FAILURE;
    }

    rc = setsockopt(listen_sd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &n, sizeof n);
    if (rc) {
        perror("setsockopt() failed");
        close(listen_sd);
        return EXIT_FAILURE;
    }

    rc = ioctl(listen_sd, FIONBIO, &n);
    if(rc < 0) {
        perror("ioctl() failed");
        close(listen_sd);
        return EXIT_FAILURE;
    }

    rc = bind(listen_sd, res->ai_addr, res->ai_addrlen);
    if (rc < 0) {
        perror("ioctl() failed");
        close(listen_sd);
        return EXIT_FAILURE;
    }
    freeaddrinfo(res);
    free(service);

    rc = listen(listen_sd, SOMAXCONN);
    if (rc < 0) {
        perror("listen() failed");
        close(listen_sd);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int query_for_connections(struct kv_context *ctx){
    char msg[sizeof "0000:000000:000000:00000000000000000000000000000000"];
    int    i, len, rc;
    int    max_sd, new_sd;
    int    desc_ready;
    struct timeval      timeout;
    fd_set              master_set, working_set;
     
   FD_ZERO(&master_set);
   max_sd = listen_sd;
   FD_SET(listen_sd, &master_set);

   timeout.tv_sec  = 0;
   timeout.tv_usec = 1;

    do
    {
        memcpy(&working_set, &master_set, sizeof(master_set));
        rc = select(max_sd + 1, &working_set, NULL, NULL, &timeout);
        if (rc < 0)
        {
            perror("  select() failed");
            break;
        }
        if (rc == 0)
        {
            //timeout
            break;
        }
        printf("  select() returned %d\n", rc);
        desc_ready = rc;
        for (i=0; (i <= max_sd)  &&  (desc_ready > 0); ++i) {
            if (FD_ISSET(i, &working_set)) { //waiting to be accepted
                desc_ready -= 1;
                if (i == listen_sd)
                {
                    printf("  Listening socket is readable\n");
                    do
                    {
                        printf("    Accepting\n");
                        usleep(10000);
                        new_sd = accept(listen_sd, NULL, NULL);
                        if (new_sd < 0)
                        {
                            if (errno != EWOULDBLOCK) {
                                perror("accept() failed");
                                break;
                            }
                            perror("accept() failed");
                            break;
                        }
                        printf("  New incoming connection - %d\n", new_sd);
                        if (current_number_conns == MAX_CLIENTS_SUPPORTED) {
                            printf("Too many clients, rejecting connection\n");
                            fflush(stdout);
                            close(new_sd);
                            break;
                        }
                        FD_SET(new_sd, &master_set);
                        if (new_sd > max_sd){
                            max_sd = new_sd;
                        }
                        ++current_number_conns;

                    } while (new_sd != -1);
                }
                else {
                    printf("  Descriptor %d is readable\n", i);
                    rc = recv(i, msg, sizeof(msg), MSG_WAITALL);
                    if(rc == EXCHANGE_MSG_SIZE){
                        len = rc;
                        printf("  %d bytes received\n", len);
                        handle_received_connection_data(msg, ctx, i);
                    }else{
                        printf("Didn't recv all data\n");
                        fflush(stdout);
                    }
                    FD_CLR(i, &master_set);
                    if (i == max_sd)
                    {
                        while (FD_ISSET(max_sd, &master_set) == FALSE)
                        max_sd -= 1;
                    }
                } 
            }else{
                printf("  Descriptor %d is not readable\n", i);
            }
        } 

    } while (TRUE);
    for (i=0; i <= max_sd; ++i)
    {
        if (FD_ISSET(i, &master_set) && (i != listen_sd)) // Do not close the listening socket
            close(i);
    }
    return EXIT_SUCCESS;
} 

int send_address_back_to_client(char *msg, char *gid, struct kv_context * ctx, int sockfd_conn) {
    char msg_out[sizeof "0000:0000:000000:000000:00000000000000000000000000000000"];
    sprintf(msg_out, "%04x:%04x:%06x:%06x:%s", (last_client_id + 1), ctx->my_dest.lid, ctx->my_dest.qpn, ctx->my_dest.psn, gid);
    printf("Sending back: %s\n", msg_out);
    if (send(sockfd_conn, msg_out, sizeof msg_out, 0) != sizeof msg_out) {
        fprintf(stderr, "Couldn't send local address\n");
        return EXIT_FAILURE;
    }
    printf("%lu bytes sent\n", sizeof msg_out);

    size_t total_read = 0;
    while (total_read < sizeof "done") {
        int j = read(sockfd_conn, msg + total_read, sizeof "done" - total_read);
        if (j < 0) {
            perror("Server: read error");
            close(sockfd_conn);
            return EXIT_FAILURE;
        } else if (j == 0) {
            // Connection closed by server
            fprintf(stderr, "Client: server closed connection unexpectedly\n");
            close(sockfd_conn);
            return EXIT_FAILURE;
        }
        total_read += j;
    }
    printf("Received: %s\n", msg);

    close(sockfd_conn);
    return EXIT_SUCCESS;
}

int remove_connection(struct kv_context *ctx, uint32_t qp_num){
    connection_t * curr_connection = get_connection(qp_num);
    if(!curr_connection) {
        fprintf(stderr, "Connection not found %d\n", qp_num);
        return EXIT_FAILURE;
    }

    struct ibv_qp *qp = curr_connection->qp;

    invalidate_requests_by_qp(qp);
    if (ibv_destroy_qp(qp)) {
        fprintf(stderr, "Couldn't destroy QP %d\n", qp_num);
        return EXIT_FAILURE;
    }
    curr_connection->qp = NULL;
    curr_connection->max_recv_buff = 0;
    curr_connection->curr_recv_buff = 0;
    --current_number_conns;
    
    int i;
    for (i = 0; i < SERVER_NUM_RECV_BUFFS; i++) {
        if (curr_connection->buff_in_use[i]) {
            release_receive_buffer(ctx, i);
            curr_connection->buff_in_use[i] = false;
        }
    }
    return EXIT_SUCCESS;
}

int check_available_connection_slots(void){
    return current_number_conns < MAX_CLIENTS_SUPPORTED;
}

int init_server_connection(struct kv_context ** out_ctx) {
    struct ibv_device       *ib_dev;
    struct kv_context       *ctx = NULL;
    int                      max_recv_buf = SERVER_NUM_RECV_BUFFS;
    int                      max_send_buf = SERVER_NUM_SEND_BUFFS;
    int                      in_size = max_recv_buf * BUFFER_SIZE;
    int                      out_size = max_send_buf * BUFFER_SIZE;
    int page_size;

    srand48(getpid() * time(NULL));
    page_size = sysconf(_SC_PAGESIZE);

    dev_list = ibv_get_device_list(NULL);
    if (!dev_list) {
        perror("Failed to get IB devices list");
        teardown_server_connection(ctx, EXIT_FAILURE);
    }

    ib_dev = *dev_list;
    if (!ib_dev) {
        fprintf(stderr, "No IB devices found\n");
        teardown_server_connection(ctx, EXIT_FAILURE);
    }
    server_pending_requests_init();
    ctx = server_init_ctx(ib_dev, in_size, out_size, SERVER_NUM_RECV_BUFFS, SERVER_NUM_SEND_BUFFS, page_size);
    if (!ctx) {
        fprintf(stderr, "Couldn't create server context\n");
        teardown_server_connection(ctx, EXIT_FAILURE);
    }
    db_init(ctx->pd);

    if (get_port_info(ctx->context, IB_PORT, &ctx->portinfo)) {
        fprintf(stderr, "Couldn't get port info\n");
        teardown_server_connection(ctx, EXIT_FAILURE);
    }

    ctx->my_dest.lid = ctx->portinfo.lid;
    if (ctx->portinfo.link_layer == IBV_LINK_LAYER_INFINIBAND && !(ctx->my_dest.lid)) {
        fprintf(stderr, "Couldn't get local LID\n");
        teardown_server_connection(ctx, EXIT_FAILURE);
    }
    memset(&(ctx->my_dest.gid), 0, sizeof ctx->my_dest.gid);
    *out_ctx = ctx;
    return EXIT_SUCCESS;
}

struct ibv_qp * create_new_qp(struct kv_context * ctx){
    struct ibv_qp *qp;
    {
        struct ibv_qp_init_attr attr = {
                .send_cq = ctx->cq,
                .recv_cq = ctx->cq,
                .cap     = {
                        .max_send_wr  = MAX_SEND_BUFFS,
                        .max_recv_wr  = MAX_RECV_BUFFS,
                        .max_send_sge = 1,
                        .max_recv_sge = 1,
                },
                .qp_type = IBV_QPT_RC
        };

        qp = ibv_create_qp(ctx->pd, &attr);
        if (!qp)  {
            fprintf(stderr, "Couldn't create QP\n");
            return NULL;
        }

        struct ibv_qp_attr qp_attr;
        ibv_query_qp(qp, &qp_attr, IBV_QP_CAP, &attr);
        ctx->max_inline_data = qp_attr.cap.max_inline_data;
    }

    {
        struct ibv_qp_attr attr = {
                .qp_state        = IBV_QPS_INIT,
                .pkey_index      = 0,
                .port_num        = IB_PORT,
                .qp_access_flags = IBV_ACCESS_REMOTE_READ |
                                   IBV_ACCESS_REMOTE_WRITE
        };

        if (ibv_modify_qp(qp, &attr,
                          IBV_QP_STATE              |
                          IBV_QP_PKEY_INDEX         |
                          IBV_QP_PORT               |
                          IBV_QP_ACCESS_FLAGS)) {
            fprintf(stderr, "Failed to modify QP to INIT\n");
            return NULL;
        }
    }
    return qp;
}

int send_server_shutdown_message(struct kv_context *ctx, struct ibv_qp *qp) {
    void *out_buffer;
    int buff_id = get_free_send_buffer(ctx, &out_buffer);
    server_response_headers_t resp_headers = {
        .keylen = 0,
        .tid = 0,
        .buflen = 0,
        .response = SERVER_SHUTDOWN,
    };
    memcpy(out_buffer, &resp_headers, sizeof(server_response_headers_t));
    server_post_send(ctx, qp, sizeof(server_response_headers_t), buff_id, out_buffer);
    return 0;
}

int server_close_ctx(struct kv_context *ctx)
{
    if (ctx->qp) {
        if (ibv_destroy_qp(ctx->qp)) {
            fprintf(stderr, "Couldn't destroy QP\n");
            return 1;
        }
    }

    int conn;
    for (conn = 0; conn < MAX_CLIENTS_SUPPORTED; conn++) {
        struct ibv_qp *qp = connections[conn].qp;
        if (qp) {
            struct ibv_qp_attr attr;
            struct ibv_qp_init_attr init_attr;
            if (ibv_query_qp(qp, &attr, IBV_QP_STATE, &init_attr) == 0) {
                if (attr.qp_state == IBV_QPS_RTS) {
                    fprintf(stdout, "Sending SERVER_SHUTDOWN to qp #%d\n", conn);
                    send_server_shutdown_message(ctx, qp);
                }
                else {
                    fprintf(stderr, "QP is in error state\n");
                    // Handle the disconnection or error
                }

            }
            remove_connection(ctx, conn);

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

void teardown_server_connection(struct kv_context *ctx, int retcode) {
    // disconnect from clients
    if (listen_sd >= 0) {
        close(listen_sd);
    }
    server_pending_requests_destroy();
    db_destroy();

    if (dev_list) {
        ibv_free_device_list(dev_list);
        dev_list = NULL;
    }

    if (ctx) {
        server_close_ctx(ctx);
    }
    exit(retcode);
}

connection_t * add_new_connection(struct ibv_qp *qp, int max_recv_buff, int curr_recv_buff) {
    int free_client_id = 0;
    for (free_client_id = 0; free_client_id < MAX_CLIENTS_SUPPORTED; free_client_id++) {
        if (connections[free_client_id].qp == NULL) {
            break;
        }
    }
    int curr_idx = free_client_id;
    connections[curr_idx].qp = qp;
    if (connections[curr_idx].curr_recv_buff == 0) {
        connections[curr_idx].curr_recv_buff = curr_recv_buff;
    }
    connections[curr_idx].max_recv_buff = max_recv_buff;
    last_client_id = curr_idx;
    return &connections[curr_idx];
}


connection_t * get_connection(uint32_t qp_num) {
    int i;
    for (i = 0; i < MAX_CLIENTS_SUPPORTED; i++) {
        if (connections[i].qp && connections[i].qp->qp_num == qp_num) {
            return &connections[i];
        }
    }
    return NULL;
}


int server_post_recv(struct kv_context *ctx, struct ibv_qp *qp, int buff_id)
{
    char *buff = get_recv_buffer(ctx, buff_id);
    memset(buff, 0, BUFFER_SIZE);
    struct ibv_sge list = {
            .addr	= (uintptr_t) buff,
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
    int res = ibv_post_recv(qp, &wr, &bad_wr);
    if (!res) {
        acquire_receive_buffer(ctx, buff_id);
    }
    return res;
}

int server_post_recv_buffers(struct kv_context *ctx, connection_t * connection) {
    int rx_outs = 0;
    int buff_idx = 0;
    int i;
    int num_buffers = connection->max_recv_buff - connection->curr_recv_buff;
    struct ibv_qp *qp = connection->qp;
    for (i = 0; i < num_buffers; i++) {
        buff_idx = get_free_recv_idx(ctx);
        if (buff_idx < 0) {
            fprintf(stderr, "No free recv buffers\n");
            return rx_outs;
        }
        if (!server_post_recv(ctx, qp, buff_idx)) {
            ++rx_outs;
            connection->buff_in_use[buff_idx] = true;
        }
    }

    return rx_outs;
}

int server_post_send(struct kv_context *ctx, struct ibv_qp *qp, uint32_t size, int buff_id, char *out_buf)
{
    struct ibv_sge list = {
            .addr	= (uint64_t) out_buf,
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

    return ibv_post_send(qp, &wr, &bad_wr);
}

int handle_set_request(struct kv_context *ctx, struct ibv_qp *qp, client_request_headers_t *headers, const char *buffer, wr_id_info info) {
    uint16_t tid = headers->tid;
    uint32_t keylen = headers->keylen;
    const char *key = buffer;
    db_entry_t *entry = db_get(key);

    if (!entry) {
        db_insert(key, "");
        entry = db_get(key);
    }

    if (!db_entry_lock_wr(entry)) {
        // entry locked by other request, push request back to pending
        push_pending_request(tid, info, headers, buffer, qp);
        return 1;
    }

    // Entry locked by current request
    uint32_t buflen = headers->buflen;
    if ((buflen+keylen+2) <= CLIENT_REQUEST_PAYLOAD_LEN) {
        const char *value = buffer + keylen + 1;
        db_insert(key, value);
        db_entry_unlock(entry);
        remove_server_pending_request(tid);
    }
    else {
        // allocate mr with enough room for incoming data
        db_reallocate_item(key, buflen);
        struct server_response_headers_t response = {
            .response = RDMA_RKEY_SET,
            .tid = tid,
            .keylen = keylen,
            .buflen = buflen,
            .rkey_info = {
                .rkey = entry->mr->rkey,
                .raddr = (void *) entry->value,
            },
        };

        void *out_buffer;
        int buff_id = get_free_send_buffer(ctx, &out_buffer);
        memcpy(out_buffer, &response, sizeof(server_response_headers_t));
        server_post_send(ctx, qp, sizeof(response), buff_id, out_buffer);
        mark_rdma_fin_required(tid, entry->key, qp->qp_num);
    }

    return 0;
}

int handle_get_request(struct kv_context *ctx, struct ibv_qp *qp, client_request_headers_t *headers, const char *buffer, wr_id_info info) {
    uint16_t tid = headers->tid;
    uint32_t keylen = headers->keylen;
    const char *key = buffer;
    db_entry_t *entry = db_get(key);
    if (!entry) {
        char *new_value = calloc(1,1);
        db_insert(key, new_value);
        entry = db_get(key);
    }

    if (!db_entry_lock_rd(entry)) {
        // entry locked by other request, push request back to pending
        push_pending_request(tid, info, headers, buffer, qp);
        return 1;
    }

    // entry locked by current request
    char *value = entry->value;
    uint32_t size = entry->size;

    void *out_buffer;
    int buff_id = get_free_send_buffer(ctx, &out_buffer);
    server_response_headers_t resp_headers = {
        .keylen = keylen,
        .tid = tid,
        .buflen = size,
    };

    int total_size = sizeof(server_response_headers_t) + size + 1;
    if (total_size <= SERVER_RESPONSE_PAYLOAD_LEN) {
        resp_headers.response = GET_RESPONSE;
        memcpy(out_buffer, &resp_headers, sizeof(server_response_headers_t));
        memcpy(out_buffer + sizeof(server_response_headers_t), value, size + 1);
        server_post_send(ctx, qp, sizeof(server_response_headers_t) + size + 1, buff_id, out_buffer);
        db_entry_unlock(entry);
        remove_server_pending_request(tid);
    }
    else {
        resp_headers.response = RDMA_RKEY_GET;
        if (!entry->mr) {
            entry->mr = ibv_reg_mr(ctx->pd, entry->value, size,
                        IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
        }
        resp_headers.rkey_info.rkey = entry->mr->rkey;
        resp_headers.rkey_info.raddr = entry->mr->addr;

        memcpy(out_buffer, &resp_headers, sizeof(server_response_headers_t));
        server_post_send(ctx, qp, sizeof(server_response_headers_t), buff_id, out_buffer);
        remove_server_pending_request(tid);
        mark_rdma_fin_required(tid, entry->key, qp->qp_num);
    }
    return 0;
}

  int handle_recv_request(struct kv_context *ctx, wr_id_info info, uint32_t qp_num) {
    int buff_id = info.data.wr_id;
    connection_t * curr_connection = get_connection(qp_num);
    if(!curr_connection) {
        fprintf(stderr, "Connection not found\n");
        return 1;
    }
    struct ibv_qp *qp = curr_connection->qp;

    if (--curr_connection->curr_recv_buff <= 100) {  // Need to post more recv buffers to recv queue
        curr_connection->curr_recv_buff += server_post_recv_buffers(ctx, curr_connection);
        if (curr_connection->curr_recv_buff < (curr_connection->max_recv_buff - 1)) {
            fprintf(stderr,
                    "Couldn't post receive (%d)\n",
                    curr_connection->curr_recv_buff);
            teardown_server_connection(ctx, EXIT_FAILURE);
        }
    }
    void *buff = get_recv_buffer(ctx, buff_id);
    client_request_headers_t *headers = (client_request_headers_t *) buff;
    char *data = buff + sizeof(client_request_headers_t);
    client_request_t request = {
        .headers = *headers,
        .buffer = data,
    };

    db_entry_t *entry = NULL;
    char *key = NULL;
    switch (request.headers.opcode)
    {
        case SET_REQUEST_OPCODE:
            handle_set_request(ctx, qp, headers, data, info);
            break;
        
        case GET_REQUEST_OPCODE:
            handle_get_request(ctx, qp, headers, data, info);
            break;
        case RDMA_FIN_CLIENT:
            key = request.buffer;
            key[request.headers.keylen] = 0;
            entry = db_get(key);
            if (entry) {
                if (mark_rdma_fin_received(headers->tid)) {
                    fprintf(stderr, "Received RDMA_FIN_CLIENT for non-pending TID %u, key %s exiting\n", request.headers.tid, key);
                    teardown_server_connection(ctx, EXIT_FAILURE);
                }
                remove_server_pending_request(headers->tid);
            }
            else {
                invalidate_requests_by_qp(qp);
                remove_connection(ctx, qp->qp_num);
                return -1;
            }
            break;
        case CLIENT_SHUTDOWN_OPCODE:
            fprintf(stderr, "Received CLIENT_SHUTDOWN_OPCODE from qp #%u\n", qp_num);
            remove_connection(ctx, qp_num);
            break;
        default:
            fprintf(stderr, "Unidentified opcode %d from qp %u, closing connection\n", request.headers.opcode, qp_num);
            remove_connection(ctx, qp_num);
            break;
    }
    return 0;
}

int server_wait_completions(struct kv_context *ctx)
{
    struct ibv_wc wc[SERVER_WC_BATCH];
    int ne, i;

    ne = ibv_poll_cq(ctx->cq, SERVER_WC_BATCH, wc);
    if (ne < 0) {
        fprintf(stderr, "poll CQ failed %d\n", ne);
        teardown_server_connection(ctx, EXIT_FAILURE);
    }

    for (i = 0; i < ne; ++i) {
        wr_id_info info = {
            .value = (uint64_t) wc[i].wr_id
        };
        if (wc[i].status != IBV_WC_SUCCESS) {
            fprintf(stderr, "Failed status %s (%d) for wr_id %d, qp #%d. Closing connection with client.\n",
                    ibv_wc_status_str(wc[i].status),
                    wc[i].status, (int) wc[i].wr_id, wc[i].qp_num);
            remove_connection(ctx, wc[i].qp_num);
        }

        switch (info.data.wr_type) {
        case SEND_WRID:
            free_send_buffer(ctx, info.data.wr_id);
            break;

        case RECV_WRID:
            handle_recv_request(ctx, info, wc[i].qp_num);
            release_receive_buffer(ctx, info.data.wr_id);
            break;

        default:
            fprintf(stderr, "Completion for unknown wr_type %d\n",
                    info.data.wr_type);
            teardown_server_connection(ctx, EXIT_FAILURE);
        }
    }

    return 0;
}

void handle_pending_request(struct kv_context *ctx) {
    server_pending_request_t *pending_req = NULL;
    int num_pending_reqs = pending_req_count();
    clock_gettime(CLOCK_MONOTONIC, &curr_time);
    unsigned long seconds = curr_time.tv_sec - last_qp_state_check.tv_sec;
    if (seconds > 1) {
        last_qp_state_check = curr_time;
        int j;
        for (j = 0; j < MAX_CLIENTS_SUPPORTED; j++) {
            struct ibv_qp *qp = connections[j].qp;
                if (qp) {
                struct ibv_qp_attr attr;
                struct ibv_qp_init_attr init_attr;
                if (ibv_query_qp(qp, &attr, IBV_QP_STATE, &init_attr) == 0) {
                    if (attr.qp_state != IBV_QPS_RTS) {
                        fprintf(stdout, "QP  %u in error state, cleaning pending WRs, closing connection\n", qp->qp_num);
                        perror("System errno:");
                        invalidate_requests_by_qp(qp);
                        remove_connection(ctx, qp->qp_num);
                    }
                }
            }
        }

        num_pending_reqs = pending_req_count();
    }

    int i;
    for (i = 0; i < num_pending_reqs; i++) {
        if (i > SERVER_WC_BATCH) {
            return;
        }
        // Try to release all pending requests, on failure push back to end of queue
        pending_req = pop_pending_request();

        client_request_headers_t *headers = &pending_req->headers;
        const char *data = pending_req->buffer;
        wr_id_info info = pending_req->info;
        switch (headers->opcode)
        {
            case SET_REQUEST_OPCODE:
                if (handle_set_request(ctx, pending_req->qp, headers, data, info)) {
                    // failure to process, push back
                }
                break;
            
            case GET_REQUEST_OPCODE:
                if (handle_get_request(ctx, pending_req->qp, headers, data, info)) {
                    // failure to process, push back
                }
                break;
            default:
                fprintf(stderr, "Unidentified opcode %d\n", headers->opcode);
                // remove connection if invalid
                break;
        }
    }
}

static void signal_handler(int signal_num) {
    switch (signal_num) {
        case SIGTERM:
        case SIGINT:
            printf("Caught signal %d\n", signal_num);
            fflush(stdout);
            immediate_shutdown = true;
            break;
        default:
            break;
    }
}

int server_loop(void) {
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    struct kv_context *ctx = NULL;
    if(init_server_connection(&ctx) == EXIT_FAILURE){
        fprintf(stderr, "Failed to initialize server connection\n");
        return EXIT_FAILURE;
    }

    clock_gettime(CLOCK_MONOTONIC, &last_qp_state_check); // init first time check

    while (!immediate_shutdown) {
        accept_new_connection(ctx);
        server_wait_completions(ctx);
        handle_pending_request(ctx);
    }

    teardown_server_connection(ctx, EXIT_SUCCESS);
    return 0; // unreachable
}
