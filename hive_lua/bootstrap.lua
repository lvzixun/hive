local hive = require "hive"

local M = {}

print("hello bootstrap!")

hive.hive_register("hive_lua/echo.lua", "echo")

function M.dispatch(source, type, session, data)
    print("dispatch", source, type, session, "data:"..tostring(#data))
end



return M