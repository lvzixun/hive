local M = {}


local function _resume_aux(co, ok, err, ...)
    if not ok then
        error(debug.traceback(co, err))
    else
        return err, ...
    end
end

function M.new(f)
    return coroutine.create(f)
end

function M.run(f, ...)
    local co = M.new(f)
    return M.resume(co, ...)
end

function M.resume(co, ...)
    return _resume_aux(co, coroutine.resume(co, ...))
end

function M.yield(co, ...)
    return coroutine.yield(co, ...)
end

function M.running()
    return coroutine.running()
end

return M