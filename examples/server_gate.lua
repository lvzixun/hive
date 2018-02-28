local hive = require "hive"

local M = {}
local Socket_M = {}

print("hello server_gate!")


local function printf(f, ...)
    local s = string.format(f, ...)
    print(s)
end


local get_echo_actor = nil
function M:on_create()
    local count = 16
    local echo_idx = 1
    local echo_poll = {}
    for i=1,count do
        echo_poll[i] = hive.hive_create("examples/echo.lua", "echo")
    end
    get_echo_actor = function ()
        local actor = echo_poll[echo_idx]
        echo_idx = echo_idx % count + 1
        return actor
    end

    -- open socket listen
    local id = hive.socket_listen("0.0.0.0", 9291, Socket_M)
end


function Socket_M:on_accept(client_id)
    local echo_actor = get_echo_actor()
    print("on_accept!", client_id)
    hive.hive_send(echo_actor, client_id)
end

function Socket_M:on_break(id)
    printf("### socket id:%s is break", id)
end

function Socket_M:on_error(id, data)
    printf("### socket id:%s is error: %s", id, data)
end

function Socket_M:on_recv(id, data)
    printf("### recive data:%s from socket id:%s", data, id)
end


hive.hive_start(M)

