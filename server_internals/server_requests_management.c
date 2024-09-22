#include <stdio.h>
#include <unistd.h>
#include <sys/param.h>
#include "server_requests_management.h"
#include "db.h"

static server_pending_request_t *internal_q = NULL;  // head of hash table
static server_pending_request_t *sorted_q = NULL;  // head of hash table
static server_pending_request_t *waiting_fin_q = NULL;  // head of hash table

static int page_size;

static server_pending_request_t **preallocated_entries = NULL;
static int current_num_allocated_entries = 0;
static int free_buffer_idx = 0;
static const int MAX_ALLOCATED_ENTRIES = 100000;

static void push_back_pending_request(server_pending_request_t *req) {
    DL_APPEND(sorted_q, req);
}

static void reallocate_entries(void) {

    server_pending_request_t **entries;
    if (preallocated_entries == NULL) {
        entries = calloc(MAX_ALLOCATED_ENTRIES, sizeof(server_pending_request_t *));
        if (!entries) {
            fprintf(stderr, "DB allocation error\n");
            exit(1);
        }
        preallocated_entries = entries;
    }

    int i;
    server_pending_request_t *curr_entry;
    for (i=0; i < MAX_ALLOCATED_ENTRIES; i++) {
        curr_entry = preallocated_entries[i];
        if (curr_entry == NULL) {
            curr_entry = calloc(1, sizeof(server_pending_request_t));
            if (!curr_entry) {
                fprintf(stderr, "DB allocation error\n");
                exit(1);
            }

            size_t allocated_size = roundup(page_size, CLIENT_REQUEST_PAYLOAD_LEN);
            curr_entry->buffer = calloc(1, allocated_size);
            preallocated_entries[i] = curr_entry;
            ++current_num_allocated_entries;
        }
    }
    free_buffer_idx = 0;
}

static server_pending_request_t* get_empty_buffer(void) {
    if (current_num_allocated_entries < 10) {
        reallocate_entries();
    }

    server_pending_request_t *empty_buffer = preallocated_entries[free_buffer_idx];
    preallocated_entries[free_buffer_idx++] = NULL;
    --current_num_allocated_entries;
    return empty_buffer;
}

static void clear_preallocated_buffers(void) {
    if(!preallocated_entries) {
        return;
    }
    int i;
    server_pending_request_t *curr_entry;
    for (i=0; i < MAX_ALLOCATED_ENTRIES; i++) {
        curr_entry = preallocated_entries[i];
        if (curr_entry) {
            if (curr_entry->buffer) {
                free(curr_entry->buffer);
                curr_entry->buffer = NULL;
            }

            free(curr_entry);
            curr_entry = NULL;
        }
    }
    free(preallocated_entries);
    preallocated_entries = NULL;
}

static int request_compare(server_pending_request_t *s1, server_pending_request_t *s2) {
    return s1->tid - s2->tid;
}

static server_pending_request_t* get_server_pending_request(uint16_t transaction_id){
    server_pending_request_t *s = NULL;
    int tmp_id = (int) transaction_id;
    HASH_FIND_INT(internal_q, &tmp_id, s);  /* id already in the hash? */
    return s;
}

static int add_server_pending_request(uint16_t transaction_id){
    server_pending_request_t *s;

    s = get_empty_buffer();
    s->tid = transaction_id;
    s->entry = NULL;
    HASH_ADD_INT(internal_q, tid, s);
    DL_APPEND(sorted_q, s);
    return 0;
}

int server_pending_requests_init(void) {
    internal_q = NULL;
    sorted_q = NULL;
    page_size = sysconf(_SC_PAGESIZE);
    server_pending_request_t *s;
    s = get_empty_buffer();
    if (s == NULL) {
        exit(-1);
    }

    // Insert bogus key with nonsense value to initialize db
    s->tid = -1; // negative TID unreachable by get, add, remove, push or push - safe to use as init value
    HASH_ADD_INT(internal_q, tid, s);
    return 0;
}

void print_server_pending_requests(void){
    server_pending_request_t *current_entry;
    server_pending_request_t *tmp;
    printf("== Pending Requests Queue Contents ==\n");
    HASH_ITER(hh, internal_q, current_entry, tmp) {
        printf("TID=%d\n", current_entry->tid);
    }
    printf("==========\n\n");
    fflush(stdout);
}

