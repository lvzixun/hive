local hive = require "hive"
local spack = string.pack
local sunpack = string.unpack

local M = {}
local Socket_M = {}


----- socks5 server gate
function M:on_create()
    if SELF_NAME ~= "agent" then
        local id = hive.socket_listen("0.0.0.0", 9941, {
                on_accept = function (_, client_id)
                    local agent_handle = hive.hive_create("examples/socks5.lua", "agent")
                    if agent_handle then
                        hive.hive_send(agent_handle, client_id)
                    else
                        hive.socket_close(client_id)
                    end
                end
            })
        print("socket_listen: 127.0.0.1:9941")
        assert(id >= 0)
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
    print("buffer", max_count, self.v_size)
    if self.v_size <= 0 then
        print("!!!")
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
        local s = cur_buffer:pop(max_count)
        if not s then
            hive.co_yield()
        else
            return s
        end
    end
end


local cur_co = false
local function resovle(id)
    local function exit_agent()
        hive.socket_close(id)
        hive.hive_free()
    end

    print("begin resovle!!!")

    -- client request
    local s = socket_read(2)
    print("call here?")
    local ver, nmethods = sunpack(">I1I1", s)
    print("ver, nmethods", ver, nmethods)

    -- check version
    if ver ~= 5 then
        exit_agent()
        return
    end

    -- ignore metchods
    print("11111111111")
    socket_read(nmethods)

    -- response
    print("2222222")
    local resp = spack(">I1I1", 0x05, 0x00)
    print("&&&&&&", id, #resp)
    local ret = hive.socket_send(id, resp)
    print("socket_send", ret)

    -- resovle request
    s = socket_read(4)
    print("333333")
    local ver, cmd, rsv, atyp = sunpack(">I1I1I1I1", s)
    if ver ~= 5 then
        exit_agent()
        return
    end

    -- only support connect protocol
    if cmd ~= 1 then
        exit_agent()
        return
    end

    print("ver, cmd, rsv, atyp", ver, cmd, rsv, atyp)
    local connect_addr
    if atyp == 1 then  -- ipv4 address
        local s = socket_read(4)
        local t = {}
        for i=1,4 do
            t[i] = string.byte(s, i)
        end
        connect_addr = string.format("%d.%d.%d.%d", t[1], t[2], t[3], t[4])

    elseif atyp == 3 then -- domain address
        local len = string.byte(socket_read(1), 1)
        connect_addr = socket_read(len)

    else  -- other address not support
        exit_agent()
        return
    end

    local connect_port = sunpack(">I2", socket_read(2))
    print("request connect:", connect_addr, connect_port)

    local proxy_m = {}
    function proxy_m:on_error(id)
        hive.socket_close(id)
        exit_agent()
    end

    function proxy_m:on_break(id)
       exit_agent() 
    end

    function proxy_m:on_recv(_, data)
        print("proxy on_recv", #data)
        hive.socket_send(id, data)
    end

    connect_addr = "127.0.0.1"
    connect_port = 9391
    -- local proxy_id = hive.socket_connect(connect_addr, connect_port, proxy_m)
    -- if not proxy_id then
    --     exit_agent()
    --     return
    -- end

    --  response connect success
    local s = spack(">I1I1I1I1I1I1I1I1I2",
        0x05, 0, 0, 1,
        127, 0, 0, 1, 9923)
    print ("####", id, #s)
    hive.socket_send(id, s)

    while true do
        local s = socket_read()
        print(string.format("read %d from client", #s))
        print("--------------")
        print(s)
        print("--------------")
        hive.socket_send(proxy_id, s)
    end
end


function Socket_M:on_recv(id, data)
    print("on_recv data:", #data, coroutine.status(cur_co), cur_co)
    cur_buffer:push(data)
    hive.co_resume(cur_co)
end

function Socket_M:on_break(id)
    print("break connect from id:"..tostring(id))
end

function Socket_M:on_error(id, data)
    print("on_error", id, data)
end


function M:on_recv(client_id)
    if SELF_NAME == "agent" then
        hive.socket_attach(client_id, Socket_M)
        assert(cur_co == false)
        cur_co = hive.co_new(resovle)
        print("begin resume cur_co", cur_co)
        hive.co_resume(cur_co, client_id)
        print("cur_co", cur_co, coroutine.status(cur_co))
    end
end



function Socket_M:on_break()
    hive.hive_free()
end


function Socket_M:on_error()
    hive.hive_free()
end



hive.hive_start(M)
