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
#include <fcntl.h>
#include <event2/event.h>
#include <event2/bufferevent.h>
#include "config.h"
#include "errors.h"
#include "io.h"
#include "proxy.h"

// -- DECLARATIONS --

/* Creates a TCP socket, binds to the given port, and starts listening. */
static int _init_listen_fd(const str listen_addr,
                           const str listen_port,
                           int *sock_fd);
/* Starts an event loop with the given listening file descriptor. */
static int _init_event_loop(int listen_fd,
                            const str up_addr,
                            const str up_port);

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
  if (SUCCESS != (rc = _init_event_loop(listen_fd, up_addr, up_port))) {
    return rc;
  }

  return SUCCESS;

}

// -- PRIVATE --

static int _init_event_loop(int listen_fd,
                            const str up_addr,
                            const str up_port) {

  // TODO return ev_listen and ev_base so that we can free them properly

  struct event_base *ev_base = NULL;
  struct event *ev_listen = NULL;
  conn_details *conn = NULL;

  // make descriptor non-blocking
  if (0 != fcntl(listen_fd, F_SETFL, O_NONBLOCK)) {
    error("fcntl");
    return ERR_NET_FCNTL;
  }

  // initialize event loop
  if (NULL == (ev_base = event_base_new())) {
    return ERR_EVENT_BASE;
  }

  // create conn_details (this transfers ownership of ev_base to conn_details)
  if (NULL == (conn = conn_details_new(ev_base, up_addr, up_port))) {
    return ERR_CONN_DETAILS_NEW;
  }

  // create a new event (EV_PERSIST means add the event back to the select set after firing)
  // EV_READ means it's a read event
  // the last argument is passed along to the callback
  if (NULL == (ev_listen = event_new(ev_base, listen_fd, EV_READ|EV_PERSIST, do_accept, conn))) {
    conn_details_free(conn);
    return ERR_EVENT_NEW;
  }

  if (0 != event_add(ev_listen, NULL)) { // NULL means no timeout
    conn_details_free(conn);
    event_free(ev_listen);
    return ERR_EVENT_ADD;
  }

  dzlog_info("constructed event objects");

  dzlog_info("dispatching event loop");
  if (0 != event_base_dispatch(ev_base)) { // start loop; blocks
    conn_details_free(conn);
    event_free(ev_listen);
    return ERR_EVENT_DISPATCH;
  }

  dzlog_info("event loop exited");  // how will we ever get here?
  conn_details_free(conn);
  event_free(ev_listen);
  return SUCCESS;
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
  if (0 != getaddrinfo(listen_addr, listen_port, &hints, &servinfo)) {  // modern way, instead of gethostinfo and htons
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
  servinfo = NULL;
  dzlog_debug("freeaddrinfo returned.");

  if (0 != listen(listen_fd, LISTEN_BACKLOG)) {
    error("listen");
    return ERR_NET_LISTEN;
  }

  dzlog_info("listening on fd %u", listen_fd);
  *sock_fd = listen_fd;
  return SUCCESS;
}
