#ifndef PROXY_H
#define PROXY_H

#define BACKLOG 10           
#define INIT_BUF_SIZE 2048
#define BUF_SIZE 8192

/**
 * Starts the proxy servr on the given port
 * @param port The port number to bind the proxy server to
 * @param enable_cache Flag to enable or disable caching mechanism
 */
void start_proxy(int port, int enable_cache);

/**
 * Connects to a given host on port 80
 * @param host The hostname to connect to
 * @return A connected socket file descriptor
 */
int connect_to_host(const char *host);

/**
 * Reads the full HTTP response from a server socket.
 * @param sockfd The socket file descriptor connected to the server.
 * @param data_length Pointer to store the total number of bytes read.
 * @return A malloc'd buffer containing the full response.
 */
char* read_from_server(int sockfd, int *data_length);

/**
 * Reads a complete HTTP request from the client socket.
 * @param sockfd The client socket file descriptor.
 * @return A malloc'd buffer containing the complete HTTP request.
 */
char* read_http_request(int sockfd);

/**
 * Extracts the last header line from a full HTTP request.
 * @param request The full HTTP request string.
 * @return A malloc'd string containing the last header line.
 */
char* extract_last_header_line(char *request);

/**
 * Extracts the Host field from the HTTP request headers.
 * @param request The full HTTP request string.
 * @return A malloc'd string containing the hostname.
 */
char* extract_host(char *request);

/**
 * Extracts the URI from the HTTP request line.
 * @param request The full HTTP request string.
 * @return A malloc'd string containing the request URI.
 */
char* extract_request_uri(char *request);

/**
 * Forwards the HTTP request to the server and returns the response.
 * @param client_fd The client socket descriptor to send back the response.
 * @param server_fd The server socket descriptor to forward the request.
 * @param request The HTTP request string.
 * @param response_length Pointer to store the response length.
 * @return A malloc'd buffer containing the full response.
 */
char *forward_request(int client_fd, int server_fd, char *request, int *response_length);

#endif