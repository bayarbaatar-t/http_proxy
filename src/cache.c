#define _GNU_SOURCE // https://stackoverflow.com/questions/9935642/how-do-i-use-strcasestr
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <sys/socket.h>

#include "cache.h"

unsigned long usage_counter = 0;
const char *cache_control_keywords[] = {
    "private",
    "no-store",
    "no-cache",
    "max-age=0",
    "must-revalidate",
    "proxy-revalidate",
};

// ============================== FUNCTION IMPLEMENTATIONS ==============================

// Initializes the given cache
void *init_cache(cache_t *cache) {
    cache->valid_entries = 0;

    for (int i = 0; i < CACHE_SIZE; i++) {
        cache->entries[i].valid = 0;
        cache->entries[i].index = i;
        cache->entries[i].last_used = 0;
        cache->entries[i].cached_time = 0;
        cache->entries[i].max_age = -1;

        cache->entries[i].request[0] = '\0'; // Initialize request string
        cache->entries[i].response[0] = '\0'; // Initialize response string
        cache->entries[i].response_size = -1;
    }

    return 0;
}

// Finds the index of an invalid entry in the cache, or -1 if none found
int find_invalid_entry(cache_t *cache) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache->entries[i].valid == 0) {
            return i;
        }
    }

    return -1;
}


// Evicts the LRU entry, returns a copy of the request string, or NULL if no valid entry found
// Assumes at least one valid entry exists
char *evict_lru_entry(cache_t *cache) {
    int lru_index = -1;
    unsigned long lru_time = (unsigned long) -1;

    // Find the LRU entry
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache->entries[i].valid == 1 && cache->entries[i].last_used < lru_time) {
            lru_index = i;
            lru_time = cache->entries[i].last_used;
        }
    }

    if (lru_index == -1) {
        return NULL; // No valid entry found
    }

    // Create a copy of the request string to return
    char *request_copy = malloc(REQUEST_SIZE + 1);
    if (!request_copy) {
        perror("malloc");
        return NULL;
    }
    strncpy(request_copy, cache->entries[lru_index].request, REQUEST_SIZE);

    // Evict the LRU entry
    cache->entries[lru_index].valid = 0;
    cache->entries[lru_index].last_used = 0;
    cache->entries[lru_index].response_size = -1;
    cache->entries[lru_index].request[0] = '\0'; // Clear request string
    cache->entries[lru_index].response[0] = '\0'; // Clear response string

    cache->valid_entries--;
    return request_copy;
}

// Updates the last used time of the entry
void update_last_used(cache_t *cache, int index, unsigned long *usage_counter) {
    cache->entries[index].last_used = ++(*usage_counter);
}

// Returns the index of the specified request, or -1 if not found
int search_cache_hit(cache_t *cache, const char *request) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache->entries[i].valid == 1 && strcmp(cache->entries[i].request, request) == 0) {
            return i;
        }
    }

    return -1;
}

// Adds an entry to the cache, returns the index of the entry, or -1 if cache is full
int add_cache_entry(cache_t *cache, const char *request, const char *response, int response_size) {
    // Check if strings are too large to cache
    if (strlen(request) >= REQUEST_SIZE || response_size > RESPONSE_SIZE) {
        return -1;
    }
    
    // Find an invalid entry in the cache to write to
    int index = find_invalid_entry(cache);
    cache->entries[index].valid = 1;

    // Copy request and response to the cache
    strncpy(cache->entries[index].request, request, REQUEST_SIZE);
    memcpy(cache->entries[index].response, response, response_size);
    cache->entries[index].response_size = response_size;

    // Set the last used time and cached time
    update_last_used(cache, index, &usage_counter);
    cache->entries[index].cached_time = time(NULL);
    cache->entries[index].max_age = get_max_age(response);

    // Update valid entries count
    if (cache->valid_entries < CACHE_SIZE) {
        cache->valid_entries++;
    }

    return index;
}

int serve_from_cache(int client_fd, cache_t *cache, int cache_index) {
    int sent_bytes = 0;

    // Fetch the response from the cache
    int response_length = cache->entries[cache_index].response_size;
    char *response = cache->entries[cache_index].response;
    update_last_used(cache, cache_index, &usage_counter);

    // Send the cached resopnse to the client
    while (sent_bytes < response_length) {
        int bytes = send(client_fd, response + sent_bytes, response_length - sent_bytes, 0);
        if (bytes == -1) {
            perror("send to client from cache");
            return -1;
        }
        sent_bytes += bytes;
    }

    return 0;
}

