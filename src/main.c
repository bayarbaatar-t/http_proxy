#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "proxy.h"
#include "cache.h"

int main(int argc, char *argv[]) {
    if (argc < 3 || argc > 4 || strcmp(argv[1], "-p") != 0) {
        fprintf(stderr, "Usage: %s -p listen-port [-c]\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[2]);
    int enable_cache = (argc == 4 && strcmp(argv[3], "-c") == 0);

    // Enable cache to be used in stage 2
    start_proxy(port, enable_cache);

    return 0;
}