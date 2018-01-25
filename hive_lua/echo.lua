local hive = require "hive"
local sf = string.format
local M = {}


local sum_actor = 2
print("hello echo!")
function M.dispatch(source, handle, type, session, data)
    if type == hive.HIVE_TNORMAL then
        local s = sf("######## echo from:%s self:%s type:%s session:%s data:%s", 
            source, handle, type, session, data)
        print(s)
        hive.hive_send(sum_actor, nil, data)
    end
end


return M

