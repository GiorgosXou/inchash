// SPDX-License-Identifier: LGPL-3.0-or-later

/**
 * gcc -fsanitize=address,undefined bare_minimal_phone_book.c -std=c99 -D_POSIX_C_SOURCE=200809L -o bare_minimal_phone_book
 *
 *  A bare-bones phone-book example. (from README.md)
 */

#define INCHASH_IMPLEMENTATION
#define _GNU_SOURCE
#include "../inchash.h"
#include <stdlib.h>


int main()
{
    IncHash table = {
        .fn_type = FNV1A, // Hash function type.    (eg. FNV1A        )
        .percent = 75,    // Load factor.           (eg. 75%          )
        .slotbit = 5,     // 2^slotbit slots.       (eg. 5 => 32 slots)
        .key_len = 16,    // Key length in bytes.   (eg. 16 bytes     )  
        .val_len = 4,     // Value length in bytes. (eg. 4  bytes     )
    };

    if(!inchash_open(&table, "./phonebook.inch", O_RDWR | O_CREAT, 0644))
        return EXIT_FAILURE;

    char key[16] = "Juliana"; // eg. given a username
    uint32_t val = 521123456; // get its phone number

    printf("Set  pair: %d\n", inchash_set(&table, &key, &val));
    printf("Get value: %u\n", *(uint32_t*)inchash_get(&table, &key));
    printf("Delete   : %d\n", inchash_del(&table, &key));
    printf("Get value: %s\n", inchash_get(&table, &key));

    if(!inchash_close(&table))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
