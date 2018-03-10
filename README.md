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
all lua interface are implemented in [hive_lua/hive.lua](https://github.com/lvzixun/hive/blob/master/hive_lua/hive.lua). hive.lua is only included socket api and actor api now.

### actor api
| api name | description |
|:------:|:------|
| `hive_create(path, name)` | create `name` actor from `path`, return actor handle |
| `hive_free(actor_handle)` | release actor |
|`hive_send(target_handle, session, data)`| send message to `target_handle` actor |
| `hive_start(actor_obj, ud)`| register actor obj |
| `hive_exit()` | exit hive process. socket manager, all actors and timer manager will be exited|

### socket api
| api name | description |
|:------:|:------|
| `socket_connect(host, port, socket_obj, socket_ud)` | connect `host`:`port` address |
| `socket_listen(host, port, socket_obj, socket_ud)`| listen `host`:`port` address |
| `socket_send(id, data)`| send socket data to id |
| `socket_attach(id, socket_obj, socket_ud)`| register socket event object |
| `socket_close(id)`| close socket id |
