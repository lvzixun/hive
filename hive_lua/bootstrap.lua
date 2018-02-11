local hive = require "hive"

local M = {}

print("hello bootstrap!")


function M.dispatch(source, type, session, data)
    print("dispatch", source, type, session, "data:"..tostring(#data))
end


local function main2()
    local echo_actor_list = {}
    local sum_actor = hive.hive_register("hive_lua/sum.lua", "sum")
    
    for i=1,10 do
        local echo_handle = hive.hive_register("hive_lua/echo.lua", "echo")
        print("register handle", echo_handle)
        echo_actor_list[i] = echo_handle
    end

    for i=1,200 do
        local handle = echo_actor_list[i % #echo_actor_list + 1]
        -- handle = sum_actor
        hive.hive_send(handle, nil, "hello_echo_"..(i))
    end

    -- hive.hive_exit()
end

local function main()
    local network_actor = hive.hive_register("hive_lua/network.lua", "network")
    print("network_actor!!!!", network_actor)
end

-- print("hive_unregister", hive.hive_unregister(echo_handle))
main()

return M