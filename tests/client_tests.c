#include <assert.h>

#include "client_tests.h"
#include "../client.h"
#include "tests_common.h"

void kv_test_client_side(struct kv_context *ctx, int iters) {
    char * key = NULL;
    char * value = NULL;
    for(int i = 0; i < iters; i++){
        init_random_key_and_value(&key, &value);
        printf("(TEST) Inserting key of size %lu and value of size %lu\n", strlen(key), strlen(value));
        kv_set(ctx, key, value);
        client_wait_completions(ctx, 1);
        char * ret_value = NULL;
        kv_get(ctx, key, &ret_value);
        if(strcmp(value, ret_value) != 0){
            printf("(TEST) Got keys of size %lu and value of size %lu\n INCORRECTLY!\n", strlen(key), strlen(value));
            assert(0);
        }
        
        printf("(TEST) Got keys of size %lu and value of size %lu\n correctly.\n", strlen(key), strlen(value));
        free(key);
        key = NULL;
        free(value);
        value = NULL;
    }
    printf("(TEST) All tests passed\n");
}


void kv_test(struct kv_context *ctx) {
    const char *test_keys[26] = {
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
        "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"
    };

    char temp_val[10] = {0};

    int max_iters = 100000;
    int i;
    char *out = NULL;
    for (i = 0; i < max_iters; i++) {
        sprintf(temp_val, "%d", i);
        const char *temp_key = test_keys[i % 26];
        printf("(iter %d) SET (K,V) = (%s, %.10s)\n", i, temp_key, temp_val);
        fflush(stdout);
        kv_set(ctx, temp_key, temp_val);
        // client_wait_completions(ctx, 1);
        kv_get(ctx, temp_key, &out);
        if (strcmp(out, temp_val)) {
            fprintf(stderr, "Comparison error\n");
            kv_release(out);
            exit(1);
        }
        // printf("out='%s'\n", out);
        kv_release(out);
        out = NULL;
    }
    // client_wait_completions(ctx, 3*26);
}

void kv_test_long(struct kv_context *ctx) {
    const char *test_keys[26] = {
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
        "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"
    };


    int i;

    char *test_values[26];
    for (i=0; i < 26; i++) {
        size_t size = 1 << i;
        test_values[i] = calloc(1, size);
        memset(test_values[i], test_keys[i][0], size - 1);
    }

    char *out = NULL;
    int iterations = 5000;
    const char *tmp_key;
    char *tmp_val;
    for (i = 0; i < iterations; i++) {
        tmp_key = test_keys[i % 26];
        tmp_val = test_values[i % 26];
        kv_set(ctx, tmp_key, tmp_val);
        // client_wait_completions(ctx, 1);
        kv_get(ctx, tmp_key, &out);
        if (strncmp(tmp_val, out, strlen(tmp_val))) {
            fprintf(stderr, "Comparison error iter %d\n",i );
            kv_release(out);
            for (i=0; i < 26; i++) {
                free(test_values[i]);
                test_values[i] = NULL;
            }
            exit(1);
        }
        printf("(iter#%d) strlen(long_value)=%lu, strlen(res)=%lu, res=0\n", i, strlen(tmp_val), strlen(out));
        kv_release(out);
    }
    for (i=0; i < 26; i++) {
        free(test_values[i]);
        test_values[i] = NULL;
    }
}


void kv_test_single_key(struct kv_context *ctx) {
    int i;
    char *test_values[26];
    for (i=0; i < 26; i++) {
        size_t size = 1 << i;
        test_values[i] = calloc(1, size);
        memset(test_values[i], 'A' + i, size - 1);
    }

    char *out = NULL;
    int iterations = 2000;
    const char *tmp_key = "A";
    char *tmp_val;
    for (i = 0; i < iterations; i++) {
        tmp_val = test_values[i % 26];
        kv_set(ctx, tmp_key, tmp_val);
        // client_wait_completions(ctx, 1);
        // if (i != 0 && (iterations % (SERVER_NUM_SEND_BUFFS / 2)) == 0) {
        if (i != 0 && (iterations % 3) == 0) {
            kv_get(ctx, tmp_key, &out);
            if (strncmp(tmp_val, out, strlen(tmp_val))) {
                fprintf(stderr, "Comparison error iter %d\n",i );
                kv_release(out);
                for (i=0; i < 26; i++) {
                    free(test_values[i]);
                    test_values[i] = NULL;
                }
                exit(1);
            }
            printf("(iter#%d) strlen(long_value)=%lu, strlen(res)=%lu, res=0\n", i, strlen(tmp_val), strlen(out));
            kv_release(out);
        }
    }
    for (i=0; i < 26; i++) {
        free(test_values[i]);
        test_values[i] = NULL;
    }
}

void kv_test_every_size(struct kv_context *ctx) {

    char temp_key[2] = {'A', 0};
    char *test_values[26] = {0};
    char *out;
    int i;
    size_t current_size;;
    size_t max_size = 1 << 25;
    // size_t max_size = 50000;
    for (current_size = 1; current_size <=max_size; current_size += 500000) {
        if ((current_size % 26) == 1) {
            int j;
            char *tmp;
            for (j=0; j < 26; j++) {
                tmp = test_values[j];
                test_values[j] = calloc(1, ++current_size);
                memset(test_values[j], 'A'+j, current_size - 1);
                if (tmp) {
                    free(tmp);
                }
            }
        }
        int iters;
        for (iters = 0; iters < 26; iters++) {
            printf("SET (K,V) = (%s, %.25s....)\n", temp_key, test_values[iters % 26]);
            fflush(stdout);
            kv_set(ctx, temp_key, test_values[iters % 26]);
        }
        kv_get(ctx, temp_key, &out);
        if (strlen(out) == 0) {
            kv_release(out);
            fprintf(stderr, "Received size 0\n");
            exit(1);
        }
        fprintf(stdout, "(iter %lu) key=%s len=%lu, value=%.10s ... (+%lu more characters)\n", current_size, temp_key, strlen(out), out, strlen(out) > 10 ? strlen(out) - 10 : 0);
        fflush(stdout);
        kv_release(out);
        ++temp_key[0];
        temp_key[0] = 'A' + ((temp_key[0] - 'A' + 1) % 26);
    }
    fprintf(stdout, "Done\n");
    fflush(stdout);
    for (i=0; i < 26; i++) {
        free(test_values[i]);
        test_values[i] = NULL;
    }
}
