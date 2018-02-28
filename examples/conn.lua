local hive = require "hive"
local M = {}
local Socket_M = {}


local function printf(f, ...)
    local s = string.format(f, ...)
    print(s)
end


function Socket_M:on_recv(id, data)
    printf("### recive data:%s from socket id:%s", data, id)
    hive.socket_send(id, "seek it!@!!")
    hive.socket_close(id)
end


function M:on_create()
    local id = hive.socket_connect("127.0.0.1", 9291, Socket_M)
    print("hive_socket_connect", id)
end


hive.hive_start(M)