// Parses out the cache_control header from the response and checks for no-cache keywords
// Returns 1 if no-cache is found, 0 otherwise
int check_no_cache(char *response) {
    char *cache_control_header = parse_cache_control(response);

    // If no Cache-Control header is found, return 0
    if (!cache_control_header) {
        return 0;
    }

    int keyword_count = sizeof(cache_control_keywords) / sizeof(cache_control_keywords[0]);

    // Tokenize the Cache-Control header
    char *token = strtok(cache_control_header, ",");
    
    // Search for no-cache keyword match per token
    while (token) {
        // Skip spaces and tabs at the front
        while (*token == ' ' || *token == '\t') {
            token++;
        }

        // Skip spaces and tabs at the end
        char *end = token + strlen(token) - 1;
        while (end > token && (*end == ' ' || *end == '\t')) {
            end--;
        }

        // Null-terminate the token
        *(end + 1) = '\0';

        // Check if the token matches any no-cache keywords
        for (int i = 0; i < keyword_count; i++) {
            if (strcasecmp(token, cache_control_keywords[i]) == 0) {

                // Found a no-cache keyword
                free(cache_control_header);
                return 1;
            }
        }
        
        token = strtok(NULL, ",");
    }
    

    // No no-cache keywords found
    free(cache_control_header);
    return 0;
}

// Parses out the cache control header from the response
// Returns a malloced string, or NULL if not found/failed.
char *parse_cache_control(const char *response) {
    // For sanity, create a copy of the headers section
    char *headers_end = strstr(response, "\r\n\r\n");
    if (!headers_end) {
        return NULL;
    }
    size_t headers_length = headers_end - response;
    char *headers_copy = malloc(headers_length + 1); // +1 for \0
    if (!headers_copy) {
        perror("malloc");
        return NULL;
    }
    memcpy(headers_copy, response, headers_length);
    headers_copy[headers_length] = '\0';

    // Tokenize the headers to find the Cache-Control header
    char *line = strtok(headers_copy, "\r\n");
    while (line) {
        int prefix_len = strlen("Cache-Control:");
        if (strncasecmp(line, "Cache-Control:", prefix_len) == 0) {
            // Skip whitespace after colon
            char *value = line + prefix_len;
            while (*value == ' ' || *value == '\t') value++;

            // Return a copy of the Cache-Control value
            char *result = strdup(value);
            free(headers_copy);
            return result;
        }
        line = strtok(NULL, "\r\n");
    }

    free(headers_copy);
    return NULL;
}

// Returns the max-age from the Cache-Control header
// Returns -1 if not found or invalid
int get_max_age(const char *response) {
    // Get a copy of the Cache-Control header
    char *cache_control_header = parse_cache_control(response);
    if (!cache_control_header) {
        return -1;
    }

    // Find the max-age field
    char *field = strtok(cache_control_header, ",");
    while (field) {
        // Skip spaces and tabs at the front
        while (*field == ' ' || *field == '\t') field++;

        // Check for max-age directive
        int prefix_len = strlen("max-age=");
        if (strncasecmp(field, "max-age=", prefix_len) == 0) {
            // Extract the value
            field += prefix_len;
            int max_age = atoi(field);
            free(cache_control_header);
            return max_age;
        }

        field = strtok(NULL, ",");
    }

    // No max-age field found
    free(cache_control_header);
    return -1;
}

// Checks whether the cache entry is timed out
int is_timed_out(cache_t *cache, int index) {
    time_t current_time = time(NULL);

    // If the entry doesn't have a max-age, it's not timed out
    if (cache->entries[index].max_age == -1) {
        return 0;
    }

    // Check if the entry has timed out
    if (current_time - cache->entries[index].cached_time >= cache->entries[index].max_age) {
        return 1;
    }

    // Else, not timed out
    return 0;
}

// Evicts the cache entry at the specified index
void evict_cache_entry(cache_t *cache, int index) {
    cache->entries[index].valid = 0;
    cache->entries[index].last_used = 0;
    cache->entries[index].cached_time = 0;
    cache->entries[index].max_age = -1;

    cache->entries[index].response_size = -1;
    cache->entries[index].request[0] = '\0'; // Clear request string
    cache->entries[index].response[0] = '\0'; // Clear response string

    cache->valid_entries--;
}