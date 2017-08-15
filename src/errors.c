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
