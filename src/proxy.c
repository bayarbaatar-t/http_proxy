#define _GNU_SOURCE // https://stackoverflow.com/questions/9935642/how-do-i-use-strcasestr
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include "proxy.h"
#include "cache.h"

// Helper functions
int connect_to_host(const char *host) {
    struct addrinfo hints, *res, *p;
    int sockfd;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET6;      // Set to IPv6 according to ED
    hints.ai_socktype = SOCK_STREAM;

    // Adapted from Beej's guide to network programming
    // https://beej.us/guide/bgnet/html/
    int rv = getaddrinfo(host, "80", &hints, &res);
    if (rv != 0) {
        fprintf(stderr, "getaddrinfo (host=%s): %s\n", host, gai_strerror(rv));
        return -1;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            continue;
        }

        break;
    }

    freeaddrinfo(res);

    // Error handling
    if (!p) {
        fprintf(stderr, "Could not connect to host %s\n", host);
        return -1;
    }

    return sockfd;
}

// Dynamically reads until recv ends
// Doesn't have a null terminator, so only good for data, not requests
char* read_from_server(int sockfd, int *data_length) {
    // Initialize buffer
    int bufsize = INIT_BUF_SIZE;
    char *buffer = malloc(bufsize);
    if (!buffer) {
        perror("malloc");
        return NULL;
    }

    // Initialize variables for recv
    int total_read = 0;
    char *cl = NULL;
    int content_length = 0;
    char *body_start;

    while (1) {
        // Realloc more space for buffer if needed
        if (total_read + 1 >= bufsize) { // +1 for null terminator
            bufsize *= 2;
            char *new_buffer = realloc(buffer, bufsize);
            if (!new_buffer) {
                free(buffer);
                perror("realloc");
                return NULL;
            }
            buffer = new_buffer;
        }

        // Read from socket
        int bytes_read = recv(sockfd, buffer + total_read, bufsize - (total_read + 1), 0); // +1 for null terminator
        if (bytes_read == -1) {
            perror("recv from server");
            free(buffer);
            return NULL;
        }
        total_read += bytes_read;
        buffer[total_read] = '\0'; // Null-terminate to make strstr work

        // Check for end of headers
        if ((body_start = strstr(buffer, "\r\n\r\n"))) {
            body_start += 4; // Move past the \r\n\r\n

            // Look for Content-Length header
            cl = strcasestr(buffer, "Content-Length:");

            if (cl) {
                // Skip until at the start of the length integer
                cl += strlen("Content-Length:");
                while (*cl == ' ' || *cl == '\t') {
                    cl++;
                }

                // Parse the length
                content_length = atoi(cl);
            }
        }

        // Check if we have received all data
        if (cl && total_read - (body_start - buffer) >= content_length) {
            break;
        }
    }

    *data_length = total_read;
    return buffer;
}

// Dynamically reads until (end of headers), returns a NULL-terminated string
char* read_http_request(int sockfd) {
    // Initialize buffer
    int bufsize = INIT_BUF_SIZE;
    char *buffer = malloc(bufsize);
    if (!buffer) {
        perror("malloc");
        return NULL;
    }

    int total_read = 0;
    int found = 0;

    while (!found) {
        // Realloc for more space if needed
        if (total_read > bufsize - 5) { // keep room for \r\n\r\n\0
            bufsize *= 2;
            char *new_buffer = realloc(buffer, bufsize);
            if (!new_buffer) {
                free(buffer);
                perror("realloc");
                return NULL;
            }
            buffer = new_buffer;
        }

        // Read from socket
        int bytes_read = recv(sockfd, buffer + total_read, bufsize - total_read - 1, 0);
        if (bytes_read <= 0) {
            free(buffer);
            return NULL;
        }

        total_read += bytes_read;
        buffer[total_read] = '\0';

        // Check for end of header
        if (strstr(buffer, "\r\n\r\n")) {
            found = 1;
        } 
    }
    return buffer;
}

// Returns pointer to last line before the \r\n\r\n
// Make sure to free the returned string
char* extract_last_header_line(char *request) {
    char *end = strstr(request, "\r\n\r\n");
    if (!end) return NULL;

    *end = '\0'; // Temporarily null-terminate at the blank line

    char *last_line = strrchr(request, '\n');
    if (!last_line) {
        *end = '\r'; // change back the \0 to \r
        return NULL;
    }
    last_line++; // Move past the \n

    // change back the \0 to \r
    *end = '\r';

    // find length of the last line
    int len = end - last_line; 

    // copy string
    char *last_line_copy = malloc(len + 1); // +1 for \0
    if (!last_line_copy) {
        perror("malloc");
        return NULL;
    }
    
    memcpy(last_line_copy, last_line, len);
    last_line_copy[len] = '\0'; // null-terminate

    return last_line_copy;
}

