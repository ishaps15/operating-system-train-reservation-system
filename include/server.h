#ifndef SERVER_H
#define SERVER_H

#include <stdbool.h>

#define DEFAULT_SERVER_PORT 9090
#define BUFFER_SIZE         4096

/* Server Lifecycle */
int  server_start(int port);
void server_stop(void);
bool server_is_running(void);

#endif /* SERVER_H */
