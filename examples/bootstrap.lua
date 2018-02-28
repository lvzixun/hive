local hive = require "hive"

print("package.path", package.path)

print("hello bootstrap!")

local M = {}

function M:on_create()
    local network_actor = hive.hive_create("examples/server_gate.lua", "server_gate")
    -- hive.hive_free()
end


hive.hive_start(M)