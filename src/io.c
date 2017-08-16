/* io.c
 *
 * Defines read/write/accept handlers.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <netdb.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <zlog.h>
#include "errors.h"
#include "defs.h"
#include "io.h"

// -- DECLARATIONS --
/* initializes read/write/error callbacks on the given file descriptor. */
static int _init_bufferevents(struct event_base *ev_base, int accept_fd);
static void readcb (struct bufferevent *bev, void *arg);
static void writecb (struct bufferevent *bev, void *arg);
static void errorcb (struct bufferevent *bev, short what, void *arg);

// -- PUBLIC --

/* Frees the struct and the inner strings. */
void conn_details_free(conn_details *conn) {
  event_base_free(conn->ev_base);
  conn->ev_base = NULL;

  free(conn->up_addr);
  conn->up_addr = NULL;

  free(conn->up_port);
  conn->up_port = NULL;
}

/* Creates a new struct and copies the contents of each string into a new memory space. */
conn_details *conn_details_new(struct event_base *ev_base,
                               const str up_addr,
                               const str up_port) {

  conn_details *conn = NULL;
  size_t length = 0;

  if (NULL == (conn = calloc(1, sizeof(conn_details)))) {
    error("calloc conn_details");
    return NULL;
  }

  conn->ev_base = ev_base;

  length = strnlen(up_addr, MAX_LINE);
  if (NULL == (conn->up_addr = calloc(1, length + 1))) {  // add one for null byte
    error("calloc up_addr");
    return NULL;
  }
  strncpy(conn->up_addr, up_addr, length);

  length = strnlen(up_port, MAX_LINE);
  if (NULL == (conn->up_port = calloc(1, length + 1))) {  // add one for null byte
    error("calloc up_port");
    return NULL;
  }
  strncpy(conn->up_port, up_port, length);

  return conn;
}

/* callback_fn on listen_fd */
void do_accept(int listen_fd, short event, void *arg) {

  char printable[BUFFER_LEN];
  conn_details *conn = arg;
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

  // print diagnostics
  port = ntoh_sockaddr(&ss);
  inet_ntop_sockaddr(&ss, printable, BUFFER_LEN);
  dzlog_info("accepted connection on %s:%u with fd %u", printable, port, accept_fd);

  // TODO create client connection to upstream

  // init buffer events
  if (SUCCESS != _init_bufferevents(conn->ev_base, accept_fd)) {
    close(accept_fd);
  }
  dzlog_info("bufferevent setup done on new connection at accept_fd %u", accept_fd);
}

static int _init_bufferevents(struct event_base *ev_base, int accept_fd) {

  struct bufferevent *bev = NULL;

  // set fd to be non blocking
  if (0 != fcntl(accept_fd, F_SETFL, O_NONBLOCK)) {
    error("fcntl");
    return ERR_NET_FCNTL;
  }

  // use bufferevent API
  // Bufferevents are higher level than evbuffers: each has an underlying evbuffer for reading and
  // one for writing, and callbacks that are invoked under certain circumstances.
  // Note that bev gets freed in the error callback
  if (NULL == (bev = bufferevent_socket_new(ev_base, accept_fd, BEV_OPT_CLOSE_ON_FREE))) {
    dzlog_error("bufferevent_socket_new returned NULL");
    return ERR_BEVENT_NEW;
  }

  bufferevent_setcb(bev, readcb, writecb, errorcb, (void *) (long) accept_fd);
  bufferevent_setwatermark(bev, EV_READ | EV_WRITE, 0, MAX_LINE);
  if (0 != bufferevent_enable(bev, EV_READ|EV_WRITE)) {
    dzlog_error("bufferevent_enable failed");
    bufferevent_free(bev);
    return ERR_BEVENT_ENABLE;
  }

  return SUCCESS;
}

static void readcb (struct bufferevent *bev, void *arg) {
  // A read callback for a bufferevent. The read callback is triggered when new data
  // arrives in the input buffer and the amount of readable data exceed the low watermark
  // which is 0 by default.
  in_port_t port = (int) arg;
  dzlog_info("received data on fd %u", port);
  bev = NULL;
}

static void writecb (struct bufferevent *bev, void *ctx) {
  bev = NULL;
  ctx = NULL;
}

static void errorcb (struct bufferevent *bev, short what, void *arg) {
  // An event/error callback for a bufferevent. The event callback is triggered if either an EOF
  // condition or another unrecoverable error was encountered.
  int fd = (int) arg;
  if (what & BEV_EVENT_EOF) {
    // connection closed
    dzlog_info("connection at fd %u closed", fd);
  } else if (what & BEV_EVENT_ERROR) {
    error("connection error");
  } else if (what & BEV_EVENT_TIMEOUT) {
    dzlog_error("connection timeout at fd %u", fd);
  }
  bufferevent_free(bev);
  bev = NULL;
  dzlog_debug("bev struct freed");
}
