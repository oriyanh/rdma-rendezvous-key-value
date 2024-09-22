#include <infiniband/verbs.h>
#include <sys/param.h>
#include "connection.h"
#include <infiniband/verbs.h>




int get_port_info(struct ibv_context *context, int port, struct ibv_port_attr *attr)
{
    return ibv_query_port(context, port, attr);
}
void wire_gid_to_gid(const char *wgid, union ibv_gid *gid)
{
    char tmp[9];
    uint32_t v32;
    int i;

    for (tmp[8] = 0, i = 0; i < 4; ++i) {
        memcpy(tmp, wgid + i * 8, 8);
        sscanf(tmp, "%x", &v32);
        *(uint32_t *)(&gid->raw[i * 4]) = ntohl(v32);
    }
}
void gid_to_wire_gid(const union ibv_gid *gid, char wgid[])
{
    int i;

    for (i = 0; i < 4; ++i)
        sprintf(&wgid[i * 8], "%08x", htonl(*(uint32_t *)(gid->raw + i * 4)));
}
int connect_ctx(struct ibv_qp *qp, int my_psn, struct kv_dest *rem_dest) {
    struct ibv_qp_attr attr = {
        .qp_state		    = IBV_QPS_RTR,
        .path_mtu		    = MTU,
        .dest_qp_num	    = rem_dest->qpn,
        .rq_psn			    = rem_dest->psn,
        .max_dest_rd_atomic	= 1,
        .min_rnr_timer		= 12,
        .ah_attr            = {
            .is_global	= 0,
            .dlid		= rem_dest->lid,
            .sl		    = SL,
            .src_path_bits = 0,
            .port_num	= IB_PORT
        }
    };

    if (rem_dest->gid.global.interface_id) {
        attr.ah_attr.is_global = 1;
        attr.ah_attr.grh.hop_limit = 1;
        attr.ah_attr.grh.dgid = rem_dest->gid;
        attr.ah_attr.grh.sgid_index = GIDX;
    }
    if (ibv_modify_qp(qp, &attr,
            IBV_QP_STATE              |
            IBV_QP_AV                 |
            IBV_QP_PATH_MTU           |
            IBV_QP_DEST_QPN           |
            IBV_QP_RQ_PSN             |
            IBV_QP_MAX_DEST_RD_ATOMIC |
            IBV_QP_MIN_RNR_TIMER)) {
        fprintf(stderr, "Failed to modify QP to RTR\n");
        perror("ibv_modify_qp");
        return EXIT_FAILURE;
    }

    attr.qp_state	    = IBV_QPS_RTS;
    attr.timeout	    = 14;
    attr.retry_cnt	    = 7;
    attr.rnr_retry	    = 7;
    attr.sq_psn	    = my_psn;
    attr.max_rd_atomic  = 1;
    if (ibv_modify_qp(qp, &attr,
            IBV_QP_STATE              |
            IBV_QP_TIMEOUT            |
            IBV_QP_RETRY_CNT          |
            IBV_QP_RNR_RETRY          |
            IBV_QP_SQ_PSN             |
            IBV_QP_MAX_QP_RD_ATOMIC)) {
        fprintf(stderr, "Failed to modify QP to RTS\n");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
struct kv_context *server_init_ctx(struct ibv_device *ib_dev, int in_size, int out_size, int max_recv_buf, int max_send_buf, size_t page_size)
{
    struct kv_context *ctx;

    ctx = calloc(1, sizeof *ctx);
    if (!ctx)
        return NULL;

    ctx->in_size           = in_size;
    ctx->out_size          = out_size;
    ctx->max_recv_buff     = max_recv_buf;
    ctx->curr_recv_buff    = 0;
    ctx->channel           = NULL;
    ctx->in_buf = calloc(1, roundup(in_size, page_size));
    if (!ctx->in_buf) {
        fprintf(stderr, "Couldn't allocate work buf (recv).\n");
        return NULL;
    }

    ctx->out_buf = calloc(1, roundup(out_size, page_size));
    if (!ctx->out_buf) {
        fprintf(stderr, "Couldn't allocate work buf (send).\n");
        return NULL;
    }

    memset(ctx->in_buf, 0, in_size);
    memset(ctx->out_buf, 0, out_size);

    ctx->context = ibv_open_device(ib_dev);
    if (!ctx->context) {
        fprintf(stderr, "Couldn't get context for %s\n",
                ibv_get_device_name(ib_dev));
        return NULL;
    }

    ctx->pd = ibv_alloc_pd(ctx->context);
    if (!ctx->pd) {
        fprintf(stderr, "Couldn't allocate PD\n");
        return NULL;
    }

    ctx->mr_in = ibv_reg_mr(ctx->pd, ctx->in_buf, in_size, IBV_ACCESS_LOCAL_WRITE);
    if (!ctx->mr_in) {
        fprintf(stderr, "Couldn't register MR\n");
        return NULL;
    }

    ctx->mr_out = ibv_reg_mr(ctx->pd, ctx->out_buf, out_size, IBV_ACCESS_LOCAL_WRITE);
    if (!ctx->mr_out) {
        fprintf(stderr, "Couldn't register MR\n");
        return NULL;
    }


    ctx->cq = ibv_create_cq(ctx->context, max_recv_buf + max_send_buf, NULL,
                            ctx->channel, 0);
    if (!ctx->cq) {
        fprintf(stderr, "Couldn't create CQ\n");
        return NULL;
    }

    return ctx;
}
struct kv_context *client_init_ctx(struct ibv_device *ib_dev, int in_size, int out_size, int rx_depth, int tx_depth, int port, int use_event, size_t page_size)
{
    struct kv_context *ctx;

    ctx = calloc(1, sizeof *ctx);
    if (!ctx)
        return NULL;

    ctx->in_size     = in_size;
    ctx->out_size     = out_size;
    ctx->max_recv_buff = rx_depth;
    ctx->curr_recv_buff    = rx_depth;
    ctx->in_buf = calloc(1, roundup(in_size, page_size));
    if (!ctx->in_buf) {
        fprintf(stderr, "Couldn't allocate work buf (recv).\n");
        return NULL;
    }

    ctx->out_buf = calloc(1, roundup(out_size, page_size));
    if (!ctx->out_buf) {
        fprintf(stderr, "Couldn't allocate work buf (send).\n");
        return NULL;
    }

    memset(ctx->in_buf, 0, in_size);
    memset(ctx->out_buf, 0, out_size);

    ctx->context = ibv_open_device(ib_dev);
    if (!ctx->context) {
        fprintf(stderr, "Couldn't get context for %s\n",
                ibv_get_device_name(ib_dev));
        return NULL;
    }

    if (use_event) {
        ctx->channel = ibv_create_comp_channel(ctx->context);
        if (!ctx->channel) {
            fprintf(stderr, "Couldn't create completion channel\n");
            return NULL;
        }
    } else
        ctx->channel = NULL;

    ctx->pd = ibv_alloc_pd(ctx->context);
    if (!ctx->pd) {
        fprintf(stderr, "Couldn't allocate PD\n");
        return NULL;
    }

    ctx->mr_in = ibv_reg_mr(ctx->pd, ctx->in_buf, in_size, IBV_ACCESS_LOCAL_WRITE);
    if (!ctx->mr_in) {
        fprintf(stderr, "Couldn't register MR\n");
        return NULL;
    }

    ctx->mr_out = ibv_reg_mr(ctx->pd, ctx->out_buf, out_size, IBV_ACCESS_LOCAL_WRITE);
    if (!ctx->mr_out) {
        fprintf(stderr, "Couldn't register MR\n");
        return NULL;
    }


    ctx->cq = ibv_create_cq(ctx->context, rx_depth + tx_depth, NULL,
            ctx->channel, 0);
    if (!ctx->cq) {
        fprintf(stderr, "Couldn't create CQ\n");
        return NULL;
    }

    {
        struct ibv_qp_init_attr attr = {
                .send_cq = ctx->cq,
                .recv_cq = ctx->cq,
                .cap     = {
                        .max_send_wr  = tx_depth,
                        .max_recv_wr  = rx_depth,
                        .max_send_sge = 1,
                        .max_recv_sge = 1,
                },
                .qp_type = IBV_QPT_RC
        };

        ctx->qp = ibv_create_qp(ctx->pd, &attr);
        if (!ctx->qp)  {
            fprintf(stderr, "Couldn't create QP\n");
            return NULL;
        }

        struct ibv_qp_attr qp_attr;
        ibv_query_qp(ctx->qp, &qp_attr, IBV_QP_CAP, &attr);
        ctx->max_inline_data = qp_attr.cap.max_inline_data;
    }

    {
        struct ibv_qp_attr attr = {
                .qp_state        = IBV_QPS_INIT,
                .pkey_index      = 0,
                .port_num        = port,
                .qp_access_flags = IBV_ACCESS_REMOTE_READ |
                IBV_ACCESS_REMOTE_WRITE
        };

        if (ibv_modify_qp(ctx->qp, &attr,
                IBV_QP_STATE              |
                IBV_QP_PKEY_INDEX         |
                IBV_QP_PORT               |
                IBV_QP_ACCESS_FLAGS)) {
            fprintf(stderr, "Failed to modify QP to INIT\n");
            return NULL;
        }
    }

    return ctx;
}

/*
 * Helper function to print the client request
 */
void print_client_request(void *buff)
{
    client_request_headers_t *headers = (client_request_headers_t *) buff;
    void *data = buff + sizeof(client_request_headers_t);
    client_request_t request = {
        .headers = *headers,
        .buffer = data,
    };
    const char * opcode_str = (request.headers.opcode == SET_REQUEST_OPCODE) ? "SET" : "GET";
    const char * key = request.buffer;
    const char * value = (request.headers.buflen > 0) ? (request.buffer + request.headers.keylen + 1) : "";
    printf("{\n\theaders = {\n");
    printf("\t\topcode = %s\n", opcode_str);
    printf("\t\ttid =  %u\n", request.headers.tid);
    printf("\t\tkeylen = %d\n", request.headers.keylen);
    printf("\t\tbuflen = %lu\n", request.headers.buflen);
    printf("\t}\n");
    printf("\n\tbody = {\n");
    printf("\t\tkey = %s\n", key);
    printf("\t\tvalue = %s\n", value);
    printf("\t}\n}\n");
    fflush(stdout);
}
