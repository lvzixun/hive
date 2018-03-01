local hive = require "hive"

local M = {}
local Socket_M = {}


function M:on_create()
    if SELF_NAME == "agent" then
    else
        local id = hive.socket_listen("0.0.0.0", 9941, Socket_M)
        assert(id >= 0)
    end
end

function Socket_M:on_accept(client_id)
    local agent_handle = hive.hive_create("examples/socks5.lua", "agent")
    if agent_handle then
        hive.hive_send(agent_handle, client_id)
    else
        hive.socket_close(client_id)
    end
end


hive.hive_start(M)