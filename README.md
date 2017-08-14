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
1. Create event loop with libevent.
1. Proxy TCP frames.
1. Handle concurrent connections.

## Developing

I use vim with YCM for writing, and XCode for profiling and debugging.

```bash
$ cd build
$ cmake .. # -G Xcode
$ make link_compile_commands_json  # for YouCompleteMe
$ make
```

### Dependencies

cmake probably won't work out of the box because of hardcoded paths to these:

- [zlog](https://github.com/HardySimpson/zlog)
- libevent

## References

- [libevent Book](http://www.wangafu.net/~nickm/libevent-book/01_intro.html)
