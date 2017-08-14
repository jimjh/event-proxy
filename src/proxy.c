/* proxy.c
 * Assembles components and runs the proxy.
 */

#include <arpa/inet.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <strings.h>
#include <unistd.h>
#include <stdlib.h>
#include <netdb.h>
#include <zlog.h>
#include <errno.h>
#include <fcntl.h>
#include <event2/event.h>
#include "config.h"
#include "proxy.h"

// -- DECLARATIONS --

static int _init_listen_fd(const str listen_addr,
                           const str listen_port,
                           int *sock_fd);

//static int _init_up_fd(const str up_addr,
//                       const str up_port,
//                       int *sock_fd);

// -- PUBLIC --

int proxy(const str listen_addr,
          const str listen_port,
          const str up_addr,
          const str up_port) {

  dzlog_debug("proxy invoked: %s:%s -> %s:%s", listen_addr, listen_port, up_addr, up_port);

  char printable[BUFFER_LEN];
  int listen_fd = -1;
  int rc = SUCCESS;
  struct event_base *ev_base = NULL;

  bzero(printable, BUFFER_LEN);

  // create listen file descriptor
  if (SUCCESS != (rc = _init_listen_fd(listen_addr, listen_port, &listen_fd))) {
    return rc;
  }

  // make descriptor non-blocking
  if (0 != fcntl(listen_fd, F_SETFL, O_NONBLOCK)) {
    strerror_r(errno, printable, BUFFER_LEN);
    dzlog_error("fcntl: %s", printable);
    return ERR_NET_FCNTL;
  }

  // initialize event loop
  ev_base = event_base_new();
  if (NULL == ev_base) {
    return ERR_EVENT_BASE;
  }
  event_new(ev_base, listen_fd, EV_READ|EV_PERSIST, callback_fn, (void *) ev_base);

  /* use libevent to handle events on listen_fd
   * on new connection, create upstream fd
   * proxy reads/writes with event loop
   */

  return SUCCESS;
}

// -- PRIVATE --

static int _init_listen_fd(const str listen_addr,
                         const str listen_port,
                         int *sock_fd) {

  char printable[BUFFER_LEN];
  struct addrinfo hints;
  struct addrinfo *servinfo = NULL;
  int listen_fd = -1;
  int yes = 1;
  struct addrinfo *p = NULL;

  bzero(printable, BUFFER_LEN);
  dzlog_debug("_init_sock_fd invoked: %s:%s", listen_addr, listen_port);

  // first, do a DNS lookup (which also works with IP addresses) to construct addrinfo
  bzero(&hints, sizeof(hints));
  hints.ai_family = AF_UNSPEC;  // both v4 and v6
  hints.ai_socktype = SOCK_STREAM;  // TCP
  hints.ai_flags = AI_PASSIVE;  // fill in IP for me
  if (0 != getaddrinfo(listen_addr, listen_port, &hints, &servinfo)) {
    strerror_r(errno, printable, BUFFER_LEN);
    dzlog_error("getaddrinfo: %s", printable);
    return ERR_NET_HOST;
  }

  dzlog_debug("getaddrinfo returned.");

  // keep trying until we find an address we can use
  for (p = servinfo; NULL != p; p = p->ai_next) {

    // create TCP socket (UNIX file)
    listen_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (0 > listen_fd) {
      continue;
    }

    // reuse recently used addresses
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    // bind to address and port
    if (0 != bind(listen_fd, p->ai_addr, p->ai_addrlen)) {
      close(listen_fd);
      continue;
    }

    // "network to presentation"
    if (p->ai_family == AF_INET) {
      struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
      inet_ntop(p->ai_family, &(ipv4->sin_addr), printable, BUFFER_LEN);
    } else {
      struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)p->ai_addr;
      inet_ntop(p->ai_family, &(ipv6->sin6_addr), printable, BUFFER_LEN);
    }

    dzlog_info("bound socket to %s:%s", printable, listen_port);
    break;
  }

  if (NULL == p) {
    // reached end of loop
    return ERR_NET_BIND;
  }

  // free address struct
  freeaddrinfo(servinfo);
  dzlog_debug("freeaddrinfo returned.");

  if (0 != listen(listen_fd, LISTEN_BACKLOG)) {
    strerror_r(errno, printable, BUFFER_LEN);
    dzlog_error("listen: %s", printable);
    return ERR_NET_LISTEN;
  }

  dzlog_info("listening on fd %u", listen_fd);
  *sock_fd = listen_fd;
  return SUCCESS;
}
