#include "client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 4096

int client_connect_and_run(const char *host, int port) {
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address / Address not supported: %s\n", host);
        close(sock_fd);
        return -1;
    }

    if (connect(sock_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection to PRS Server failed");
        close(sock_fd);
        return -1;
    }

    printf("=======================================================================\n");
    printf(" Connected to Central PRS Server at %s:%d\n", host, port);
    printf(" Type commands (e.g. PING, LOGIN <user> <pass>, SEARCH <src> <dst>, QUIT)\n");
    printf("=======================================================================\n");

    char buffer[BUFFER_SIZE];
    ssize_t bytes = read(sock_fd, buffer, sizeof(buffer)-1);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        printf("%s", buffer);
    }

    while (1) {
        printf("PRS-TERM> ");
        fflush(stdout);

        if (!fgets(buffer, sizeof(buffer), stdin)) {
            break;
        }

        if (strncmp(buffer, "QUIT", 4) == 0 || strncmp(buffer, "quit", 4) == 0) {
            write(sock_fd, "QUIT\n", 5);
            break;
        }

        write(sock_fd, buffer, strlen(buffer));

        memset(buffer, 0, sizeof(buffer));
        bytes = read(sock_fd, buffer, sizeof(buffer)-1);
        if (bytes <= 0) {
            printf("[Client] Server disconnected.\n");
            break;
        }
        buffer[bytes] = '\0';
        printf("%s", buffer);
    }

    close(sock_fd);
    return 0;
}
