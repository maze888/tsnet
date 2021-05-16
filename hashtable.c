#include "tsnet_common_inter.h"
#include "hashtable.h"
#include "halfsiphash.h"

#define HASHTABLE_START_SIZE 16 /* size must expand << 1 (for hash table index) */
#define HASHTABLE_DEFAULT_MAX_SIZE (HASHTABLE_START_SIZE << 13) /* 131072 */
#define HASHTABLE_DEFAULT_BUCKET_LINK 8 /* for use full bucket */

static uint8_t ht_halfsiphash_key[8] = {
  0x8b, 0x04, 0xe0, 0x12, 0xbb, 0x21, 0xae, 0x57
};

static int ht_insert_inter(HashTable *ht, void *key, size_t key_len, void *value, size_t value_len);
static int ht_bucket_clear(HashTable *ht);
static void ht_bucket_clear_inter(HashTableBucket **buckets, size_t buckets_size);
static int ht_insert(HashTable *ht, const void *key, size_t key_len, const void *value, size_t value_len);

static inline int compare_key(const void *key, size_t key_len, const void *cmp_key, size_t cmp_key_len)
{
	if ( key_len == cmp_key_len ) return memcmp(key, cmp_key, key_len);

	return 1; // not same
}

static HashTableBucket * get_ht_bucket(HashTable *ht, const void *key, size_t key_len, uint32_t *index)
{
	uint32_t hash;
	
	halfsiphash(key, key_len, ht_halfsiphash_key, (uint8_t *)&hash, sizeof(hash));
	
	*index = hash & (ht->curr_buckets_size - 1);

	return ht->buckets[*index];
}

static int rearrange_hashtable(HashTable *ht)
{
	HashTableBucket **old_buckets = NULL, **new_buckets = NULL;
	size_t old_buckets_size = 0, new_buckets_size = 0, old_bucket_count = 0;

	if ( ht && !ht->rearrange_fail ) {
		new_buckets_size = ht->curr_buckets_size << 1;

		if ( new_buckets_size > ht->max_buckets_size ) goto fail;

		if ( !(new_buckets = calloc(new_buckets_size, sizeof(HashTableBucket *))) ) goto fail;

		// backup for restore
		old_buckets = ht->buckets;
		old_buckets_size = ht->curr_buckets_size;
		old_bucket_count = ht->bucket_count;

		// set new buckets
		ht->buckets = new_buckets;
		ht->curr_buckets_size = new_buckets_size;
		ht->bucket_count = 0;

		// copy old buckets to new buckets
		for ( size_t i = 0; i < old_buckets_size; i++ ) {
			HashTableBucket *bucket = old_buckets[i];
			for ( ; bucket; bucket = bucket->next ) { // walk to bucket link
				if ( ht_insert(ht, bucket->key, bucket->key_len, bucket->value, bucket->value_len) < 0 ) goto restore;
			}
		}

		ht_bucket_clear_inter(old_buckets, old_buckets_size);
		safe_free(old_buckets);
	}

	return 0;

restore:
	ht->buckets = old_buckets;
	ht->curr_buckets_size = old_buckets_size;
	ht->bucket_count = old_bucket_count;

	ht_bucket_clear_inter(new_buckets, new_buckets_size);

fail:
	ht->rearrange_fail = 1;

	return -1;
}

static int ht_insert_inter(HashTable *ht, void *key, size_t key_len, void *value, size_t value_len)
{
	uint32_t index;
	HashTableBucket *bucket = get_ht_bucket(ht, key, key_len, &index);

	if ( bucket ) { // insert to bucket link
		HashTableBucket *bucket_next;
		
		if ( compare_key(bucket->key, bucket->key_len, key, key_len) == 0 ) {
			TSNET_SET_ERROR("compare_key() is failed: (errmsg: duplication key)");
			goto out;
		}
		
		// move bucket last link
		size_t bucket_link_count = 0;
		for ( ; bucket->next; bucket = bucket->next ) {
			if ( compare_key(bucket->key, bucket->key_len, key, key_len) == 0 ) {
				TSNET_SET_ERROR("compare_key() is failed: (errmsg: duplication key)");
				goto out;
			}
			bucket_link_count++;
		}

		if ( !(bucket_next = malloc(sizeof(HashTableBucket))) ) {
			TSNET_SET_ERROR("malloc() is failed: (errmsg: %s, errno: %d, size: %lu)", strerror(errno), errno, sizeof(HashTableBucket));
			goto out;
		}
		
		bucket_next->key = key;
		bucket_next->key_len = key_len;
		bucket_next->value = value;
		bucket_next->value_len = value_len;
		bucket_next->next = NULL;
		
		bucket->next = bucket_next;

		// extend hashtable buckets
		if ( bucket_link_count >= ht->max_bucket_link ) (void)rearrange_hashtable(ht);
	}
	else { // insert to new bucket
		if ( !(bucket = malloc(sizeof(HashTableBucket))) ) {
			TSNET_SET_ERROR("malloc() is failed: (errmsg: %s, errno: %d, size: %lu)", strerror(errno), errno, sizeof(HashTableBucket));
			goto out;
		}

		bucket->key = key;
		bucket->key_len = key_len;
		bucket->value = value;
		bucket->value_len = value_len;
		bucket->next = NULL;

		ht->buckets[index] = bucket;
		ht->bucket_count++;
	}

	return 0;

out:
	return -1;
}

