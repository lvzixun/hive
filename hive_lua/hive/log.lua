local c = require "hive.c"
local hive_log = c.hive_log
local HIVE_LOG_INF = c.HIVE_LOG_INF

local M = {}

local function _log(level, ...)
    local n = select("#", ...)
    local s = ""
    if n > 0 then
        local t = {}
        for i=1,n do
            local v = select(i, ...)
            t[i] = tostring(v)
        end
        local h = string.format("actor:%s[%s] ", SELF_NAME, SELF_HANDLE)
        s = h..table.concat(t, "  ")
    end
    return hive_log(level, s)
end


function M.log(...)
    return _log(HIVE_LOG_INF, ...)
end


function M.logf(fmt, ...)
    local s = string.format(fmt, ...)
    return _log(HIVE_LOG_INF, s)
end


return M