void remove_server_pending_request(uint16_t transaction_id) {
    server_pending_request_t *s, *tmp, temp_el;

    // remove from pending queue
    int tmp_id = (int) transaction_id;
    temp_el.tid = transaction_id;
    DL_SEARCH(sorted_q, tmp, &temp_el, request_compare);
    if (tmp) {
        DL_DELETE(sorted_q, tmp);
    }

    // mark RDMA received (unlock db)
    mark_rdma_fin_received(tmp_id);

    // free internal queue
    HASH_FIND_INT(internal_q, &tmp_id, s);  /* id already in the hash? */
    if (s) {
        if (s->buffer) {
            free(s->buffer);
            s->buffer = NULL;
        }

        if (s->entry) {
            db_entry_unlock(s->entry);
        }
        s->entry = NULL;
        if (s->qp) {
		    s->qp = NULL;
	    }

        HASH_DEL(internal_q, s);
        free(s);
        s = NULL;
    }
}

void push_pending_request(uint16_t tid, wr_id_info info, client_request_headers_t *headers, const char *buffer, struct ibv_qp *qp) {
    server_pending_request_t *req = get_server_pending_request(tid);

    if (req) {
        push_back_pending_request(req);
        return;
    }
    add_server_pending_request(tid);
    req = get_server_pending_request(tid);
    req->is_pending = true;
    req->tid = tid;
    req->info = info;
    req->headers = *headers;
    req->qp = qp;
    req->qp_num = qp->qp_num;

    size_t size = sizeof(client_request_headers_t) + headers->keylen + headers->buflen + 2;
    if (size <= CLIENT_REQUEST_PAYLOAD_LEN) {
        memcpy(req->buffer, buffer, size);
    }
    else {
        memcpy(req->buffer, buffer, headers->keylen);
    }
}

server_pending_request_t* pop_pending_request(void) {
    if (sorted_q) {
        server_pending_request_t *res = sorted_q;
        DL_DELETE(sorted_q, res);
        return res;

    }
    return NULL;
}

int pending_req_count(void) { /// FIXME expensiveee keep static counters instead
    int count;
    server_pending_request_t *tmp;
    DL_COUNT(sorted_q, tmp, count);
    return count;
}

void server_pending_requests_destroy(void) {

    server_pending_request_t *current_entry;
    server_pending_request_t *tmp;
    int init_key = -1;
    int count;
    count = HASH_COUNT(internal_q);
    HASH_ITER(hh, internal_q, current_entry, tmp) {
        if (current_entry->tid != init_key) {
            remove_server_pending_request(current_entry->tid);
        }
        else {
        // remove init val
            HASH_DEL(internal_q, current_entry);
            free(current_entry);
        }
    }

    count = 0;
    count = HASH_COUNT(internal_q);
    if (count) {
        fprintf(stderr, "Failure to delete pending requests from hash table\n");
        print_server_pending_requests();
    }
    int dl_count = 0;
    tmp = NULL;
    DL_COUNT(sorted_q, tmp, dl_count);
    if (count) {
        fprintf(stderr, "Failure to delete pending requests from queue\n");
    }

    if (sorted_q) {
        sorted_q = NULL;
    }

    if (internal_q) {
        free(internal_q);
        internal_q = NULL;
    }
    clear_preallocated_buffers();
}

void mark_rdma_fin_required(uint16_t transaction_id, const char *key, uint32_t qp_num) {
    server_pending_request_t *request = NULL;
    int tmp_tid = (int) transaction_id;
    HASH_FIND_INT(waiting_fin_q, &tmp_tid, request);
    if (request) {
        fprintf(stderr, "Already have pending request with tid %u, key %s. invalidating request in favor of tid %u, key %s\n",
                        request->tid, request->entry->key, transaction_id, key);
        fflush(stderr);
        remove_server_pending_request(request->tid);
        request = NULL;
    }
    request = get_empty_buffer();
    request->tid = transaction_id;
    db_entry_t *entry = db_get(key);
    request->entry = entry;
    request->qp_num = qp_num;
    HASH_ADD_INT(waiting_fin_q, tid, request);
}

int mark_rdma_fin_received(uint16_t tid) {
    server_pending_request_t *request = NULL;
    int tmp_tid = (int) tid;
    HASH_FIND_INT(waiting_fin_q, &tmp_tid, request);
    if (request) {
        db_entry_unlock(request->entry);
        if (request->buffer) {
            free(request->buffer);
            request->buffer = NULL;
        }
        HASH_DEL(waiting_fin_q, request);
        free(request);
        request = NULL;
    } else {
        return -1;
    }
    return 0;
}

void invalidate_requests_by_qp(struct ibv_qp *qp) {
    server_pending_request_t *current_entry;
    server_pending_request_t *tmp;
    int tid;
    HASH_ITER(hh, internal_q, current_entry, tmp) {
        if (current_entry->qp_num == qp->qp_num) {
            tid = current_entry->tid;
            remove_server_pending_request(tid);
        }
    }
}