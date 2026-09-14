/**
 * A fuzz-style regression test
 *
 * gcc -fsanitize=address,undefined test_with_sequence_replay.c -std=c99 -D_POSIX_C_SOURCE=200809L -o test_with_sequence_replay
 *
 */

#define numberof(arg) ((unsigned) (sizeof(arg) / sizeof(arg[0])))
#define INCHASH_IMPLEMENTATION
#define _GNU_SOURCE
#define MAX_KEYS 128 /* forces key reuse */
#include "../inchash.h"


typedef enum {
    OP_SET = 0,
    OP_GET = 1,
    OP_DEL = 2,
    OP_MOD = 3,
    OP_COUNT = 4
} Operator;


static void* update(void *restrict slot_val, const void *restrict val, size_t n) {
    if (slot_val == NULL) return NULL; // if not found
    *(uint32_t*)slot_val += *(uint32_t*)val;
    return NULL;
}


void perform_actual(unsigned seed, unsigned n_ops, IncHash* table){
    srand(seed);
    while (n_ops--){
        Operator op =  (Operator)(rand() % OP_COUNT);
        uint32_t key = (uint32_t) rand() % MAX_KEYS;
        uint32_t val = (uint32_t) rand();
        switch (op) {
            case OP_SET: inchash_set(table, &key, &val); break;
            case OP_GET: inchash_get(table, &key); break;
            case OP_DEL: inchash_del(table, &key); break;
            case OP_MOD: inchash_mod(table, update, &key, &val); break;
            default: break;
        }
    }
}


void compute_expected(unsigned seed, unsigned n_ops, bool present[MAX_KEYS], uint32_t value[MAX_KEYS]){
    srand(seed);
    while (n_ops--){
        Operator op =  (Operator)(rand() % OP_COUNT);
        uint32_t key = (uint32_t) rand() % MAX_KEYS;
        uint32_t val = (uint32_t) rand();
        switch (op) {
            case OP_SET: present[key] = true; value[key] = val; break;
            case OP_GET: break;
            case OP_DEL: present[key] = false; break;
            case OP_MOD: value[key] += present[key] ? val : 0; break;
            default: break;
        }
    }
}


bool compare_results(bool present[MAX_KEYS], uint32_t values[MAX_KEYS]){
    IncHash table;
    uint32_t occupants = 0;
    bool ok = true;

    if (!inchash_open(&table, "./test.inch", O_RDWR, 0644))
        return false;

    for (uint32_t key = 0; key < MAX_KEYS; key++) {
        void* found = inchash_get(&table, &key);

        if (present[key]) {
            occupants++;
            if (found == NULL) {
                ok = false;
                fprintf(stderr, "key %u: expected value %u, found missing\n",
                        key, values[key]);
            } else if (*(uint32_t*)found != values[key]) {
                ok = false;
                fprintf(stderr, "key %u: expected value %u, found %u\n",
                        key, values[key], *(uint32_t*)found);
            }
        } else {
            if (found != NULL) {
                ok = false;
                fprintf(stderr, "key %u: expected absent, found value %u\n",
                        key, *(uint32_t*)found);

            }
        }
    }
    ok = (inchash_occupants(&table) == occupants) && ok;
    ok = inchash_close(&table) && ok;
    printf("%s\n", ok ? "true" : "false");
    return ok;
}


bool run_random_sequence(unsigned seed, unsigned n_ops){
    unlink("./test.inch");
    printf("random_sequence  seed=%u  n_ops=%u success=", seed, n_ops);

    IncHash table ={
        .fn_type = FNV1A,
        .percent = 75,
        .slotbit = 5, // deliberately small: forces resizes mid-sequence too 
        .key_len = 4,
        .val_len = 4,
    };

    if (!inchash_open(&table, "./test.inch", O_RDWR | O_CREAT, 0644))
        return false;

    perform_actual(seed, n_ops, &table);

    if(!inchash_close(&table))
        return false;

    bool    present[MAX_KEYS] = {};
    uint32_t values[MAX_KEYS] = {};
    compute_expected(seed, n_ops, present, values);

    if(!compare_results(present, values))
        return false;

    return true;
}


int main(){

    unsigned seeds[] = {123, 5434, 21543};
    unsigned n_ops[] = {200, 2000, 20000};

    for (unsigned i = 0; i < numberof(seeds); ++i)
        for (unsigned j = 0; j < numberof(n_ops); ++j)
            if (!run_random_sequence(seeds[i], n_ops[j])){
                unlink("./test.inch");
                return EXIT_FAILURE;
            }

    unlink("./test.inch");
    return EXIT_SUCCESS;
}
