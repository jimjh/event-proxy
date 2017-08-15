/* defs.h
 * Convenience macros, error codes.
 */

#ifndef defs_h
#define defs_h

// -- TYPES --
#define str char *
#define BUFFER_LEN 256

// -- ERROR CODES --
#define SUCCESS 0

#define ERR_LOG_INIT 41

#define ERR_NET_BIND 51
#define ERR_NET_HOST 52
#define ERR_NET_LISTEN 53
#define ERR_NET_FCNTL 54

#define ERR_EVENT_BASE 61
#define ERR_EVENT_NEW 62
#define ERR_EVENT_ADD 63
#define ERR_EVENT_DISPATCH 64

#endif /* defs_h */
