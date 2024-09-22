#ifndef __DB_H__
#define __DB_H__
#include <stdbool.h>
#include <pthread.h>

#include "../external_libs/uthash.h"
#include "../common/constants.h"

typedef struct db_entry_t {
    UT_hash_handle hh;  // hash handler for setting up a hash table using UThash
    char *key;
    char *value;
    uint32_t keylen;
    uint32_t size;
    size_t _allocated_size;  // internal bookkeeping
    struct ibv_mr *mr;
    pthread_rwlock_t lock;
    int num_readers; // internal bookkeeping
    int num_writers; // internal bookkeeping
} db_entry_t;


int db_init(struct ibv_pd *pd);
void db_destroy(void);

int db_insert(const char *key, const char *value);
db_entry_t* db_get(const char *key);
void print_db(void);


bool db_entry_lock_wr(db_entry_t *entry);

bool db_entry_lock_rd(db_entry_t *entry);


bool db_entry_unlock(db_entry_t *entry);
void db_reallocate_item(const char *key, size_t new_size); // also invalidates previous buffer

#endif // __DB_H__
