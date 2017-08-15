/* proxy.c
 * Assembles components and runs the proxy.
 */

 /* TODO use libevent to handle events on listen_fd
  * TODO on new connection, create upstream fd
  * TODO proxy reads/writes with event loop
  */

#include <arpa/inet.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <strings.h>
#include <unistd.h>
#include <stdlib.h>
#include <netdb.h>
#include <zlog.h>
#include <fcntl.h>
#include <event2/event.h>
#include "config.h"
#include "errors.h"
#include "proxy.h"

// -- DECLARATIONS --

static int _init_listen_fd(const str listen_addr,
                           const str listen_port,
                           int *sock_fd);
static int _init_event_loop(int listen_fd);
static void inet_ntop_addrinfo(struct addrinfo *p, char *printable, size_t length);
static void inet_ntop_sockaddr(struct sockaddr_storage *p, char *printable, size_t length);
static void _do_accept(int listen_fd, short event, void *arg);

//static int _init_up_fd(const str up_addr,
//                       const str up_port,
//                       int *sock_fd);

// -- PUBLIC --

int proxy(const str listen_addr,
          const str listen_port,
          const str up_addr,
          const str up_port) {

  dzlog_debug("proxy invoked: %s:%s -> %s:%s", listen_addr, listen_port, up_addr, up_port);

  int listen_fd = -1;
  int rc = SUCCESS;

  // create listen file descriptor
  if (SUCCESS != (rc = _init_listen_fd(listen_addr, listen_port, &listen_fd))) {
    return rc;
  }

  // create event loop on given fd
  if (SUCCESS != (rc = _init_event_loop(listen_fd))) {
    return rc;
  }

  return SUCCESS;

}

// -- PRIVATE --

static int _init_event_loop(int listen_fd) {

  // TODO return ev_listen so that we can free it properly

  struct event_base *ev_base = NULL;
  struct event *ev_listen = NULL;

  // make descriptor non-blocking
  if (0 != fcntl(listen_fd, F_SETFL, O_NONBLOCK)) {
    error("fcntl");
    return ERR_NET_FCNTL;
  }

  // initialize event loop
  ev_base = event_base_new();
  if (NULL == ev_base) {
    return ERR_EVENT_BASE;
  }

  // create a new event (EV_PERSIST means add the event back to the select set after firing)
  ev_listen = event_new(ev_base, listen_fd, EV_READ|EV_PERSIST, _do_accept, (void *) ev_base);
  // EV_READ means it's a read event
  // the last argument is passed along to the callback
  if (NULL == ev_listen) {
    event_base_free(ev_base);
    return ERR_EVENT_NEW;
  }

  if (0 != event_add(ev_listen, NULL)) { // NULL means no timeout
    event_base_free(ev_base);
    event_free(ev_listen);
    return ERR_EVENT_ADD;
  }

  dzlog_info("constructed event objects");

  dzlog_info("dispatching event loop");
  if (0 != event_base_dispatch(ev_base)) { // start loop; blocks
    event_base_free(ev_base);
    event_free(ev_listen);
    return ERR_EVENT_DISPATCH;
  }

  return SUCCESS;
}

static void inet_ntop_addrinfo(struct addrinfo *p, char *printable, size_t length) {
  struct sockaddr_storage *ss = (struct sockaddr_storage *)p->ai_addr;
  inet_ntop_sockaddr(ss, printable, length);
}

static void inet_ntop_sockaddr(struct sockaddr_storage *ss, char *printable, size_t length) {
  // "network to presentation"
  if (ss->ss_family == AF_INET) {
    struct sockaddr_in *ipv4 = (struct sockaddr_in *)ss;
    inet_ntop(ss->ss_family, &(ipv4->sin_addr), printable, length);
  } else {
    struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)ss;
    inet_ntop(ss->ss_family, &(ipv6->sin6_addr), printable, length);
  }
}

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
    error("getaddrinfo");
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
    inet_ntop_addrinfo(p, printable, BUFFER_LEN);
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
    error("listen");
    return ERR_NET_LISTEN;
  }

  dzlog_info("listening on fd %u", listen_fd);
  *sock_fd = listen_fd;
  return SUCCESS;
}

/* callback_fn on listen_fd */
static void _do_accept(int listen_fd, short event, void *arg) {

//  struct fd_state *state = NULL;
  char printable[BUFFER_LEN];
  struct event_base *base = NULL;
  struct sockaddr_storage ss;
  socklen_t slen = sizeof(ss);
  int accept_fd = -1;
  in_port_t port = -1;

  bzero(printable, BUFFER_LEN);

  // accept connection
  dzlog_debug("received event: %u on fd: %u", event, listen_fd);
  accept_fd = accept(listen_fd, (struct sockaddr *) &ss, &slen);
  if (0 > accept_fd) {
    error("accept");
    return;
  } else if (accept_fd > FD_SETSIZE) { // when does this happen?
    dzlog_error("accept_fd: %u > %u, closing", accept_fd, FD_SETSIZE);
    close(accept_fd);
    return;
  }

  // set fd to be non blocking
  base = arg;
  if (0 != fcntl(listen_fd, F_SETFL, O_NONBLOCK)) {
    error("fcntl");
    return;
  }

  // print diagnostics
  if (ss.ss_family == AF_INET) {
    port = ((struct sockaddr_in *)(&ss))->sin_port;
  } else {
    port = ((struct sockaddr_in6 *)(&ss))->sin6_port;
  }
  inet_ntop_sockaddr(&ss, printable, BUFFER_LEN);
  dzlog_info("accepted connection on %s:%u", printable, port);

//  state = alloc_fd_state(base, accept_fd);
//  if (NULL == state) {
//    dzlog_error("could not allocate fd state for request");
//    close(accept_fd);
//  }
//
//  if (0 != event_add(state->read_event, NULL)) {
//    // TODO free state
//    close(accept_fd);
//    return;
//  }

  return;
}
