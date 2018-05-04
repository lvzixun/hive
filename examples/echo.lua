local hive = require "hive"
local socket = require "hive.socket"
local thread = require "hive.thread"

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


function M:echo(client_id)
    socket.attach(client_id)
    local s, err = socket.read(client_id)
    if s and #s>0 then
        printf("\nrecv:%s from socket id:%s by actor:%s\n", s, client_id, SELF_HANDLE)
        socket.send(client_id, source)
        socket.close(client_id)
    elseif s and #s == 0 then
        printf("socket id:%s is break", client_id)
    else
        printf("s:%s error:%s", s, err)
    end
end


hive.start(M)

