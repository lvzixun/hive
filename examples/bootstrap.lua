local hive = require "hive"
local hive_log = require "hive.log"

hive_log.log("hello bootstrap!")

local M = {}

function M:on_create()
    local network_actor = hive.create("examples/server_gate.lua", "server_gate")
end


hive.start(M)