static int ht_insert(HashTable *ht, const void *key, size_t key_len, const void *value, size_t value_len)
{
	void *key_copy = NULL, *value_copy = NULL;

	if ( !ht || !key || key_len == 0 || !value || value_len == 0 ) {
		TSNET_SET_ERROR("invalid argument: (ht = %s, key = %s, key_len = %lu, value = %s, value_len = %lu)", CKNUL(ht), CKNUL(key), key_len, CKNUL(value), value_len);
		return -1;
	}
	
	key_copy = calloc(1, key_len + 1);
	if ( !key_copy ) {
		TSNET_SET_ERROR("calloc() is failed: (errmsg: %s, errno: %d, size: %lu)", strerror(errno), errno, key_len + 1);
		goto out;
	}
	value_copy = calloc(1, value_len + 1);
	if ( !value_copy ) {
		TSNET_SET_ERROR("calloc() is failed: (errmsg: %s, errno: %d, size: %lu)", strerror(errno), errno, value_len + 1);
		goto out;
	}
	memcpy(key_copy, key, key_len);
	memcpy(value_copy, value, value_len);

	if ( ht_insert_inter(ht, key_copy, key_len, value_copy, value_len) < 0 ) goto out;

	return 0;

out:
	safe_free(key_copy);
	safe_free(value_copy);

	return -1;
}

static int ht_erase(HashTable *ht, const void *key, size_t key_len)
{
	int rv = HT_BUCKET_NOT_FOUND; // not found
	HashTableBucket *bucket, *bucket_prev;
	uint32_t index;

	bucket = get_ht_bucket(ht, key, key_len, &index);
	
	for ( int i = 0; bucket; bucket = bucket->next, i++ ) { // walk to bucket link
		if ( (rv = compare_key(bucket->key, bucket->key_len, key, key_len)) == 0 ) {
			size_t bucket_link_count = 0;
			HashTableBucket *bucket_tmp = bucket->next;
			for ( ; bucket_tmp; bucket_tmp = bucket_tmp->next ) bucket_link_count++;

			safe_free(bucket->key);
			safe_free(bucket->value);
			switch (i) {
				case 0:
					ht->buckets[index] = bucket->next;
					break;
				default:
					bucket_prev->next = bucket->next;
					break;
			}
			safe_free(bucket);
	
			if ( i == 0 && bucket_link_count == 0 ) ht->bucket_count--;
			break;
		}

		bucket_prev = bucket;
	}

	return rv;
}

static void ht_bucket_clear_inter(HashTableBucket **buckets, size_t buckets_size)
{
	HashTableBucket *bucket, *bucket_next;

	for ( size_t i = 0; i < buckets_size; i++ ) {
		bucket = buckets[i];
		while ( bucket ) {
			bucket_next = bucket->next;

			safe_free(bucket->key);
			safe_free(bucket->value);
			safe_free(bucket);

			bucket = bucket_next;
		}
	}
}

static int ht_bucket_clear(HashTable *ht)
{
	if ( !ht || !ht->buckets ) return -1;

	ht_bucket_clear_inter(ht->buckets, ht->curr_buckets_size);

	memset(ht->buckets, 0x00, sizeof(HashTableBucket *) * ht->curr_buckets_size);

	ht->bucket_count = 0;

	return 0;
}

static HashTableBucket * ht_find(HashTable *ht, const void *key, size_t key_len)
{
	HashTableBucket *bucket;
	uint32_t index;

	bucket = get_ht_bucket(ht, key, key_len, &index);

	for ( ; bucket; bucket = bucket->next ) {
		if ( compare_key(bucket->key, bucket->key_len, key, key_len) == 0 ) return bucket;
	}

	return NULL;
}

HashTable * ht_create(size_t max_buckets_size, size_t max_bucket_link)
{
	HashTable *ht = NULL;

	if ( !(ht = calloc(1, sizeof(HashTable))) ) goto out;

	ht->curr_buckets_size = HASHTABLE_START_SIZE;
	if ( !(ht->buckets = calloc(ht->curr_buckets_size, sizeof(HashTableBucket *))) ) {
		TSNET_SET_ERROR("calloc() is failed: (errmsg: %s, errno: %d, size: %lu)", strerror(errno), errno, ht->curr_buckets_size * sizeof(HashTableBucket));
		goto out;
	}
	
	size_t size = HASHTABLE_START_SIZE;
	for ( ; size < max_buckets_size; size = size << 1 ) {/* no action */}
	ht->max_buckets_size = size;

	if ( max_bucket_link == 0 ) ht->max_bucket_link = HASHTABLE_DEFAULT_BUCKET_LINK;
	else ht->max_bucket_link = max_bucket_link;
	
	ht->insert = ht_insert;
	ht->erase = ht_erase;
	ht->clear = ht_bucket_clear;
	ht->find = ht_find;

	return ht;

out:
	ht_delete(ht);

	return NULL;
}

void ht_delete(HashTable *ht)
{
	if ( ht ) {
		ht_bucket_clear(ht);
		safe_free(ht->buckets);
		safe_free(ht);
	}
}

void ht_dump(HashTable *ht, char detail)
{
	if ( ht ) {
		printf("curr_buckets_size:       %lu\n", ht->curr_buckets_size);
		printf("max_buckets_size:        %lu\n", ht->max_buckets_size);
		printf("bucket_count:            %lu\n", ht->bucket_count);
		if ( detail ) {
			for ( size_t i = 0; i < ht->curr_buckets_size; i++ ) {
				size_t bucket_link_count = 0;
				HashTableBucket *bucket = ht->buckets[i];
				while ( bucket ) {
					bucket = bucket->next;
					if ( bucket ) bucket_link_count++;
				}

				if ( ht->buckets[i] ) printf("bucket[%lu] link count: %lu\n", i, bucket_link_count);
				else printf("bucket[%lu] link count: empty\n", i);
			}
		}
	}
}
