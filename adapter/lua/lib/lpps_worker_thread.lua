

local pipes = require('pipes')
local log = pipes.log

local id, th = pipes.self()
log('sys worker service start, th=',th)


local fnProcCmd
pipes.dispatch('lua', function(src, ss, ...)
    local arg = {...}
    --log('sys recv msg: ',src,', ', ss,', arg1=', arg[1])
    local ok, err = pcall(fnProcCmd, src, ss, ...)
    if not ok then
        log('[ERROR] src=',src, ', err: ', tostring(err))
    end
end)

fnProcCmd = function(src, ss, ...)
    local arg = {...}
    local cmd = arg[1]
    if cmd == 'memth' then
        local mem = pipes.stat('memth')
        pipes.ret('lua', mem)
    end
end

