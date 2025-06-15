## SYPNOSIS
```c
/* Add value `value` for key `key` to hm `hm` */
void hm_insert(HM *hm, char *key, void *value);

/* Get the value from hm `hm` at key `key` or null */
void *hm_get(HM hm, char *key);

/* Remove the value from hm `hm` at key `key` */
void hm_remove(HM *hm, char *key);

/* Remove all data and set `hm` to empty */
void hm_destroy(HM *hm);
```

## DESCRYPTION

Hash map implementation. The functions declared above should provide
the functionality needed to use a hash map.
