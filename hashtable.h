#pragma once

typedef struct hash_table HashTable;
typedef struct hash_table_bucket HashTableBucket;

typedef int (*hashtable_insert_func)(HashTable *ht, const void *key, size_t key_len, const void *value, size_t value_len);
typedef HashTableBucket * (*hashtable_find_func)(HashTable *ht, const void *key, size_t key_len);
typedef int (*hashtable_erase_func)(HashTable *ht, const void *key, size_t key_len, int8_t multi_key_erase);
typedef int (*hashtable_key_func)(HashTable *ht, const void *key, size_t key_len);
typedef int (*hashtable_func)(HashTable *ht);
typedef void (*hashtable_erase_free)(void *data);

typedef struct hash_table_bucket {
	void *key, *value;
	size_t key_len, value_len;
	HashTableBucket *next;
} HashTableBucket;

typedef struct hash_table {
	/* private */
	HashTableBucket **buckets;
	size_t curr_buckets_size; // current hash table max bucket count
	size_t max_buckets_size;  // maximum hash table bucket count
	size_t max_bucket_link; // bucket link count by each bucket (for autoscale hash table)
	size_t bucket_count; // used bucket count

	char rearrange_fail;
	char multi_key;

	hashtable_erase_free erase_free;

	/* public */
	hashtable_insert_func insert;
	hashtable_erase_func erase;
	hashtable_func clear;
	hashtable_find_func find;
	hashtable_key_func count;
	hashtable_key_func empty;
} HashTable;

HashTable * ht_create(size_t max_size /* It is changed to an approximate value. (2^n) */, size_t max_bucket_link, char multi_key);
void ht_set_erase_free(HashTable *ht, hashtable_erase_free erase_free);
void ht_delete(HashTable *ht);

void ht_dump(HashTable *ht, char detail);
