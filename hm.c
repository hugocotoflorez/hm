#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "frog/frog.h"

/* not defined in c99 */
#ifndef strdup
#define strdup(s) ({                        \
        void *p = calloc(1, strlen(s) + 1); \
        strcat(p, s);                       \
        p;                                  \
})
#endif

typedef struct Entry {
        int d;
        char *key;
        void *value;
        struct Entry *next;
        struct Entry *prev;
} Entry;

typedef struct HM {
        int d;
        Entry **entries;
        int (*hash)(char *key, int bits);
} HM;

#define HM_INIT_SIZE 1
#define HASH_MAX_BITS 12

DA(void *)
alloc_list = { 0 };

/* -- Public Functions -- */

/* Add value `value` for key `key` to hm `hm` */
void hm_insert(HM *hm, char *key, void *value);

/* Get the value from hm `hm` at key `key` or null */
void *hm_get(HM hm, char *key);

/* Remove the value from hm `hm` at key `key` */
void hm_remove(HM *hm, char *key);

/* Remove all data and set `hm` to empty */
void hm_destroy(HM *hm);

static inline int
two_pow(int n)
{
        return 0x1 << n;
}

static void
free_entry(Entry *e)
{
        if (e->next) {
                free_entry(e->next);
        }
        free(e->key);
        free(e);
}

void
hm_destroy(HM *hm)
{
        int i = 0;
        DA(void *)
        to_free = { 0 };
        for (; i < two_pow(hm->d); i++) {
                if (da_exists(to_free, hm->entries[i])) continue;
                da_append(&to_free, hm->entries[i]);
        }
        for_da_each(e, to_free) free_entry(*e);
        da_destroy(&to_free);
        free(hm->entries);
        hm->d = 0;
        hm->entries = NULL;
}

static inline int
hm_hash_rotl(int x, int r)
{
        return (x << r) | (x >> (sizeof(int) * 8 - r));
}

static int
hm_hash(char *key, int bits)
{
        /* some fast hash implementation with low collision
         * (copied from somewhere) */
        int h = 0x165667B1U;
        int len = strlen(key);
        h += len;
        int i = 0;
        while (i + 4 <= len) {
                int k = *(int *) (key + i);
                k *= 0xC2B2AE3DU;
                k = hm_hash_rotl(k, 17);
                k *= 0x27D4EB2FU;
                h ^= k;
                h = hm_hash_rotl(h, 17) * 0x9E3779B1U + 0x27D4EB2FU;
                i += 4;
        }
        while (i < len) {
                h += key[i] * 0x165667B1U;
                h = hm_hash_rotl(h, 11) * 0x9E3779B1U;
                i++;
        }
        h ^= h >> 15;
        h *= 0x85EBCA77U;
        h ^= h >> 13;
        h *= 0xC2B2AE3DU;
        h ^= h >> 16;
        return h & (two_pow(bits < HASH_MAX_BITS ? bits : HASH_MAX_BITS) - 1);
}

static Entry *
new_entry(int d)
{
        Entry *e = malloc(sizeof(Entry));
        e->d = d;
        e->key = NULL;
        e->value = NULL;
        e->next = NULL;
        e->prev = NULL;
        return e;
}

static void
print_table(HM hm)
{
        int i = 0;
        printf("+--------------------------------------------------------+\n");
        printf("| Hash table                  | d = %-4d                 |\n", hm.d);
        printf("|------------+----------------+----------------+---------|\n");
        for (; i < two_pow(hm.d); i++) {
                Entry *e = hm.entries[i];
                printf("|%-12b|%16s|  %14p| d = %-4d|\n", i, e->key, e->value, e->d);
                while ((e = e->next))
                        printf("|       list |%16s|  %14p| d = %-4d|\n", e->key, e->value, e->d);
        }
        printf("+------------+----------------+----------------+---------+\n");
}

static HM *
hm_grow(HM *hm)
{
        if (hm->d == 0) {
                int i;
                hm->d = HM_INIT_SIZE;
                hm->entries = malloc(two_pow(hm->d) * sizeof(Entry *));
                for (i = 0; i < two_pow(hm->d); i++)
                        hm->entries[i] = new_entry(1);
                return hm;
        }
        hm->d++;
        hm->entries = realloc(hm->entries, two_pow(hm->d) * sizeof(Entry *));
        memcpy(hm->entries + two_pow(hm->d - 1),
               hm->entries, two_pow(hm->d - 1) * sizeof(Entry *));
        return hm;
}

