#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* No direct dependency, just for test */
#include "frog/frog.h"

typedef struct Entry {
        int d;
        char *key;
        void *value;
        struct Entry *next;
} Entry;

typedef struct HM {
        int d;
        Entry **entries;
        int (*hash)(char *key, int bits);
} HM;

#define HM_INIT_SIZE 1
#define HASH_MAX_BITS 16

typedef DA(void *) ptr_da;
ptr_da alloc_list = { 0 };

/* -- Public Functions -- */

/* Add value `value` for key `key` to hm `hm` */
void hm_insert(HM *hm, char *key, void *value);

/* Get the value from hm `hm` at key `key` or null */
void *hm_get(HM hm, char *key);

/* Remove the value from hm `hm` at key `key` */
// void *hm_remove(HM hm, char *key);

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

static int
hm_hash(char *key, int bits)
{
        /* Just lineal hashing, to be improved */
        int h = 0;
        char *c;
        for (c = key; *c != 0; c++) {
                h *= 31;
                h = ~h;
                h ^= *c;
        }
        return h & (two_pow(bits < HASH_MAX_BITS ? bits : HASH_MAX_BITS) - 1);
}

static Entry *
new_entry(int d)
{
        Entry *e = malloc(sizeof(Entry));
        assert(e);
        *e = (Entry) {
                .d = d,
                .key = NULL,
                .value = NULL,
                .next = NULL,
        };
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
                // print_table(*hm);
                return;
        }

        /* Same key or exceed max size */
        if (!strcmp(hm->entries[hash]->key, key) || hm->d == HASH_MAX_BITS) {
                Entry *e = hm->entries[hash];
                while (e->next)
                        e = e->next;
                e->next = new_entry(e->d);
                e->next->key = strdup(key);
                e->next->value = value;
                // print_table(*hm);
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
                /* Now at hash1 is the prev value at hash, if it is not in the correct place, swap with
                 * the new entry created */
                if (hm->hash(hm->entries[hash1]->key, hm->entries[hash1]->d) == hash2) {
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

void *
hm_get(HM hm, char *key)
{
        int hash = hm.hash(key, hm.d);
        Entry *e = hm.entries[hash];
        while (e) {
                if (strcmp(key, e->key) == 0) return e->value;
                e = e->next;
        }
        return NULL;
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

        int i;
        srand(time(0));
        for (i = 0; i < 256; i++)
                test_single_insert(&hm, random_word(8), random_word(8));

        hm_destroy(&hm);
        for_da_each(ptr, alloc_list) free(*ptr);
        da_destroy(&alloc_list);
        return 0;
}
