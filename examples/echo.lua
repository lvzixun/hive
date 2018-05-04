local hive = require "hive"
local socket = require "hive.socket"
local thread = require "hive.thread"
local logf = require("hive.log").logf

local M = {}
local Socket_M = {}



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
        logf("\nrecv:%s from socket id:%s by actor:%s\n", s, client_id, SELF_HANDLE)
        socket.send(client_id, source)
        socket.close(client_id)
    elseif s and #s == 0 then
        logf("socket id:%s is break", client_id)
    else
        logf("s:%s error:%s", s, err)
    end
end


hive.start(M)

