# event-proxy

A event-based proxy written in C for self-study purposes.

## Design/Requirements

- bind to and listen on IPv4 or IPv6 address
- run an event loop
- handle concurrent connections
- proxy connections

See https://www.nginx.com/blog/inside-nginx-how-we-designed-for-performance-scale/

### Steps

1. Write proxy that accepts and proxies connections on IPv4 and IPv6 addresses.
1. Review error handling.
1. Run valgrind checks.

## Developing

```bash
$ cd build
$ cmake .. # -G Xcode
$ make
```
