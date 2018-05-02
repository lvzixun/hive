local hive = require "hive"
local timer = require "hive.timer"
local thread = require "hive.thread"
local hive_log = require "hive.log"


local M = {}

function M:add(v1, v2)
    return v1 + v2
end

function M:mul(v1, v2)
    return v1 * v2
end

function M:nothing(...)
    hive_log.log(...)
    return nil
end

function M:timeout_add(v1, v2)
    local cur_co = thread.running()
    timer.register(100, function ()
            thread.resume(cur_co, v1 + v2)
        end)
    return thread.yield(cur_co)
end


function M:timeout_mul(v1, v2)
    local cur_co = thread.running()
    timer.register(10, function ()
            thread.resume(cur_co, v1 * v2)
        end)
    return thread.yield(cur_co)
end



function M:hello(s)
    hive_log.logf("send hello: %s", s)
end


function M:on_create(is_excahnge)
    if is_excahnge then
        return
    end

    local server_handle = hive.create("examples/exchange.lua", "ExchangeActor", true)
    thread.run(function ()
            hive.send(server_handle, "hello", "world")
            local ret = hive.call(server_handle, "nothing", "test nothing", 3.45, true, false, nil, {key = "value"})
            hive_log.log("nothing:", ret)

            local add_sum = hive.call(server_handle, "add", 11, 22)
            hive_log.logf("add_sum:%d", add_sum)

            local mul_sum = hive.call(server_handle, "mul", 44, 55)
            hive_log.logf("mul_sum:%d", mul_sum)

            add_sum = hive.call(server_handle, "timeout_add", 66, 77)
            hive_log.log("timeout_add add_sum:", add_sum)

            mul_sum = hive.call(server_handle, "timeout_mul", 88, 99)
            hive_log.log("timeout_mul mul_sum:", mul_sum)
        end)
end




hive.start(M)