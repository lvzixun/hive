local c = require "hive.c"
local HIVE_TCREATE = c.HIVE_TCREATE
local HIVE_TRELEASE = c.HIVE_TRELEASE
local HIVE_TSOCKET = c.HIVE_TSOCKET
local HIVE_TTIMER = c.HIVE_TTIMER
local HIVE_TNORMAL = c.HIVE_TNORMAL

local SE_BREAK = c.SE_BREAK
local SE_ACCEPT = c.SE_ACCEPT
local SE_RECIVE = c.SE_RECIVE
local SE_ERROR = c.SE_ERROR


local _actor_obj = false
local _actor_ud  = nil
local socket_map = {}


local function check_call(obj, name, ...)
    local f = obj[name]
    if f then
        f(...)
    end
end


local socket_driver = {
    [SE_BREAK] = function (socket_obj, socket_ud, id)
        check_call(socket_obj, "on_break", socket_ud, id)
    end,

    [SE_ACCEPT] = function (socket_obj, socket_ud, _, client_id)
        check_call(socket_obj, "on_accept", socket_ud, client_id)
    end,

    [SE_RECIVE] = function (socket_obj, socket_ud, id, data)
        check_call(socket_obj, "on_recv", socket_ud, id, data)
    end,

    [SE_ERROR] = function (socket_obj, socket_ud, id, data)
        check_call(socket_obj, "on_error", socket_ud, id, data)
    end,
}


local dispatch_driver = {
    [HIVE_TCREATE] = function (source, handle, type, ...)
        SELF_HANDLE = handle
        SELF_NAME = c.hive_name()
        check_call(_actor_obj, "on_create", _actor_ud)
    end,

    [HIVE_TRELEASE] = function (source, handle, type, ...)
        check_call(_actor_obj, "on_release", _actor_ud)
    end,

    [HIVE_TTIMER] = function (source, handle, type, ...)
        check_call(_actor_obj, "on_timer", _actor_ud)
    end,

    [HIVE_TNORMAL] = function (source, handle, type, session, data)
        check_call(_actor_obj, "on_recv", _actor_ud, source, session, data)
    end,

    [HIVE_TSOCKET] = function (source, handle, type, id, event_type, data)
        local sd_f = socket_driver[event_type]
        local entry = socket_map[id]
        if entry then
            sd_f(entry.obj, entry.ud, id, data)
        end
    end,
}

local function hive_dispatch(source, handle, type, ...)
    if not _actor_obj then
        return
    end

    local f = dispatch_driver[type]
    f(source, handle, type, ...)
end



local M = {}

function M.hive_create(path, name)
    return c.hive_register(path, name)
end


function M.hive_free(actor_handle)
    actor_handle = actor_handle or SELF_HANDLE
    return c.hive_unregister(actor_handle)
end


function M.hive_exit()
    return c.hive_exit()
end


function M.hive_send(target_handle, session, data)
    return c.hive_send(target_handle, session, data)
end


function M.hive_start(actor_obj, ud)
    _actor_obj = actor_obj
    _actor_ud = ud
    c.hive_start(hive_dispatch)
end


function M.socket_connect(host, port, socket_obj, socket_ud)
    local id = c.hive_socket_connect(host, port)
    if id then
        M.socket_attach(id, socket_obj, socket_ud)
    end
    return id
end


function M.socket_listen(host, port, socket_obj, socket_ud)
   local id = c.hive_socket_listen(host, port) 
   if id then
        M.socket_attach(id, socket_obj, socket_ud)
    end
    return id
end


function M.socket_send(id, data)
    return c.hive_socket_send(id, data)
end


function M.socket_attach(id, socket_obj, socket_ud)
    assert(not socket_map[id])
    socket_map[id] = {
        id = id,
        obj = socket_obj,
        ud = socket_ud,
    }
    return c.hive_socket_attach(id, SELF_HANDLE)
end


function M.socket_close(id)
    assert(socket_map[id])
    c.hive_socket_close(id)
    socket_map[id] = nil
end


local function _resume_aux(co, ok, err, ...)
    if not ok then
        error(debug.traceback(co, err))
    else
        return err, ...
    end
end

function M.co_new(f)
    return coroutine.create(f)
end

function M.co_resume(co, ...)
    return _resume_aux(co, coroutine.resume(co, ...))
end

function M.co_yield(co, ...)
    return coroutine.yield(co, ...)
end

return M

