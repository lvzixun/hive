local c = require "hive.c"
local socket = require "hive.socket"
local timer = require "hive.timer"

local HIVE_TCREATE = c.HIVE_TCREATE
local HIVE_TRELEASE = c.HIVE_TRELEASE
local HIVE_TSOCKET = c.HIVE_TSOCKET
local HIVE_TTIMER = c.HIVE_TTIMER
local HIVE_TNORMAL = c.HIVE_TNORMAL


local _actor_obj = false
local _actor_ud  = nil


local function check_call(obj, name, ...)
    local f = obj[name]
    if f then
        f(...)
    end
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
        check_call(_actor_obj, "on_recv", _actor_ud, source, session, data)
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


function M.send(target_handle, session, data)
    return c.hive_send(target_handle, session, data)
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

