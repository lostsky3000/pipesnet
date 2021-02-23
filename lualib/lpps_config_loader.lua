

--print('config_loader called')

local function trim(str)
    str = string.gsub(str, '^%s+', '')
    str = string.gsub(str, '%s+$', '')
    return str
end

local function parseKV(str, sep)
    local pos = string.find(str, sep)
    if not pos then return nil end
    local key = string.sub(str, 1, pos - 1)
    local val = string.sub(str, pos + 1)
    key = trim(key)
    val = trim(val)
    if key == '' then
        key = nil
    end
    if val == '' then
        val = nil
    end
    return key, val
end

cfg_load_result = {}
local ret = cfg_load_result

local path = 'config.lua'
local f = assert(io.open(path))
for line in f:lines() do 
    line = trim(line)
    if line ~= '' then
        local key, val = parseKV(line, '=')
        if key and val then
            ret[key] = val
        end
    end
end
f:close()


--ret['boot'] = 'boot'
--ret['thread'] = 2
--ret['harbor'] = 1

