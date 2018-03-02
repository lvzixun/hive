local hive = require "hive"
local spack = string.pack
local sunpack = string.unpack

local M = {}
local Socket_M = {}


----- socks5 server gate
function M:on_create()
    if SELF_NAME ~= "agent" then
        local id = hive.socket_listen("0.0.0.0", 9941, Socket_M)
        assert(id >= 0)
    end
end

function Socket_M:on_accept(client_id)
    local agent_handle = hive.hive_create("examples/socks5.lua", "agent")
    if agent_handle then
        hive.hive_send(agent_handle, client_id)
    else
        hive.socket_close(client_id)
    end
end



----- buffer class -----
local buffer_mt = {}
buffer_mt.__index = buffer_mt
local function new_buffer()
    local raw = {
        v_size = 0,
        v_list = {},
    }
    return setmetatable(raw, buffer_mt)
end

function buffer_mt:push(s)
    local list = self.v_list
    list[#list+1] = s
    self.v_size = self.v_size + #s
end


function buffer_mt:pop(max_count)
    if self.v_size <= 0 then
        return false
    end

    if  not max_count then
        local list = self.v_list
        local ret = table.concat(list)
        self.v_size = 0
        for i,v in ipairs(list) do
            list[i] = nil
        end
        return ret
    end

    if max_count == 0 then
        return ""
    end

    if self.v_size < max_count or max_count < 0 then
        return false
    end

    local idx = 1
    local ret = {}
    local c = 0
    local list = self.v_list
    local count = #list
    for i=1, count do
        local s = list[i]
        local len = #s
        if c + len <= max_count then
            ret[#ret+1] = s
            c = c + len
            idx = i + 1
        else
            local sub_len = max_count - c
            local sub_s = string.sub(s, 1, sub_len)
            local new_s = string.sub(s, sub_len+1, -1)
            list[i] = new_s
            ret[#ret+1] = sub_s
            c = c + sub_len
            idx = i
        end

        if c == max_count then
            local new_list = {}
            for i=idx, count do
                new_list[#new_list+1] = list[i]
            end
            self.v_list = new_list
            self.v_size = self.v_size - max_count
            break
        end
    end

    return table.concat(ret)
end



----- socks5 auth and resovle

local cur_buffer = new_buffer()
local function socket_read(max_count)
    local co = coroutine.running()

    while true do
        local s = buffer:pop(max_count)
        if not s then
            coroutine.yield()
        else
            return s
        end
    end
end


local cur_co = false
local function resovle(id)
    -- client request
    local s = socket_read(2)
    local ver, nmethods = sunpack(">I1I1", s)

    -- check version
    if ver ~= 5 then
        goto EXIT_AGENT
    end

    -- ignore metchods
    socket_read(nmethods)

    -- response
    local resp = spack(">I1I1", 0x05, 0x00)
    hive.socket_send(id, resp)

    -- resovle request

::EXIT_AGENT::
    hive.socket_close(id)
    hive.hive_free()
end


function Socket_M:on_recv(id, data)

end


function M:on_recv(client_id)
    if SELF_NAME == "agent" then
        hive.socket_attach(client_id, Socket_M)
        assert(cur_co == false)
        cur_co = coroutine.create(resovle)
        coroutine.resume(cur_co, id)
    end
end



function Socket_M:on_break()
    hive.hive_free()
end


function Socket_M:on_error()
    hive.hive_free()
end



hive.hive_start(M)