void
hm_insert(HM *hm, char *key, void *value)
{
        if (hm->d == 0) hm_grow(hm);
        if (hm->hash == NULL) hm->hash = hm_hash;
        int hash = hm->hash(key, hm->d);

        /* Entry at hash index is empty */
        if (hm->entries[hash]->key == NULL) {
                hm->entries[hash]->key = strdup(key);
                hm->entries[hash]->value = value;
                return;
        }

        /* Same key: replace */
        if (strcmp(hm->entries[hash]->key, key) == 0) {
                hm->entries[hash]->value = value;
                return;
        }

        /* Exceed max size */
        if (hm->d == HASH_MAX_BITS) {
                Entry *e = hm->entries[hash];
                while (e->next)
                        e = e->next;
                e->next = new_entry(e->d);
                e->next->key = strdup(key);
                e->next->value = value;
                e->next->prev = e;
                return;
        }

        /* Split entries */
        if (hm->entries[hash]->d < hm->d) {
                int hash1 = hash;
                int hash2 = hash ^ two_pow(hm->entries[hash]->d);
                hm->entries[hash1]->d++;

                if (hm->entries[hash1] != hm->entries[hash2]) {
                        /* It should never be executed, but sometimes
                         * it does for some reason. The following code
                         * fix it. TEMP SOLUTION: TODO */
                        hm_grow(hm);
                        return hm_insert(hm, key, value);
                }
                hm->entries[hash2] = new_entry(hm->entries[hash1]->d);
                if (hm->hash(hm->entries[hash1]->key, hm->entries[hash1]->d) == hash2) {
                        /* Now as hash1 is the prev value at hash, if it is not in the correct place, swap with
                         * the new entry created */
                        Entry e = *hm->entries[hash2];
                        *hm->entries[hash2] = *hm->entries[hash1];
                        *hm->entries[hash1] = e;
                }
                /* Recursive call until it found an empty entry */
                return hm_insert(hm, key, value);
        }

        else if (hm->entries[hash]->d == hm->d) {
                hm_grow(hm);
                return hm_insert(hm, key, value);
        }

        else {
                printf("Impossible path reached");
                exit(1);
        }
}

static void *
hm_get_entry(HM hm, char *key)
{
        int hash = hm.hash(key, hm.d);
        Entry *e = hm.entries[hash];
        while (e) {
                if (e->key && strcmp(key, e->key) == 0) return e;
                e = e->next;
        }
        return NULL;
}

void *
hm_get(HM hm, char *key)
{
        Entry *e = hm_get_entry(hm, key);
        return e ? e->value : NULL;
}

void
hm_remove(HM *hm, char *key)
{
        Entry *e = hm_get_entry(*hm, key);
        if (e == NULL) return;
        free(e->key);

        if (e->prev) {
                e->prev->next = e->next;
                if (e->next) e->next->prev = e->prev;
                free(e);
                return;
        }
        if (e->next) {
                Entry *n = e->next;
                e->key = n->key;
                e->value = n->value;
                e->next = n->next;
                if (e->next) e->next->prev = e;
                free(n);
                return;
        }
        e->key = NULL;
        e->value = NULL;
}

static void
test_single_insert(HM *hm, char *key, char *value)
{
        static int t = 0;
        ++t;
        hm_insert(hm, key, value);
        char *val;
        if ((val = hm_get(*hm, key)) != value) {
                printf("Test %d failed:\n", t);
                printf("  hm_insert(hm, `%s`, `%s`) inserts `%s`\n",
                       key, value, val);
        }
        da_append(&alloc_list, key);
        da_append(&alloc_list, value);
}

static void
test_single_remove(HM *hm)
{
        static int t = 0;
        int i = (rand() % alloc_list.size);
        i -= i % 2;
        char *key = alloc_list.data[i];
        char *value = alloc_list.data[i + 1];
        char *val;
        ++t;
        hm_remove(hm, key);
        if ((val = hm_get(*hm, key)) != NULL) {
                printf("Test %d failed:\n", t);
                printf("  hm_remove(hm, `%s`) does not remove `%s`\n", key, key);
        }
        free(key);
        free(value);
        da_remove(&alloc_list, i);
        da_remove(&alloc_list, i);
}

static char *
random_word(int len)
{
        char *s = malloc(len + 1);
        assert(s);
        s[len] = 0;
        for (int i = 0; i < len; i++)
                s[i] = rand() % ('A' - 'Z' + 1) + 'A';
        return s;
}

int
main(void)
{
        HM hm = { 0 };
        unsigned int i;
        int iters = 10;
        srand(time(0));
        for (; iters--;) {
                for (i = 0; i < 0x1 << HASH_MAX_BITS; i++)
                        test_single_insert(&hm, random_word(8), random_word(8));
                while (alloc_list.size) {
                        test_single_remove(&hm);
                }
        }
        hm_destroy(&hm);
        for_da_each(ptr, alloc_list) free(*ptr);
        da_destroy(&alloc_list);
        return 0;
}
