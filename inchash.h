// SPDX-License-Identifier: LGPL-3.0-or-later

/**
 * # IncHash - A Disk Based Hash Table
 *
 * A disk-based, dynamically resizable, fixed-slot, (open-addressed) hash table
 * with Fibonacci-hashing (Knuth's multiplicative method),triangular probing,
 * per-home-slot probe-bound metadata (with additional early-exit logic), partial
 * in-place value updates (without relocating entries) and incremental rehashing,
 * all designed for Unix-like systems with modern extent-based filesystems.
 *
 *
 *  TODO: Draw a visualization of the structure with text borders and stuff
 */

  

#ifndef INCHASH_H
    #define INCHASH_H

    #include <fcntl.h>
    #include <errno.h>
    #include <stdio.h> 
    #include <unistd.h>
    #include <stdlib.h>
    #include <string.h>
    #include <stddef.h> /* NULL */
    #include <stdint.h>
    #include <stdbool.h>
    #include <sys/stat.h>
    #include <sys/mman.h>
    #include <sys/statfs.h>


    enum {
        // FNV-1a generic hash function
        // (taken from klib's khashl.h)
        FNV1A,
        // splitmix64 (taken from klib's khashl.h)
        // https://nullprogram.com/blog/2018/07/31/
        SPLITMIX64,
        // fmix32 portion of MurmurHash3
        // (taken from klib's khashl.h)
        MURMURMIX32
    };


    // Uses compiler specific extensions if possible.
    // https://en.cppreference.com/c/program/unreachable
    #if defined(__GNUC__) // GCC, Clang, ICC
        #define _inchash_unreachable() (__builtin_unreachable())
    #elif defined(_MSC_VER) // MSVC 
        #define _inchash_unreachable() (__assume(false))
    #else
        [[noreturn]] inline void unreachable_impl() {}
        #define _inchash_unreachable() (unreachable_impl())
    #endif


    /**
     * ======================================
     * CORE - PREFERENCES +++ VERSION CONTROL
     * ======================================
     */

    #define INCHASH_CORE_VERSION 1

    // Predefined hash-function. (Reduces indirect calls)
    #if defined(INCHASH_CORE_HASH)
        #if (INCHASH_CORE_HASH == 0)
            #define INCHASH_HASH inchash_fnv1a
        #elif  (INCHASH_CORE_HASH == 1)
            #define INCHASH_HASH inchash_mix64
        #elif  (INCHASH_CORE_HASH == 2)
            #define INCHASH_HASH inchash_mix32
        #else
            #error "`INCHASH_CORE_HASH` Must be: FNV1A, SPLITMIX64 or MURMURMIX32"
        #endif
    #else
        #define INCHASH_HASH table->hash
    #endif

    // Disables resizability logic.
    // #define INCHASH_CORE_UNRESIZABLE


    /**
     * ====================================
     * METADATA PER SLOT - TOTAL OF 8 BYTES
     * ====================================
     */
    #define INCHASH_SLOT_METADATA_STATE_SIZE sizeof(uint8_t)  /* STATE = State */
    #define INCHASH_SLOT_METADATA_IHOME_SIZE sizeof(uint8_t)  /* IHOME = Home index (fingerprint) */
    #define INCHASH_SLOT_METADATA_MULTI_SIZE sizeof(uint8_t)  /* MULTI = Has Multiple Displacements */
    #define INCHASH_SLOT_METADATA_FINGS_SIZE sizeof(uint8_t)  /* FINGS = fingerprint */
    #define INCHASH_SLOT_METADATA_DISPS_SIZE sizeof(uint32_t) /* DISPS = Farthest from home displacement */


    // STATE = State
    #define INCHASH_SLOT_METADATA_STATE_OFFSET 0

    // IHOME = Home index (fingerprint)
    #define INCHASH_SLOT_METADATA_IHOME_OFFSET \
    (                                          \
        INCHASH_SLOT_METADATA_STATE_SIZE       \
    )

    // MULTI = Has Multiple Displacements
    #define INCHASH_SLOT_METADATA_MULTI_OFFSET \
    (                                          \
        INCHASH_SLOT_METADATA_IHOME_OFFSET +   \
        INCHASH_SLOT_METADATA_IHOME_SIZE       \
    )
    // FINGS = fingerprint
    #define INCHASH_SLOT_METADATA_FINGS_OFFSET \
    (                                          \
        INCHASH_SLOT_METADATA_MULTI_OFFSET +   \
        INCHASH_SLOT_METADATA_MULTI_SIZE       \
    )
    // DISPS = Farthest from home displacement
    #define INCHASH_SLOT_METADATA_DISPS_OFFSET \
    (                                          \
        INCHASH_SLOT_METADATA_FINGS_OFFSET +   \
        INCHASH_SLOT_METADATA_FINGS_SIZE       \
    )
    // Total slot metadata offset
    #define INCHASH_SLOT_METADATA_OFFSET       \
    (                                          \
        INCHASH_SLOT_METADATA_DISPS_OFFSET +   \
        INCHASH_SLOT_METADATA_DISPS_SIZE       \
    )


    #define INCHASH_HOME_SLOT_HAS_MULTIPLE_DISPLACEMENTS true  /* Has Multiple Displacements mask */
    #define INCHASH_HOME_SLOT_HAS_NO_MULTI_DISPLACEMENTS false /* Has Zero or One Displacement(s) */

    #define INCHASH_SLOT_EMPTY false 
    #define INCHASH_SLOT_OCCUPIED true



    typedef void* (*inchcpy)(void *restrict __dest, const void *restrict __src, size_t __n); // default = `memcpy()`
    typedef uint32_t (*hashinc)(const void *key, uint32_t len);
    typedef struct IncHash IncHash;

    /**
    * @brief IncHash structure
    */
    struct IncHash{
        uint8_t  fn_type;   // Hash function type. (eg. FNV1A)
        uint8_t  percent;   // Load factor. (eg. 75%)
        uint8_t  slotbit;   // Slot count as a power-of-2 exponent. (eg. 6 => 2^6 = 64 slots)
        uint32_t key_len;   // Key length in bytes. (eg. 8 bytes)
        uint32_t val_len;   // Value length in bytes. (eg. 4 bytes)
        uint32_t n_slots;   // (Power-of-2) Number of slots. (2^slotbit = n_slots)
        uint32_t maximum;   // Max number-of-slots the table can have based on percentage.
        uint32_t slot_mask; // (n_slots - 1) .
        uint32_t cur_index; // Migration-cursor. Current index of migration-cursor.  
        uint8_t  cur_steps; // Migration-cursor number-of-slot-steps per set or del (ceil(1/percent) + 1)
        uint8_t  closed_ok; // If file got safely closed or not.
        uint8_t  version;   // Library-version when table was first created (INCHASH_CORE_VERSION).
        uint8_t  is_old;    // If the current table is the old table from which we migrate.
        uint32_t occupants; // Occupied slots in the latest table.
        uint64_t slot_size; // Size of slot + it's metadata for key & val lens. (4-aligned) 
        uint64_t file_size; // Size of the actual file (round-up to multiple of f_bsize).                             
        uint64_t f_bsize;   // Filesystem logical block-size.                       
        uint64_t offset;    // Offset to reach new table, when OLD table exists.           
        uint32_t checksum;  // CRC32C checksum of the IncHash struct.
        hashinc  hash;      // Hash function from fn_type.                          
        IncHash* old;       // Old table if exists.
        void*    map;       // `mmap()` of current File.                            
        int      flags;     // `mmap()` protection flags.
        int      fd;        // File-descriptor.                                     
    };



    // Main Functions
    bool  inchash_open  (IncHash* table, const char* path, int flags, mode_t mode);
    bool  inchash_sync  (IncHash* table);
    bool  inchash_close (IncHash* table);
    bool  inchash_set   (IncHash* table, const void* key, const void* val);
    void* inchash_get   (IncHash* table, const void* key);
    bool  inchash_del   (IncHash* table, const void* key);
    bool  inchash_mod   (IncHash* table, inchcpy update, const void* key, const void* ctx);

    // Extra Functions
    static inline uint32_t inchash_occupants (IncHash* table);

    #if !defined(INCHASH_CORE_UNRESIZABLE)
        bool inchash_migrate_remaining (IncHash* table);
        bool inchash_migrate           (IncHash* table, uint32_t steps);
    #endif



    #if defined(INCHASH_IMPLEMENTATION)


        /**
         * @brief FNV-1a generic hash function (taken from klib's khashl.h)
         *
         * @param key 
         * @param len
         *
         * @return 
         */
        static uint32_t inchash_fnv1a(const void *key, uint32_t len)
        {
	        const uint8_t *s = (const uint8_t*)key;
	        uint32_t h = 2166136261U;
	        for (uint32_t i = 0; i < len; ++i)
		        h ^= s[i], h *= 16777619;
	        return h;
        }



        /**
         * @brief splitmix64 (taken from klib's khashl.h)
         *
         * @param key
         * @param len   (unused, assumes sizeof 8-bytes)
         *
         * @return 
         */
        static uint32_t inchash_mix64(const void *key, uint32_t len)
        { /* splitmix64; see https://nullprogram.com/blog/2018/07/31/ for inversion */
            uint64_t x;
            memcpy(&x, key, sizeof x);
	        x ^= x >> 30;
	        x *= 0xbf58476d1ce4e5b9ULL;
	        x ^= x >> 27;
	        x *= 0x94d049bb133111ebULL;
	        x ^= x >> 31;
	        return (uint32_t)x;
        }



        /**
         * @brief fmix32 portion of MurmurHash3 (taken from klib's khashl.h)
         *
         * @param key
         * @param len   (unused, assumes sizeof 4-bytes)
         *
         * @return 
         */
        static uint32_t inchash_mix32(const void *key, uint32_t len)
        { /* murmur finishing */
            uint32_t x;
            memcpy(&x, key, sizeof x);
	        x ^= x >> 16;
	        x *= 0x85ebca6bU;
	        x ^= x >> 13;
	        x *= 0xc2b2ae35U;
	        x ^= x >> 16;
	        return x;
        }



        static hashinc get_hash_function_from(uint8_t type)
        {
            switch (type) {
                case FNV1A       :  return inchash_fnv1a;
                case SPLITMIX64  :  return inchash_mix64; // TODO:
                case MURMURMIX32 :  return inchash_mix32; // TODO:
            }
            return inchash_fnv1a;
        }



        /**
         * @brief gets `mmap()` protection flags given `open()` flags
         *
         * @param flags   `open()` flags.   (eg. O_RDWR)
         *
         * @return protection flags
         */
        static inline int get_prot_from_open(int flags) 
        {
            switch (flags & O_ACCMODE) {
                case O_RDONLY: return PROT_READ;
                case O_WRONLY: return PROT_WRITE;
                case O_RDWR:   return PROT_READ | PROT_WRITE;
                default:       return 0;
            }
        }



        static uint32_t _inchash_crc32c(const void *buf, uint64_t len)
        {
            const uint8_t *ptr = (const uint8_t *)buf;
            uint32_t crc = 0xFFFFFFFFu; // initial value

            while (len--) {
                crc ^= *ptr++;

                for (uint8_t bit = 0; bit < 8; ++bit)
                    crc = (crc >> 1) ^ ((crc & 1u) ? 0x82F63B78u : 0u);
            }

            return crc ^ 0xFFFFFFFFu;
        }



        static inline uint32_t _inchash_checksum(IncHash* table)
        {
            return _inchash_crc32c(table, offsetof(IncHash, checksum));
        }



        static inline bool _inchash_checksum_matches(IncHash* table)
        {
            return _inchash_checksum(table) == table->checksum;
        }



        /**
         * This function assumes that the table was collapsed-shrank-resized
         * (via FALLOC_FL_COLLAPSE_RANGE) during `_inchash_migrate()`, and that
         * afterwards it was unsafely-closed (`!closed_ok`). With that in mind
         * it attempts to recover a part of it.
         */
        static inline bool _inchash_checksum_matches_collapsed(IncHash* table, uint64_t st_size, int flags)
        {
            // if the old table exists, it means that it (the old table) wasn't collapsed.
            // if slotbit is 1 it means that there was no prior "old" table.
            // if it was properly-closed there is no such-case\reason to check.
            if (table->old || table->slotbit == 1 || table->closed_ok)
                return false;

            // (old table) Number of slots in a table with half the capacity.
            const uint32_t n_slots = 
                1ULL << (table->slotbit-1); // 2^(slotbit-1)

            // (old table) file size based on the half capacity (n_slots).
            const uint64_t old_file_size_offset = 
                ((( // round-up to page/table->f_bsize
                ( offsetof(IncHash, hash)
                + ((uint64_t)n_slots) * table->slot_size)
                + table->f_bsize - 1) / table->f_bsize) * table->f_bsize);

            // file size based on the half + current capacity (n_slots).
            const uint64_t new_file_size_offset = 
                old_file_size_offset + ((( // round-up to page/table->f_bsize
                ( offsetof(IncHash, hash)
                + ((uint64_t)table->n_slots) * table->slot_size)
                + table->f_bsize - 1) / table->f_bsize) * table->f_bsize);

            // Assumed current st_size calculated based on new - old
            const uint64_t assumed_st_size =
                new_file_size_offset - old_file_size_offset;

            // Compare file sizes to make sure that only one resizing has 
            // happened since last `inchash_sync`.
            if (assumed_st_size != st_size){
                fprintf(stderr,
                    "_inchash_checksum_matches_collapsed() -> assumed_st_size != st_size.\n");
                return false;
            }

            // Temporally set the offset and file size to their pre-resizing values.
            table->offset = old_file_size_offset; 
            table->file_size = new_file_size_offset;

            // Re-evaluate the checksum ...
            if (_inchash_checksum(table) == table->checksum){
                // ##17 ... and recover 
                table->offset = 0;
                table->file_size = st_size;

                return true;
            }

            fprintf(stderr,
                "_inchash_checksum_matches_collapsed() -> Table checksum mismatch.\n");

            return false;

        }



        static inline bool _has_valid_parameters(IncHash* table)
        {
            return !(
               !(table->val_len || table->key_len  || 
                 table->slotbit || table->percent) || 
                (table->percent == 0 ) ||
                (table->slotbit == 0 ) ||
                (table->percent > 100) ||
                (table->slotbit > 31 ) 
            );
        }



        // (see #18 if you wonder how I came up with all those cases)
        static inline bool _inchash_is_valid(IncHash* table, uint64_t st_size, uint64_t f_bsize, int flags)
        {
            // Check struct parameters. 
            // They seem to disappear when the table undergoes multiple 
            // resize-expand\collapse cycles before being closed unsafely.
            if (!_has_valid_parameters(table)){
                fprintf(stderr,
                    "_inchash_is_valid() -> Parameters are corrupt.\n");
                return false;
            }

            // Check if table-struct is corrupt. If so, 
            // Check if table-struct is corrupt in a "collapsed" way. If so, 
            // attempt to recover it's `offset` and `file_size` properties.
            if (!_inchash_checksum_matches(table) && 
                !_inchash_checksum_matches_collapsed(table, st_size, flags)){ // #17
                fprintf(stderr,
                    "_inchash_is_valid() -> Table checksum mismatch.\n");
                return false;
            }

            // Check if old table-struct is corrupt too.
            if (table->old && !_inchash_checksum_matches(table->old)){
                fprintf(stderr,
                    "_inchash_is_valid() -> Old-table checksum mismatch.\n");
                return false;
            }

            // if there is a pre-defined core hash-function
            // Check if the file\table matches that function
            #if defined(INCHASH_CORE_HASH)
                if (table->fn_type != INCHASH_CORE_HASH){
                    fprintf(stderr, "INCHASH_CORE_HASH vs fn_type mismatch.\n");
                    return false;
                }
            #endif

            // Check to make sure file sizes match. But only raise an error 
            // if the table WAS properly-closed or if the old table exists.
            // (Both the fact that it was properly-closed OR the existence of 
            // the old-table, indicates that the issue is deeper than just 
            // simply a case for `_inchash_recover_new`) see #12
            if ((table->closed_ok || table->old) && st_size != table->file_size){
                fprintf(stderr,
                    "_inchash_is_valid() -> st.st_size vs file_size mismatch.\n");
                return false;

            // Check if `st_size`\file-size is multiple of `f_bsize`\block-size
            // (Notice that st_size may not be `table->file_size` when closed 
            // unsafely therefore in that case we have to check both) 
            }else if (st_size % table->f_bsize){
                fprintf(stderr, 
                    "_inchash_is_valid() -> st_size is not a multiple of f_bsize.\n");
                return false;
            }

            // Check if table->file_size is multiple of f_bsize (block-size)
            if (table->file_size % table->f_bsize){
                fprintf(stderr, 
                    "_inchash_is_valid() -> file_size is not a multiple of f_bsize.\n");
                return false;
            }

            // Fail if there's file vs filesystem block-size missmatch (but only 
            // if it is in write-mode, in read-only mode it's ok [although i
            // might want to also check if `table->f_bsize` is a power of 2]).
            // TODO: if t1 doesn't exist yet add blocks until match.
            if (table->f_bsize != f_bsize && (flags & O_ACCMODE) != O_RDONLY){
                fprintf(stderr, 
                    "_inchash_is_valid() -> File vs Filesystem block-size mismatch.\n");
                return false;
            }

            return true;
        }



        static inline bool _inchash_recover_struct(IncHash* table, bool is_old)
        {
            // First recover the old table if it exists.
            // Necessary even if INCHASH_CORE_UNRESIZABLE is enabled because we 
            // may have opened a file that already is resized and therefore if 
            // we `inchash_del` something and then unsafely close it, occupants 
            // will be wrong.
            if (table->old)
                _inchash_recover_struct(table->old, true);

            // we need to recover occupants after an unsafe close for:
            // - migration #9
            // - table->maximum
            uint32_t occupants = 0;
            uint32_t cur_index = 0;

            const uint8_t *const struct_offset =
                (uint8_t*)(table->map)
                + offsetof(IncHash, hash)
                + table->offset;

            for (uint32_t i=0; i<table->n_slots; ++i){

                const uint8_t *const slot_state =
                    (const uint8_t *)
                    ( struct_offset 
                    + i * table->slot_size);

                // Check if slot exists ...
                if (*slot_state){
                    const uint8_t *const slot_ihome = // home_index
                        slot_state + INCHASH_SLOT_METADATA_IHOME_OFFSET;

                    const uint8_t *const slot_fingerprint = // Slot Fingerprint
                        slot_state + INCHASH_SLOT_METADATA_FINGS_OFFSET;

                    const void *const slot_key =
                        ( slot_state + INCHASH_SLOT_METADATA_OFFSET);

                    const uint32_t hash = // Multiplied by Knuth's constant.
                        INCHASH_HASH(slot_key, table->key_len) * 2654435769U; // table->hash
                                                                         //
                    const uint32_t home_index = // extract top table->slotbit bits.
                        hash >> (32 - table->slotbit);

                    const uint8_t recomputed_fingerprint =
                        (uint8_t)(hash >> table->slotbit);

                    occupants++; // #9
                    // set cur_index to the first occupied slot
                    if (is_old){
                        cur_index = i;
                        is_old = false;
                    }

                    if (recomputed_fingerprint != *slot_fingerprint ||
                        *slot_ihome != (uint8_t)(home_index)){
                        //TODO: callback event and\or implement repair tactics.
                        // if i do so I might need to remove madvise at #11 or
                        // switch it or something. 
                        fprintf(stderr, 
                            "_inchash_recover_struct -> corrupt slot at %u.\n", i);
                        return false;
                    }
                }
            }

            // recover occupants
            table->occupants = occupants; // #9

            // recover cur_index
            table->cur_index = cur_index;
             
            return true;
        }



        static inline bool _inchash_recover_new(IncHash* table, uint64_t st_size, uint64_t f_bsize)
        {
            // Assumed file-size based on how `inchash_migrate` resizes the file 
            // when a new table needs to be created. (resizing occurs at #14)
            const uint64_t assumed_st_size =
                table->file_size +
                ((( // round-up to page/table->f_bsize
                ( offsetof(IncHash, hash) // NOTE: where * 2 happens
                + ((uint64_t)table->n_slots * 2) * table->slot_size)
                + table->f_bsize - 1) / table->f_bsize) * table->f_bsize);

            // If the file-size differs from what we expect/assume based on the 
            // initially read table, we return, as this indicates that either the 
            // initially read table structure or the file itself is corrupt. #14
            if (st_size != assumed_st_size){
                fprintf(stderr, 
                    "_inchash_recover_new() -> st_size != assumed_st_size.\n");
                return false;
            }

            // Allocate memory for a copy of the old table.
            IncHash* table_copy = (IncHash*)malloc(sizeof(IncHash));

            if (table_copy == NULL) {
                perror("_inchash_recover_new() -> malloc()");
                return false;
            }

            // Copy the initial table into the old table.
            memcpy(table_copy, table, sizeof(IncHash));

            // recover stuff based on the `memcpy`-ed old one (don't assume
            // that the new is synced correctly, base it upon the old).
            table->slotbit++;                      // add one bit (power-of-2)
            table->offset = table_copy->file_size; // set offset from OLD
            table->file_size = assumed_st_size;    // set new file_size
            table->n_slots *= 2;                   // double the number-of-slots
            table->maximum *= 2;                   // double the maximum of ^^^^
            table->occupants = 0;                  // reset occupants
            table->cur_index = 0;                  // reset Migration-cursor
            table->slot_mask = (table->n_slots -1);

            // unmap the old size
            if(munmap(table_copy->map, table_copy->file_size)){
                perror("_inchash_recover_new() -> munmap()");
                return false;
            }

            // re-map using the new file_size
            table->map = mmap(NULL, table->file_size, table->flags, 
                MAP_SHARED, table->fd, 0
            );

            // check if it was successful
            if (table->map == MAP_FAILED){
                perror("_inchash_recover_new() -> mmap()");
                return false;
            }

            // Set the table to point at the correct old table
            table->old = table_copy;

            // set the old table to point to the new mmap
            table->old->map = table->map;

            // set old's old table to point to NULL (Defaults)
            table->old->old = NULL;

            return true;
        }



        /**
         *  WARN: ##13 This "private" function assumes that `_inchash_is_valid`
         *  has already been called (particularly, that condition #12 has
         *  been checked) plus that the table was not properly closed
         *  (`!table->closed_ok`). Both conditions are satisfied in
         *  `inchash_open`, where this function is used.
         *
         *  I'm documenting these assumptions here just in case someone,
         *  out of desperation, attempts to call this function elsewhere.
         *  At least they'll know that this is not the proper solution,
         *  and that there is another method I intend to implement once
         *  I find the time.
         *
         * (see #18 if you wonder how I came up with all those cases)
         */
        static inline bool _inchash_recover(IncHash* table, uint64_t st_size, uint64_t f_bsize)
        {
            // if it is in read-mode, fail.
            if (!(table->flags & PROT_WRITE)){
                fprintf(stderr, 
                    "_inchash_recover() -> Needs write permissions.\n");
                return false;
            }

            // If sizes don't match, it's most likely due to the old table being
            // read as if it is the only one existing [...] Therefore we can 
            // attempt to recover the new one (not by looking forwards in the
            // file but based upon the old one [which is still credible
            // since it's checksum was validated]).
            // WARN: (see #12 #13 #14 for more infos as to why).
            if (st_size != table->file_size && 
                !_inchash_recover_new(table, st_size, f_bsize)){
                perror("_inchash_recover() -> Failed to recover new table.");
                return false;
            }

            // Hint the kernel that we will be accessing the file sequentially #11
            if (madvise(table->map, table->file_size, MADV_SEQUENTIAL) == -1) {
                perror("_inchash_recover() -> madvise MADV_SEQUENTIAL");
                return false;
            }

            // recover both old and new table structs 
            const bool ret = _inchash_recover_struct(table, false);

            // Reset the hint back to normal
            if (madvise(table->map, table->file_size, MADV_NORMAL) == -1) {
                perror("_inchash_recover() -> madvise MADV_NORMAL");
                return false;
            }

            return ret;
        }



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
        bool inchash_open(IncHash* table, const char *path, int flags, mode_t mode)
        {
            struct stat st;
            struct statfs sfs;

            // Open file-path and if it doesn't exist create it
            table->fd = open(path, flags, mode);

            if (table->fd == -1){
                perror("inchash_open() -> open()"); 
                return false;
            }

            // fstatfs because we need filesystem logical block-size (see #1)
            if (fstatfs(table->fd , &sfs)){
                perror("inchash_open() -> fstatfs()"); 
                close(table->fd);
                return false;
            }

            if (fstat(table->fd, &st)){
                perror("inchash_open() -> fstat()"); 
                close(table->fd);
                return false;
            } 

            // If file-size is 0 treat it as new. (notice #10)
            if (!st.st_size) {

                // Check if parameters are set correctly
                if (!_has_valid_parameters(table)){
                    fprintf(stderr, "Parameters are not set correctly.\n");
                    close(table->fd);
                    return false;
                }

                // if there is a pre-defined core hash-function set it's type
                #if defined(INCHASH_CORE_HASH)
                    table->fn_type = INCHASH_CORE_HASH;
                #endif

                // Set Defaults and stuff...
                table->version = INCHASH_CORE_VERSION;
                table->n_slots = 1ULL << table->slotbit; // 2^slotbit
                table->f_bsize = sfs.f_bsize; // fs logical block-size
                table->maximum = ((uint64_t)table->percent * (table->n_slots)) / 100;
                table->slot_mask = ((table->n_slots) - 1);
                table->cur_steps = ((100 + table->percent - 1) / table->percent) + 1;

                table->slot_size = 
                    ( INCHASH_SLOT_METADATA_OFFSET
                    + (uint64_t)table->key_len
                    + (uint64_t)table->val_len 
                    + 3) & ~3; // 4-aligned

                table->file_size = ((( // round-up to page/table->f_bsize
                    ( offsetof(IncHash, hash)
                    + table->n_slots * table->slot_size) 
                    + table->f_bsize - 1) / table->f_bsize) * table->f_bsize);

                // Sanity Check (see also #0)
                if (table->n_slots > UINT64_MAX / table->slot_size){
                    fprintf(stderr, 
                        "Max slots reached, reduce slot_size or slotbit.\n");
                    close(table->fd);
                    return false;
                }

                // ftruncate up to the nearest multiple of logical block-size
                if(ftruncate(table->fd, table->file_size)){
                    perror("inchash_open() -> ftruncate()"); 
                    close(table->fd);
                    return false;
                }

                // Write table into the file and check if written.
                if (write(table->fd, table, offsetof(IncHash, hash)) == -1){
                    perror("inchash_open() -> write(offsetof(IncHash, hash)"); 
                    close(table->fd);
                    return false;
                }

            // todo: also check if it is a valid file via seek and greater than
            }else{

                // Load the table that lives in the beginning of the file.
                read(table->fd, table, offsetof(IncHash, hash));

                // Check if `table->version` matches the version of the library. #16
                if (table->version && table->version != INCHASH_CORE_VERSION){
                    fprintf(stderr, "IncHash file, version missmatch.\n");
                    close(table->fd);
                    return false;
                }

                // If the initially read table is the old one, preserve a copy 
                // of it before reading the new table, allowing the new table to 
                // reference the previous table.
                if (table->is_old){

                    // Allocate memory for a copy of the old table.
                    IncHash* table_copy = (IncHash*)malloc(sizeof(IncHash));

                    if (table_copy == NULL) {
                        perror("inchash_open() -> malloc()");
                        return false;
                    }

                    // Copy the initial table into the old table.
                    memcpy(table_copy, table, sizeof(IncHash));

                    // Set the old table reference in the new table.
                    table->old = table_copy;

                    // Seek to the beginning of the new table on disk.
                    lseek(table->fd, table->old->file_size, SEEK_SET);

                    // Read the new table's metadata.
                    read(table->fd, table, offsetof(IncHash, hash));

                // Otherwise, initialize the old table reference to NULL,
                // as no previous table needs to be referenced.
                }else{
                    table->old = NULL;
                }

                // Check if things are valid (eg. struct checksums) + recover #17
                if (!_inchash_is_valid(table, st.st_size, sfs.f_bsize, flags)){
                    fprintf(stderr, "inchash_open() -> Failed to validate.\n");
                    close(table->fd);
                    return false;
                }
            }

            // Set the hash-function used for inchash_set\get [...]
            table->hash = get_hash_function_from(table->fn_type);

            // Store `mmap()` protection flags for later use
            table->flags = get_prot_from_open(flags);

            // Memory-Map the file
            table->map = mmap(NULL, table->file_size, table->flags, 
                MAP_SHARED, table->fd, 0
            );

            // check if it was successful
            if (table->map == MAP_FAILED){
                perror("inchash_open() -> mmap()");
                close(table->fd);
                return false;
            }

            // if old table exists set common stuff
            if (table->old){
                table->old->flags = table->flags;
                table->old->hash = table->hash;
                table->old->map = table->map;
                table->old->fd = table->fd;
                table->old->old = NULL;
            }

            // if existing-table was unsafely-closed attempt to recover it #10
            if (st.st_size && !table->closed_ok && 
                !_inchash_recover(table, st.st_size, sfs.f_bsize)){
                fprintf(stderr, "inchash_open() -> Failed to recover.\n");
                close(table->fd);
                return false;
            }

            // Mark the file as open (aka. not properly-closed yet)
            table->closed_ok = false;

            // if file is opened with write permissions sync
            if (table->flags & PROT_WRITE){
                return inchash_sync(table);
            }

            return true;
        }



        bool _inchash_del(IncHash* table, const void* key)
        {
            // if there's an old table attempt to delete first there.
            // if found inside the old table and got deleted then
            // store it in case nothing is found in the new too.
            bool is_old_deleted = (table->old) // see #6
                ? _inchash_del(table->old, key)
                : false;

            const uint32_t hash = // Multiplied by Knuth's constant.
                INCHASH_HASH(key, table->key_len) * 2654435769U; // table->hash

            const uint32_t home_index = // extract top table->slotbit bits.
                hash >> (32 - table->slotbit);

            const uint8_t fingerprint =
                (uint8_t)(hash >> table->slotbit);

            const uint8_t *const struct_offset =
                (uint8_t*)(table->map)
                + offsetof(IncHash, hash)
                + table->offset;

            const uint64_t home_slot =
                home_index * table->slot_size;

            uint8_t *const home_has_multiple_displacements = 
                (uint8_t *)
                ( struct_offset + home_slot
                + INCHASH_SLOT_METADATA_MULTI_OFFSET);

            uint32_t *const farthest_displacement_from_home_slot = 
                (uint32_t *)
                ( struct_offset + home_slot
                + INCHASH_SLOT_METADATA_DISPS_OFFSET);


            uint32_t probe =  // i * (i + 1) / 2;
                ((uint64_t)(*farthest_displacement_from_home_slot) * 
                ((uint64_t)(*farthest_displacement_from_home_slot) + 1)) / 2;

            for (uint32_t i= *farthest_displacement_from_home_slot; ; --i){

                uint64_t slot = // is home_slot if i=0
                    ((home_index + probe) & table->slot_mask) * table->slot_size;

                uint8_t* slot_state = // Slot State
                    (uint8_t*)(struct_offset + slot);

                uint8_t* slot_ihome = // Slot home_index (fingerprint)
                    (uint8_t* )(slot_state + INCHASH_SLOT_METADATA_IHOME_OFFSET);

                uint8_t* slot_fingerprint = // Slot Fingerprint
                    (uint8_t* )(slot_state + INCHASH_SLOT_METADATA_FINGS_OFFSET);

                const void* slot_key =
                                slot_state + INCHASH_SLOT_METADATA_OFFSET;

                // If slot is not empty and all: 
                // fingerprints, home indexes & keys, match
                if (*slot_state && 
                    *slot_fingerprint == fingerprint &&
                    *slot_ihome == (uint8_t)(home_index) &&
                    memcmp(slot_key, key, table->key_len) == 0) {

                    // mark slot as INCHASH_SLOT_EMPTY 
                    *slot_state = INCHASH_SLOT_EMPTY; 

                    // Decrement table's slot occupants
                    table->occupants--;

                    // if key was found at home_slot return;
                    if(!i)
                        return true;

                    // if i is not 0, (aka. home_slot) then
                    // it means that the current pair is displaced, so:
                    // if home_slot has only one displacement (not multiple)
                    // it means that the farthest_displacement_from_home_slot
                    // is the only-one displacement it can be. Therefore:
                    if (!(*home_has_multiple_displacements)){

                        // we can safely zero vvv and return true;
                        *farthest_displacement_from_home_slot = 0;

                    // else if i is the farthest_displacement_from_home_slot
                    // but INCHASH_HOME_HAS_MULTIPLE_DISPLACEMENTS
                    //( TODO: use common uint32_t j for both loops)
                    } else if (i == *farthest_displacement_from_home_slot){
                        for (uint32_t j=i-1; j > 0; --j){
                            probe = 
                                j * (j + 1) / 2;

                            slot =
                                ((home_index + probe) & table->slot_mask) 
                                * table->slot_size;

                            slot_state = 
                                (uint8_t*)(struct_offset + slot);

                            slot_ihome = // Slot home_index (fingerprint)
                                (uint8_t*)(
                                slot_state + INCHASH_SLOT_METADATA_IHOME_OFFSET);

                            slot_key =
                                slot_state + INCHASH_SLOT_METADATA_OFFSET;

                            // find the next live displacement\probe behind 
                            // i\farthest_displacement_from_home_slot
                            // that belongs to the home_index
                            if (*slot_state && 
                                *slot_ihome == (uint8_t)(home_index)){

                                const uint32_t prob_hash = // table->hash
                                    INCHASH_HASH(slot_key, table->key_len)
                                    * 2654435769U; 

                                const uint32_t prob_home_index = 
                                    prob_hash >> (32 - table->slotbit);

                                // if probe's home index is equal to home index
                                // replace farthest_displacement_from_home_slot 
                                // with j and goto clear;
                                if (prob_home_index == home_index){
                                    *farthest_displacement_from_home_slot = j;

                                    // if `j == 1` there's no room for anything
                                    // else besides `home_slot`, therefore: 
                                    // since `j` is our match we can safely 
                                    // reset `home_has_multiple_displacements`
                                    if (j == 1)
                                        *home_has_multiple_displacements = 
                                            INCHASH_HOME_SLOT_HAS_NO_MULTI_DISPLACEMENTS;

                                    return true;
                                }
                            }
                        }

                        // if no live displacement\probe was found then, reset:
                        // farthest_displacement_from_home_slot to zero/home
                        // (even if it is not alive [because we know that there 
                        // is nothing to probe relative to home_slot])
                        *farthest_displacement_from_home_slot = 0;

                        // and reset displacements
                        *home_has_multiple_displacements = 
                            INCHASH_HOME_SLOT_HAS_NO_MULTI_DISPLACEMENTS;
                    }
                    return true;
                }

                // if key wasn't found at none of the probed-slots 
                // Then return whether was deleted in old table (is_old_deleted)
                if (i == 0)
                    return is_old_deleted;

                // Decrement triangular probe
                probe -= i; // i * (i + 1) / 2;
            }
        }



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
         * If it does not exist, it simply returns `NULL`.
         */
        void* inchash_get(IncHash* table, const void* key)
        {
            const uint32_t hash = // Multiplied by Knuth's constant.
                INCHASH_HASH(key, table->key_len) * 2654435769U; // table->hash

            const uint32_t home_index = // extract top table->slotbit bits.
                hash >> (32 - table->slotbit);

            const uint8_t fingerprint =
                (uint8_t)(hash >> table->slotbit);

            const uint8_t *const struct_offset =
                (const uint8_t*)(table->map)
                + offsetof(IncHash, hash)
                + table->offset;

            const uint64_t home_slot =
                home_index * table->slot_size;

            const uint8_t *const home_has_multiple_displacements =
                (const uint8_t*)
                (struct_offset + home_slot
                + INCHASH_SLOT_METADATA_MULTI_OFFSET);

            const uint32_t *const farthest_displacement_from_home_slot =
                (const uint32_t*)
                ( struct_offset + home_slot
                + INCHASH_SLOT_METADATA_DISPS_OFFSET);

            // if we know that home_slot has only one displacement (or none)
            // then once we check for home_slot, we can safely skip probing
            // and directly check for the farthest_displacement_from_home_slot.
            const uint32_t step =
                (*home_has_multiple_displacements)
                ? 1 : (((*farthest_displacement_from_home_slot) *
                       ((*farthest_displacement_from_home_slot) + 1) / 2));

            const uint32_t far_step =
                (*home_has_multiple_displacements)
                ? 1 : (*farthest_displacement_from_home_slot);

            uint32_t farthest =
                (*farthest_displacement_from_home_slot);

            uint32_t probe = 0; // i * (i + 1) / 2;
            
            for(uint32_t i = 0; ; i += step){

                const uint64_t slot = // is home_slot if i=0
                    ((home_index + probe) & table->slot_mask) * table->slot_size;

                const uint8_t *const slot_state = // Slot State
                    struct_offset + slot;

                const uint8_t *const slot_ihome = // home_index
                    slot_state + INCHASH_SLOT_METADATA_IHOME_OFFSET;

                const uint8_t *const slot_fingerprint = // Slot Fingerprint
                    slot_state + INCHASH_SLOT_METADATA_FINGS_OFFSET;

                const void *const slot_key =
                    slot_state + INCHASH_SLOT_METADATA_OFFSET;

                // If slot is not empty and all:
                //  fingerprints, home indexes & keys, match
                if (*slot_state &&
                    *slot_fingerprint == fingerprint &&
                    *slot_ihome == (uint8_t)(home_index) &&
                    memcmp(slot_key, key, table->key_len) == 0) {

                    // found, return pointer to the `slot_val`
                    return (void*)(slot_key + table->key_len);

                // else if we've looked all
                } else if (!farthest){
                    break;
                }

                farthest -= far_step;
                probe += (i + step); // i * (i + 1) / 2;
            }

            // if nothing was found and the old table exist, look at that.
            // else return NULL.
            return (table->old)
                ? inchash_get(table->old, key) // (see also idea #3)
                : NULL;
        }



        void _inchash_new(IncHash* table, inchcpy update, const void* key, const void* val)
        {
            const uint32_t hash = // Multiplied by Knuth's constant.
                INCHASH_HASH(key, table->key_len) * 2654435769U; // table->hash

            const uint32_t home_index = // extract top table->slotbit bits.
                hash >> (32 - table->slotbit);

            const uint8_t fingerprint =
                (uint8_t)(hash >> table->slotbit);

            uint8_t *const struct_offset =
                (uint8_t*)(table->map)
                + offsetof(IncHash, hash)
                + table->offset;

            const uint64_t home_slot =
                home_index * table->slot_size;

            uint8_t *const home_state =
                (uint8_t *)(struct_offset + home_slot);

            uint8_t *const home_has_multiple_displacements = 
                (uint8_t * )( home_state + INCHASH_SLOT_METADATA_MULTI_OFFSET);

            uint32_t *const farthest_displacement_from_home_slot = 
                (uint32_t *)( home_state + INCHASH_SLOT_METADATA_DISPS_OFFSET);

            uint32_t probe = 0; // i * (i + 1) / 2;

            for(uint32_t i = 0; i < table->n_slots; ++i){

                const uint64_t slot = // is home_slot if i=0
                    ((home_index + probe) & table->slot_mask) * table->slot_size;

                uint8_t *const slot_state =
                    struct_offset + slot;

                // if (probe)-slot is empty (INCHASH_SLOT_EMPTY) insert pair
                if (!(*slot_state)) {

                    uint8_t *const slot_ihome =
                        slot_state + INCHASH_SLOT_METADATA_IHOME_OFFSET;

                    uint8_t *const slot_fingerprint = // Slot Fingerprint
                        slot_state + INCHASH_SLOT_METADATA_FINGS_OFFSET;

                    void *const slot_key =
                        slot_state + INCHASH_SLOT_METADATA_OFFSET;

                    void *const slot_val =
                        slot_key + table->key_len;

                    *slot_ihome = (uint8_t)(home_index);
                    
                    *home_has_multiple_displacements = // (see #4)
                        (bool)(*farthest_displacement_from_home_slot);

                    *slot_fingerprint = fingerprint;

                    *farthest_displacement_from_home_slot =
                        ( i > *farthest_displacement_from_home_slot)
                        ? i : *farthest_displacement_from_home_slot;

                    memcpy(slot_key, key, table->key_len);
                    update(slot_val, val, table->val_len);

                    // Commit changes, mark slot as occupied
                    *slot_state = INCHASH_SLOT_OCCUPIED;

                    // Increment table's slot occupants
                    table->occupants++;

                    return;
                }

                // Increment triangular probe
                probe += (i + 1); // i * (i + 1) / 2;
            }

            //  unreachable thanks to triangular probing.
            // (unless i've done something horribly wrong)
            _inchash_unreachable();
        }



        void _inchash_copy_struct_to_mmap(IncHash* table)
        {
            // Update the CRC-checksum of the table-struct
            table->checksum = _inchash_checksum(table);

            if (table->old){

                // Update the CRC-checksum of the old table-struct 
                table->old->checksum = _inchash_checksum(table->old);

                memcpy(table->map, table->old, offsetof(IncHash, hash));

            }

            memcpy((uint8_t*)
                table->map + table->offset, table, offsetof(IncHash, hash));
        }



        // if user has disabled realizability
        #if defined(INCHASH_CORE_UNRESIZABLE)

            // compiler is smart enough to inline it :P
            static inline bool _inchash_migrate(IncHash* old_table, IncHash* table, uint32_t steps){
                // return `true` if we haven't reached maximum else `false`
                return (table->occupants != table->maximum);
            }

        #else

            static inline bool _inchash_migrate(IncHash* old_table, IncHash* table, uint32_t steps)
            {
                // if OLD-table exists do _steps amount of migration-steps
                if (old_table){

                    steps = steps ? steps : old_table->cur_steps;

                    const uint32_t remaining =
                        old_table->n_slots - old_table->cur_index;

                    const uint32_t _steps =
                        ( steps < remaining ) ? steps : remaining;

                    const uint8_t *const old_struct_offset =
                        (const uint8_t*)(old_table->map)
                        + offsetof(IncHash, hash);

                    for (uint32_t i=0; i<_steps; ++i){

                        const uint8_t *const cur_slot_state = 
                            (const uint8_t *)
                            ( old_struct_offset 
                            + old_table->cur_index * old_table->slot_size);

                        // Check if slot exists
                        if (*cur_slot_state){
                            const void *const cur_slot_key = 
                                ( cur_slot_state 
                                + INCHASH_SLOT_METADATA_OFFSET);

                            const void *const cur_slot_val = 
                                ( cur_slot_key 
                                + old_table->key_len);

                            // Since `inchash_set` always checks whether a key
                            // already exists in both tables, a key that previously
                            // belonged to the old table is guaranteed not to exist
                            // in the new table, regardless of what has been set
                            // since. We can therefore insert it directly with
                            // `_inchash_new` and remove the old entry with
                            // `_inchash_del`.
                            _inchash_new(table, memcpy, cur_slot_key, cur_slot_val);
                            _inchash_del(old_table, cur_slot_key);

                        }
                        // increment current-index of migration-cursor.
                        old_table->cur_index++;
                    }

                    // if no more occupants, remove old table from file.
                    if (!old_table->occupants){ // #9

                        // reset table
                        table->old = NULL;
                        table->offset = 0;

                        // We set `errno` to `ENOENT` for `inchash_del`
                        errno = ENOENT;

                        // unmap the whole file
                        if(munmap(table->map, table->file_size)){
                            perror("_inchash_migrate() -> collapse -> munmap()");
                            return false;
                        }

                        // subtract OLD file_size & collapse the old_table's blocks
                        table->file_size -= old_table->file_size;

                        // attempt to collapse\resize // see #6
                        if (fallocate(table->fd, 
                            FALLOC_FL_COLLAPSE_RANGE, 0, old_table->file_size)){
                            perror("_inchash_migrate() -> collapse -> fallocate()");
                            return false;
                        }

                        // free old_table
                        free(old_table);

                        // remap the file
                        table->map = mmap(NULL, table->file_size, table->flags, 
                            MAP_SHARED, table->fd, 0
                        );

                        // check if it was successful
                        if (table->map == MAP_FAILED){
                            perror("_inchash_migrate() -> collapse -> mmap()");
                            return false;
                        }

                        // Copying the struct+core to the underlying map 
                        // effectively writes changes to the disk asynchronously
                        _inchash_copy_struct_to_mmap(table);

                    // We `else` and not `return` directly afterwards, because if
                    // user performs only `inchash_set()` consecutively, he may
                    // already have migrated all the OLD slots & (at the same time)
                    // reached the maximum capacity of the NEW one, therefore 
                    // in that case it will be necessary to expand\double-the-NEW
                    // immediately after cropping the OLD.
                    }else{
                        return true;
                    }

                } 

                // if full create a new table & keep old as reference for migration
                if (table->occupants == table->maximum){

                    // We set `errno` to `ENOENT` for `inchash_del`
                    errno = ENOENT;

                    if (table->slotbit == 31){
                        fprintf(stderr, 
                            "You reached the maximum limit of 2^31 slots.\n");
                        errno = EOVERFLOW;
                        return false;
                    }

                    // Sanity Check (see also #0)
                    if (table->n_slots*2 > UINT64_MAX / table->slot_size){
                        fprintf(stderr, 
                            "You reached maximum slots based on slot_size.\n");
                        errno = EOVERFLOW;
                        return false;
                    }

                    // Allocate memory for a copy of the old table.
                    IncHash *copy_table = (IncHash*)malloc(sizeof(IncHash));

                    if (copy_table == NULL){
                        perror("_inchash_migrate() -> malloc()");
                        return false;
                    }

                    const uint64_t old_file_size = 
                        table->file_size;

                    const uint64_t new_file_size = 
                        table->file_size +
                        ((( // round-up to page/table->f_bsize
                        ( offsetof(IncHash, hash) // NOTE: where * 2 happens
                        + ((uint64_t)table->n_slots * 2) * table->slot_size)
                        + table->f_bsize - 1) / table->f_bsize) * table->f_bsize);

                    // Sanity Check (see also #0)
                    if (new_file_size < old_file_size){
                        fprintf(stderr, 
                            "You reached maximum slots based on file_size.\n");
                        errno = EOVERFLOW;
                        return false;
                    }

                    if(munmap(table->map, table->file_size)){
                        perror("_inchash_migrate() -> munmap()");
                        return false;
                    }

                    if (ftruncate(table->fd, new_file_size)){ // ##14
                        perror("_inchash_migrate() -> ftruncate()");
                        return false;
                    }
                    
                    // Memory-Map the file
                    table->map = mmap(NULL, new_file_size, table->flags, 
                        MAP_SHARED, table->fd, 0
                    );

                    // check if it was successful
                    if (table->map == MAP_FAILED){
                        perror("_inchash_migrate() -> mmap()");
                        return false;
                    }

                    // copy table into OLD
                    memcpy(copy_table, table, sizeof(IncHash));

                    // Increment main table to the NEW state
                    table->old = copy_table;           // reference OLD from NEW
                    table->slotbit++;                  // add one bit (power-of-2)
                    table->offset = old_file_size;     // set offset from OLD
                    table->file_size = new_file_size;  // set new file_size
                    table->n_slots *= 2;               // double the number-of-slots
                    table->maximum *= 2;               // double the maximum of ^^^^
                    table->occupants = 0;              // reset occupants
                    table->cur_index = 0;              // reset Migration-cursor
                    table->slot_mask = (table->n_slots -1);

                    // Mark the old table as old
                    copy_table->is_old = true;

                    // Copying the struct to the underlying map 
                    // effectively writes changes to the disk asynchronously
                    _inchash_copy_struct_to_mmap(table);
                }


                return true;
            }



            /**
            * @brief Migrates a `steps`-amount of slots.
            *
            * @param table    An IncHash struct.
            * @param steps    The amount of check-steps.
            *
            * @return Always `true` unless error (errno)
            */
            bool inchash_migrate(IncHash* table, uint32_t steps)
            {
                return _inchash_migrate(table->old, table, steps);
            }



            /**
            * @brief Migrates all the remaining slots from the old table.
            *
            * @param table    An IncHash struct.
            *
            * @return Always `true` unless error (errno)
            */
            bool inchash_migrate_remaining(IncHash* table)
            {
                return _inchash_migrate(table->old, table, UINT32_MAX);
            }

        #endif // defined(INCHASH_CORE_UNRESIZABLE)



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
        bool inchash_set(IncHash* table, const void* key, const void* val)
        {
            // Do a few (migration + overflow) checks.
            // ( ensures that we can add new keys )
            if (!_inchash_migrate(table->old, table, 0))
                return false;

            void* found = inchash_get(table, key); // see #6

            // If key was found inside one of the tables
            // simply `memcpy` the new `val` exactly there
            if (found)
                memcpy(found, val, table->val_len);

            // Else insert it directly into the new table
            else
                _inchash_new(table, memcpy, key, val);

            // If checks didn't fail simply return true
            return true;
        }



        /**
         * @brief Modifies\Updates an existing key-value pair using `ctx`
         * passed to the `update` callback. If `key` does not already exist,
         * `NULL` is passed to the `update`-callback's `__dest` parameter.
         * Additionally, it does fixed-step incremental migration-checks
         * (usually `old_table->cur_steps`) if `table->old` exists and
         * auto-resizes the file when needed.
         *
         * @param table   An IncHash struct.
         * @param update  A callback function.
         * @param key     Key associated with value.
         * @param ctx     Value\Context passed to the `update` callback.
         *
         * @return `true` unless migration fails. Even if it fails, the value
         * should still `update` successfully. (errno)
         */
        bool inchash_mod(IncHash* table, inchcpy update, const void* key, const void* ctx)
        {
            // update & ...
            update(inchash_get(table, key), ctx, table->val_len);

            #if defined(INCHASH_CORE_UNRESIZABLE)
                // ... return
                return true;
            #else
                // ... do a few migration-checks.
                return _inchash_migrate(table->old, table, 0);
            #endif
        }


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
        bool inchash_del(IncHash* table, const void* key)
        {
            #if defined(INCHASH_CORE_UNRESIZABLE)
                return _inchash_del(table, key);
            #else
                return _inchash_del(table, key) &&
                       _inchash_migrate(table->old, table, 0);
            #endif
        }



        /**
         * @brief Synchronizes changes to IncHash file.
         *
         * @param table    An IncHash struct.
         *
         * @return `true` or `false` depending on if 
         * synchronization was successful or not. 
         */
        bool inchash_sync(IncHash* table)
        {
            // Copying the struct+core to the underlying map
            _inchash_copy_struct_to_mmap(table);

            if (msync(table->map, table->file_size, MS_SYNC)) {
                perror("inchash_sync() -> msync()");
                return false;
            }

            if (fsync(table->fd)) {
                perror("inchash_sync() -> fsync()");
                return false;
            }

            return true;
        }



        /**
         * @brief Synchronizes and Closes IncHash file.
         *
         * @param table   An IncHash struct.
         *
         * @return `true` or `false` depending on if clean-up +
         * synchronization was successful or not. (it frees `table->old` too).
         */
        bool inchash_close(IncHash* table) 
        {
            // synchronize without table marked as closed_ok
            if ((table->flags & PROT_WRITE) && !inchash_sync(table))
                return false;

            // Mark table as properly-closed and ...
            table->closed_ok = true; 

            // re-synchronize changes
            bool ret = (table->flags & PROT_WRITE)  // see #6
                ? inchash_sync(table)
                : true;

            // it's NULL if not anything else, don't panic 
            free(table->old);

            if (munmap(table->map, table->file_size)) {
                perror("inchash_close() -> munmap()");
                ret = false;
            }

            if (close(table->fd)) {
                perror("inchash_close() -> close()");
                ret = false;
            }

            return ret;
        }



        /**
         * @brief Gets total occupants. 
         *
         * @param table   An IncHash struct.
         *
         * @return The sum of both old plus new table occupants.
         */
        static inline uint32_t inchash_occupants(IncHash* table)
        {
            return table->occupants + (table->old ? table->old->occupants : 0);
        }

        // #define inchash_open(X, ...) _inchash_open( __VA_ARGS__, 0, 0, 0, 0)
        // might also make a function called inchash_read(const char* path) just for readonly access
    #endif
