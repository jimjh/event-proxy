/* client.c
 *
 * client for connecting to the upstream
 */

#ifndef client_h
#define client_h

#include "defs.h"

/* Creates a connection to the upstream host.
 *
 * @return success or error codes.
 */
int init_client_fd(const str up_addr,
                   const str up_port,
                   int *sock_fd);

#endif  // client_h
