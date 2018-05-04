local hive = require "hive"
local socket = require "hive.socket"
local hive_log = require "hive.log"

local M = {}
local Socket_M = {}

hive_log.log("hello server_gate!")



local get_echo_actor = nil
function M:on_create()
    local count = 16
    local echo_idx = 1
    local echo_poll = {}
    for i=1,count do
        echo_poll[i] = hive.create("examples/echo.lua", "echo")
    end
    get_echo_actor = function ()
        local actor = echo_poll[echo_idx]
        echo_idx = echo_idx % count + 1
        return actor
    end

    -- open socket listen
    local ip = "127.0.0.1"
    local port = 9291
    local id = socket.listen(ip, port, function (client_id)
            local echo_actor = get_echo_actor()
            hive_log.log("on_accept!", client_id)
            hive.send(echo_actor, "echo", client_id)
        end)
    assert(id >=0)
    hive_log.logf("listen %s:%s", ip, port)
end

hive.start(M)

