# hive
a actor model server, inspired by [skynet](https://github.com/cloudwu/skynet), just for fun. ;D


## build
support macosx and linux
```
$ git clone https://github.com/lvzixun/hive.git
$ cd hive
$ make
$ ./hive [bootstrap_actor_lua_path]
```
`bootstrap_actor_lua_path` by default is `examples/bootstrap.lua`

## tutorial
read actors lua source code in [examples](https://github.com/lvzixun/hive/tree/master/examples) for more detail.

[examples/socks5.lua](https://github.com/lvzixun/hive/blob/master/examples/socks5.lua) is a simple socks5 proxy server. only support no authentication method.

[hive_lua/hive.lua](https://github.com/lvzixun/hive/blob/master/hive_lua/hive.lua) implements the actor interface.
[hive_lua/hive/socket.lua](https://github.com/lvzixun/hive/blob/master/hive_lua/hive/socket.lua) implements socket operation inreface (use coroutine wrap).
[hive_lua/hive/thread.lua](https://github.com/lvzixun/hive/blob/master/hive_lua/hive/thread.lua) lua coroutine wrap.

### actor api
| api name | description |
|:------:|:------|
| `hive.create(path, name)` | create `name` actor from `path`, return actor handle |
| `hive.exit(actor_handle)` | exit actor |
|`hive.send(target_handle, session, data)`| send message to `target_handle` actor |
| `hive.start(actor_obj, ud)`| register actor obj |
| `hive.abort()` | exit hive process. socket manager, all actors and timer manager will be exited|

### socket api
| api name | description |
|:------:|:------|
| `socket.connect(host, port)` | connect `host`:`port` address |
| `socket.listen(host, port, on_accept_func)`| listen `host`:`port` address `on_accept_func` is accept event callback |
| `socket.read(id [, size])` | read data from socket id |
| `socket.send(id, data)`| send socket data to id |
| `socket.attach(id)`| start accpet socket event |
| `socket.close(id)`| close socket id |
