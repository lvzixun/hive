package.path = './examples/sproto_agent/?.lua;./examples/sproto_agent/sproto/?.lua;' .. package.path
package.cpath = './examples/sproto_agent/sproto/?.so;' .. package.cpath

local sproto = require "sproto.sproto"
local log = require "hive.log"
local hive = require "hive"

local servergate_msg = require "servergate_msg.c"
local msg_send = servergate_msg.msg_send

local M = {}

local server_proto = sproto.parse [[
    .package {
        type 0 : integer
        session 1 : integer
    } 

    hello 1 {
        request {
            name 0 : string
            type 1 : integer
        }

        response {
            ok  0 : boolean
            ret 1 : string
        }
    }
]]
local server = server_proto:host "package"



function M:hello(request)
    log.logf("hello request: name:%s type:%d", request.name, request.type)
    return {
        ok = true,
        ret = "hello_ret:" .. request.name
    }
end


function M:on_dispatch(data)
    local type, name, request, response = server:dispatch(data)
    assert(type == "REQUEST")
    local f = self[name]
    local ret = f(self, request)
    if response then
        local resp = response(ret)
        msg_send(self.v_client_id, resp)
    end
end



function M:on_create(client_id)
    self.v_client_id = client_id
end



hive.start(M, M)