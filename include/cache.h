#ifndef CACHE_H
#define CACHE_H

#include <time.h>

#define REQUEST_SIZE 2048
#define RESPONSE_SIZE 102400
#define CACHE_SIZE 10

/**
 * Represents a single cache entry with request-response metadata.
 */
typedef struct {
    int index;
    int valid;
    char request[REQUEST_SIZE + 1];
    char response[RESPONSE_SIZE];
    int response_size;
    unsigned long last_used;
    time_t cached_time;
    time_t max_age;
} cache_entry_t;

/**
 * Represents the full cache containing multiple entries.
 */
typedef struct {
    int valid_entries;
    cache_entry_t entries[CACHE_SIZE];
} cache_t;

/**
 * Initializes the cache data structure.
 * @param cache Pointer to the cache structure to initialize.
 * @return NULL.
 */
void *init_cache(cache_t *cache);

/**
 * Finds the index of the first invalid cache slot.
 * @param cache Pointer to the cache.
 * @return Index of invalid entry, or -1 if none available.
 */
int find_invalid_entry(cache_t *cache);

/**
 * Evicts the least recently used entry from the cache.
 * @param cache Pointer to the cache.
 * @return Malloc'd copy of the evicted request string, or NULL on error.
 */
char *evict_lru_entry(cache_t *cache);

/**
 * Updates the LRU timestamp of a cache entry.
 * @param cache Pointer to the cache.
 * @param index Index of the entry to update.
 * @param usage_counter Pointer to a global usage counter.
 */
void update_last_used(cache_t *cache, int index, unsigned long *usage_counter);

/**
 * Searches the cache for a matching request.
 * @param cache Pointer to the cache.
 * @param request The request string to search for.
 * @return Index of cache hit, or -1 if not found.
 */
int search_cache_hit(cache_t *cache, const char *request);

/**
 * Adds a new entry to the cache.
 * @param cache Pointer to the cache.
 * @param request The request string to store.
 * @param response The response string to store.
 * @param response_size Size of the response in bytes.
 * @return Index where entry was added, or -1 on failure.
 */
int add_cache_entry(cache_t *cache, const char *request, const char *response, int response_size);

/**
 * Sends a cached response to the client.
 * @param client_fd Socket descriptor to the client.
 * @param cache Pointer to the cache.
 * @param cache_index Index of the cache entry to serve.
 * @return Number of bytes sent, or -1 on error.
 */
int serve_from_cache(int client_fd, cache_t *cache, int cache_index);

/**
 * Checks the Cache-Control header for no-store or no-cache directives.
 * @param response The full HTTP response.
 * @return 1 if caching is disallowed, 0 otherwise.
 */
int check_no_cache(char *response);

/**
 * Parses the Cache-Control line from the response.
 * @param response The full HTTP response.
 * @return Malloced string of the Cache-Control header.
 */
char *parse_cache_control(const char *response);

/**
 * Extracts the max-age value from a Cache-Control header.
 * @param response The full HTTP response.
 * @return Parsed max-age in seconds, or -1 if not found.
 */
int get_max_age(const char *response);

/**
 * Determines whether a cache entry is stale based on max-age.
 * @param cache Pointer to the cache.
 * @param index Index of the entry to check.
 * @return 1 if timed out, 0 otherwise.
 */
int is_timed_out(cache_t *cache, int index);

/**
 * Removes an entry from the cache at the given index.
 * @param cache Pointer to the cache.
 * @param index Index of the entry to remove.
 */
void evict_cache_entry(cache_t *cache, int index);

#endif