// Extract Host from headers
// Make sure to free the returned string
char* extract_host(char *request) {
    // Find the start of the Host header
    const char *host_prefix = "\r\nHost:";
    char *host_line = strcasestr(request, host_prefix);
    if (!host_line) {
        return NULL;
    }

    // This is adapted from Ahmed's tip in ED #563
    host_line += strlen(host_prefix);

    // Skip spaces or tabs until the start of the host
    while (*host_line == ' ' || *host_line == '\t') {
        host_line++;
    }

    // Find the end of the host line
    char *end = strstr(host_line, "\r\n");
    if (!end) {
        return NULL;
    }

    // Create a new string to hold the host
    size_t len = end - host_line;
    char *host = malloc(len + 1); // +1 for \0
    if (!host) {
        perror("malloc");
        return NULL;
    }
    memcpy(host, host_line, len);
    host[len] = '\0';

    return host;
}

// Extract URI from request line
// Make sure to free the returned string
char* extract_request_uri(char *request) {
    // Skip the "GET" or any other method
    char *path_start = request;
    while (*path_start != ' ' && *path_start != '\t') {
        path_start++;
    }

    // Skip spaces or tabs until the start of the path
    while (*path_start == ' ' || *path_start == '\t') {
        path_start++;
    }

    // Find the location of the first space or tab
    char *path_end = path_start;
    while (*path_end != ' ' && *path_end != '\t') {
        path_end++;
    }

    // Calculate the length of the path
    int len = path_end - path_start;

    // Create a new string to hold the URI
    char *uri = malloc(len + 1); // +1 for \0
    if (!uri) {
        perror("malloc");
        return NULL;
    }
    memcpy(uri, path_start, len);
    uri[len] = '\0';

    return uri;
}

char *forward_request(int client_fd, int server_fd, char *request, int *response_length) {
    // Send the request to origin server
    int sent_bytes = 0;
    int request_length = strlen(request);
    while (sent_bytes < request_length) {
        int bytes = send(server_fd, request + sent_bytes, request_length - sent_bytes, 0);
        if (bytes == -1) {
            perror("send to server");
            return NULL;
        }
        sent_bytes += bytes;
    }

    // Read the response from the server
    int data_length = 0;
    char *response = read_from_server(server_fd, &data_length);
    if (!response) {
        perror("read from server");
        return NULL;
    }

    // Find content length header from the response
    char *cl = strcasestr(response, "Content-Length:");
    if (!cl) {
        fprintf(stderr, "No Content-Length header found\n");
        free(response);
        return NULL;
    }

    // Skip until at the start of the length
    cl += strlen("Content-Length:");
    while (*cl == ' ' || *cl == '\t') {
        cl++;
    }

    // Find and log the content length
    int len = atoi(cl);
    printf("Response body length %d\n", len);
    fflush(stdout);

    // Send the response back to the client
    sent_bytes = 0;
    while (sent_bytes < data_length) {
        int bytes = send(client_fd, response + sent_bytes, data_length - sent_bytes, 0);
        if (bytes == -1) {
            perror("send to client");
            free(response);
            return NULL;
        }
        sent_bytes += bytes;
    }

    *response_length = data_length;
    return response;
}

