local hive = require "hive"
local socket = require "hive.socket"
local thread = require "hive.thread"
local hive_log = require "hive.log"


hive_log.log("hello conn!")

local M = {}

local source = [[
GET / HTTP/1.1
Host: baidu.com
User-Agent: curl/7.54.0
Accept: */*

]]

function M:on_create()
    thread.run(function ()
            local host = "baidu.com"
            local port = 80
            local id, err = socket.connect(host, port)
            assert(id, err)
            socket.send(id, source)
            while true do
                local data, err = socket.read(id)
                assert(data, err)
                hive_log.log(data)
                if data == "" then
                    hive_log.log("connect break")
                    break
                end
            end
        end)
end


hive.start(M)
