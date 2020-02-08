/* proxy.c
 * Assembles components and runs the proxy.
 */

#include <arpa/inet.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <signal.h>
#include <string.h>
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
static void quit_cb (int signum, short event, void *arg) {
  struct event_base *ev_base = arg;
  int rc = 0;
  dzlog_info("quitting on signal: %d, event: %d", signum, event);
  if (0 > (rc = event_base_loopexit(ev_base, NULL))) {  // exit after all current events
    dzlog_error("loopexit failed with rc: %d", rc);
  }
}

static int _init_event_loop(int listen_fd,
                            const str up_addr,
                            const str up_port) {

  struct event_base *ev_base = NULL;
  struct event *ev_listen = NULL;
  struct event *ev_quit = NULL;
  conn_details *conn = NULL;  // allocated struct to be shared with all upstream clients

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
    event_base_free(ev_base); ev_base = NULL;
    return ERR_CONN_DETAILS_NEW;
  }

  // create a new event (EV_PERSIST means add the event back to the select set after firing)
  // EV_READ means it's a read event
  // the last argument is passed along to the callback
  if (NULL == (ev_listen = event_new(ev_base, listen_fd, EV_READ|EV_PERSIST, do_accept, conn))) {
    event_base_free(ev_base); ev_base = NULL;
    conn_details_free(conn); conn = NULL;
    return ERR_EVENT_NEW;
  }

  if (0 != event_add(ev_listen, NULL)) { // NULL means no timeout
    event_base_free(ev_base); ev_base = NULL;
    conn_details_free(conn); conn = NULL;
    event_free(ev_listen); ev_listen = NULL;
    return ERR_EVENT_ADD;
  }

  if (NULL == (ev_quit = evsignal_new(ev_base, SIGQUIT, quit_cb, ev_base))) {
    event_base_free(ev_base); ev_base = NULL;
    conn_details_free(conn); conn = NULL;
    event_free(ev_listen); ev_listen = NULL;
    return ERR_EVENT_NEW;
  }

  if (0 != event_add(ev_quit, NULL)) { // NULL means no timeout
    event_base_free(ev_base); ev_base = NULL;
    conn_details_free(conn); conn = NULL;
    event_free(ev_listen); ev_listen = NULL;
    event_free(ev_quit); ev_quit = NULL;
    return ERR_EVENT_ADD;
  }

  dzlog_info("constructed event objects");

  dzlog_info("dispatching event loop");
  if (0 != event_base_dispatch(ev_base)) { // start loop; blocks
    event_base_free(ev_base); ev_base = NULL;
    conn_details_free(conn); conn = NULL;
    event_free(ev_listen); ev_listen = NULL;
    event_free(ev_quit); ev_quit = NULL;
    return ERR_EVENT_DISPATCH;
  }

  dzlog_info("event loop exited");
  event_base_free(ev_base);
  conn_details_free(conn);
  event_free(ev_listen);
  event_free(ev_quit);
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

  memset(printable, 0, BUFFER_LEN);
  dzlog_debug("_init_sock_fd invoked: %s:%s", listen_addr, listen_port);

  // first, do a DNS lookup (which also works with IP addresses) to construct addrinfo
  inet_hints(&hints);
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

  // free address struct
  if (NULL != servinfo) {
    freeaddrinfo(servinfo);
    servinfo = NULL;
  }

  if (NULL == p) {
    // reached end of loop
    return ERR_NET_BIND;
  }

  if (0 != listen(listen_fd, LISTEN_BACKLOG)) {
    error("listen");
    return ERR_NET_LISTEN;
  }

  dzlog_info("listening on fd %u", listen_fd);
  *sock_fd = listen_fd;
  return SUCCESS;
}
