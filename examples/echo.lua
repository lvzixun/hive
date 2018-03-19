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


function M:on_recv(_, client_id)
    thread.run(function ()
            socket.attach(client_id)
            while true do
                local s, err = socket.read(client_id)
                if s and #s>0 then
                    printf("\nrecv:%s from socket id:%s by actor:%s\n", s, client_id, SELF_HANDLE)
                    socket.send(client_id, source)
                    socket.close(client_id)
                    break
                elseif s and #s == 0 then
                    printf("socket id:%s is break", client_id)
                    break
                else
                    printf("s:%s error:%s", s, err)
                    break
                end
            end
        end)
end


hive.start(M)

