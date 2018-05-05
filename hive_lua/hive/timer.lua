local c = require "hive.c"
local timer_register = c.hive_timer_register

local M = {}

local timer_map = {}

function M.register(offset, func)
    local timer_id = timer_register(offset)
    timer_map[timer_id] = func
    return timer_id
end


function M.unregister(timer_id)
    timer_map[timer_id] = nil
end


function M.trigger(timer_id)
    local func = timer_map[timer_id]
    if func then
        func()
        timer_map[timer_id] = nil
    end
end


function M.gettime()
    return c.hive_timer_gettime()
end


return M