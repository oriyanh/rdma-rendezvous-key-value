#ifndef __CLIENT_REQ_MANAGEMENT_H__
#define __CLIENT_REQ_MANAGEMENT_H__

#include <stdbool.h>
#include <infiniband/verbs.h>
#include "../external_libs/uthash.h"
#include "../common/constants.h"

typedef struct client_pending_request_t {
    UT_hash_handle hh;  // hash handler for setting up a hash table using UThash
    int tid; // transaction id
    bool is_pending;
    opcode_t opcode;
    size_t out_size;
    const char *out_buf;
    char *in_buf;
    struct ibv_mr *mr;
    char *key;
} client_pending_request_t;

int client_pending_requests_init(void);
void client_pending_requests_destroy(void);
int add_client_pending_request(uint16_t tid);
client_pending_request_t* get_client_pending_request(uint16_t tid);
void remove_client_pending_request(uint16_t tid);
void print_client_pending_requests(void);
void destroy_pending_request_queue(void);
int num_pending_requests(void);
#endif // __CLIENT_REQ_MANAGEMENT_H__
