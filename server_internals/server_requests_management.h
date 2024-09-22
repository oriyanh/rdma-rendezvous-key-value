#ifndef __SERVER_REQ_MANAGEMENT_H__
#define __SERVER_REQ_MANAGEMENT_H__

#include <stdbool.h>
#include <infiniband/verbs.h>
#include "../external_libs/uthash.h"
#include "../external_libs/utlist.h"
#include "../common/constants.h"
#include "db.h"

/**
 * Structs and Types
 */
typedef struct server_pending_request_t {
    UT_hash_handle hh;  // hash handler for setting up a hash table using UThash
    int tid; // transaction id
    wr_id_info info;
    bool is_pending;
    db_entry_t *entry;
    client_request_headers_t headers;
    char* buffer;
    struct ibv_qp *qp;
    uint32_t qp_num;
    struct server_pending_request_t *next, *prev;  // for setting up pending TID queue
    
} server_pending_request_t;

typedef struct pending_rdma_fin_t {
    UT_hash_handle hh;  // hash handler for setting up a hash table using UThash
    uint16_t tid;
    db_entry_t *entry;
} pending_rdma_fin_t;

/**
 * Main functions
 */
int server_pending_requests_init(void);
void server_pending_requests_destroy(void);
void push_pending_request(uint16_t tid, wr_id_info info, client_request_headers_t *headers, const char *buffer, struct ibv_qp *qp);
server_pending_request_t* pop_pending_request(void);
int pending_req_count(void);
void remove_server_pending_request(uint16_t tid);
void mark_rdma_fin_required(uint16_t transaction_id, const char *key, uint32_t qp_num);
int mark_rdma_fin_received(uint16_t tid);
void invalidate_requests_by_qp(struct ibv_qp *qp);

/**
 * Debug functions
 */
void print_server_pending_requests(void);

#endif // ___SERVER_REQ_MANAGEMENT_H__
