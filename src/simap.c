#include "simap.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>


// FNV-1a hash algorithm constants
#define FNV_OFFSET_BASIS 0xcbf29ce484222325ULL
#define FNV_PRIME 0x100000001b3ULL

// Load factor threshold that triggers resizing
#define MAX_LOAD_FACTOR 0.75

// Hash table entry
typedef struct {
    enum {
        ENTRY_EMPTY,    // Empty slot
        ENTRY_OCCUPIED, // Occupied slot
        ENTRY_DELETED   // Deleted slot (tombstone)
    } state;
    int id;
    char key[MAX_KEY_LEN];
} SiItem_t;

struct SiMap {
    SiItem_t *entries;
    uint32_t capacity;
    pthread_rwlock_t lock;
    uint32_t count;
};


static uint64_t fnv1a_hash(const char *key);
static int resize_simap(SiMap_t *map);
static SiItem_t *find_item(SiMap_t *map, const char *key);

// FNV-1a hash function implementation
static uint64_t fnv1a_hash(const char *key)
{
    uint64_t hash = FNV_OFFSET_BASIS;
    for (const char *p = key; *p; p++)
    {
        hash ^= (uint64_t)(*p);
        hash *= FNV_PRIME;
    }
    return hash;
}

// Find entry in the hash table
static SiItem_t *find_item(SiMap_t *map, const char *key)
{
    uint64_t hash = fnv1a_hash(key);
    size_t start_index = (size_t)(hash & (map->capacity - 1));
    size_t index = start_index;

    SiItem_t *tombstone = NULL;

    do {
        SiItem_t *entry = &map->entries[index];
        switch (entry->state)
        {
            case ENTRY_EMPTY:
                return tombstone != NULL ? tombstone : entry;
            case ENTRY_DELETED:
                if (tombstone == NULL) tombstone = entry;
                break;
            case ENTRY_OCCUPIED:
                if (strcmp(key, entry->key) == 0)
                    return entry;
                break;
        }
        index = (index + 1) & (map->capacity - 1); // Linear probing, circular array
    } while(index != start_index);

    return tombstone;
}

// Resize hash table (expand), and migrate data
static int resize_simap(SiMap_t *map)
{
    const uint32_t old_capacity = map->capacity;
    const uint32_t new_capacity = map->capacity * 2;

    SiItem_t *new_entries = (SiItem_t *)malloc(sizeof(SiItem_t) * new_capacity);

    if (!new_entries) return SIMAP_ERR_NO_MEMORY;

    for (uint32_t i = 0; i < new_capacity; i++)
    {
        new_entries[i].state = ENTRY_EMPTY;
    }

    // Migrate data
    for (uint32_t i = 0; i < old_capacity; i++) {
        SiItem_t *old_entry = &map->entries[i];
        if (old_entry->state == ENTRY_OCCUPIED) {
            SiItem_t *new_entry = &new_entries[fnv1a_hash(old_entry->key) & (new_capacity - 1)];
            // Handle collisions with linear probing
            while (new_entry->state == ENTRY_OCCUPIED) {
                new_entry = &new_entries[(new_entry - new_entries + 1) & (new_capacity - 1)];
            }
            *new_entry = *old_entry;
        }
    }

    free(map->entries);
    map->entries = new_entries;
    map->capacity = new_capacity;
    return SIMAP_OK;
}

#include <stdint.h> // For uint32_t
#include <limits.h> // For UINT32_MAX

static uint32_t next_power_of_two(uint32_t n)
{
    if (n == 0) return 1; // Minimum power of two is 1
    // If n is already a power of two, return n directly
    if ((n & (n - 1)) == 0) return n;
    // If n is greater than 2^31, the next power of two (2^32) will overflow uint32_t
    // Return 0 to indicate error
    if (n > (UINT32_MAX >> 1)) return 0;

    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n++;

    return n;
}

// --- SIMAP_API Section ---

SIMAP_API SiMap_t *simap_create(uint32_t capacity)
{
    if (capacity < 16) capacity = 16;
    capacity = next_power_of_two(capacity);

    SiMap_t *map = (SiMap_t *)malloc(sizeof(SiMap_t));
    if (!map) return NULL;

    map->entries = (SiItem_t *)malloc(sizeof(SiItem_t) * capacity);
    if (!map->entries) {
        free(map);
        return NULL;
    }

    for (size_t i = 0; i < capacity; i++) {
        map->entries[i].state = ENTRY_EMPTY;
    }

    map->capacity = capacity;
    map->count = 0;
    if (pthread_rwlock_init(&map->lock, NULL) != 0) {
        free(map->entries);
        free(map);
        return NULL;
    }

    return map;
}

