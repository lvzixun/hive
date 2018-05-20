local log = require "hive.log"
local thread = require "hive.thread"
local servergate = require "hive.servergate"

local handle = {}


function handle:on_accept(client_id)
    log.logf("accept socket id:%d", client_id)
end


function handle:on_package(client_id, s)
    log.logf("package socket id:%d data:%s", client_id, s)
end


function handle:on_break(client_id)
    log.logf("break socket id:%d", client_id)
end


function handle:on_error(client_id)
    log.logf("error socket id:%d", client_id)
end


thread.run(function ()
        local host = "0.0.0.0"
        local port = 9934
        servergate.start(host, port, handle)
        log.logf("listening %s:%s...", host, port)
    end)

