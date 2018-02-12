local hive = require "hive.c"
local HIVE_TCREATE = hive.HIVE_TCREATE

local M = {}

print("hello bootstrap!")

local function main()
    local network_actor = hive.hive_register("hive_lua/server_gate.lua", "server_gate")
end


function M.dispatch(source, handle, type, session, data)
    if type == HIVE_TCREATE then
        main()
    end
end


return M