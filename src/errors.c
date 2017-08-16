/* errors.c */

#include <errno.h>
#include <strings.h>
#include <zlog.h>
#include "errors.h"

// -- PUBLIC --

int error(const str message) {

  char printable[BUFFER_LEN];
  bzero(printable, BUFFER_LEN);

  strerror_r(errno, printable, BUFFER_LEN);
  dzlog_error("%s: %s", message, printable);

  return SUCCESS;
}

void inet_ntop_addrinfo(struct addrinfo *p, char *printable, size_t length) {
  struct sockaddr_storage *ss = (struct sockaddr_storage *)p->ai_addr;
  inet_ntop_sockaddr(ss, printable, length);
}

void inet_ntop_sockaddr(struct sockaddr_storage *ss, char *printable, size_t length) {
  // "network to presentation"
  if (ss->ss_family == AF_INET) {
    struct sockaddr_in *ipv4 = (struct sockaddr_in *)ss;
    inet_ntop(ss->ss_family, &(ipv4->sin_addr), printable, length);
  } else {
    struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)ss;
    inet_ntop(ss->ss_family, &(ipv6->sin6_addr), printable, length);
  }
}

int ntoh_sockaddr(const struct sockaddr_storage *ss) {
  if (ss->ss_family == AF_INET) {
    return ((struct sockaddr_in *)(&ss))->sin_port;
  } else {
    return ((struct sockaddr_in6 *)(&ss))->sin6_port;
  }
}
