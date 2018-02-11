local hive = require "hive"
local HIVE_TCREATE = hive.HIVE_TCREATE
local HIVE_TSOCKET = hive.HIVE_TSOCKET

local M = {}

print("hello network!")


local function printf(f, ...)
    local s = string.format(f, ...)
    print(s)
end

local function start()
    local id = hive.hive_socket_listen("0.0.0.0", 9291)
    print("hive_socket_listen!!!", id)
end

local function socket_dispatch(id, event_type, data)
    if event_type == hive.SE_ACCEPT then
        local client_id = data
        printf("### socket accept id:%s from id:%s", client_id, id)
        hive.hive_socket_send(client_id, "\nwelcome hive~\n")

    elseif event_type == hive.SE_BREAK then
        printf("### socket id:%s is break", id)
        
    elseif event_type == hive.SE_RECIVE then
        printf("### recive data:%s from socket id:%s", data, id)
        local s = string.format("\nhive recive data:%s len:%d\n", data, #data)
        hive.hive_socket_send(id, s)

    elseif event_type == hive.SE_ERROR then
        printf("### socket id:%s is error: %s", id, data)

    else
        error("!!!! socket_dispatch !!!!")
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
