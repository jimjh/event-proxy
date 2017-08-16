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
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <zlog.h>
#include "errors.h"
#include "defs.h"
#include "client.h"
#include "io.h"

typedef struct cb_arg_struct {
  int accept_fd;
  int client_fd;
  struct evbuffer *a2c;
  struct evbuffer *c2a;
} cb_arg;

static void cb_arg_free(cb_arg *arg) {
  evbuffer_free(arg->a2c); arg->a2c = NULL;
  evbuffer_free(arg->c2a); arg->c2a = NULL;
  free(arg); arg = NULL;
}

static cb_arg *cb_arg_new() {

  cb_arg *p = calloc(1, sizeof(cb_arg));

  if (NULL == (p->a2c = evbuffer_new())) {
    dzlog_error("evbuffer_new failed");
    free(p); p = NULL;
    return NULL;
  }

  if (NULL == (p->c2a = evbuffer_new())) {
    dzlog_error("evbuffer_new failed");
    evbuffer_free(p->a2c); p->a2c = NULL;
    free(p); p = NULL;
    return NULL;
  }

  return p;
}

// -- DECLARATIONS --
/* initializes read/write/error callbacks on the given file descriptors. */
static int _init_bufferevents(struct event_base *ev_base, int accept_fd, int client_fd);
/* helper method for _init_bufferevents */
static int _fd_event_new(struct event_base *ev_base, int fd, struct bufferevent **event, cb_arg *partner_arg);
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
    free(conn);
    return NULL;
  }
  strncpy(conn->up_addr, up_addr, length);

  length = strnlen(up_port, MAX_LINE);
  if (NULL == (conn->up_port = calloc(1, length + 1))) {  // add one for null byte
    error("calloc up_port");
    free(conn->up_addr);
    free(conn);
    return NULL;
  }
  strncpy(conn->up_port, up_port, length);

  return conn;
}

void do_accept(int listen_fd, short event, void *arg) {

  char printable[BUFFER_LEN];
  conn_details *conn = arg;
  struct sockaddr_storage ss;
  socklen_t slen = sizeof(ss);
  int accept_fd = -1;
  int client_fd = -1;
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

  // create client connection to upstream
  if (SUCCESS != init_client_fd(conn->up_addr, conn->up_port, &client_fd)) {
    dzlog_error("could not connect to %s:%s", conn->up_addr, conn->up_port);
    close(accept_fd);
    return;
  }
  dzlog_info("created connection to %s:%s with fd %u", conn->up_addr, conn->up_port, client_fd);

  // init buffer events
  if (SUCCESS != _init_bufferevents(conn->ev_base, accept_fd, client_fd)) {
    close(client_fd);
    close(accept_fd);
  }
  dzlog_info("bufferevent setup done on new connection at accept_fd %u and client_fd %u", accept_fd, client_fd);
}

static int _init_bufferevents(struct event_base *ev_base, int accept_fd, int client_fd) {
  // Use bufferevent API.
  // Bufferevents are higher level than evbuffers: each has an underlying evbuffer for reading and
  // one for writing, and callbacks that are invoked under certain circumstances.

  int rc = SUCCESS;
  struct bufferevent *accept_event = NULL;
  struct bufferevent *client_event = NULL;
  cb_arg *pipe = NULL;

  if (NULL == (pipe = cb_arg_new())) {
    error("cb_arg");
    return ERR_BEVENT_NEW;
  }
  pipe->accept_fd = accept_fd;
  pipe->client_fd = client_fd;

  if (0 > (rc = _fd_event_new(ev_base, accept_fd, &accept_event, pipe))) {
    cb_arg_free(pipe); pipe = NULL;
    return rc;
  }

  // note that client_event should be freed in the error callback
  if (0 > (rc = _fd_event_new(ev_base, client_fd, &client_event, pipe))) {
    cb_arg_free(pipe); pipe = NULL;
    bufferevent_free(accept_event); accept_event = NULL;
    return rc;
  }

  return SUCCESS;
}

