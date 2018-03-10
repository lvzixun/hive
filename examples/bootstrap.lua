local hive = require "hive"

print("hello bootstrap!")

local M = {}

function M:on_create()
    local network_actor = hive.hive_create("examples/server_gate.lua", "server_gate")
end


hive.hive_start(M)