void start_proxy(int port, int enable_cache) {
    int sockfd, new_fd;
    struct addrinfo hints, *servinfo, *p;
    int yes = 1;
    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", port);
    int rv;

    // Initialise cache if enabled (stage 2)
    cache_t cache;
    if (enable_cache) {
        init_cache(&cache);
    }

    // Set up hints for IPv6 and allow AI_PASSIVE
    // Based on Ahmed's tip in #685
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET6; // Use IPv6 (allows IPv4-mapped)
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // Use my IP

    // Get address info for binding
    if ((rv = getaddrinfo(NULL, port_str, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }

    // Loop through results and bind to first valid one
    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            perror("socket");
            continue;
        }

        // Code given from SPEC to allow reuse of port in time_wait state
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
        {
            perror("setsockopt");
            exit(1);
        }

        // Bind the socket
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(sockfd);
            perror("bind");
            continue;
        }

        break;
    }

    if (p == NULL)
    {
        fprintf(stderr, "failed to bind socket\n");
        exit(2);
    }

    freeaddrinfo(servinfo); // Done with address info

    if (listen(sockfd, BACKLOG) == -1)
    {
        perror("listen");
        exit(1);
    }

    while (1)
    {
        // MAIN CODE OF THE FUNCTION
        struct sockaddr_storage client_addr;
        socklen_t sin_size = sizeof client_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&client_addr, &sin_size);
        if (new_fd == -1)
        {
            perror("accept");
            continue;
        }

        printf("Accepted\n");
        fflush(stdout);

        // Read the request
        char *request = read_http_request(new_fd);
        if (!request) {
            close(new_fd);
            continue;
        }
        int request_length = strlen(request);

        // Log last header line
        char *last_line = extract_last_header_line(request);
        if (last_line) {
            printf("Request tail %s\n", last_line);
            fflush(stdout);
        }
        free(last_line);

        // Extract Host and URI
        char *host = extract_host(request);
        char *uri = extract_request_uri(request);

        if (!host || !uri) {
            perror("extract_host or extract_request_uri");
            free(request);
            close(new_fd);
            continue;
        }

        int cache_index = -1;

        // Check if the request is in the cache
        if (enable_cache && request_length < REQUEST_SIZE) {
            if ((cache_index = search_cache_hit(&cache, request)) != -1) {

                // Cache hit, check it it's timed out
                if (is_timed_out(&cache, cache_index)) {
                    printf("Stale entry for %s %s\n", host, uri);
                    fflush(stdout);

                // Else, not timed out. Serve from cache.
                } else {
                    printf("Serving %s %s from cache\n", host, uri);
                    fflush(stdout);

                    serve_from_cache(new_fd, &cache, cache_index);

                    free(request);
                    free(host);
                    free(uri);
                    close(new_fd);
                    continue;
                }
            }
        }

        // If cache is full, evict the LRU entry from the cache 
        if (enable_cache && cache_index == -1 && cache.valid_entries == CACHE_SIZE) {
            // Evict the LRU entry
            char *evicted_request = evict_lru_entry(&cache);
            if (evicted_request) {
                // Extract host and URI from the evicted request for logging
                char *evict_host = extract_host(evicted_request);
                char *evict_uri = extract_request_uri(evicted_request);
                free(evicted_request);

                // If extraction successful, log the eviction
                if (evict_host && evict_uri) {
                    printf("Evicting %s %s from cache\n", evict_host, evict_uri);
                    fflush(stdout);
                    free(evict_host);
                    free(evict_uri);

                } else {
                    fprintf(stderr, "LRU eviction successful but the logging has failed.\n");
                    free(evict_host);
                    free(evict_uri);
                }

            } else {
                perror("evict_lru_entry");
            }
        }

        // Log the request before forwarding
        if (host && uri) {
            printf("GETting %s %s\n", host, uri);
            fflush(stdout);
        }

        // Forward request and retrieve response from the server
        int server_fd = connect_to_host(host);
        char *response = NULL;
        int response_length = 0;
        if (server_fd != -1) {
            response = forward_request(new_fd, server_fd, request, &response_length);
            if (!response) {
                fprintf(stderr, "Failed to forward request to %s %s\n", host, uri);
                close(server_fd);
                free(request);
                free(host);
                free(uri);
                close(new_fd);
                continue;
            }

            // Check if the response doesn't want to be cached
            int no_cache = check_no_cache(response);
            
            if (no_cache) {
                printf("Not caching %s %s\n", host, uri);
                fflush(stdout);              
            }
            

            // If request and response are within size limits, add to cache
            if (enable_cache && !no_cache && response && request_length <= REQUEST_SIZE && response_length <= RESPONSE_SIZE) {
                // Evict the older stale entry if it exists, before adding a new version to the cache
                if (cache_index != -1) {
                    evict_cache_entry(&cache, cache_index);
                    cache_index = -1; // Reset cache index
                }

                // Add to cache
                cache_index = add_cache_entry(&cache, request, response, response_length);
                if (cache_index == -1) {
                    fprintf(stderr, "Failed to add to cache\n");
                }

            } else {
                // If the request is not cacheable, evict the stale entry if it exists
                if (cache_index != -1) {
                    printf("Evicting %s %s from cache\n", host, uri);
                    fflush(stdout);
                    evict_cache_entry(&cache, cache_index);
                    cache_index = -1; // Reset cache index
                }
            }
        }
        
        // Clean up
        close(server_fd);
        close(new_fd);
        free(request);
        free(host);
        free(uri);
        free(response);
    }
    close(sockfd);
}