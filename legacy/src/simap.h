#ifndef STRING_ID_MAP_H
#define STRING_ID_MAP_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SIMAP_API

#define MAX_KEY_LEN 64

// Error code definitions
#define SIMAP_OK 0
#define SIMAP_ERR_NULL_PTR -1
#define SIMAP_ERR_KEY_NOT_FOUND -2
#define SIMAP_ERR_KEY_EXISTS -3
#define SIMAP_ERR_KEY_TOO_LONG -4
#define SIMAP_ERR_NO_MEMORY -5
#define SIMAP_ERR_LOCK_FAILED -6
#define SIMAP_ERR_RESIZE_FAILED -7
#define SIMAP_ERR_INVALID_PARAM -8

typedef struct SiMap SiMap_t;

// Create a string-ID map with the `capacity`
SIMAP_API SiMap_t *simap_create(uint32_t capacity);

// Destroy the string-ID map and free all associated memory
SIMAP_API int simap_destroy(SiMap_t *map);

// Set a key-value pair in the map
SIMAP_API int simap_set(SiMap_t *map, const char *key, uint32_t id);

// Get the ID associated with a key
SIMAP_API int simap_get(SiMap_t *map, const char *key, uint32_t *id);

// Delete a key from the map
SIMAP_API int simap_delete(SiMap_t *map, const char *key);

// Convert error code to human-readable string
SIMAP_API const char *simap_strerror(int err);


#endif // STRING_ID_MAP_H