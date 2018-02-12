local hive = require "hive"
local HIVE_TCREATE = hive.HIVE_TCREATE
local HIVE_TSOCKET = hive.HIVE_TSOCKET

local M = {}


local function printf(f, ...)
    local s = string.format(f, ...)
    print(s)
end


local function start()
    local id = hive.hive_socket_connect("localhost", 9291)
    print("hive_socket_connect", id)
end


local function socket_dispatch(id, event_type, data)
    if event_type == hive.SE_RECIVE then
        printf("### recive data:%s from socket id:%s", data, id)
        hive.hive_socket_send(id, "seek it!@!!")
        hive.hive_socket_close(id)
    end
end



function M.dispatch(source, handle, type, ...)
    if type == HIVE_TCREATE then
        start()
    elseif type == HIVE_TSOCKET then
        socket_dispatch(...)
    end

    print("dispatch", source, handle, type, ...)
end


return M