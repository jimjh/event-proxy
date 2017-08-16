//
//  main.c
//  event-proxy
//
//  CLI for the proxy. Deals with argument parsing.
//
//  Created by jim.lim  on 8/12/17.
//
//

#include <strings.h>
#include <zlog.h>
#include "proxy.h"
#include "main.h"


// -- DECLARATIONS --

static void _free_logger();
static int _init_logger();

// -- PUBLIC --

int main(const int argc,
         const char **argv) {

  int rc = 0;

  if (SUCCESS != (rc = _init_logger())) {
    return rc;
  }

  dzlog_info("logger initialized.");
  dzlog_debug("argc: %d", argc);
  dzlog_debug("argv: %lu", sizeof(argv));

  if (SUCCESS != (rc = proxy("localhost", "8080", "www.wangafu.net", "80"))) {
    _free_logger();
    return rc;
  }

  _free_logger();
  return SUCCESS;
}

// -- PRIVATE --

static void _free_logger() {
  zlog_fini();
}


static int _init_logger() {

  int rc = 0;

  if (0 != (rc = dzlog_init("zlog.conf", "main"))) {
    printf("Unable to initialize logger.\n");
    return ERR_LOG_INIT;
  }

  return SUCCESS;

}
