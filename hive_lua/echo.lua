local hive = require "hive.c"
local HIVE_TCREATE = hive.HIVE_TCREATE
local HIVE_TSOCKET = hive.HIVE_TSOCKET
local self_handle = nil

local function printf(f, ...)
    local s = string.format(f, ...)
    print(s)
end

local M = {}



local function socket_dispatch(id, event_type, data)
    if event_type == hive.SE_RECIVE then
        local s = string.format("\nrecv:%s from socket id:%s by actor:%s\n", data, id, self_handle)
        hive.hive_socket_send(id, s)
    elseif event_type == hive.SE_BREAK then
        printf("break socket id:%s by actor:%s", id, self_handle)
    end
end



function M.dispatch(source, handle, type, ...)
    if type == HIVE_TCREATE then
        self_handle = handle
    elseif type == HIVE_TSOCKET then
        socket_dispatch(...)
    end
end


return M

