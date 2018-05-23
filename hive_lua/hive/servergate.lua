local c = require "hive.c"
local thread = require "hive.thread"

local servergate = require "servergate.c"
local servergate_msg = require "servergate_msg.c"
local msg_type = servergate_msg.msg_type
local msg_package = servergate_msg.msg_package
local msg_bindret = servergate_msg.msg_bindret
local msg_accept = servergate_msg.msg_accept
local msg_break = servergate_msg.msg_break
local msg_error = servergate_msg.msg_error


local HIVE_TNORMAL = c.HIVE_TNORMAL
local HIVE_TCREATE = c.HIVE_TCREATE
local GATE_HANDLE = servergate.GATE_HANDLE


local M = {}
local _handle_obj = nil
local _handle_ud = nil
local _gate_state = {
    co = nil,
    status = nil,
}


local msg_driver = {
    [servergate_msg.MT_PACKAGE] = function (data)
        local id, s = msg_package(data)
        _handle_obj["on_package"](_handle_ud, id, s)
    end,

    [servergate_msg.MT_BINDRET] = function (data)
        assert(_gate_state.status == "binding")
        _gate_state.status = "running"
        local ret = msg_bindret(data)
        thread.resume(_gate_state.co, ret)
    end,

    [servergate_msg.MT_ACCEPT] = function (data)
        local client_id = msg_accept(data)
        _handle_obj["on_accept"](_handle_ud, client_id)
    end,

    [servergate_msg.MT_BREAK] = function (data)
        local client_id = msg_break(data)
        _handle_obj["on_break"](_handle_ud, client_id)
    end,

    [servergate_msg.MT_ERROR] = function (data)
        local client_id, err = msg_error(data)
        _handle_obj["on_error"](_handle_ud, client_id, err)
    end,
}


local function dispatch(source, handle, type, session, data)
    if type == HIVE_TNORMAL and source == GATE_HANDLE then
        local mt = msg_type(data)
        msg_driver[mt](data)
    elseif type == HIVE_TCREATE then
        SELF_HANDLE = handle
        SELF_NAME = c.hive_name()
        assert(_gate_state.status == "initing")
        thread.resume(_gate_state.co)
    end
end


function M.start(ip, port, obj, ud)
    if _handle_obj or not obj then
        error("start servergate error")
    end

    local co = thread.running()
    assert(_gate_state.status == nil)

    _gate_state.status = "initing"
    _gate_state.co = co
    _handle_obj = obj
    _handle_ud = ud

    c.hive_start(dispatch)
    thread.yield(co)

    ip = ip or "127.0.0.1"
    local ret = servergate.start(SELF_HANDLE, ip, port)
    if ret ~= 0 then
        error(string.format("servergate start error:%s", ret))
    end

    _gate_state.status = "binding"
    local ret = thread.yield(co)
    if ret ~= 0 then
        error(string.format("servergate start error:%s", ret))
    end
end


function M.close(client_id)
    servergate.close(SELF_HANDLE, client_id)
end


return M

