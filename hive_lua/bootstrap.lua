local hive = require "hive"

local M = {}

print("hello bootstrap!")


function M.dispatch(source, type, session, data)
    print("dispatch", source, type, session, "data:"..tostring(#data))
end


local function main()
    local echo_actor_list = {}
    for i=1,10 do
        local echo_handle = hive.hive_register("hive_lua/echo.lua", "echo")
        print("register handle", echo_handle)
        echo_actor_list[i] = echo_handle
    end

    for i=1,100 do
        local handle = echo_actor_list[i % #echo_actor_list + 1]
        hive.hive_send(handle, nil, "hello_echo_"..(i))
    end
end



-- print("hive_unregister", hive.hive_unregister(echo_handle))
main()

return M