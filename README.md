<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->


## IncHash - A Disk Based Hash Table

A general-purpose, header-only C99 library for Unix-like systems, implementing a disk-based, dynamically resizable, fixed-slot, *(open-addressed)* hash table with incremental rehashing, Fibonacci-hashing *(Knuth's multiplicative method)*, per home-slot probe-bound metadata *(with additional early-exit logic)*, and triangular probing, designed for modern extent-based filesystems.

## Getting Started
Here's a very minimal example of how you should use this library:

```bash
gcc test.c -std=c99 -D_POSIX_C_SOURCE=200809L -o test && ./test
```

```c
#define INCHASH_IMPLEMENTATION
#define _GNU_SOURCE
#include "inchash.h"
#include <stdlib.h>

// A bare-bones phone-book example.
int main(int argc, char *argv[])
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
```
for anything more than that you should look into the [examples](./examples) folder.

## Disadvantages

- Metadata lives on the same pages as slots themselves.
- Metadata per slot occupies `8` whole bytes.
- No benchmarks, feel free to contribute.
- **Early** release no thread-safety yet.



## Documentation
The library file itself is fairly self explanatory and well documented. That said, everything is listed here too.

1. **Main Functions:**

```c
bool  inchash_open  (IncHash* table, const char* path, int flags, mode_t mode);
bool  inchash_sync  (IncHash* table);
bool  inchash_close (IncHash* table);
bool  inchash_set   (IncHash* table, const void* key, const void* val);
void* inchash_get   (IncHash* table, const void* key);
bool  inchash_del   (IncHash* table, const void* key);
```


```c
bool inchash_open (IncHash* table, const char* path, int flags, mode_t mode);
```
```c
/**
  * @brief Opens a hash-table file and initializes an IncHash structure.
  * When loading pre-existing files structure-initialization is optional.
  *
  * @param table   An IncHash struct.
  * @param path    A path of the file.    (eg. ./table.inch      ) 
  * @param flags   File `open()` flags.   (e.g. O_RDWR | O_CREAT )
  * @param mode    File `open()` mode.    (e.g. 0644             )
  *
  * @return `true` or `false` depending on if the table 
  * initializes successfully or not. (errno)
  */
```


```c
bool inchash_sync(IncHash* table);
```

```c
/**
  * @brief Synchronizes changes to IncHash file.
  *
  * @param table    An IncHash struct.
  *
  * @return `true` or `false` depending on if 
  * synchronization was successful or not. 
  */
```


```c
bool inchash_close(IncHash* table);
```

```c
/**
  * @brief Synchronizes and Closes IncHash file.
  *
  * @param table   An IncHash struct.
  *
  * @return `true` or `false` depending on if clean-up +
  * synchronization was successful or not. (it frees `table->old` too).
  */
```


```c
bool inchash_set(IncHash* table, const void* key, const void* val);
```

```c
/**
  * @brief Sets a key-value pair. Additionally, it does fixed-step
  * incremental migration-checks (usually `old_table->cur_steps`) if 
  * `table->old` exists and auto-resizes the file when needed.
  *
  * @param table   An IncHash struct.
  * @param key     Key associated with value.
  * @param val     Value to associate with key.
  *
  * @return `true` unless migration fails. (errno)
  */
```

```c
void* inchash_get(IncHash* table, const void* key);
```

```c
/**
  * @brief Gets a value associated with key. 
  *
  * @param table   An IncHash struct.
  * @param key     Key associated with value.
  *
  * @return A (borrowed) pointer to the value stored in the mmap'd table.
  * The caller must NOT `free()` or `munmap()` the returned pointer.
  * The pointer remains valid until the table is unmapped, either
  * during resizing or when `inchash_close()` is called. The caller
  * may copy the value with `memcpy()` while the pointer remains valid.
  */
```


```c
bool inchash_del(IncHash* table, const void* key)
```

```c
/**
  * @brief Deletes a key-value pair. Additionally, it does fixed-step
  * incremental migration-checks (usually `old_table->cur_steps`) if 
  * `table->old` exists and auto-resizes the file when needed.
  *
  * @param table   An IncHash struct.
  * @param key     Key associated with value.
  *
  * @return `true` unless nothing was deleted or migration fails. (errno)
  */
```



2. **Extra Functions:**
```c
static inline uint32_t
     inchash_occupants         (IncHash* table);
bool inchash_migrate_remaining (IncHash* table);
bool inchash_migrate           (IncHash* table, uint32_t steps);
```


```c
static inline uint32_t inchash_occupants(IncHash* table);
```

```c
/**
  * @brief Gets total occupants. 
  *
  * @param table   An IncHash struct.
  *
  * @return The sum of both old plus new table occupants.
  */
```

```c
bool inchash_migrate(IncHash* table, uint32_t steps);
```

```c
/**
* @brief Migrates a `steps`-amount of slots.
*
* @param table    An IncHash struct.
* @param steps    The amount of check-steps.
*
* @return Always `true` unless error (errno)
*/
```

```c
bool inchash_migrate_remaining(IncHash* table)
```

```c
/**
* @brief Migrates all the remaining slots from the old table.
*
* @param table    An IncHash struct.
*
* @return Always `true` unless error (errno)
*/
```



## Research
This is an insignificant portion of a larger research project I'm working on, which I haven’t released yet. Things related to:

- *Hash Tables:*
- - [Fibonacci Hashing: The Optimization that the World Forgot][4]
- - [A fast alternative to the modulo reduction][9]
- - [Triangular numbers mod 2^n][12]
- *C & Filesystems:*
- - *mmap():*
- - - [Does mmap return aligned pointer values][5]
- - - [Why in mmap PROT_READ equals PROT_EXEC][15]
- - - [How to portably extend a file accessed using mmap()][8]
- - - [CSCI 2021: mmap()'d files and pmap utility][24]
- - *fallocate():*
- - - [fallocate vs posix_fallocate][7]
- - - [Is trimming beginning of a file using fallocate with FALLOC_FL_COLLAPSE_RANGE atomic on ext4?][23]
- - *Filesystems:*
- - - [filesystem that allows to insert blocks in file in O(1)?][10]
- - - [What do f_bsize and f_frsize in struct statvfs stand for?][17]
- - - [What can f_bsize be used for? (Is it similar to st_blksize?)][18]
- - - [What's the difference between page and block in operating systems?][20]
- - *C:*
- - - [Is there a 128 bit integer in gcc?][6]
- - - [A cool macro for optional arguments][16]
- - - [Specifying size of enum type in C][21]
- - - [Store C structs for multiple plarform use][22]
- - - [How to use ftruncate in c99 without warning][25]
- - - [Is errno thread-safe][26]
- *Similar Projects:*
- - [Diskhash - Disk-based, persistent hash tables][1]
- - [Stasher - Linear-hash table on disk][3]
- - [SetDisk - on-disk storage system][2]
- *Other:*
- - [Writing My Own Database From Scratch][11]
- - [Wiki - CDB (constant database)][27]


## Donation
Anything will be greatly appreciated, roll a dice, choose a number of your will.

- [Paypal Donation Page](https://www.paypal.com/donate/?hosted_button_id=WMCYYUQCNJS4L)
- Monero: <sub><sup>`83wNYXY1s3EByNXXwgwGpjPHjKT1AfrtyK66dWJbyuRm31rjp3zNYUvUXkocYHvWfbEEQU3unTvPK6UALXB6q8wBSeWLxcF`</sup></sub>


## License
I was tempted to use `GPLv3` but at the last moment I chose `LGPLv3` only because of those two reasons:
> Linking with Proprietary Software: It allows developers to link or integrate LGPL-licensed libraries into proprietary, closed-source applications without forcing the main application's source code to be released, provided dynamic linking (or another mechanism allowing user replacement) is used.

> Modifications: If you modify the LGPL library itself, those specific modifications must be released under the terms of the LGPL.



## Outro
If you found this project useful, or at least interesting, and would like to support my work, please consider making a donation or getting in touch if you’d like to hire me. I live with my parents, programming because I love to, since I was a teenager.





<sub>(IncHash - aka. Incas, In-Cache or INC-hash)</sub>






[27]: https://en.wikipedia.org/wiki/Cdb_(software)
[26]: https://stackoverflow.com/questions/1694164/is-errno-thread-safe
[25]: https://stackoverflow.com/questions/26806764/how-to-use-ftruncate-in-c99-without-warning
[24]: https://www.youtube.com/watch?v=PjRotgB8MHA                                                                  '2026-09-07 04:44:52 AM'
[23]: https://stackoverflow.com/questions/70066539                                                                 '2026-09-07 04:38:18 AM'
[22]: https://stackoverflow.com/questions/34728536                                                                 '2026-09-07 04:24:18 AM'
[21]: https://stackoverflow.com/questions/4879286/                                                                 '2026-09-07 04:23:17 AM'
[20]: https://stackoverflow.com/questions/22137555/whats-the-difference-between-page-and-block-in-operating-system '2026-08-12 03:23:07 AM'
[19]: https://stackoverflow.com/questions/12102332/when-should-i-use-perror-and-fprintfstderr                      '2026-08-12 05:53:11 AM'
[18]: https://unix.stackexchange.com/questions/463369/what-can-f-bsize-be-used-for-is-it-similar-to-st-blksize     '2026-08-12 06:05:51 AM'
[17]: https://stackoverflow.com/questions/54823541/what-do-f-bsize-and-f-frsize-in-struct-statvfs-stand-for        '2026-08-12 06:25:46 AM'
[16]: https://cplusplus.com/forum/beginner/284071/#msg1229998                                                      '2026-08-12 07:13:26 PM cool macro'
[15]: https://stackoverflow.com/questions/32730643/why-in-mmap-prot-read-equals-prot-exec                          '2026-08-14 10:18:20 PM'
[14]: https://gcc.gnu.org/onlinedocs/gcc/Bit-Operation-Builtins.html                                               '2026-08-17 09:27:08 AM'
[13]: https://en.cppreference.com/c/header/stdbit                                                                  '2026-08-17 09:28:02 AM'
[12]: https://fgiesen.wordpress.com/2015/02/22/triangular-numbers-mod-2n/                                          '2026-08-20 02:26:15 AM'
[11]: https://www.youtube.com/watch?v=5Pc18ge9ohI                                                                  '2026-08-23 06:00:51 PM'
[10]: https://unix.stackexchange.com/questions/281652                                                              '2026-08-23 06:02:46 PM'
[9]: https://lemire.me/blog/2016/06/27/a-fast-alternative-to-the-modulo-reduction/                                 '2026-08-23 06:09:36 PM'
[8]: https://stackoverflow.com/a/51392323/11465149                                                                 '2026-08-29 09:26:26 PM Interesting'
[7]: https://stackoverflow.com/questions/14063046/                                                                 '2026-08-29 09:37:39 PM'
[6]: https://stackoverflow.com/questions/16088282/                                                                 '2026-09-03 12:21:45 AM'
[5]: https://stackoverflow.com/questions/42259495/#comment125175573_42259829                                       '2026-09-06 11:47:26 PM' 
[4]: https://news.ycombinator.com/item?id=43677122                                                                 '2026-08-05 05:15:31 PM | klib khashl'
[3]: https://github.com/cdrttn/stasher                                                                             '2026-08-03 06:47:05 PM'
[2]: https://github.com/JuanForge/SetDisk                                                                          '2026-08-03 06:46:15 PM'
[1]: https://news.ycombinator.com/item?id=14725716                                                                 '2026-08-03 06:41:52 PM'



<!-- an incas is in chase of an incas while probing in chache just in case an incas is in chase ... lol -->
