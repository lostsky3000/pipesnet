
local pipes = require('pipes')

local function fmtTm()
    return os.date('%Y%m%d %H:%M:%S')
end
local function fmtSrc(src)
    return string.format('%08x', src)
end

local function funcStr(src, ss, str)
    io.output(io.stderr):write(fmtTm(),' [', fmtSrc(src), '] ', str, '\n')
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
        io.output(io.stderr):write(fmtTm(),' [', fmtSrc(pipes.self()), '] ', 'logger error: ', err, '\n')
    end
end)

pipes.dispatch('string', function(src, ss, str)
    local ok, err = pcall(funcStr, src, ss, str)
    if not ok then
        local tm = pipes.now()
        io.output(io.stderr):write(fmtTm(),' [', fmtSrc(pipes.self()), '] ', 'logger error: ', err, '\n')
    end
end)