static int _fd_event_new(struct event_base *ev_base, int fd, struct bufferevent **event, cb_arg *arg) {

  struct bufferevent *bev = NULL;

  // set fd to be non blocking
  if (0 != fcntl(fd, F_SETFL, O_NONBLOCK)) {
    error("fcntl");
    return ERR_NET_FCNTL;
  }

  // note that bev and partner_arg get freed in the error callback
  if (NULL == (bev = bufferevent_socket_new(ev_base, fd, BEV_OPT_CLOSE_ON_FREE))) {
    dzlog_error("bufferevent_socket_new returned NULL");
    return ERR_BEVENT_NEW;
  }

  bufferevent_setwatermark(bev, EV_READ | EV_WRITE, 0, MAX_LINE);
  bufferevent_setcb(bev, readcb, writecb, errorcb, arg);

  if (0 != bufferevent_enable(bev, EV_READ|EV_WRITE)) {
    dzlog_error("bufferevent_enable failed");
    bufferevent_free(bev); bev = NULL;
    return ERR_BEVENT_ENABLE;
  }

  *event = bev;
  return SUCCESS;
}

static void readcb (struct bufferevent *bev, void *arg) {
  // A read callback for a bufferevent. The read callback is triggered when new data
  // arrives in the input buffer and the amount of readable data exceed the low watermark
  // which is 0 by default.

  cb_arg *pipe = arg;
  struct evbuffer *input = NULL;
  struct evbuffer *output = NULL;
  int fd = bufferevent_getfd(bev);

  dzlog_debug("received data on fd %u", fd);

  // copy bytes from input to partner write buffer
  input = bufferevent_get_input(bev);
  if (fd == pipe->accept_fd) {
    output = pipe->a2c;
  } else if (fd == pipe->client_fd) {
    output = pipe->c2a;
  } else {
    dzlog_error("unknown fd: %u", fd);
    return;
  }

  if (0 > evbuffer_add_buffer(output, input)) {  // do we need a lock here?
    dzlog_error("evbuffer_add_buffer failed");  // what do we do here?
  }
}

static void writecb (struct bufferevent *bev, void *arg) {

  cb_arg *pipe = arg;
  struct evbuffer *input = NULL;
  struct evbuffer *output = NULL;
  int fd = bufferevent_getfd(bev);

  dzlog_debug("writing data to fd %u", fd);

  // copy bytes from input to partner write buffer
  output = bufferevent_get_output(bev);
  if (fd == pipe->accept_fd) {
    input = pipe->c2a;
  } else if (fd == pipe->client_fd) {
    input = pipe->a2c;
  } else {
    dzlog_error("unknown fd: %u", fd);
    return;
  }

  // copy bytes from write buffer into output
  if (0 > evbuffer_add_buffer(output, input)) {  // do we need a lock here?
    dzlog_error("evbuffer_add_buffer failed");  // what do we do here?
  }
}

static void errorcb (struct bufferevent *bev, short what, void *arg) {
  // An event/error callback for a bufferevent. The event callback is triggered if either an EOF
  // condition or another unrecoverable error was encountered.
  int fd = bufferevent_getfd(bev);
  cb_arg *pipe = arg;
  if (what & BEV_EVENT_EOF) {
    // connection closed
    dzlog_info("connection with fd %u closed", fd);
  } else if (what & BEV_EVENT_ERROR) {
    error("connection error");
  } else if (what & BEV_EVENT_TIMEOUT) {
    dzlog_error("connection timeout with fd %u", fd);
  }
  bufferevent_free(bev); bev = NULL;
  dzlog_debug("bev struct freed");
  cb_arg_free(pipe); pipe = NULL;
  dzlog_debug("cb_arg struct freed");
  // TODO close partner fd
  // TODO flush on close
}
