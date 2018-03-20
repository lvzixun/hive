local c = require "hive.c"
local thread = require "hive.thread"
local new_buffer = require "hive.buffer"

local HIVE_TSOCKET = c.HIVE_TSOCKET

local SE_CONNECTED = c.SE_CONNECTED
local SE_BREAK = c.SE_BREAK
local SE_ACCEPT = c.SE_ACCEPT
local SE_RECIVE = c.SE_RECIVE
local SE_ERROR = c.SE_ERROR



local M = {}
local status_map = {}

local socket_driver = {
    [SE_CONNECTED] = function (id, data)
        local entry = status_map[id]
        assert(entry.status == "connecting")
        local co = entry.co
    
        if not data then    -- connect success
            entry.status = "forward"
            entry.buffer = new_buffer()
            thread.resume(co, id)
        else                -- connect fail
            status_map[id] = nil  -- remove id
            thread.resume(co, false, data)
        end
    end,

    [SE_ACCEPT] = function (server_id, client_id)
        local entry = status_map[server_id]
        assert(entry.status == "listening")
        assert(not status_map[client_id])
        local on_accept_func = entry.on_accept
        if on_accept_func then
            on_accept_func(client_id)
        end
    end,

    [SE_RECIVE] = function (id, data)
        local entry = status_map[id]
        local status = entry.status
        if status == "receive" then
            local buffer = entry.buffer
            local co = entry.co
            local need_size = entry.need_size
            buffer:push(data)
            if need_size then
                local real_size = buffer:size()
                if real_size >= need_size then
                    local data = buffer:pop(need_size)
                    entry.need_size = nil
                    entry.status = "forward"
                    thread.resume(co, data)
                end
            else
                local data = buffer:pop()
                entry.need_size = nil
                entry.status = "forward"
                thread.resume(co, data)
            end
        elseif status ~= "forward" then
            local s = string.format("invalid status:%s from socket id:%s", status, id)
            error(s)
        end
    end,

    [SE_ERROR] = function (id, data)
        local entry = status_map[id]
        if entry then
            local status = entry.status
            if status == "receive" then
                local co = entry.co
                status_map[id] = nil
                return thread.resume(co, false, err)
            else
                entry.status = "error"
                entry.error = data
                return
            end
        end
        local s = string.format("socket id:%s receive fatal error:%s", id, data)
        error(s)
    end,

    [SE_BREAK] = function (id)
        local entry = status_map[id]
        if entry then
            local status = entry.status
            if status == "receive" then
                local co = entry.co
                status_map[id] = nil
                return thread.resume(co, "")
            else
                entry.status = "break"
                return
            end
        end
    end,
}


local function check_id(id)
    local entry = status_map[id]
    if not entry then
        local s = string.format("invalid socket id: %s", id)
        error(s)
    end
    return entry
end


function M.dispatch(source, handle, type, id, event_type, data)
    local sd_f = socket_driver[event_type]
    if sd_f then
        sd_f(id, data)
    else
        local s = string.format("invalid event_type:%s", event_type)
        error(s)
    end
end


function M.connect(host, port)
    local id, err = c.hive_socket_connect(host, port)
    if id then
        assert(status_map[id] == nil)
        local co, main_thread = thread.running()
        status_map[id] = {
            status = "connecting",
            co = co,
        }
        assert(not main_thread)
        return thread.yield(co)
    end
    return id, err
end


function M.listen(host, port, on_accept)
    local id = c.hive_socket_listen(host, port)
    if id < 0 then
        local s = string.format("listen errorcode:%s", id)
        return false, s
    else
        assert(not status_map[id])
        status_map[id] = {
            status = "listening",
            on_accept = on_accept,
        }
        return id
    end
end


function M.read(id, size)
    local entry = check_id(id)
    local status = entry.status

    if status == "forward" then
        local buffer = entry.buffer
        local co = entry.co
        local data = buffer:pop(size)
        if not data then
            entry.status = "receive"
            entry.need_size = size
            return thread.yield(co)
        else
            return data
        end

    elseif status == "break" then
        status_map[id] = nil
        return ""

    elseif status == "error" then
        local err = entry.error
        status_map[id] = nil
        return false, err
    else
        local s = string.format("invalid status: %s from socket id:%s", status, id)
        error(s)
    end
end


function M.send(id, data)
    return c.hive_socket_send(id, data)
end


function M.close(id)
    c.hive_socket_close(id)
    status_map[id] = nil
end


function M.attach(id)
    assert(not status_map[id])
    status_map[id] = {
        status = "forward",
        buffer = new_buffer(),
        co = thread.running(),
    }
    return c.hive_socket_attach(id, SELF_HANDLE)
end



return M

