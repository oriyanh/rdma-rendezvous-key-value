#include <stdio.h>
#include "client_requests_management.h"

static client_pending_request_t *internal_q = NULL;  // head of hash table


int client_pending_requests_init(void) {
    internal_q = NULL;
    client_pending_request_t *s;
    s = (struct client_pending_request_t*)calloc(1, sizeof(struct client_pending_request_t));
    if (s == NULL) {
        exit(-1);
    }

    s->tid = -1; // negative TID unreachable by get, add, remove, push or push - safe to use as init value

    HASH_ADD_INT(internal_q, tid, s);
    return 0;
}

void print_client_pending_requests(void){
    client_pending_request_t *current_entry;
    client_pending_request_t *tmp;
    printf("== Pending Requests Queue Contents ==\n");
    HASH_ITER(hh, internal_q, current_entry, tmp) {
        printf("TID=%d'\n", current_entry->tid);
    }
    printf("==========\n\n");
    fflush(stdout);
}

client_pending_request_t* get_client_pending_request(uint16_t tid){
    client_pending_request_t *s;
    int tmp_id = (int) tid;
    HASH_FIND_INT(internal_q, &tmp_id, s);  /* id already in the hash? */
    return s;
}

int add_client_pending_request(uint16_t transaction_id){
    client_pending_request_t *s;

    int tmp_id = (int)transaction_id;
    HASH_FIND_INT(internal_q, &tmp_id, s);  /* id already in the hash? */
    if (s == NULL) {
        s = (struct client_pending_request_t *)calloc(1, sizeof (struct client_pending_request_t));
        s->tid = tmp_id;
        s->key = calloc(1, CLIENT_REQUEST_PAYLOAD_LEN);
        HASH_ADD_INT(internal_q, tid, s);
        return 0;
    }
    else {
        fprintf(stderr, "Error: TID already exists in the pending requests queue %u\n", tmp_id);
        exit(-1);
    }
}

void remove_client_pending_request(uint16_t tid) {
    client_pending_request_t *s;

    int tmp_id = (int) tid;
    HASH_FIND_INT(internal_q, &tmp_id, s);  /* id already in the hash? */
    if (s) {
        if (s->mr) {
            ibv_dereg_mr(s->mr);
            s->mr = NULL;
        }
        if (s->key) {
            free(s->key);
            s->key = NULL;
        }
        HASH_DEL(internal_q, s);
        free(s);
        s = NULL;
    }
}
int num_pending_requests(void) {
    int count;
    count = HASH_COUNT(internal_q);
    return count;
}
void destroy_pending_request_queue(void) {
    client_pending_request_t *s, *tmp;
    HASH_ITER(hh, internal_q, s, tmp) {
        remove_client_pending_request(s->tid);
    }
}