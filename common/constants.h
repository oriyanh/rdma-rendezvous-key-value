
#ifndef __CONSTANTS_LIB_H__

#include <stdlib.h>
#include <stdbool.h>
#include <infiniband/verbs.h>

#define __CONSTANTS_LIB_H__

#define SERVER_PORT 21234
#define MAX_CLIENTS_SUPPORTED 4
#define WC_BATCH (50)
#define SERVER_WC_BATCH (200)
#define RECV_BUFF_SIZE 4096
#define SERVER_NUM_RECV_BUFFS 4000
#define SERVER_NUM_SEND_BUFFS 1000
#define CLIENT_HEADER_LEN (24)
#define SERVER_HEADER_LEN (40)

#define BUFFER_SIZE (4096)
#define MAX_RECV_BUFFS ((1.0/MAX_CLIENTS_SUPPORTED) * SERVER_NUM_RECV_BUFFS)
#define MAX_SEND_BUFFS ((1.0/MAX_CLIENTS_SUPPORTED) * SERVER_NUM_SEND_BUFFS)
#define EXCHANGE_MSG_SIZE 52
#define EXCHANGE_MSG_SIZE_OUT 59
#define IB_PORT 1
#define MTU (IBV_MTU_2048)
#define SL (0)
#define GIDX (-1)
#define CLIENT_REQUEST_PAYLOAD_LEN (BUFFER_SIZE - CLIENT_HEADER_LEN)
#define SERVER_RESPONSE_PAYLOAD_LEN (BUFFER_SIZE - SERVER_HEADER_LEN)

#define NOT_CONNECTED   -1
#define TRUE             1
#define FALSE            0


typedef struct connection_t {
    struct ibv_qp * qp;
    int curr_recv_buff;
    int max_recv_buff;
    bool buff_in_use[SERVER_NUM_RECV_BUFFS];
    int connection_id;
} connection_t;


struct kv_dest {
    int lid;
    int qpn;
    int psn;
    union ibv_gid gid;
};

enum {
    RECV_WRID = 1,
    SEND_WRID = 2,
    RDMA_READ_WRID = 3,
    RDMA_WRITE_WRID = 4,
};

typedef union wr_id_info {
    uint64_t value;
    struct wr_data {
        uint32_t wr_id;
        uint32_t wr_type;
    } data;
} wr_id_info;

typedef enum {
    SET_REQUEST_OPCODE = 1,
    GET_REQUEST_OPCODE,
    RDMA_SET_REQUEST_OPCODE,
    RDMA_GET_REQUEST_OPCODE,
    RDMA_FIN_CLIENT,
    CLIENT_SHUTDOWN_OPCODE,
} opcode_t;

typedef enum {
    GET_RESPONSE = 1,
    SET_RESPONSE,
    RDMA_RKEY_SET,
    RDMA_RKEY_GET,
    RDMA_FIN_SERVER,
    SERVER_SHUTDOWN,
} response_t;

typedef struct client_request_headers_t {
    opcode_t opcode;
    uint16_t tid; // transaction id
    uint32_t keylen;
    uint64_t buflen;
} client_request_headers_t;

typedef struct client_request_t {
    client_request_headers_t headers;
    char *buffer;
} client_request_t;

typedef struct rkey_info_t {
            uint32_t rkey;
            void *raddr;  // remote address
} rkey_info_t;

typedef struct server_response_headers_t {
    response_t response;
    uint16_t tid; // transaction id
    uint32_t keylen;
    uint64_t buflen;
    rkey_info_t rkey_info;
} server_response_headers_t;

typedef struct server_response_t {
    server_response_headers_t headers;
    char *buffer;
} server_response_t;


struct kv_context {
    struct ibv_context		*context;
    struct ibv_comp_channel	*channel;
    struct ibv_pd		*pd;
    struct ibv_mr		*mr_in;
    struct ibv_mr		*mr_out;
    struct ibv_cq		*cq;
    struct ibv_qp		*qp;
    struct kv_dest my_dest;
    void			*in_buf;
    int				in_size;
    void			*out_buf;
    int				out_size;
    int				max_recv_buff;
    int				curr_recv_buff;
    struct ibv_port_attr	portinfo;
    uint32_t max_inline_data;
    int last_used_recv_buff;
    int last_used_send_buff;
    bool in_use_recv[SERVER_NUM_RECV_BUFFS];
    bool in_use_send[SERVER_NUM_SEND_BUFFS];
};

#endif // __CONSTANTS_LIB_H__
