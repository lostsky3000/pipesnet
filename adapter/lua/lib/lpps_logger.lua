
local pipes = require('pipes')

local function fmtSrc(src)
    local idRaw = pipes.localid(src)
    return string.format('%08x', idRaw)
end

local function funcStr(src, ss, str)
    local tm = pipes.now()
    io.output(io.stderr):write('[',tm,'] ', fmtSrc(src), ' ', str, '\n')
end

local function funcLua(src, ss, ...)
    local tb = table.pack(...)
    local tbStr = {}
    for i=1, tb.n do
        local tmp = tb[i]
        if tmp then
            table.insert(tbStr, tmp)
        end
    end
    local str = table.concat(tbStr)
    funcStr(src, ss, str)
end

pipes.dispatch('lua', function(src, ss, ...)
    local ok, err = pcall(funcLua, src, ss, ...)
    if not ok then
        local tm = pipes.now()
        io.output(io.stderr):write('[',tm,'] ', fmtSrc(pipes.self()), ' ', 'logger error: ', err, '\n')
    end
end)

pipes.dispatch('string', function(src, ss, str)
    local ok, err = pcall(funcStr, src, ss, str)
    if not ok then
        local tm = pipes.now()
        io.output(io.stderr):write('[',tm,'] ', fmtSrc(pipes.self()), ' ', 'logger error: ', err, '\n')
    end
end)