#endif



/*
 * TODOS:
 * - ##0 improve sanity checks)
 * 
 *
 * IDEAS:
 * - Ideas for wrappers: Cusco, Quinoa, Quipu, Intihuatana.
 *
 * - ##3 during migration-steps I could keep track of the farthest (out of all) 
 *   a slot had to be displaced relative to it's home (but <= n_slots)
 *   such that I could skip checking the old table in `...get()` like:
 *   `if (home_index + farthest_behind_n_slots <= table->cur_index){return NULL}`
 *
 *
 * NOTES:
 *  - ##1 https://unix.stackexchange.com/questions/463369
 *        https://stackoverflow.com/questions/3080836/3080899#comment141136412_3080899
 *
 *  - ##2 https://stackoverflow.com/questions/24693694 
 *    hehe I bet u didn't know this :D
 *
 *  - ###4 INCHASH_HOME_SLOT_HAS_MULTIPLE_DISPLACEMENTS_MASK:
 *    for home is 0 (ftruncate)
 *    for the 1st displacement still 0
 *    for the 2nd displacement and on is 1
 *
 *  - ##8 If I ever use them might want to add them at `inchash_sync`
 *
 *  - ##9 if `closed_ok` is false, count occupants in both the old and new
 *    tables under `_inchash_recover`, as some may have been migrated and written
 *    to the underlying file before an unsafe shutdown. Otherwise, the stale old
 *    table may not be removed during migration, eventually causing a failure.
 *
 *  - ##16 we do also check if `inch_version` is not zero, in case of an 
 *    unsafe-close after resizing. (unsafe but it is what it is at this
 *    point...)
 *
 *  - ##18 if you wonder how I came up with all those validation and restoration
 *    cases: I just commented out all `_inchash_copy_struct_to_mmap()` under
 *    `_inchash_migrate` then run the `example/arbitrary_id_user_data.c` 
 *    and manually set pairs and killed the process after resizing.
 *    
 *    
 *    
 *
 *
 *
 * OTHER:
 *  - ###6 Fixed issues thanks to skeeto's comment:
 *    https://www.reddit.com/r/C_Programming/comments/1wb12he/comment/p8r93p0
 */

