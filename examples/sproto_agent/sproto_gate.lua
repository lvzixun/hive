local log = require "hive.log"
local thread = require "hive.thread"
local servergate = require "hive.servergate"
local hive = require "hive"

local handle = {}
local agent_map = {}


local function exit_agent(client_id)
    local agent_handle = agent_map[client_id]
    hive.exit(agent_handle)
    agent_map[client_id] = nil
end


function handle:on_accept(client_id)
    log.logf("accept socket id:%d", client_id)
    local agent_handle = hive.create("examples/sproto_agent/agent.lua", "sproto_agent", client_id)
    agent_map[client_id] = agent_handle
end


function handle:on_package(client_id, s)
    log.logf("package socket id:%d data:%s", client_id, s)
    local agent_handle = agent_map[client_id]
    hive.send(agent_handle, "on_dispatch", s)
end


function handle:on_break(client_id)
    log.logf("break socket id:%d", client_id)
    exit_agent(client_id)
end


function handle:on_error(client_id, errinfo)
    log.logf("error socket id:%d errinfo:%s", client_id, errinfo)
    exit_agent(client_id)
end


thread.run(function ()
        local host = "0.0.0.0"
        local port = 9987
        servergate.start(host, port, handle)
        log.logf("listening %s:%s...", host, port)
    end)

