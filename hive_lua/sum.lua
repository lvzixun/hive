local hive = require "hive"
local sf = string.format
local M = {}


local all_msg = {}
print("hello sum!")
function M.dispatch(source, handle, type, session, data)
    if type == hive.HIVE_TNORMAL then
        all_msg[#all_msg+1] = data
        local s = sf("$$$$$$$$$$$$ sum from:%s self:%s type:%s session:%s data:%s all_msg_count:%d", 
            source, handle, type, session, data, #all_msg)
        print(s)
        if #all_msg == 200 then
            hive.hive_exit()
        end
    end
end

return M

