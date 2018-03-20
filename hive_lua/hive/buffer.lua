-- todo: rewrite with c.

local mt = {}
mt.__index = mt


local function new_buffer()
    local raw = {
        v_size = 0,
        v_list = {},
    }
    return setmetatable(raw, mt)
end

function mt:push(s)
    local list = self.v_list
    list[#list+1] = s
    self.v_size = self.v_size + #s
end


function mt:size()
    return self.v_size
end



function mt:pop(max_count)
    if self.v_size <= 0 then
        return false
    end

    if  not max_count then
        local list = self.v_list
        local ret = table.concat(list)
        self.v_size = 0
        for i,v in ipairs(list) do
            list[i] = nil
        end
        return ret
    end

    if max_count == 0 then
        return ""
    end

    if self.v_size < max_count or max_count < 0 then
        return false
    end

    local idx = 1
    local ret = {}
    local c = 0
    local list = self.v_list
    local count = #list
    for i=1, count do
        local s = list[i]
        local len = #s
        if c + len <= max_count then
            ret[#ret+1] = s
            c = c + len
            idx = i + 1
        else
            local sub_len = max_count - c
            local sub_s = string.sub(s, 1, sub_len)
            local new_s = string.sub(s, sub_len+1, -1)
            list[i] = new_s
            ret[#ret+1] = sub_s
            c = c + sub_len
            idx = i
        end

        if c == max_count then
            local new_list = {}
            for i=idx, count do
                new_list[#new_list+1] = list[i]
            end
            self.v_list = new_list
            self.v_size = self.v_size - max_count
            break
        end
    end

    return table.concat(ret)
end

return new_buffer
