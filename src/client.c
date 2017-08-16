/* client.c
 *
 * client for connecting to the upstream
 */

#include <strings.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/param.h>
#include <netdb.h>
#include <sys/socket.h>
#include <zlog.h>
#include "errors.h"
#include "client.h"

int init_client_fd(const str up_addr,
                   const str up_port,
                   int *sock_fd) {

  char printable[BUFFER_LEN];
  struct addrinfo hints;
  struct addrinfo *servinfo = NULL;
  int client_fd = -1;
  struct addrinfo *p = NULL;

  bzero(printable, BUFFER_LEN);
  dzlog_debug("init_client_fd invoked: %s:%s", up_addr, up_port);

  // convert str to addr_info
  inet_hints(&hints);
  if (0 != getaddrinfo(up_addr, up_port, &hints, &servinfo)) {  // modern way, instead of gethostinfo and htons
    error("getaddrinfo");
    return ERR_NET_HOST;
  }

  // create socket
  for(p = servinfo; NULL != p; p = p->ai_next) {
    client_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (0 > client_fd) {
      continue;
    }
    if (0 > connect(client_fd, p->ai_addr, p->ai_addrlen)) {
      close(client_fd);
      continue;
    }
    inet_ntop_addrinfo(p, printable, BUFFER_LEN);
    dzlog_info("connected to %s:%s", printable, up_port);
    break;
  }

  // free address struct
  if (NULL != servinfo) {
    freeaddrinfo(servinfo);
    servinfo = NULL;
  }

  if (NULL == p) {
    // reached end of loop
    return ERR_NET_CONNECT;
  }

  *sock_fd = client_fd;
  return SUCCESS;
}
