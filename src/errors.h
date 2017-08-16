/* errors.h
 * Error-handling utils.
 */
#ifndef error_h
#define error_h

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "defs.h"

/* Constructs error message from errno and logs it. */
int error(const str message);
/* Network-to-presentation (for IP addresses), from addrinfo */
void inet_ntop_addrinfo(struct addrinfo *p, char *printable, size_t length);
/* Network-to-presentation (for IP addresses), from sockaddr */
void inet_ntop_sockaddr(struct sockaddr_storage *p, char *printable, size_t length);
/* Network-to-host (for port), from sockaddr */
int ntoh_sockaddr(const struct sockaddr_storage *ss);
/* Prepares a hints struct suitable for getaddrinfo.  */
void inet_hints(struct addrinfo *hints);

#endif /* error_h */
