// SPDX-License-Identifier: LGPL-3.0-or-later

/**
 * gcc -fsanitize=address,undefined arbitrary_id_user_data.c -std=c99 -D_POSIX_C_SOURCE=200809L -o arbitrary_id_user_data
 *
 * A simple example demonstrating how to use IncHash to 
 * store, retrieve, and delete user records using 
 * arbitrary numeric user ID as the key.
 */

#define INCHASH_IMPLEMENTATION
#define _GNU_SOURCE
#include "../inchash.h"
#include <stdlib.h>
#include <stdio.h>


typedef struct {
    char name[16];
    char gender;
    uint8_t age;
} User;


int main()
{
    // Initializing an IncHash structure.
    IncHash table = {
        .fn_type = FNV1A,            // Hash function type.    (eg. FNV1A        )
        .percent = 75,               // Load factor.           (eg. 75%          )
        .slotbit = 2,                // 2^slotbit slots.       (eg. 2 => 4 slots )
        .key_len = sizeof(unsigned), // Key length in bytes.   (eg. 4  bytes     )  
        .val_len = sizeof(User)      // Value length in bytes. (eg. 18 bytes     )
    };

    // Opening our IncHash file.
    if(!inchash_open(&table, "./userdata.inch", O_RDWR | O_CREAT, 0644))
        return EXIT_FAILURE;

    // Key-value pair.
    unsigned uid;
    User user = {};

    // ==================================
    // Begin Insertion of new users.
    // ==================================
    printf("Press enter to begin setting new users\n");
    printf("or press `q` followed by enter to skip: ");

    while (getchar() != 'q'){
        printf("\nINSERT NEW USER:\n");

        printf("- Id: "    ); scanf("%u"  , &uid   ); while (getchar() != '\n');
        printf("- Age: "   ); scanf("%hhu", &user.age); while (getchar() != '\n');
        printf("- Name: "  ); scanf("%15s",  user.name); while (getchar() != '\n');
        printf("- Gender: "); scanf(" %c" , &user.gender); while (getchar() != '\n');

        if(!inchash_set(&table, &uid, &user))
            return EXIT_FAILURE;

        printf("\nPress enter to continue setting new users\n");
        printf("or press `q` followed by enter to move on: ");
    }
    while (getchar() != '\n');

    // ==================================
    // Begin retrieval of existing users.
    // ==================================
    printf("\nPress enter to begin retrieving users\n");
    printf("or press `q` followed by enter to skip: ");

    while (getchar() != 'q'){
        printf("\nGET USER BY ID:\n");
        printf("- Id: "); scanf("%u", &uid); while (getchar() != '\n');

        User *found = inchash_get(&table, &uid);

        if(!found){
            printf("\nUser not found press enter to continue\n");
            printf("or press `q` followed by enter to move on: ");
            continue;
        }

        user = *found;
        printf("- Age: %hhu\n" , user.age); 
        printf("- Name: %s\n"  , user.name);
        printf("- Gender: %c\n", user.gender);
        
        printf("\nPress enter to continue retrieving users\n");
        printf("or press `q` followed by enter to move on: ");

    } 
    while (getchar() != '\n');

    // ==================================
    // Begin deletion of existing users.
    // ==================================
    printf("\nPress enter to begin deletion of user\n");
    printf("or press `q` followed by enter to skip: ");

    while (getchar() != 'q'){
        printf("\nDELETE USER BY ID:\n");
        printf("- Id: "); scanf("%u", &uid); while (getchar() != '\n');

        if(!inchash_del(&table, &uid)){
            printf("\nUser not found press enter to continue\n");
            printf("or press `q` followed by enter to move on: ");
            continue;
        }

        printf("User with id '%u', successfully deleted\n", uid);
        printf("\nPress enter to continue deleting users\n");
        printf("or press `q` followed by enter to move on: ");

    }

    if(!inchash_close(&table))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
