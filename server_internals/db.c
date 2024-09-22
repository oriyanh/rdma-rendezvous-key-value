#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/param.h>
#include <infiniband/verbs.h>

#include "db.h"


static db_entry_t *internal_db = NULL;  // head of hash table
static struct ibv_pd *db_pd = NULL;
static int page_size;


static db_entry_t **preallocated_entries = NULL;
static int current_num_allocated_entries = 0;
static int free_buffer_idx = 0;
static const int MAX_ALLOCATED_ENTRIES = 1000;


static void reallocate_entries(void) {

    db_entry_t **entries;
    if (preallocated_entries == NULL) {
        entries = calloc(MAX_ALLOCATED_ENTRIES, sizeof(db_entry_t *));
        if (!entries) {
            fprintf(stderr, "DB allocation error\n");
            exit(1);
        }
        preallocated_entries = entries;
    }

    int i;
    db_entry_t *curr_entry;
    for (i=0; i < MAX_ALLOCATED_ENTRIES; i++) {
        curr_entry = preallocated_entries[i];
        if (curr_entry == NULL) {
            curr_entry = calloc(1, sizeof(db_entry_t));
            if (!curr_entry) {
                fprintf(stderr, "DB allocation error\n");
                exit(1);
            }

            size_t allocated_size = roundup(page_size, CLIENT_REQUEST_PAYLOAD_LEN);
            curr_entry->key = calloc(1, allocated_size);
            curr_entry->value = calloc(1, 2 * allocated_size);
            curr_entry->_allocated_size = 2 * allocated_size;
            curr_entry->mr = ibv_reg_mr(db_pd, curr_entry->value, curr_entry->_allocated_size,
                                IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
            preallocated_entries[i] = curr_entry;
            ++current_num_allocated_entries;
        }
    }
    free_buffer_idx = 0;
}

static db_entry_t* get_empty_buffer(void) {
    if (current_num_allocated_entries < 10) {
        reallocate_entries();
    }

    db_entry_t *empty_buffer = preallocated_entries[free_buffer_idx];
    preallocated_entries[free_buffer_idx++] = NULL;
    --current_num_allocated_entries;
    return empty_buffer;
}

static void clear_preallocated_buffers(void) {
    int i;
    db_entry_t *curr_entry;
    for (i=0; i < MAX_ALLOCATED_ENTRIES; i++) {
        curr_entry = preallocated_entries[i];
        if (curr_entry) {
            if (curr_entry->key) {
                free(curr_entry->key);
                curr_entry->key = NULL;
            }

            if (curr_entry->mr) {
                ibv_dereg_mr(curr_entry->mr);
                curr_entry->mr = NULL;
            }

            if (curr_entry->value) {
                free(curr_entry->value);
                curr_entry->value = NULL;
            }

            free(curr_entry);
            curr_entry = NULL;
        }
    }
    free(preallocated_entries);
    preallocated_entries = NULL;
}


int db_init(struct ibv_pd *pd) {
    internal_db = NULL;
    db_pd = pd;
    page_size = sysconf(_SC_PAGESIZE);
    db_entry_t *s;
    // Insert bogus key with nonsense value to initialize db
    s = get_empty_buffer();
    if (s == NULL) {
        exit(-1);
    }
    s->keylen = 3;
    const char *key = "bla";
    strncpy(s->key, key, s->keylen);
    s->size = 1;
    pthread_rwlock_init(&s->lock, NULL);
    HASH_ADD_KEYPTR( hh, internal_db, s->key, s->keylen, s );
    return 0;
}

db_entry_t* db_get(const char *key) {
    db_entry_t *s;
    HASH_FIND_STR(internal_db, key, s);  /* id already in the hash? */
    return s;
}

int db_insert(const char *key, const char *value) {
    if (strlen(key) == 0) {
        fprintf(stderr, "Key error, null key\n");
        exit(1);
    }
    db_entry_t *s = db_get(key);

    size_t size = strlen(value) + 1;
    if (s == NULL) {
        s = get_empty_buffer();
        s->keylen = strlen(key);
        strncpy(s->key, key, s->keylen+1);
        s->size = size - 1;
        strncpy(s->value, value, s->size);
        pthread_rwlock_init(&s->lock, NULL);
        HASH_ADD_KEYPTR( hh, internal_db, s->key, s->keylen, s );
        return 0;
    }
    else {
        db_reallocate_item(key, size);
        strncpy(s->value, value, s->size);
    }
    return 0;
}

void db_reallocate_item(const char *key, size_t new_size) {
    db_entry_t *entry = db_get(key);
    if (!entry) {
        // create item
        db_insert(key, "");
    }
    if (new_size < (entry->_allocated_size / 2)) {
        // no need to reallocate, buffer is already big enough
        entry->size = new_size;
        return;
    }

    // Round up to nearest page size, allocate the requested memory so we have fewer total allocations
    char *prev_buff = entry->value;
    size_t allocated_size = roundup(new_size * 4, page_size);
    entry->value = calloc(1, allocated_size);
    if (!entry->value) {
        fprintf(stderr, "Failed reallocating key %s with size %lu (requested size %lu)\n", key, allocated_size, new_size);
        exit(1);
    }
    entry->_allocated_size = allocated_size;

    struct ibv_mr *prev_mr = entry->mr;
    entry->mr = ibv_reg_mr(db_pd, entry->value, allocated_size,
                            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
    if (!entry->mr) {
        fprintf(stderr, "Failed allocating MR for key %s with size %lu (requested size %lu)\n", key, allocated_size, new_size);
        exit(1);
    }

    if (prev_mr) {
        ibv_dereg_mr(prev_mr);
        prev_mr = NULL;
    }

    if (prev_buff) {
        free(prev_buff);
        prev_buff = NULL;
    }
    entry->size = new_size;
}


void db_destroy()
{
    db_entry_t *current_entry;
    db_entry_t *tmp;

    HASH_ITER(hh, internal_db, current_entry, tmp) {
        if (current_entry->mr) {
            ibv_dereg_mr(current_entry->mr);
        }

        if (current_entry->key) {
            free(current_entry->key);
        }
        if (current_entry->value) {
            free(current_entry->value);
        }

        pthread_rwlock_destroy(&current_entry->lock);
        HASH_DEL(internal_db, current_entry);  /* delete it (and advance to next) */
        free(current_entry);             /* free it */
    }
    internal_db = NULL;
    db_pd = NULL;
    clear_preallocated_buffers();
}

void print_db(void) {
    db_entry_t *current_entry;
    db_entry_t *tmp;
    printf("== DB contents ==\n");
    HASH_ITER(hh, internal_db, current_entry, tmp) {
        printf("KEY='%s', VALUE='%s'\n", current_entry->key, current_entry->value);
    }
    printf("==========\n\n");
    fflush(stdout);
}

bool db_entry_lock_wr(db_entry_t *entry) {
    int res = pthread_rwlock_trywrlock(&(entry->lock));
    if (!res) {
        ++entry->num_writers;
        return true;
    }
    return false;
}


bool db_entry_lock_rd(db_entry_t *entry) {
    int res = pthread_rwlock_tryrdlock(&(entry->lock));
    if (!res) {
        ++entry->num_readers;
        return true;
    }
    return false;
}


bool db_entry_unlock(db_entry_t *entry) {
    if (entry->num_readers == 0 && entry->num_writers == 0) {
        fprintf(stderr, "Error, double unlock on key %s\n", entry->key);
        exit(1);
        return false;
    }

    int res = pthread_rwlock_unlock(&(entry->lock));
    if (!res) {
        if (entry->num_readers > 0) {
            --entry->num_readers;
        } else {
            --entry->num_writers;
        }
        return true;
    }
    return false;
}
