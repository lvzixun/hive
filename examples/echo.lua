local hive = require "hive"

local M = {}
local Socket_M = {}


local function printf(f, ...)
    local s = string.format(f, ...)
    print(s)
end


local source = [[
    HTTP/1.0 200 OK
    Content-Type: text/html; charset=UTF-8

    <!DOCTYPE html>
    <html>
      <p>welcome to hive~</p>
    </html>
]]

function Socket_M:on_recv(id, data)
    hive.socket_send(id, source)
    hive.socket_close(id)
    local s = string.format("\nrecv:%s from socket id:%s by actor:%s\n", data, id, SELF_HANDLE)
    print(s)
end

function Socket_M:on_close(id)
    print("close socket id", id)
end


function Socket_M:on_break(id)
    printf("break socket id:%s by actor:%s", id, SELF_HANDLE)
end


function M:on_recv(source, client_id)
    hive.socket_attach(client_id, Socket_M)
end


hive.hive_start(M)

