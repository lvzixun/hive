local c = require "hive.c"
local socket = require "hive.socket"
local timer = require "hive.timer"
local thread = require "hive.thread"
local hive_pack = require "pack.c"

local HIVE_TCREATE = c.HIVE_TCREATE
local HIVE_TRELEASE = c.HIVE_TRELEASE
local HIVE_TSOCKET = c.HIVE_TSOCKET
local HIVE_TTIMER = c.HIVE_TTIMER
local HIVE_TNORMAL = c.HIVE_TNORMAL


local _actor_obj = false
local _actor_ud  = nil
local session_idx = 1
local session_map = {}


local function hive_error(fmt, ...)
    local header = string.format("actor:%s[%s]  ", SELF_NAME, SELF_HANDLE)
    error(header..string.format(fmt, ...))
end


local function check_call(obj, name, ...)
    local f = obj[name]
    if f then
        f(...)
    end
end


local function normal_solve(func_name, ...)
    local f = _actor_obj[func_name]
    if not f then
        hive_error("invalid dispatch function name:%s", func_name)
    end
    return f(_actor_ud, ...)
end


local function normal_solve_and_pack(source, session, func_name, ...)
    local ret = hive_pack.pack(normal_solve(func_name, ...))
    c.hive_send(source, session, ret)
end


local dispatch_driver = {
    [HIVE_TCREATE] = function (source, handle, type, ...)
        SELF_HANDLE = handle
        SELF_NAME = c.hive_name()
        check_call(_actor_obj, "on_create", _actor_ud)
    end,

    [HIVE_TRELEASE] = function (source, handle, type, ...)
        check_call(_actor_obj, "on_release", _actor_ud)
    end,

    [HIVE_TTIMER] = function (source, handle, type, session)
        timer.trigger(session)
    end,

    [HIVE_TNORMAL] = function (source, handle, type, session, data)
        if session == 0 then
            normal_solve(hive_pack.unpack(data))
        elseif session then
            local source_map = session_map[source]
            local session_context = source_map and source_map[session]
            if not session_context then
                thread.run(normal_solve_and_pack, source, session, hive_pack.unpack(data))
            else
                local co = session_context
                source_map[session] = nil
                thread.resume(co, hive_pack.unpack(data))
            end
        else
            hive_error("invalid session %s", session)
        end
    end,

    [HIVE_TSOCKET] = function (source, handle, type, id, event_type, data)
        return socket.dispatch(source, handle, type, id, event_type, data)
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

function M.create(path, name)
    return c.hive_register(path, name)
end


function M.send(target_handle, func_name, ...)
    local s = hive_pack.pack(func_name, ...)
    return c.hive_send(target_handle, nil, s)
end


function M.call(target_handle, func_name, ...)
    local cur_co = thread.running()
    local source_map = session_map[target_handle] or {}
    session_map[target_handle] = source_map
    assert(source_map[session_idx] == nil)
    source_map[session_idx] = cur_co
    local data = hive_pack.pack(func_name, ...)
    c.hive_send(target_handle, session_idx, data)
    session_idx = session_idx + 1
    return thread.yield(cur_co)
end


function M.start(actor_obj, ud)
    _actor_obj = actor_obj
    _actor_ud = ud
    c.hive_start(hive_dispatch)
end


function M.exit(actor_handle)
    actor_handle = actor_handle or SELF_HANDLE
    return c.hive_unregister(actor_handle)
end

function M.abort()
    return c.hive_exit()
end


return M

