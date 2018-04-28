local print_r = require "print_r"
local t = require "pack"
local hive_pack = t.pack
local hive_unpack = t.unpack



local function dump_hex(s)
    local len = #s
    local t = {}
    local c = 0
    for i=1,len do
        c = c + 1
        if c % 10 == 0 then
            t[#t+1] = "\n"
        end
        c = c + 1
        t[#t+1] = string.format("0X%.2x ", string.byte(s, i))
    end

    local s = table.concat(t)
    print(string.format("----- hex len %s -----", len))
    print(s)
end


local tbl = {
    key1 = "tbl1",
    key2 = "tbl2",
    hello = 1123,
    world = 4.567,
    nice = {
        11, 22, 33, 44,
        files = {"test1", "test2"}
    }
}



local s = hive_pack(11, tbl, true, false, nil, "test_string")
dump_hex(s)

local vars = {hive_unpack(s)}
print_r(vars)

