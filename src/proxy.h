/* proxy.h
 * Assembles components and runs the proxy.
 */
#ifndef proxy_h
#define proxy_h

#include <stdio.h>
#include <zlog.h>
#include "defs.h"

/**
 * Proxies TCP frames from listen to up.
 */
int proxy(const str listen_addr,
          const str listen_port,
          const str up_addr,
          const str up_port);

#endif /* proxy_h */
