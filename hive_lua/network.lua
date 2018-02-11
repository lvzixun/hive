local hive = require "hive"
local HIVE_TCREATE = hive.HIVE_TCREATE

local M = {}

print("hello network!")


local function start()
    local id = hive.hive_socket_listen("0.0.0.0", 9291)
    print("hive_socket_listen!!!", id)
end


function M.dispatch(source, handle, type, session, data, ud)
    if type == HIVE_TCREATE then
        start()
    end

    print("dispatch", source, handle, type, session, data, ud)
end




return M
