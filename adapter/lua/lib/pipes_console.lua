
local pipes = require('pipes')
local inner = require('pipes_inner')
local log = pipes.log
local s = require('pipes_string')

local cs = {}

local function isThValid(th)
    th = tonumber(th)
    local ths = tonumber(pipes.env('thread'))
    if th >= 0 and th < ths then
        return true
    end
end

local function fnQuit()
    return 'Bye', true
end
local function fnMem(arr, n)
    local arg = arr[2]
    if not arg then -- no arg, query total mem
        local mem = 0
        local threads = tonumber(pipes.env('thread'))
        for i=1, threads do
            local tmp, err = pipes.stat('memth', i-1)
            if tmp then
                mem = mem + tmp
            end
        end
        return mem
    end
    arg = tostring(arg)
    if string.match(arg, '^%d+$') and string.len(arg) < 4 then  -- qurey thread mem
        local th = tonumber(arg)
        local threads = tonumber(pipes.env('thread'))
        if th < 0 or th >= threads then
            return 'thread invalid: '..th
        end
        local mem, err = pipes.stat('memth', th)
        return mem
    end
    if string.match(arg, '^%x+$') then -- query service mem
        local localId = tonumber(arg, 16)
        local id = pipes.local2Id(localId)
        local mem, err = pipes.stat('mem', id)
        if mem then
            return mem
        end
        return 'service not exist'
    end
    return 'input invalid'
end
local function fnSvcNum(arr, n)
    local arg = arr[2]
    if not arg then
        local num, err = pipes.stat('svcnum')
        return num
    end
    if string.match(arg, '^%d+$') and string.len(arg) < 4 then
        arg = tonumber(arg)
        if isThValid(arg) then
            local num, err = pipes.stat('svcnum', arg)
            return num
        end
    end
    return 'input invalid'
end
local function fnShutdown(arr, n)
    pipes.shutdown()
    return nil, true
end

local tbFn = {
mem = fnMem, 
shutdown = fnShutdown,
quit = fnQuit,
svcnum = fnSvcNum
}


function cs.procCmd(lineOri)
    lineOri = s.trim(lineOri)
    local line = s.lower(lineOri)
    --log('cmd=', line, ', len=', s.len(line))
    local arr, n = s.split(line)
    local cmd = arr[1]
    if not cmd then
        return false, 'no input'
    end
    local function procRet(fn)
        local echo, quit = fn(arr, n)
        return quit, echo
    end
    if not tbFn[cmd] then
        return false, 'unknown cmd: '..cmd
    end
    return procRet(tbFn[cmd])
end

return cs

