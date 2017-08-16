/* io.h
 *
 * Defines read/write/accept handlers.
 */
#ifndef io_h
#define io_h

#include <event2/event.h>

/* connection details to be passed along to callbacks;
 * note that this struct "owns" ev_base and is responsible for free-ing the memory.
 */
struct conn_details_struct {
  struct event_base *ev_base;
  str up_addr;
  str up_port;
};

typedef struct conn_details_struct conn_details;

/* Callback used by the event loop when a connection is ready to be accepted. */
void do_accept(int listen_fd, short event, void *arg);

/* Frees the struct and the inner strings. */
void conn_details_free(conn_details *conn);

/* Creates a new struct and copies the contents of each string into a new memory space. */
conn_details *conn_details_new(struct event_base *ev_base,
                               const str up_addr,
                               const str up_port);

#endif /* io_h */