SIMAP_API int simap_destroy(SiMap_t *map)
{
    if (!map) return SIMAP_ERR_NULL_PTR;

    int result = SIMAP_OK;
    if (pthread_rwlock_destroy(&map->lock) != 0) {
        result = SIMAP_ERR_LOCK_FAILED;
    }
    free(map->entries);
    free(map);
    return result;
}

SIMAP_API int simap_set(SiMap_t *map, const char *key, uint32_t id)
{
    if (!map || !key) return SIMAP_ERR_NULL_PTR;
    
    // Check key length
    if (strlen(key) >= MAX_KEY_LEN) {
        return SIMAP_ERR_KEY_TOO_LONG;
    }

    int lock_result = pthread_rwlock_wrlock(&map->lock);
    if (lock_result != 0) {
        return SIMAP_ERR_LOCK_FAILED;
    }

    // Check if resize is needed
    if ((double)(map->count + 1) / map->capacity > MAX_LOAD_FACTOR) {
        if (resize_simap(map) != 0) {
            pthread_rwlock_unlock(&map->lock);
            return SIMAP_ERR_RESIZE_FAILED;
        }
    }

    SiItem_t *entry = find_item(map, key);

    if (entry == NULL) {
        pthread_rwlock_unlock(&map->lock);
        return SIMAP_ERR_RESIZE_FAILED;
    }

    if (entry->state == ENTRY_OCCUPIED) {
        pthread_rwlock_unlock(&map->lock);
        return SIMAP_ERR_KEY_EXISTS;
    }

    strncpy(entry->key, key, MAX_KEY_LEN - 1);
    entry->key[MAX_KEY_LEN - 1] = '\0';
    entry->state = ENTRY_OCCUPIED;
    entry->id = id;

    map->count++;

    pthread_rwlock_unlock(&map->lock);
    return SIMAP_OK;
}

SIMAP_API int simap_get(SiMap_t *map, const char *key, uint32_t *id)
{
    if (!map || !key || !id) return SIMAP_ERR_NULL_PTR;
    
    // Check key length
    if (strlen(key) >= MAX_KEY_LEN) {
        return SIMAP_ERR_KEY_TOO_LONG;
    }

    int lock_result = pthread_rwlock_rdlock(&map->lock);
    if (lock_result != 0) {
        return SIMAP_ERR_LOCK_FAILED;
    }
    
    SiItem_t *entry = find_item(map, key);
    
    if (entry != NULL && entry->state == ENTRY_OCCUPIED) {
        *id = entry->id;
        pthread_rwlock_unlock(&map->lock);
        return SIMAP_OK;
    }
    
    pthread_rwlock_unlock(&map->lock);
    return SIMAP_ERR_KEY_NOT_FOUND;
}

SIMAP_API int simap_delete(SiMap_t *map, const char *key)
{
    if (!map || !key) return SIMAP_ERR_NULL_PTR;
    
    // Check key length
    if (strlen(key) >= MAX_KEY_LEN) {
        return SIMAP_ERR_KEY_TOO_LONG;
    }

    int lock_result = pthread_rwlock_wrlock(&map->lock);
    if (lock_result != 0) {
        return SIMAP_ERR_LOCK_FAILED;
    }
    
    SiItem_t *entry = find_item(map, key);
    
    if (entry != NULL && entry->state == ENTRY_OCCUPIED) {
        entry->state = ENTRY_DELETED;
        map->count--;
        pthread_rwlock_unlock(&map->lock);
        return SIMAP_OK;
    }
    
    pthread_rwlock_unlock(&map->lock);
    return SIMAP_ERR_KEY_NOT_FOUND;
}


// Convert error code to human-readable string
SIMAP_API const char *simap_strerror(int err)
{
    switch (err) {
        case SIMAP_OK: return "OK";
        case SIMAP_ERR_NULL_PTR : return "NULL pointer";
        case SIMAP_ERR_KEY_NOT_FOUND : return "Key not found";
        case SIMAP_ERR_KEY_EXISTS : return "Key exists";
        case SIMAP_ERR_KEY_TOO_LONG : return "Key is too long";
        case SIMAP_ERR_NO_MEMORY : return "No memory";
        case SIMAP_ERR_LOCK_FAILED : return "Lock failed";
        case SIMAP_ERR_RESIZE_FAILED : return "Resize failed";
        case SIMAP_ERR_INVALID_PARAM : return "Invalid parameter";
        default: return "Unknown error";
    }
    return "Unknown error";
}
