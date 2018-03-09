local hive = require "hive"

print("hello bootstrap!")

local M = {}

function M:on_create()
    -- local network_actor = hive.hive_create("examples/server_gate.lua", "server_gate")
    -- hive.hive_free()
    -- local host = "baidu.com"
    -- local host = "220.181.57.216"
    -- local port = 80
    local host = "127.0.0.1"
    -- local host = "localhost"
    local port = 9391
    local id = hive.socket_connect(host, port, {
            on_error = function (_, id, err)
                print("connect error ", err)
            end,

            on_break = function ()
                print("connect break")
            end,

            on_recv = function (_, id, data)
                print("recv data:", #data, data)
            end
        })
    assert(id >= 0)
    local source = [[
GET / HTTP/1.1
User-Agent: curl/7.33.0
Host: baidu.com
Accept: */*

]]
    local ret = hive.socket_send(id, source)
    print("hive send source!", ret, #source)
end


hive.hive_start(M)